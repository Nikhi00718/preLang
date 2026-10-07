"""Data certificates for real pandas scripts (RQ9).

    py -3 eval/pandas_corpus.py collect        # once: real Titanic scripts from GitHub (needs gh, signed in)
    py -3 eval/pandas_corpus.py [path/to/preplang.exe]

Corpus: GitHub code search for Python files that contain read_csv, fillna and
Embarked (the Kaggle Titanic data), in GitHub's ranking order, without
duplicates. The scripts are never run as a whole: frontend/pandas2prep.py lifts
the part of their cleaning that PrepLang expresses, and only those lifted
statements are run as the ground truth.

For every script that reads the Titanic training file, the lifted program is
compiled against it (eval/data/titanic/train.csv) and then given new batches:
the real Kaggle test file, resampled training rows, and training rows with
extra missing values. Each batch is run twice:

  * PrepLang     the program compiled once, with train=batch (admitted or refused by its certificate)
  * the script   the lifted original pandas statements, executed on the batch (the ground truth)

and the two cleaned tables are compared. Writes eval/results/pandas_corpus.json and .md.
"""
import ast
import hashlib
import json
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from collections import Counter
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "frontend"))
from pandas2prep import lift  # noqa: E402

CORPUS = ROOT / "eval" / "data" / "pandas_corpus"
TITANIC = ROOT / "eval" / "data" / "titanic"
WORK = ROOT / "eval" / "work" / "pandas_corpus"
RESULTS = ROOT / "eval" / "results"
PYTHON = sys.executable
QUERY = "read_csv fillna Embarked language:python"
PAGES = 5  # 100 results per page

# A file name with "test" means the Kaggle Titanic test file; with "titanic" or "train",
# the training file. (Scripts written for another Titanic table, such as the lower-case
# one of seaborn, then fail on unknown columns and are counted apart.)
def data_role(path):
    name = Path(path.replace("\\", "/")).name.lower()
    if not name.endswith(".csv"):
        return None
    if "test" in name:
        return "test.csv"
    if "titanic" in name or "train" in name:
        return "train.csv"
    return None


# ---------- collecting the corpus ----------

def collect():
    CORPUS.mkdir(parents=True, exist_ok=True)
    index, seen = [], set()
    for page in range(1, PAGES + 1):
        done = subprocess.run(["gh", "api", "-X", "GET", "search/code", "-f", f"q={QUERY}", "-f", "per_page=100",
                               "-f", f"page={page}"], capture_output=True, text=True,
                              encoding="utf-8")
        items = json.loads(done.stdout).get("items", [])
        for item in items:
            repo = item["repository"]["full_name"]
            sha = item["url"].split("ref=")[-1]
            raw = f"https://raw.githubusercontent.com/{repo}/{sha}/" + urllib.parse.quote(item["path"])
            try:
                text = urllib.request.urlopen(raw, timeout=30).read()
            except OSError:
                continue
            digest = hashlib.sha256(text).hexdigest()[:16]
            if digest in seen:
                continue
            seen.add(digest)
            (CORPUS / f"{digest}.py").write_bytes(text)
            index.append({"id": digest, "url": item["html_url"], "rank": len(index) + 1})
        print("page", page, "files", len(index), flush=True)
        time.sleep(7)  # the code search API allows 10 requests a minute
    (CORPUS / "index.json").write_text(json.dumps(index, indent=1), encoding="utf-8")


# ---------- one script ----------

def map_paths(result):
    """Data files of the lifted program -> the local Titanic files, or None if one is unknown."""
    mapping = {}
    for frame in result.frames.values():
        role = data_role(frame.path)
        if role is None:
            return None
        mapping[frame.path] = role
    return mapping


