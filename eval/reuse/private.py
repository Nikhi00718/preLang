"""Private compilation (RQ10): compiling against data the compiler never sees.

    py -3 eval/reuse/private.py [path/to/preplang.exe]

The data owner runs preplang --queries and tools/release_facts.py on the compile-time
file; the analyst compiles from the released facts alone (--facts), in a folder that
holds only the program and the facts file, so a successful compilation shows that no
input file was opened. The generated program is then run by the data owner on every
batch. Workloads:

  * rq8      the three pipelines of RQ8 on real monthly data (traffic, bikes, taxi),
             compiled for the first month and run on every month
  * titanic  the lifted Titanic scripts of RQ9 that compile, compiled for the training
             file and run on it and on the RQ9 batches (Kaggle test file, resamples,
             extra missing values)

Each is compiled from several kinds of facts:

  * exact     the same questions answered without noise (--epsilon inf; not private)
  * eps=e     epsilon-differentially private answers, several seeds
  * random    random facts, true or false: bounds at random program numbers, missing
              values at random (tests that correctness never depends on the facts)
  * none      facts that claim nothing (missing values everywhere, no bounds)

Measured:
  1. exactness: every run is scored against a fresh compilation of the batch, run
     (eval/reuse/policies.py); none may be unsound, whatever the facts;
  2. what the analyst learns: the removals of the private compilation against those of
     the normal compilation of the same file (precision, recall), and the warnings;
  3. the cost: deoptimizations on the compiled file itself, refusals.
Writes eval/results/private.json and .md.
"""
import contextlib
import io
import json
import math
import os
import random
import re
import runpy
import shutil
import subprocess
import sys
import time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from policies import OUTCOMES, outputs, score  # noqa: E402
from realbatches import WORKLOADS  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "eval" / "work" / "private"
RESULTS = ROOT / "eval" / "results"
PYTHON = sys.executable
RELEASE = ROOT / "tools" / "release_facts.py"
TITANIC = ROOT / "eval" / "data" / "titanic"
CORPUS_WORK = ROOT / "eval" / "work" / "pandas_corpus"
EPSILONS = (0.1, 1, 10, 100)


def configs(epsilons, seeds, randoms):
    """(name, epsilon or None, seed) for every kind of facts."""
    out = [("exact", math.inf, 0)]
    out += [(f"eps={e:g}", e, s) for e in epsilons for s in range(seeds)]
    out += [("random", None, s) for s in range(randoms)]
    return out + [("none", None, 0)]


# ---------- Facts ----------

def read_queries(path):
    constants, tables = [], []
    for line in Path(path).read_text(encoding="utf-8").splitlines()[1:]:
        fields = line.split("\t")
        if fields[0] == "constants":
            constants = [float(value) for value in fields[1:]]
        elif fields[0] == "table":
            tables.append((fields[1], fields[2], fields[3], []))
        elif fields[0] == "column":
            tables[-1][3].append((fields[1], fields[2], fields[3] == "range"))
    return constants, tables


def made_up_facts(queries_file, seed):
    """Facts that claim nothing (seed None), or random ones that may be false."""
    constants, tables = read_queries(queries_file)
    anchors = sorted(set(constants) | {0.0})
    rng = random.Random(seed)
    lines = ["preplang-facts\t1", "mechanism\t" + ("no facts" if seed is None else f"random facts, seed {seed}")]
    for load_path, name, kind, columns in tables:
        lines.append(f"table\t{load_path}\t{name}\t{kind}")
        for column, ctype, ranged in columns:
            missing, low, low_strict, high, high_strict = "?", "-inf", 0, "inf", 0
            if seed is not None:
                missing = rng.choice(("0", "?"))
                if ctype in ("INTEGER", "FLOAT") and ranged:
                    a, b = rng.choice([-math.inf] + anchors), rng.choice(anchors + [math.inf])
                    a, b = min(a, b), max(a, b)
                    low, high = repr(a), repr(b)
                    low_strict = int(rng.random() < 0.5 and a != b and not math.isinf(a))
                    high_strict = int(rng.random() < 0.5 and a != b and not math.isinf(b))
            lines.append(f"column\t{column}\t{ctype}\t{missing}\t{low}\t{low_strict}\t{high}\t{high_strict}\t-\t-\t-\t-")
    return "\n".join(lines) + "\n"


