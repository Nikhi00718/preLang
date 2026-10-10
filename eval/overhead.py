"""What does reusing a compiled program cost? (RQ11)

    py -3 eval/overhead.py [path/to/preplang.exe]

For the real workloads of eval/realbatches.py, compiled on the first month, every
later month is run (median of REPEAT runs, nothing else running):
    trusted   the optimized program on the month, with no admission check (the floor)
    full      with admission against the full certificate (--no-deopt)
    deopt     with admission, input facts and deoptimization (PrepLang's default)
    compile   a fresh compilation of the month (preplang --python), without running it
    fresh     a fresh compilation and run
The admission overhead is (deopt - trusted) / trusted; recompiling instead costs
compile. The validators' own validation time comes from eval/baselines.py.
Writes eval/results/overhead.json and .md.
"""
import json
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from policies import compile_variants, run_program  # noqa: E402
from realbatches import WORKLOADS, prepare  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "eval" / "work" / "overhead"
RESULTS = ROOT / "eval" / "results"
REPEAT = 3


def timed(action):
    times = []
    for _ in range(REPEAT):
        start = time.perf_counter()
        result = action()
        times.append(time.perf_counter() - start)
    return statistics.median(times), result


def main(compiler):
    rows = []
    for workload, (program, input_file, name, folder) in WORKLOADS.items():
        months = sorted(folder.glob("*.*"))
        compiled = WORK / workload / "compiled"
        prepare(compiled, program, input_file, months[0])
        ok, text = compile_variants(compiler, compiled, "program.prep")
        assert ok, text
        for month in months[1:]:
            row = {"workload": workload, "month": month.stem}
            for key, script, trust in (("trusted", "generated_full.py", True), ("full", "generated_full.py", False),
                                       ("deopt", "generated.py", False)):
                seconds, result = timed(lambda: run_program(compiled, script, {name: month}, trust=trust))
                row[key] = seconds
                row[key + "_status"] = result["status"]
            fresh = WORK / workload / "fresh"
            prepare(fresh, program, input_file, month)
            row["compile"], _ = timed(lambda: subprocess.run([str(compiler), "program.prep", "--python"], cwd=fresh,
                                                             capture_output=True))
            row["fresh"], _ = timed(lambda: subprocess.run([str(compiler), "program.prep", "--run"], cwd=fresh,
                                                           capture_output=True))
            rows.append(row)
            print(workload, month.stem, {k: round(v, 3) for k, v in row.items() if isinstance(v, float)}, flush=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "overhead.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    write_markdown(rows)


def write_markdown(rows):
    lines = ["# RQ11: the cost of reusing a compiled program", "",
             f"Medians over the months (each month: median of {REPEAT} runs); only months every variant finished.", "",
             "| Workload | Months | Trusted run (s) | Admission, full certificate (s) | Admission + deoptimization (s) | "
             "Overhead | Fresh compile (s) | Fresh compile + run (s) |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for workload in dict.fromkeys(r["workload"] for r in rows):
        items = [r for r in rows if r["workload"] == workload and
                 r["trusted_status"] == r["deopt_status"] == r["full_status"] == "finished"]
        if not items:
            continue
        med = lambda key: statistics.median(r[key] for r in items)  # noqa: E731
        overhead = statistics.median((r["deopt"] - r["trusted"]) / r["trusted"] for r in items)
        lines.append(f"| {workload} | {len(items)} | {med('trusted'):.2f} | {med('full'):.2f} | {med('deopt'):.2f} | "
                     f"{100 * overhead:.1f}% | {med('compile'):.2f} | {med('fresh'):.2f} |")
    lines.append("")
    (RESULTS / "overhead.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe")
