"""Performance of the optimizations (RQ3) and the cost of compiling (RQ4).

    py -3 eval/datasets.py benchmark          # once: writes eval/data/
    py -3 eval/bench.py [path/to/preplang.exe]

Writes eval/results/rq3.json and eval/results/rq3.md. Times are the median of
REPEAT runs and include starting Python, as a user would run the program.
"""
import filecmp
import json
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "eval" / "data"
WORK = ROOT / "eval" / "work" / "bench"
RESULTS = ROOT / "eval" / "results"
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe"
PYTHON = sys.executable
REPEAT = 3

MEMORY_RUNNER = (
    "import runpy, sys, tracemalloc\n"
    "tracemalloc.start()\n"
    "sys.argv = sys.argv[1:]\n"
    "runpy.run_path(sys.argv[0], run_name='__main__')\n"
    "print('PEAK', tracemalloc.get_traced_memory()[1])\n"
)


def median_time(command, cwd):
    times = []
    for _ in range(REPEAT):
        start = time.perf_counter()
        subprocess.run(command, cwd=cwd, check=True, capture_output=True)
        times.append(time.perf_counter() - start)
    return statistics.median(times)


def peak_memory(command, cwd):
    run = subprocess.run([PYTHON, "-c", MEMORY_RUNNER, *command], cwd=cwd, check=True, capture_output=True, text=True)
    return int(run.stdout.split("PEAK")[-1]) / 2**20  # MiB


def compile_variant(name, program, *flags):
    """Compiles `program` in its own folder and returns the folder."""
    folder = WORK / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    (folder / "program.prep").write_text(program, encoding="utf-8")
    subprocess.run([str(COMPILER), "program.prep", "--python", *flags], cwd=folder, check=True, capture_output=True)
    return folder


def table_optimizations():
    program = f"""LOAD "{(DATA / 'wide.csv').as_posix()}" AS wide;
EXPECT wide.f0 BETWEEN 0 AND 1;
EXPECT wide.id UNIQUE;
FILTER wide WHERE f0 > 0.5 AND k0 >= 2;
SELECT wide [id, f0, f1, s0];
SAVE wide AS "wide_clean.csv";
"""
    optimized = compile_variant("table_optimized", program)
    unoptimized = compile_variant("table_O0", program, "--O0")
    (WORK / "table_baseline").mkdir(exist_ok=True)
    (WORK / "table_baseline" / "program.prep").write_text(program, encoding="utf-8")

    result = {
        "optimized_s": median_time([PYTHON, "output/generated.py"], optimized),
        "O0_s": median_time([PYTHON, "output/generated.py"], unoptimized),
        "baseline_s": median_time([PYTHON, str(ROOT / "eval" / "baseline.py"), "program.prep", "out"],
                                  WORK / "table_baseline"),
        "same_output": filecmp.cmp(optimized / "output" / "wide_clean.csv",
                                   unoptimized / "output" / "wide_clean.csv", shallow=False),
        "columns_read": [5, 40],
        "runtime_checks": [0, 2],
    }
    result["speedup_vs_O0"] = result["O0_s"] / result["optimized_s"]
    return result


def image_streaming():
    program = f"""LOAD_IMAGES "{(DATA / 'images_1000').as_posix()}/" AS imgs;
RESIZE imgs TO 224 224;
GRAYSCALE imgs;
CONVERT imgs TO JPG;
SAVE_IMAGES imgs TO "small/";
"""
    folder = compile_variant("images_streaming", program)
    baseline = WORK / "images_baseline"
    baseline.mkdir(exist_ok=True)
    (baseline / "program.prep").write_text(program, encoding="utf-8")
    baseline_command = [str(ROOT / "eval" / "baseline.py"), "program.prep", "out"]

    names = sorted(p.name for p in (folder / "output" / "small").glob("*.jpg"))[:20]
    subprocess.run([PYTHON, *baseline_command], cwd=baseline, check=True, capture_output=True)
    identical = all(np.array_equal(cv2.imread(str(folder / "output" / "small" / n), cv2.IMREAD_UNCHANGED),
                                   cv2.imread(str(baseline / "out" / "small" / n), cv2.IMREAD_UNCHANGED))
                    for n in names)
    return {
        "images": 1000,
        "preplang_s": median_time([PYTHON, "output/generated.py"], folder),
        "baseline_s": median_time([PYTHON, *baseline_command], baseline),
        "preplang_peak_mib": peak_memory(["output/generated.py"], folder),
        "baseline_peak_mib": peak_memory(baseline_command, baseline),
        "same_pixels_in_sample": identical,
    }