def release(owner, analyst, epsilon, seed):
    """The data owner's side: queries (needs the data), then the facts file for the analyst."""
    queries = owner / "output" / "preplang_queries.tsv"
    analyst.mkdir(parents=True, exist_ok=True)
    if epsilon is None:
        (analyst / "facts.tsv").write_text(made_up_facts(queries, seed if seed != "none" else None), encoding="utf-8")
        return
    subprocess.run([PYTHON, str(RELEASE), str(queries), "--epsilon", repr(float(epsilon)), "--seed", str(seed),
                    "--out", str(analyst / "facts.tsv")], check=True, capture_output=True, text=True)


# ---------- Compiling and comparing ----------

REMOVED = re.compile(r"^# line (\d+): (.*?)  \(removed")


def removals(generated):
    """The instructions a compiled program removed (speculatively or for good)."""
    found = set()
    if generated.exists():
        for line in generated.read_text(encoding="utf-8").splitlines():
            match = REMOVED.match(line)
            if match:
                found.add(f"line {match.group(1)}: {match.group(2)}")
    return found


def compile_in(compiler, folder, facts=False):
    args = [str(compiler), "program.prep", "--python"] + (["--facts", "facts.tsv"] if facts else [])
    done = subprocess.run(args, cwd=folder, capture_output=True, text=True)
    text = done.stdout + done.stderr
    return {"ok": "Compilation failed" not in text and done.returncode == 0,
            "warnings": [line for line in text.splitlines() if line.startswith("warning[")],
            "errors": [line for line in text.splitlines() if line.startswith("error")],
            "removed": sorted(removals(folder / "output" / "generated.py"))}


def owner_setup(compiler, owner, program, files):
    """The data owner's folder: the program, its data, a normal compilation and the queries."""
    if owner.exists():
        shutil.rmtree(owner)
    owner.mkdir(parents=True)
    (owner / "program.prep").write_text(program, encoding="utf-8")
    for name, source in files.items():
        shutil.copyfile(source, owner / name)
    normal = compile_in(compiler, owner)
    subprocess.run([str(compiler), "program.prep", "--queries"], cwd=owner, capture_output=True, text=True, check=True)
    return normal


def run_here(folder, script, inputs):
    """Runs output/<script> inside this worker process, as policies.run_program runs it in a
    new one (same statuses, deoptimizations and files); pandas is then imported once per
    worker instead of once per run, which is most of the time of a run."""
    out = Path(folder) / "output"
    for path in list(out.rglob("*")):
        if path.is_file() and not path.name.startswith(("generated", "preplang_contract")):
            path.unlink()
    text, argv = io.StringIO(), sys.argv
    sys.argv = [str(out / script)] + [f"{name}={path}" for name, path in inputs.items()]
    os.environ["PREPLANG_TRUST_INPUTS"] = "0"
    status, reason = "finished", ""
    try:
        with contextlib.redirect_stdout(text), contextlib.redirect_stderr(text):
            runpy.run_path(str(out / script), run_name="__main__")
    except Exception as error:  # noqa: BLE001 - the program's own failure is a result
        if type(error).__name__ == "ContractError":
            reason = f"ContractError: {error}"
            status = "refused" if "is not the input the program was compiled with" in str(error) else "contract error"
        else:
            status, reason = "crash", f"{type(error).__name__}: {error}"
    finally:
        sys.argv = argv
    deopts = [line[len("Deoptimized "):] for line in text.getvalue().splitlines() if line.startswith("Deoptimized ")]
    return {"status": status, "reason": reason, "deopts": deopts, "files": outputs(out) if status == "finished" else {}}


def truth_of(folder):
    meta = json.loads((folder / "truth.json").read_text(encoding="utf-8"))
    return {"status": meta["status"], "files": outputs(folder / "output")}


def make_truth(compiler, folder, program, files):
    """A fresh compilation of the batch, run: the ground truth (kept on disk for the workers),
    as policies.run_fresh computes it."""
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    (folder / "program.prep").write_text(program, encoding="utf-8")
    for name, source in files.items():
        shutil.copyfile(source, folder / name)
    done = subprocess.run([str(compiler), "program.prep", "--python"], cwd=folder, capture_output=True, text=True)
    if "Compilation failed" in done.stdout + done.stderr:
        status = "compile error"
    else:
        ran = run_here(folder, "generated.py", {})
        status = "finished" if ran["status"] == "finished" else "contract error"
    (folder / "truth.json").write_text(json.dumps({"status": status}), encoding="utf-8")