def ground_truth(source_lines, mapping, frames, batch_for_train, folder):
    """Runs the lifted original statements on the batch; returns {variable: DataFrame} or an error text.
    A chained call such as df["c"].fillna(v, inplace=True) changed df in pandas 1 and 2, but not with
    the copy-on-write of pandas 3; it is run as df["c"] = df["c"].fillna(v), as the script meant."""
    code = []
    for line in source_lines:
        tree = ast.parse(line).body[0]
        call = tree.value if isinstance(tree, ast.Expr) and isinstance(tree.value, ast.Call) else None
        if call and isinstance(call.func, ast.Attribute) and isinstance(call.func.value, (ast.Subscript, ast.Attribute)) \
                and any(k.arg == "inplace" for k in call.keywords) and call.func.attr == "fillna":
            call.keywords = [k for k in call.keywords if k.arg != "inplace"]
            tree = ast.Assign(targets=[call.func.value], value=call, lineno=0)
            line = ast.unparse(ast.fix_missing_locations(tree))
        elif call and isinstance(call.func, ast.Attribute) and call.func.attr == "to_csv":
            continue  # files are compared in memory
        code.append(line)
    namespace = {"pd": pd, "np": np, "numpy": np, "pandas": pd}
    text = "\n".join(code)
    for original, local in mapping.items():
        target = batch_for_train if local == "train.csv" else TITANIC / local
        text = text.replace(repr(original), repr(str(target))).replace('"' + original + '"', repr(str(target)))
    try:
        exec(compile(text, "<lifted>", "exec"), namespace)
    except Exception as error:  # noqa: BLE001 - the script's own failure is a result
        return f"{type(error).__name__}: {error}"
    return {variable: namespace[variable] for variable in frames}


def same_table(prep_csv, frame):
    """The PrepLang output file and the script's DataFrame hold the same table."""
    try:
        ours = pd.read_csv(prep_csv)
        theirs = pd.read_csv(pd.io.common.StringIO(frame.to_csv(index=False)))
        pd.testing.assert_frame_equal(ours, theirs, check_dtype=False, rtol=1e-9, atol=1e-12)
        return True
    except (AssertionError, ValueError, TypeError):
        return False


def batches(seed_count=5):
    """name -> path of a CSV file with the training columns (or the Kaggle test file)."""
    folder = WORK / "batches"
    folder.mkdir(parents=True, exist_ok=True)
    train = pd.read_csv(TITANIC / "train.csv", keep_default_na=False, dtype=str)
    out = {"kaggle test file": TITANIC / "test.csv"}
    for seed in range(seed_count):
        rng = np.random.default_rng(seed)
        resampled = train.sample(len(train), replace=True, random_state=seed)
        out[f"resampled {seed}"] = folder / f"resampled_{seed}.csv"
        resampled.to_csv(out[f"resampled {seed}"], index=False)
        holes = train.copy()
        column = ["Age", "Fare", "Embarked", "Sex", "Pclass"][seed]
        holes.loc[rng.random(len(holes)) < 0.2, column] = ""
        out[f"missing {column}"] = folder / f"missing_{column}.csv"
        holes.to_csv(out[f"missing {column}"], index=False)
    return out


def run_script(compiler, script_id, source):
    record = {"id": script_id}
    try:
        result = lift(source)
    except SyntaxError:
        record["outcome"] = "not Python 3"
        return record, None
    train_vars = [v for v, f in result.frames.items() if data_role(f.path) == "train.csv"]
    if not train_vars:
        record["outcome"] = "does not read the training file with pd.read_csv(path)"
        return record, None
    mapping = map_paths(result)
    if mapping is None:
        record["outcome"] = "reads other data files"
        return record, None
    train = train_vars[0]
    frame = result.frames[train]
    record.update(outcome="lifted", changes=result.changes, lifted=result.lifted,
                  complete=all(not f.stopped_at for f in result.frames.values()), stopped_at=frame.stopped_at,
                  statements=len(result.prep))
    program = result.program()
    for original, local in mapping.items():
        program = program.replace(f'LOAD "{original}"', f'LOAD "{local}"')
    folder = WORK / script_id
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    (folder / "program.prep").write_text(program, encoding="utf-8")
    for name in ("train.csv", "test.csv"):
        shutil.copyfile(TITANIC / name, folder / name)
    done = subprocess.run([str(compiler), "program.prep", "--python", "--certificate"], cwd=folder,
                          capture_output=True, text=True)
    text = done.stdout + done.stderr
    record["warnings"] = [line for line in text.splitlines() if line.startswith("warning[")]
    record["errors"] = [line for line in text.splitlines() if line.startswith("error[")]
    if "Compilation failed" in text:
        record["outcome"] = "compile error"
        truth = ground_truth(result.source_lines, mapping, result.frames, TITANIC / "train.csv", folder)
        record["script_on_train"] = truth if isinstance(truth, str) else "runs"
        return record, None
    record["facts"] = text.count("<- ")
    return record, (folder, train, result, mapping)


