"""Fault benchmark (RQ1) and contract statistics (RQ2).

    py -3 eval/run_eval.py [path/to/preplang.exe]

For every program (the original examples as controls, plus every mutant):
  * PrepLang compiles it (--explain, nothing is executed);
  * the script-style baseline runs it with pandas/OpenCV.
Writes eval/results/rq1.json and eval/results/rq1.md.
"""
import json
import re
import shutil
import subprocess
import sys
import time
import warnings
from pathlib import Path

from baseline import Runner
from mutants import programs

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "eval" / "work" / "mutants"
RESULTS = ROOT / "eval" / "results"
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Debug" / "preplang.exe"


def compile_program(path):
    start = time.perf_counter()
    run = subprocess.run([str(COMPILER), str(path), "--explain"], capture_output=True, text=True, cwd=WORK)
    elapsed = (time.perf_counter() - start) * 1000
    errors = re.findall(r"^error\[(\w+)\]: (.*)$", run.stdout, re.M)
    notes = re.findall(r"^warning\[\w+\]: (.*)$", run.stdout, re.M)
    contracts = {kind: int(count) for kind, count in
                 re.findall(r"contracts (proven|violated|checked at run time)\s+(\d+)", run.stdout)}
    if errors:
        verdict = f"compile error ({errors[0][0]})"
    elif notes:
        verdict = "compile warning"
    else:
        verdict = "not reported"
    return {"verdict": verdict, "errors": [e[1] for e in errors], "warnings": notes, "ms": elapsed,
            "contracts": contracts}


def run_baseline(path, output, oracle):
    shutil.rmtree(output, ignore_errors=True)
    start = time.perf_counter()
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            Runner(path, output).run(path.read_text(encoding="utf-8"))
    except Exception as error:  # the baseline has no static checks: mistakes surface as exceptions
        elapsed = (time.perf_counter() - start) * 1000
        return {"verdict": f"run-time crash ({type(error).__name__})", "detail": str(error)[:120], "ms": elapsed}
    elapsed = (time.perf_counter() - start) * 1000
    if oracle and oracle(output):
        return {"verdict": "silent wrong output", "detail": "", "ms": elapsed}
    return {"verdict": "completed", "detail": "", "ms": elapsed}


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    RESULTS.mkdir(parents=True, exist_ok=True)

    rows = []
    for program_id, fault, description, text, oracle in programs():
        path = WORK / f"{program_id}.prep"
        path.write_text(text, encoding="utf-8")
        preplang = compile_program(path)
        baseline = run_baseline(path, WORK / f"{program_id}_baseline_output", oracle)
        rows.append({"id": program_id, "class": fault, "fault": description, "preplang": preplang,
                     "baseline": baseline})
        print(f"{program_id:10} {fault:10} PrepLang: {preplang['verdict']:24} baseline: {baseline['verdict']}")

    (RESULTS / "rq1.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    write_markdown(rows)


def write_markdown(rows):
    mutants = [r for r in rows if r["class"] != "control"]
    controls = [r for r in rows if r["class"] == "control"]
    before = sum(r["preplang"]["verdict"].startswith("compile") for r in mutants)
    errors = sum(r["preplang"]["verdict"].startswith("compile error") for r in mutants)
    crashes = sum(r["baseline"]["verdict"].startswith("run-time crash") for r in mutants)
    silent = sum(r["baseline"]["verdict"] == "silent wrong output" for r in mutants)
    false_alarms = sum(r["preplang"]["verdict"] != "not reported" for r in controls)

    lines = [
        "# RQ1: when is each injected mistake found?",
        "",
        f"- Mutants: {len(mutants)}; controls (original programs): {len(controls)}",
        f"- PrepLang reports {before}/{len(mutants)} before any data is processed "
        f"({errors} as errors, {before - errors} as warnings); false alarms on controls: {false_alarms}",
        f"- Script-style baseline: {crashes} crash while processing, {silent} finish with silently wrong output",
        "",
        "| ID | Class | Injected mistake | PrepLang (compile time) | Baseline (run time) | PrepLang ms | Baseline ms |",
        "|---|---|---|---|---|---:|---:|",
    ]
    for r in rows:
        lines.append(f"| {r['id']} | {r['class']} | {r['fault']} | {r['preplang']['verdict']} | "
                     f"{r['baseline']['verdict']} | {r['preplang']['ms']:.0f} | {r['baseline']['ms']:.0f} |")

    proven = sum(r["preplang"]["contracts"].get("proven", 0) for r in controls)
    deferred = sum(r["preplang"]["contracts"].get("checked at run time", 0) for r in controls)
    lines += ["", "# RQ2: contracts decided at compile time (controls)", "",
              f"- proven statically: {proven}, left for run time: {deferred}, "
              f"decided statically: {100 * proven / max(proven + deferred, 1):.0f}%"]
    (RESULTS / "rq1.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:6]))


if __name__ == "__main__":
    main()