def private_run(job):
    """One workload item and one kind of facts: release, compile privately, run every batch."""
    compiler, workload, item, (kind, epsilon, seed), owner, batches, anchor, normal = job
    analyst = WORK / workload / item / f"{kind}-{seed}"
    if analyst.exists():
        shutil.rmtree(analyst)
    analyst.mkdir(parents=True)
    shutil.copyfile(owner / "program.prep", analyst / "program.prep")  # the program, but no data
    release(owner, analyst, epsilon, "none" if kind == "none" else seed)
    compiled = compile_in(compiler, analyst, facts=True)
    record = {"workload": workload, "item": item, "kind": kind, "epsilon": epsilon, "seed": seed,
              "compiled": compiled["ok"], "errors": compiled["errors"][:3],
              "warnings": compiled["warnings"], "removed": compiled["removed"], "normal_removed": normal["removed"],
              "facts": (analyst / "facts.tsv").read_text(encoding="utf-8").splitlines()[1], "runs": []}
    if not compiled["ok"]:
        return record
    for batch, (inputs, truth_folder) in batches.items():
        start = time.perf_counter()
        ran = run_here(analyst, "generated.py", inputs)
        seconds = time.perf_counter() - start
        record["runs"].append({"batch": batch, "status": ran["status"], "deopts": len(ran["deopts"]),
                               "outcome": score(ran, truth_of(truth_folder)), "anchor": batch == anchor,
                               "seconds": round(seconds, 3), "reason": ran["reason"][:200]})
    return record


# ---------- Workloads ----------

def items():
    """(workload, item, program, files of the compiled data, {batch: {file name: path}}, anchor)"""
    out = []
    for workload, (program, input_file, _, folder) in WORKLOADS.items():
        months = sorted(folder.glob("*" + Path(input_file).suffix))
        out.append(("rq8", workload, program, {input_file: months[0]},
                    {month.stem: {input_file: month} for month in months}, months[0].stem))
    files = {"train.csv": TITANIC / "train.csv", "test.csv": TITANIC / "test.csv"}
    batch_files = {"train (compiled file)": TITANIC / "train.csv", "kaggle test file": TITANIC / "test.csv"}
    batch_files.update({path.stem: path for path in sorted((CORPUS_WORK / "batches").glob("*.csv"))})
    for folder in sorted(CORPUS_WORK.iterdir()):
        if (folder / "output" / "generated.py").exists():
            program = (folder / "program.prep").read_text(encoding="utf-8")
            out.append(("titanic", folder.name, program, files,
                        {batch: {**files, "train.csv": path} for batch, path in batch_files.items()},
                        "train (compiled file)"))
    return out


def prepare_item(task):
    """The data owner's folder (normal compilation and queries) and the ground truth of every batch."""
    compiler, (workload, item, program, files, batch_files, anchor) = task
    owner = WORK / workload / item / "owner"
    normal = owner_setup(compiler, owner, program, files)
    if not normal["ok"]:
        return None
    batches = {}
    for batch, inputs in batch_files.items():
        truth = WORK / workload / item / "truth" / re.sub(r"\W+", "_", batch)
        make_truth(compiler, truth, program, inputs)
        # The generated program is given every input by its dataset name: the analyst's folder has no data.
        loads = re.findall(r'LOAD "([^"]+)" AS (\w+);', program)  # a file may be loaded twice
        batches[batch] = ({dataset: str(inputs[file]) for file, dataset in loads if file in inputs}, truth)
    return workload, item, owner, batches, anchor, normal


# ---------- Report ----------

