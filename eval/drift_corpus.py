"""Other people's pandas scripts on real monthly data (part of RQ5).

    py -3 eval/drift_corpus.py collect traffic|bikes|hours    # once: scripts from GitHub (needs gh, signed in)
    py -3 eval/drift_corpus.py traffic|bikes|hours [path/to/preplang.exe]

The pipelines of RQ4 are ours; these are not. GitHub code search returns Python files that read the
UCI Metro Interstate traffic file, or the UCI bike-sharing day.csv or hour.csv, in GitHub's ranking
order without duplicates. frontend/pandas2prep.py lifts the cleaning that PrepLang expresses (the
longest prefix of each followed DataFrame's statements). The lifted program is compiled once, on the
first month of the real data, and run on every later month, with deoptimization (the default) and with
--no-deopt (the full certificate). The ground truth is the lifted pandas statements run on the same
month; the cleaned tables are compared value by value.

Month files are cut from the published files by the date column and get an empty cell wherever pandas'
read_csv reads a missing value (the traffic file writes "no holiday" as None, which pandas reads as
missing), so that both sides read the same table. Months with fewer than 20 rows (the traffic data has a
gap) are left out. Writes eval/results/drift_corpus_<name>.json and .md.
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

REAL = ROOT / "eval" / "data" / "real"
RESULTS = ROOT / "eval" / "results"
PYTHON = sys.executable
PAGES = 5  # 100 results per page
CORPORA = {  # name -> code search query, file name a script must read, published file, date column
    "traffic": ("Metro_Interstate_Traffic_Volume read_csv language:python", "metro_interstate_traffic_volume",
                REAL / "Metro_Interstate_Traffic_Volume.csv", "date_time"),
    "bikes": ("day.csv read_csv cnt atemp hum language:python", "day.csv", REAL / "day.csv", "dteday"),
    "hours": ("hour.csv read_csv cnt atemp hum language:python", "hour.csv", REAL / "hour.csv", "dteday"),
}
ARGS = [a for a in sys.argv[1:] if a != "collect"]
NAME = next((a for a in ARGS if a in CORPORA), "traffic")
QUERY, FILE_KEY, PUBLISHED, DATE = CORPORA[NAME]
CORPUS = ROOT / "eval" / "data" / f"drift_corpus_{NAME}"
WORK = ROOT / "eval" / "work" / f"drift_corpus_{NAME}"


def data_role(path):
    """Whether a script's read_csv path is the corpus's data file (any folder, .csv or .csv.gz)."""
    name = Path(path.replace("\\", "/")).name.lower()
    return name.startswith(FILE_KEY.split(".")[0]) and (".csv" in name) if NAME == "traffic" else name == FILE_KEY


def months():
    """The published file cut into month files; returns their paths in order."""
    from pandas._libs.parsers import STR_NA_VALUES
    folder = WORK / "months"
    folder.mkdir(parents=True, exist_ok=True)
    table = pd.read_csv(PUBLISHED, dtype=str, keep_default_na=False)
    table = table.replace({token: "" for token in STR_NA_VALUES})
    paths = []
    for month, rows in table.groupby(table[DATE].str[:7], sort=True):
        if len(rows) >= 20:
            path = folder / f"{month}.csv"
            rows.to_csv(path, index=False)
            paths.append(path)
    return paths


# ---------- collecting the corpus ----------

def collect():
    """Code search pages in order, then the files themselves, fetched in parallel."""
    from concurrent.futures import ThreadPoolExecutor
    CORPUS.mkdir(parents=True, exist_ok=True)
    found = []
    for page in range(1, PAGES + 1):
        done = subprocess.run(["gh", "api", "-X", "GET", "search/code", "-f", f"q={QUERY}", "-f", "per_page=100",
                               "-f", f"page={page}"], capture_output=True, text=True, encoding="utf-8")
        items = json.loads(done.stdout).get("items", [])
        found += items
        print("page", page, "results", len(found), flush=True)
        if len(items) < 100:
            break
        time.sleep(7)  # the code search API allows 10 requests a minute

    def fetch(item):
        repo = item["repository"]["full_name"]
        sha = item["url"].split("ref=")[-1]
        raw = f"https://raw.githubusercontent.com/{repo}/{sha}/" + urllib.parse.quote(item["path"])
        for _ in range(3):
            try:
                return urllib.request.urlopen(raw, timeout=30).read()
            except OSError:
                time.sleep(2)
        return None

    with ThreadPoolExecutor(max_workers=16) as pool:
        texts = list(pool.map(fetch, found))
    index, seen = [], set()
    for item, text in zip(found, texts):  # GitHub's ranking order
        if text is None:
            continue
        digest = hashlib.sha256(text).hexdigest()[:16]
        if digest in seen:
            continue
        seen.add(digest)
        (CORPUS / f"{digest}.py").write_bytes(text)
        index.append({"id": digest, "url": item["html_url"], "rank": len(index) + 1})
    print("files", len(index), flush=True)
    (CORPUS / "index.json").write_text(json.dumps(index, indent=1), encoding="utf-8")


# ---------- running the corpus ----------

def ground_truth(source_lines, frames, month):
    """Runs the lifted original statements on the month; returns {variable: DataFrame} or an error text.
    A chained df["c"].fillna(v, inplace=True), which pandas 3's copy-on-write ignores, runs as
    df["c"] = df["c"].fillna(v), as the script meant."""
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
            continue  # tables are compared in memory
        code.append(line)
    text = "\n".join(code)
    for frame in frames.values():
        text = text.replace(repr(frame.path), repr(str(month))).replace('"' + frame.path + '"', repr(str(month)))
    namespace = {"pd": pd, "np": np, "numpy": np, "pandas": pd}
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
    except (AssertionError, ValueError, TypeError, FileNotFoundError):
        return False


def run_script(compiler, item, month_files):
    record = {"id": item["id"], "url": item["url"], "rank": item["rank"]}
    source = (CORPUS / f"{item['id']}.py").read_text(encoding="utf-8", errors="replace")
    try:
        result = lift(source)
    except SyntaxError:
        record["outcome"] = "not Python 3"
        return record
    roles = [data_role(frame.path) for frame in result.frames.values()]
    if not any(roles):
        record["outcome"] = "does not read the data file with pd.read_csv(path)"
        return record
    if not all(roles):
        record["outcome"] = "reads other data files"
        return record
    record.update(changes=result.changes, lifted=result.lifted, statements=len(result.prep),
                  complete=all(not f.stopped_at for f in result.frames.values()))
    if result.lifted == 0:
        record["outcome"] = "no cleaning statement lifted"
        return record
    program = result.program()
    for frame in result.frames.values():
        program = program.replace(f'LOAD "{frame.path}"', 'LOAD "data.csv"')
    folder = WORK / item["id"]
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    (folder / "program.prep").write_text(program, encoding="utf-8")
    shutil.copyfile(month_files[0], folder / "data.csv")
    full = subprocess.run([str(compiler), "program.prep", "--python", "--no-deopt"], cwd=folder,
                          capture_output=True, text=True)
    if full.returncode == 0:
        shutil.copyfile(folder / "output" / "generated.py", folder / "output" / "generated_full.py")
    done = subprocess.run([str(compiler), "program.prep", "--python", "--certificate"], cwd=folder,
                          capture_output=True, text=True)
    text = done.stdout + done.stderr
    record["errors"] = [line for line in text.splitlines() if line.startswith("error[")]
    if "Compilation failed" in text:
        record["outcome"] = "compile error"
        truth = ground_truth(result.source_lines, result.frames, month_files[0])
        record["script_on_first_month"] = truth if isinstance(truth, str) else "runs"
        return record
    record["outcome"] = "lifted"
    record["facts"] = text.count("<- ")
    output = folder / "output"
    args = [f"{frame.name}=" for frame in result.frames.values()]
    record["months"] = []
    for month in month_files[1:]:
        truth = ground_truth(result.source_lines, result.frames, month)
        script = "crashes" if isinstance(truth, str) else "runs"
        entry = {"month": month.stem, "script": script, "script_error": truth if isinstance(truth, str) else ""}
        for variant, generated in (("prep", "generated.py"), ("prep_full", "generated_full.py")):
            for old in output.rglob("*.csv"):
                old.unlink()
            run = subprocess.run([PYTHON, str(output / generated), *[a + str(month) for a in args]],
                                 capture_output=True, text=True)
            log = run.stdout + run.stderr
            if run.returncode == 0:
                ours = "finished"
            elif "is not the input the program was compiled with" in log:
                ours = "refused"
            else:
                ours = "contract error"
            same = None
            if ours == "finished" and script == "runs":
                same = all(same_table(output / f"{frame.name}_lifted.csv", truth[variable])
                           for variable, frame in result.frames.items())
            suffix = "" if variant == "prep" else "_full"
            entry.update({variant: ours, "same" + suffix: same,
                          "reason" + suffix: next((line for line in log.splitlines()
                                                   if line.startswith("ContractError")), "")[:300]})
            if variant == "prep":
                entry["deopts"] = [line[len("Deoptimized "):] for line in run.stdout.splitlines()
                                   if line.startswith("Deoptimized ")]
        record["months"].append(entry)
    return record


def process(job):
    import warnings
    warnings.simplefilter("ignore")  # invalid escape sequences in the scripts' own source
    return run_script(*job)


def main(compiler):
    from concurrent.futures import ProcessPoolExecutor
    index = json.loads((CORPUS / "index.json").read_text(encoding="utf-8"))
    month_files = months()
    records = []
    with ProcessPoolExecutor(max_workers=6) as pool:
        for record in pool.map(process, [(compiler, item, month_files) for item in index]):
            records.append(record)
            print(record["rank"], record["outcome"], flush=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / f"drift_corpus_{NAME}.json").write_text(json.dumps(records, indent=1), encoding="utf-8")
    write_markdown(records, month_files)


def write_markdown(records, month_files):
    runs = [m for r in records for m in r.get("months", [])]

    def count(test):
        return sum(1 for m in runs if test(m))
    lines = [f"# Other people's pandas scripts on real monthly data: {NAME}", "",
             f"Corpus: {len(records)} Python files from GitHub code search `{QUERY}` (first {PAGES * 100} "
             f"results). Compiled on {month_files[0].stem}, run on {len(month_files) - 1} later months.", ""]
    for outcome, n in Counter(r["outcome"] for r in records).most_common():
        lines.append(f"- {outcome}: {n}")
    lifted = [r for r in records if r["outcome"] in ("lifted", "compile error")]
    if lifted:
        lines += ["", f"Of the {len(lifted)} scripts with lifted cleaning: {sum(r['lifted'] for r in lifted)} of "
                  f"{sum(r['changes'] for r in lifted)} cleaning statements translated; "
                  f"{sum(r['complete'] for r in lifted)} lifted completely."]
    lines += ["", "## Later months", "",
              f"- runs: {len(runs)}",
              f"- finished and identical to the script: {count(lambda m: m['prep'] == 'finished' and m['same'])}",
              f"- finished, but different from the script, or the script crashed (wrong): "
              f"{count(lambda m: m['prep'] == 'finished' and not m['same'])}",
              f"- stopped by a contract check, and the script crashes too: "
              f"{count(lambda m: m['prep'] == 'contract error' and m['script'] == 'crashes')}",
              f"- stopped by a contract check, but the script runs: "
              f"{count(lambda m: m['prep'] == 'contract error' and m['script'] == 'runs')}",
              f"- refused, and the script crashes too: {count(lambda m: m['prep'] == 'refused' and m['script'] == 'crashes')}",
              f"- refused, but the script runs: {count(lambda m: m['prep'] == 'refused' and m['script'] == 'runs')}",
              f"- ran with removals re-enabled: {count(lambda m: m.get('deopts'))}",
              "", "Without deoptimization (--no-deopt, the full certificate):",
              f"- finished and identical: {count(lambda m: m['prep_full'] == 'finished' and m['same_full'])}",
              f"- finished, different (wrong): {count(lambda m: m['prep_full'] == 'finished' and not m['same_full'])}",
              f"- refused, but the script runs: {count(lambda m: m['prep_full'] == 'refused' and m['script'] == 'runs')}",
              "", "## Removals re-enabled", ""]
    for text, n in Counter(d.split(":")[0] + ":" + d.split(":")[1] for m in runs for d in m.get("deopts", [])
                           ).most_common(20):
        lines.append(f"- {n} x {text}")
    lines.append("")
    (RESULTS / f"drift_corpus_{NAME}.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    if sys.argv[1:2] == ["collect"]:
        collect()
    else:
        rest = [a for a in ARGS if a not in CORPORA]
        main(Path(rest[0]).resolve() if rest else ROOT / "build" / "Release" / "preplang.exe")
