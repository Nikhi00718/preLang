"""What does certification cost at compile time? (part of RQ7)

    py -3 eval/certcost.py [path/to/preplang.exe]

For the example programs and the real pipelines of eval/realbatches.py (compiled on their first
month), the number of re-analyses the certifier runs and the compile time (median of 5), with
deoptimization (full, admission and item certificates) and with --no-deopt (the full certificate).
Writes eval/results/certcost.json and .md.
"""
import json
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from realbatches import WORKLOADS, prepare  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "eval" / "work" / "certcost"
RESULTS = ROOT / "eval" / "results"


def measure(compiler, folder, program, extra):
    times = []
    for _ in range(5):
        start = time.perf_counter()
        done = subprocess.run([str(compiler), program, "--certificate", *extra], cwd=folder, capture_output=True,
                              text=True)
        times.append(time.perf_counter() - start)
    runs = re.search(r"program (\d+) more times", done.stdout)
    removals = done.stdout.count("\nline ") if "Speculatively removed" in done.stdout else 0
    return {"seconds": statistics.median(times), "analyses": int(runs.group(1)) if runs else 0,
            "removals": removals}


def main(compiler):
    jobs = []
    for prep in sorted((ROOT / "examples").rglob("*.prep")):
        if "errors" not in prep.parts:
            copy = WORK / "examples" / prep.parent.name  # compiling writes output/ next to the program
            if not copy.exists():
                shutil.copytree(prep.parent, copy, ignore=shutil.ignore_patterns("output", "processed"))
            jobs.append((prep.stem, copy, prep.name))
    for workload, (program, input_file, _, folder) in WORKLOADS.items():
        target = WORK / workload
        prepare(target, program, input_file, sorted(folder.glob("*.*"))[0])
        jobs.append((workload, target, "program.prep"))
    rows = []
    for name, folder, program in jobs:
        row = {"program": name, "deopt": measure(compiler, folder, program, []),
               "full": measure(compiler, folder, program, ["--no-deopt"])}
        rows.append(row)
        print(name, row, flush=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "certcost.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    lines = ["# Certification cost at compile time", "",
             "| Program | Removals | Re-analyses (deopt) | Compile s (deopt) | Re-analyses (--no-deopt) | "
             "Compile s (--no-deopt) |", "|---|---:|---:|---:|---:|---:|"]
    for r in rows:
        lines.append(f"| {r['program']} | {r['deopt']['removals']} | {r['deopt']['analyses']} | "
                     f"{r['deopt']['seconds']:.3f} | {r['full']['analyses']} | {r['full']['seconds']:.3f} |")
    lines.append("")
    (RESULTS / "certcost.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe")