def summarize(records):
    groups = defaultdict(list)
    for r in records:
        key = (r["workload"] if r["workload"] == "titanic" else "rq8:" + r["item"], r["kind"])
        groups[key].append(r)
    rows = []
    for (workload, kind), items in sorted(groups.items()):
        runs = [run for r in items for run in r["runs"]]
        outcomes = {o: sum(run["outcome"] == o for run in runs) for o in OUTCOMES}
        true_pos = sum(len(set(r["removed"]) & set(r["normal_removed"])) for r in items if r["compiled"])
        claimed = sum(len(r["removed"]) for r in items if r["compiled"])
        truth = sum(len(r["normal_removed"]) for r in items if r["compiled"])
        anchor = [run for run in runs if run["anchor"]]
        rows.append({
            "workload": workload, "kind": kind, "compilations": len(items),
            "compiled": sum(r["compiled"] for r in items), "runs": len(runs), **outcomes,
            "precision": true_pos / claimed if claimed else None, "recall": true_pos / truth if truth else None,
            "removed": claimed, "normal_removed": truth,
            "anchor_deopts": sum(run["deopts"] for run in anchor), "anchor_runs": len(anchor),
            "anchor_with_deopt": sum(run["deopts"] > 0 for run in anchor),
            "likely_violated": sum(sum("likely violated" in w for w in r["warnings"]) for r in items),
            "median_anchor_seconds": sorted(run["seconds"] for run in anchor)[len(anchor) // 2] if anchor else None,
        })
    return rows


def write_markdown(rows, records):
    def pct(value):
        return "-" if value is None else f"{100 * value:.0f}%"
    lines = ["# RQ10: private compilation", "",
             "The analyst compiles from facts the data owner released (tools/release_facts.py), in a folder without "
             "the data; the data owner runs the generated program on every batch. Each run is scored against a fresh "
             "compilation of the batch, run (eval/reuse/policies.py). *Removals*: the removed instructions of the "
             "private compilation against those of the normal compilation of the compiled file (precision, recall). "
             "*Deoptimized on the compiled file*: runs on the very file the facts describe in which a removal had to "
             "run again because a released fact was false.", "",
             "| Workload | Facts | Compilations | Runs | Correct run | Correct stop | Needless stop | Wrong stop | "
             "Unsound | Removals precision | Removals recall | Deoptimized on the compiled file | Likely violated |",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for r in rows:
        lines.append(f"| {r['workload']} | {r['kind']} | {r['compiled']}/{r['compilations']} | {r['runs']} | "
                     f"{r['correct run']} | {r['correct stop']} | {r['needless stop']} | {r['wrong stop']} | "
                     f"{r['unsound']} | {pct(r['precision'])} | {pct(r['recall'])} | "
                     f"{r['anchor_with_deopt']}/{r['anchor_runs']} | {r['likely_violated']} |")
    total = {o: sum(r[o] for r in rows) for o in OUTCOMES}
    lines += ["", f"All runs: {sum(r['runs'] for r in rows)}; " +
              ", ".join(f"{o}: {total[o]}" for o in OUTCOMES) + "."]
    failed = [r for r in records if not r["compiled"]]
    if failed:
        lines += ["", "## Private compilations that failed", ""]
        for r in failed[:20]:
            lines.append(f"- {r['workload']} {r['item']} {r['kind']} seed {r['seed']}: {'; '.join(r['errors'])[:200]}")
    unsound = [(r, run) for r in records for run in r["runs"] if run["outcome"] in ("unsound", "wrong stop")]
    if unsound:
        lines += ["", "## Unsound runs and wrong stops", ""]
        for r, run in unsound[:30]:
            lines.append(f"- {r['workload']} {r['item']} {r['kind']} seed {r['seed']} on {run['batch']}: "
                         f"{run['outcome']} ({run['status']}) {run['reason']}")
    return "\n".join(lines) + "\n"


# ---------- Diagnostics at scale ----------

EVENTS_PROGRAM = """LOAD "events.parquet" AS events;
EXPECT events.amount BETWEEN 0 AND 999;
EXPECT events.s NOT_NULL;
EXPECT events.a BETWEEN 0 AND 1;
FILL_NULL events.b WITH 0;
SAVE events AS "events_clean.parquet";
"""
MISTAKES = {  # kind -> (the contract it breaks, the column it changes, the value written into m rows)
    "amount above 999": ("events.amount BETWEEN 0 AND 999", "amount", 1500),
    "s missing": ("events.s NOT_NULL", "s", None),
    "a below 0": ("events.a BETWEEN 0 AND 1", "a", -0.5),
}


def diagnostics(compiler, sizes=(0, 1, 10, 100, 1000, 10000), epsilons=(0.1, 1, 10, math.inf), seeds=5):
    """Does a private compilation warn about a mistake in m rows of a 1,000,000-row file,
    and does it warn about anything else? Ground truth: the normal compilation, which
    reports the broken contract as violated for every m > 0."""
    import numpy as np
    import pandas as pd
    base = pd.read_parquet(ROOT / "eval" / "data" / "events_1000000.parquet")
    rows = []
    for kind, (contract, column, value) in MISTAKES.items():
        for m in sizes:
            owner = WORK / "diagnostics" / re.sub(r"\W+", "_", kind) / str(m) / "owner"
            if owner.exists():
                shutil.rmtree(owner)
            owner.mkdir(parents=True)
            frame = base.copy()
            changed = np.zeros(len(frame), dtype=bool)
            changed[np.random.default_rng(m).choice(len(frame), size=m, replace=False)] = True
            frame[column] = frame[column].where(~changed, value)
            frame.to_parquet(owner / "events.parquet", index=False)
            (owner / "program.prep").write_text(EVENTS_PROGRAM, encoding="utf-8")
            normal = compile_in(compiler, owner)
            subprocess.run([str(compiler), "program.prep", "--queries"], cwd=owner, capture_output=True, check=True)
            for epsilon in epsilons:
                for seed in range(1 if math.isinf(epsilon) else seeds):
                    analyst = owner.parent / f"eps={epsilon:g}-{seed}"
                    if analyst.exists():
                        shutil.rmtree(analyst)
                    analyst.mkdir()
                    shutil.copyfile(owner / "program.prep", analyst / "program.prep")
                    release(owner, analyst, epsilon, seed)
                    private = compile_in(compiler, analyst, facts=True)
                    warned = [w for w in private["warnings"] if "likely violated" in w]
                    rows.append({"kind": kind, "rows": m, "epsilon": epsilon, "seed": seed,
                                 "violated": any(contract in e for e in normal["errors"]),
                                 "warned": any(contract in w for w in warned),
                                 "false_warnings": sum(contract not in w for w in warned),
                                 "compiled": private["ok"]})
            print("diagnostics", kind, m)
    return rows


def write_diagnostics(rows):
    lines = ["# RQ10: what a private compilation tells the analyst", "",
             "A 1,000,000-row Parquet file (eval/data/events_1000000.parquet) with a mistake injected into m rows; "
             "the program has three contracts, one of which the mistake breaks. A normal compilation reports that "
             "contract as violated for every m > 0. Cells: the share of private compilations (5 seeds; 1 without "
             "noise) that warn that the broken contract is likely violated; in brackets, warnings about a contract "
             "the data does not break.", ""]
    epsilons = sorted({r["epsilon"] for r in rows})
    sizes = sorted({r["rows"] for r in rows})
    lines.append("| Mistake | m | " + " | ".join(f"epsilon {e:g}" for e in epsilons) + " |")
    lines.append("|---|---:|" + "---:|" * len(epsilons))
    for kind in MISTAKES:
        for m in sizes:
            cells = []
            for e in epsilons:
                found = [r for r in rows if r["kind"] == kind and r["rows"] == m and r["epsilon"] == e]
                share = sum(r["warned"] for r in found) / len(found) if found else 0
                false = sum(r["false_warnings"] for r in found)
                cells.append(f"{100 * share:.0f}%" + (f" ({false})" if false else ""))
            lines.append(f"| {kind} | {m} | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def main(compiler, quick=False):
    WORK.mkdir(parents=True, exist_ok=True)
    kinds = {"rq8": configs(EPSILONS, 1 if quick else 3, 2 if quick else 10),
             "titanic": configs((1, 10), 1, 2)}
    work = items()
    if quick:
        work = work[:6]
    records = []
    with ProcessPoolExecutor(max_workers=max(1, (os.cpu_count() or 2) - 2)) as pool:
        prepared = [p for p in pool.map(prepare_item, [(compiler, item) for item in work], chunksize=1) if p]
        print(len(prepared), "items prepared:", sum(p[0] == "rq8" for p in prepared), "RQ8 pipelines,",
              sum(p[0] == "titanic" for p in prepared), "Titanic scripts")
        jobs = [(compiler, workload, item, config, owner, batches, anchor, normal)
                for workload, item, owner, batches, anchor, normal in prepared for config in kinds[workload]]
        print(len(jobs), "private compilations")
        for i, record in enumerate(pool.map(private_run, jobs, chunksize=1), 1):
            records.append(record)
            if i % 25 == 0:
                print(i, "done")
    rows = summarize(records)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "private.json").write_text(json.dumps({"rows": rows, "records": records}, indent=1), encoding="utf-8")
    (RESULTS / "private.md").write_text(write_markdown(rows, records), encoding="utf-8")
    print(write_markdown(rows, records))


if __name__ == "__main__":
    arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
    if arguments and arguments[0] == "diagnostics":
        found = diagnostics(Path(arguments[1]).resolve() if len(arguments) > 1 else
                            ROOT / "build" / "Release" / "preplang.exe")
        (RESULTS / "private_diagnostics.json").write_text(json.dumps(found, indent=1), encoding="utf-8")
        (RESULTS / "private_diagnostics.md").write_text(write_diagnostics(found), encoding="utf-8")
        print(write_diagnostics(found))
    else:
        main(Path(arguments[0]).resolve() if arguments else ROOT / "build" / "Release" / "preplang.exe",
             quick="--quick" in sys.argv)