def resize_fusion():
    program = f"""LOAD_IMAGES "{(DATA / 'images_1000').as_posix()}/" AS imgs;
RESIZE imgs TO 512 512;
RESIZE imgs TO 224 224;
CONVERT imgs TO PNG;
SAVE_IMAGES imgs TO "small/";
"""
    exact = compile_variant("fusion_exact", program)
    approximate = compile_variant("fusion_approx", program, "--approx")
    exact_s = median_time([PYTHON, "output/generated.py"], exact)
    approximate_s = median_time([PYTHON, "output/generated.py"], approximate)
    psnr = []
    for path in sorted((exact / "output" / "small").glob("*.png")):
        a = cv2.imread(str(path))
        b = cv2.imread(str(approximate / "output" / "small" / path.name))
        psnr.append(cv2.PSNR(a, b))
    return {"exact_s": exact_s, "approx_s": approximate_s, "speedup": exact_s / approximate_s,
            "mean_psnr_db": statistics.mean(psnr), "min_psnr_db": min(psnr)}


def compile_cost():
    """Compile time (--explain reads every input) next to one plain read of the same data in Python."""
    import pandas as pd
    rows = []
    for count in (10_000, 100_000, 1_000_000):
        csv = DATA / f"tall_{count}.csv"
        program = f'LOAD "{csv.as_posix()}" AS t;\nSAVE t AS "o.csv";\n'
        folder = WORK / f"compile_table_{count}"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "program.prep").write_text(program, encoding="utf-8")
        seconds = median_time([str(COMPILER), "program.prep", "--explain"], folder)
        start = time.perf_counter()
        pd.read_csv(csv)
        read = time.perf_counter() - start
        rows.append({"input": f"CSV, {count:,} rows x 10 columns", "compile_s": seconds, "read_all_s": read})
    for count in (100, 1000, 3000):
        images = DATA / f"images_{count}"
        program = f'LOAD_IMAGES "{images.as_posix()}/" AS i;\nSAVE_IMAGES i TO "o/";\n'
        folder = WORK / f"compile_images_{count}"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "program.prep").write_text(program, encoding="utf-8")
        seconds = median_time([str(COMPILER), "program.prep", "--explain"], folder)
        start = time.perf_counter()
        for path in images.glob("*.jpg"):
            cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
        decode = time.perf_counter() - start
        rows.append({"input": f"{count} JPG images, 640x480", "compile_s": seconds, "read_all_s": decode})
    return rows


def main():
    if not DATA.exists():
        sys.exit("run  py -3 eval/datasets.py benchmark  first")
    WORK.mkdir(parents=True, exist_ok=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    results = {}
    for name, experiment in [("table_optimizations", table_optimizations), ("image_streaming", image_streaming),
                             ("resize_fusion", resize_fusion), ("compile_cost", compile_cost)]:
        print("running", name, flush=True)
        results[name] = experiment()
        print(json.dumps(results[name], indent=2), flush=True)
    (RESULTS / "rq3.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    write_markdown(results)


def write_markdown(r):
    t, s, f = r["table_optimizations"], r["image_streaming"], r["resize_fusion"]
    lines = [
        "# RQ3: what the optimizations buy",
        "",
        "## Tables: column pruning and contract-check elimination (200,000 rows x 40 columns)",
        "",
        "| Variant | Columns read | Run-time checks | Time (s) |",
        "|---|---:|---:|---:|",
        f"| PrepLang, optimized | 5 | 0 | {t['optimized_s']:.2f} |",
        f"| PrepLang, --O0 | 40 | 2 | {t['O0_s']:.2f} |",
        f"| Script-style baseline | 40 | 2 | {t['baseline_s']:.2f} |",
        "",
        f"Speedup of the optimizer: {t['speedup_vs_O0']:.2f}x; optimized and --O0 outputs identical: {t['same_output']}",
        "",
        "## Images: one streaming pass vs. loading everything (1,000 images, 640x480)",
        "",
        "| Variant | Time (s) | Peak memory (MiB) |",
        "|---|---:|---:|",
        f"| PrepLang (streaming) | {s['preplang_s']:.2f} | {s['preplang_peak_mib']:.0f} |",
        f"| Script-style baseline (all in memory) | {s['baseline_s']:.2f} | {s['baseline_peak_mib']:.0f} |",
        "",
        f"Same pixels in a sample of 20 outputs: {s['same_pixels_in_sample']}",
        "",
        "## Approximate RESIZE fusion (512x512 then 224x224, 1,000 images)",
        "",
        f"- exact: {f['exact_s']:.2f} s, --approx: {f['approx_s']:.2f} s, speedup {f['speedup']:.2f}x",
        f"- PSNR of approximate vs. exact output: mean {f['mean_psnr_db']:.1f} dB, minimum {f['min_psnr_db']:.1f} dB",
        "",
        "# RQ4: cost of compiling (reading the data at compile time)",
        "",
        "| Input | Compile time (s) | One read in Python (s): pandas.read_csv, or cv2.imread of every image |",
        "|---|---:|---:|",
    ]
    for row in r["compile_cost"]:
        lines.append(f"| {row['input']} | {row['compile_s']:.2f} | {row['read_all_s']:.2f} |")
    (RESULTS / "rq3.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
