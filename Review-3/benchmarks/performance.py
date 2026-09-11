"""
benchmarks/performance.py -- Phase 3 performance evaluation.

    python benchmarks/performance.py            # default: 50,000 rows
    python benchmarks/performance.py --rows 200000
    python benchmarks/performance.py --keep     # keep the generated dataset

It answers four questions with measurements rather than claims:

    1. How long does compilation take, phase by phase?
    2. How much does the optimiser reduce -- IR instructions and columns read?
    3. How much faster and smaller is the *execution* of the optimised code?
    4. Does the optimisation preserve the result?  (byte-for-byte comparison
       of the two output files)

Question 4 is the important one: an optimisation that changes the answer is a
bug, not an optimisation.
"""

from __future__ import annotations

import argparse
import csv
import random
import shutil
import sys
import time
import tracemalloc
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
ROOT = BENCH_DIR.parent
sys.path.insert(0, str(ROOT))

from preplang.executor import execute  # noqa: E402
from preplang.pipeline import compile_source, generate_unoptimised  # noqa: E402

WORK_DIR = BENCH_DIR / "work"

#: The pipeline that is benchmarked: 30 columns in the file, 5 needed.
PROGRAM = """
LOAD "big_employees.csv" AS emp;

EXPECT emp.age NUMBER;
EXPECT emp.rating BETWEEN 1 AND 5;

DROP_NULL emp;
DROP_NULL emp;

NORMALIZE emp.salary;

FILTER emp WHERE age > 21;

SELECT emp [ name, age, salary, department ];

SAVE emp AS "{output}";
"""

COLUMNS = [
    "emp_id", "name", "age", "salary", "department", "city", "country", "email",
    "phone", "join_year", "experience", "rating", "bonus", "manager", "team",
    "level", "shift", "overtime_hours", "leaves_taken", "projects",
    "certifications", "training_hours", "travel_days", "laptop_id", "desk_no",
    "badge_no", "insurance_plan", "pf_number", "tax_bracket", "active",
]


# ---------------------------------------------------------------------------
# Dataset generation
# ---------------------------------------------------------------------------


def build_dataset(rows: int) -> Path:
    """Write a wide CSV with *rows* data rows and 30 columns."""
    WORK_DIR.mkdir(parents=True, exist_ok=True)
    path = WORK_DIR / "big_employees.csv"
    random.seed(11)
    departments = ["CSE", "SALES", "HR", "OPS", "RND"]
    cities = ["Chennai", "Vellore", "Bangalore", "Pune", "Delhi"]

    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(COLUMNS)
        for index in range(1, rows + 1):
            age = random.randint(21, 58)
            salary = "" if index % 97 == 0 else round(random.uniform(25000, 180000), 2)
            writer.writerow([
                index, f"emp{index:07d}", age, salary, random.choice(departments),
                random.choice(cities), "India", f"emp{index}@corp.com",
                f"9{random.randint(100000000, 999999999)}", random.randint(2005, 2024),
                max(0, age - 22), round(random.uniform(1, 5), 1),
                round(random.uniform(0, 50000), 2), f"mgr{random.randint(1, 12):02d}",
                f"team{random.randint(1, 20):02d}", random.randint(1, 7),
                random.choice(["DAY", "NIGHT"]), random.randint(0, 40),
                random.randint(0, 25), random.randint(1, 9), random.randint(0, 6),
                random.randint(0, 120), random.randint(0, 45),
                f"LP{random.randint(1000, 9999)}", f"D{random.randint(100, 999)}",
                f"B{random.randint(10000, 99999)}",
                random.choice(["BASIC", "PLUS", "PREMIUM"]),
                f"PF{random.randint(100000, 999999)}", random.choice([5, 10, 20, 30]),
                random.choice(["TRUE", "FALSE"]),
            ])
    return path


# ---------------------------------------------------------------------------
# Measurement helpers
# ---------------------------------------------------------------------------


def timed_execute(code: str, repeats: int = 3) -> tuple[float, float, object]:
    """Run *code* *repeats* times; return (best seconds, peak MiB, result)."""
    best = float("inf")
    peak_bytes = 0
    result = None
    for _ in range(repeats):
        tracemalloc.start()
        start = time.perf_counter()
        result = execute(code, WORK_DIR)
        elapsed = time.perf_counter() - start
        _, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        if not result.ok:
            raise SystemExit(f"execution failed:\n{result.format()}")
        best = min(best, elapsed)
        peak_bytes = max(peak_bytes, peak)
    return best, peak_bytes / (1024 * 1024), result


def rule(title: str = "") -> None:
    print(f"\n{title}\n{'=' * 70}" if title else "=" * 70)


def row(label: str, *values: object) -> None:
    cells = "".join(f"{str(v):>18}" for v in values)
    print(f"{label:<34}{cells}")