def run_batches(prepared, record, batch_files):
    folder, train, result, mapping = prepared
    name = result.frames[train].name
    output = folder / "output"
    record["batches"] = []
    for batch, path in batch_files.items():
        for old in output.rglob("*.csv"):
            old.unlink()
        done = subprocess.run([PYTHON, str(output / "generated.py"), f"{name}={path}"], capture_output=True, text=True)
        text = done.stdout + done.stderr
        if done.returncode == 0:
            ours = "finished"
        elif "is not the input the program was compiled with" in text:
            ours = "refused"
        else:
            ours = "contract error"
        truth = ground_truth(result.source_lines, mapping, result.frames, path, folder)
        script = "crashes" if isinstance(truth, str) else "runs"
        same = None
        if ours == "finished" and script == "runs":
            same = same_table(output / f"{name}_lifted.csv", truth[train])
        reason = next((line for line in text.splitlines() if line.startswith("ContractError")), "")
        record["batches"].append({"batch": batch, "prep": ours, "script": script, "same": same,
                                  "script_error": truth if isinstance(truth, str) else "", "reason": reason[:400]})


def process(job):
    """One script: lift, compile, and run every batch. Runs in a worker process."""
    import warnings
    warnings.simplefilter("ignore")  # invalid escape sequences in the scripts' own source
    compiler, item, batch_files = job
    source = (CORPUS / f"{item['id']}.py").read_text(encoding="utf-8", errors="replace")
    record, prepared = run_script(compiler, item["id"], source)
    record["url"] = item["url"]
    if prepared:
        run_batches(prepared, record, batch_files)
    return record


def main(compiler):
    from concurrent.futures import ProcessPoolExecutor
    index = json.loads((CORPUS / "index.json").read_text(encoding="utf-8"))
    batch_files = batches()  # written once, before the workers start
    jobs = [(compiler, item, batch_files) for item in index]
    records = []
    with ProcessPoolExecutor(max_workers=6) as pool:
        for item, record in zip(index, pool.map(process, jobs)):
            records.append(record)
            print(item["rank"], record["outcome"], flush=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "pandas_corpus.json").write_text(json.dumps(records, indent=1), encoding="utf-8")
    write_markdown(records)


def write_markdown(records):
    outcomes = Counter(r["outcome"] for r in records)
    lifted = [r for r in records if r["outcome"] in ("lifted", "compile error")]
    runs = [b for r in records for b in r.get("batches", [])]
    admitted = [b for b in runs if b["prep"] != "refused"]
    lines = ["# RQ9: data certificates for real pandas scripts", "",
             f"Corpus: {len(records)} Python files from GitHub code search `{QUERY}` (first {PAGES * 100} results).", ""]
    for outcome, count in outcomes.most_common():
        lines.append(f"- {outcome}: {count}")
    if lifted:
        lines += ["", f"Of the {len(lifted)} scripts whose training-file cleaning was lifted: "
                  f"{sum(r['lifted'] for r in lifted)} of {sum(r['changes'] for r in lifted)} cleaning statements "
                  f"translated; {sum(r['complete'] for r in lifted)} scripts lifted completely."]
    if runs:
        def count(test):
            return sum(1 for b in runs if test(b))
        lines += ["", "## New batches", "",
                  f"- runs: {len(runs)} ({len(runs) // max(1, len([r for r in records if r.get('batches')]))} per script)",
                  f"- admitted by the certificate: {len(admitted)}",
                  f"- admitted, finished and identical to the script's own result: "
                  f"{count(lambda b: b['prep'] == 'finished' and b['same'])}",
                  f"- admitted, finished, but different from the script, or the script crashed (unsound): "
                  f"{count(lambda b: b['prep'] == 'finished' and not b['same'])}",
                  f"- admitted, stopped by a run-time contract check: {count(lambda b: b['prep'] == 'contract error')}",
                  f"- refused, and the script crashes on it too: {count(lambda b: b['prep'] == 'refused' and b['script'] == 'crashes')}",
                  f"- refused, but the script runs (conservative): {count(lambda b: b['prep'] == 'refused' and b['script'] == 'runs')}",
                  "", "| Batch | Runs | Admitted | Identical to the script | Script crashes |",
                  "|---|---:|---:|---:|---:|"]
        for batch in dict.fromkeys(b["batch"] for b in runs):
            items = [b for b in runs if b["batch"] == batch]
            lines.append(f"| {batch} | {len(items)} | {sum(b['prep'] != 'refused' for b in items)} | "
                         f"{sum(bool(b['same']) for b in items)} | {sum(b['script'] == 'crashes' for b in items)} |")
    lines.append("")
    (RESULTS / "pandas_corpus.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "collect":
        collect()
    else:
        main(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe")