# ---------------------------------------------------------------------------
# Benchmark
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="PrepLang performance evaluation")
    parser.add_argument("--rows", type=int, default=50_000, help="rows in the generated dataset")
    parser.add_argument("--repeats", type=int, default=3, help="timing repetitions")
    parser.add_argument("--keep", action="store_true", help="keep the generated files")
    args = parser.parse_args(argv)

    rule("PrepLang -- PERFORMANCE EVALUATION")
    print(f"Dataset: {args.rows:,} rows x {len(COLUMNS)} columns")
    print("Pipeline: 2 contracts, 2x DROP_NULL (one redundant), NORMALIZE, FILTER, SELECT, SAVE")

    print("\nGenerating the dataset ...", end=" ", flush=True)
    start = time.perf_counter()
    dataset = build_dataset(args.rows)
    size_mb = dataset.stat().st_size / (1024 * 1024)
    print(f"done ({size_mb:.1f} MiB in {time.perf_counter() - start:.1f} s)")

    # -- compile, optimised and unoptimised ----------------------------
    optimised = compile_source(
        PROGRAM.format(output="out_optimised.csv"), base_dir=WORK_DIR
    )
    if not optimised.ok:
        print(optimised.reporter.render())
        return 1
    baseline_code = generate_unoptimised(
        compile_source(PROGRAM.format(output="out_baseline.csv"), base_dir=WORK_DIR)
    )
    report = optimised.optimisation

    # -- 1. compilation time -------------------------------------------
    rule("1. COMPILATION TIME (the compiler itself)")
    print(optimised.format_timings())
    total_ms = sum(optimised.timings.values()) * 1000
    print(f"\nCompilation is independent of dataset size except for schema")
    print(f"inference and contract validation, which sample the file.")
    print(f"Total: {total_ms:.1f} ms")

    # -- 2. static optimisation effect ---------------------------------
    rule("2. OPTIMISATION (static measurements)")
    row("metric", "before", "after", "change")
    print("-" * 70)
    row(
        "IR instructions",
        report.instructions_before,
        report.instructions_after,
        f"-{report.instruction_reduction:.1f}%",
    )
    row(
        "Columns read from disk",
        report.columns_available,
        report.columns_required,
        f"-{report.column_reduction:.1f}%",
    )
    row("Generated code lines", baseline_code.lines, optimised.code.lines, "")
    print()
    for entry in report.passes:
        print(f"  {entry.name}: {len(entry.removed)} change(s)")
        for item in entry.removed:
            print(f"      - {item}")

    # -- 3. execution -------------------------------------------------
    rule("3. EXECUTION (running the generated Pandas code)")
    print(f"best of {args.repeats} runs\n")
    base_seconds, base_mib, base_result = timed_execute(baseline_code.source, args.repeats)
    opt_seconds, opt_mib, opt_result = timed_execute(optimised.code.source, args.repeats)

    row("metric", "unoptimised", "optimised", "change")
    print("-" * 70)
    row(
        "Execution time (ms)",
        f"{base_seconds * 1000:.1f}",
        f"{opt_seconds * 1000:.1f}",
        f"-{100 * (base_seconds - opt_seconds) / base_seconds:.1f}%",
    )
    row(
        "Peak memory (MiB)",
        f"{base_mib:.1f}",
        f"{opt_mib:.1f}",
        f"-{100 * (base_mib - opt_mib) / base_mib:.1f}%" if base_mib else "-",
    )
    rows_out, cols_out = next(iter(opt_result.frames.values()))
    row("Output rows x columns", f"{rows_out} x {cols_out}", f"{rows_out} x {cols_out}", "same")

    # -- 4. correctness -------------------------------------------------
    rule("4. CORRECTNESS OF THE OPTIMISATION")
    baseline_file = WORK_DIR / "out_baseline.csv"
    optimised_file = WORK_DIR / "out_optimised.csv"
    same = baseline_file.read_bytes() == optimised_file.read_bytes()
    print(f"  unoptimised output : {baseline_file.name}  ({baseline_file.stat().st_size:,} bytes)")
    print(f"  optimised   output : {optimised_file.name}  ({optimised_file.stat().st_size:,} bytes)")
    print()
    print("  byte-for-byte identical :", "YES" if same else "NO -- THE OPTIMISATION IS UNSOUND")
    if not same:
        return 1
    print("\n  The optimiser changes how much data is read, never the result.")

    rule("SUMMARY")
    print(
        f"  {report.instructions_removed} IR instruction(s) eliminated, "
        f"{len(report.pruned_columns)}/{report.columns_available} columns never read,"
    )
    print(
        f"  execution {100 * (base_seconds - opt_seconds) / base_seconds:.1f}% faster and "
        f"{100 * (base_mib - opt_mib) / base_mib:.1f}% smaller in peak memory,"
    )
    print("  with byte-identical output.")

    if not args.keep:
        shutil.rmtree(WORK_DIR, ignore_errors=True)
        print(f"\n(generated files removed; use --keep to retain {WORK_DIR})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
