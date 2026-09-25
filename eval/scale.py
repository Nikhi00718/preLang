"""How long until a mistake is reported, on large inputs (RQ5).

    py -3 eval/datasets.py benchmark     # once: images
    py -3 eval/datasets.py scale         # once: Parquet tables, broken image folder
    py -3 eval/scale.py [path/to/preplang.exe]

Each case is run by PrepLang (compile; run the generated program if it compiles)
and by a script that performs the same pipeline: the script-style baseline for
tables, and eval/handwritten.py, which streams images one at a time, for images.
Writes eval/results/scale.json and eval/results/scale.md.
"""
import json
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

import cv2
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "eval" / "data"
WORK = ROOT / "eval" / "work" / "scale"
RESULTS = ROOT / "eval" / "results"
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe"
PYTHON = sys.executable
REPEAT = 3

TABLE_PROGRAM = """LOAD "{path}" AS events;
EXPECT events.amount BETWEEN 0 AND 999;
FILL_NULL events.b WITH 0;
NORMALIZE events.a USING ZSCORE;
FILTER events WHERE k >= 1 AND b > 0.1;
SELECT events [id, a, b, amount];
SAVE events AS "events_clean.parquet";
"""

IMAGE_PROGRAM = """LOAD_IMAGES "{folder}/" AS imgs;
RESIZE imgs TO 224 224;
GRAYSCALE imgs;
{extra}CONVERT imgs TO {format};
SAVE_IMAGES imgs TO "small/";
"""


def median_run(command, cwd):
    """Median time of REPEAT runs, and the result of the last run."""
    times = []
    for _ in range(REPEAT):
        start = time.perf_counter()
        done = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
        times.append(time.perf_counter() - start)
    return statistics.median(times), done


def outcome(done):
    if done.returncode == 0:
        return "finished"
    lines = (done.stderr or done.stdout).strip().splitlines()
    return "crashed: " + (lines[-1][:90] if lines else "")


def prepare(name, program):
    folder = WORK / re.sub(r"\W+", "_", name)
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    (folder / "program.prep").write_text(program, encoding="utf-8")
    return folder


def all_black(folder):
    files = sorted(folder.rglob("*.png"))[:50]
    return bool(files) and max(cv2.imread(str(p), cv2.IMREAD_UNCHANGED).mean() for p in files) < 2


def compare(name, program, script):
    """PrepLang against a script running the same pipeline. `script(folder)` is the script's command."""
    folder = prepare(name, program)
    compile_s, compiled = median_run([str(COMPILER), "program.prep", "--python"], folder)
    errors = re.findall(r"^error\[\w+\]: (.*)$", compiled.stdout, re.M)
    row = {"case": name, "preplang_compile_s": compile_s,
           "preplang_report": "compile error: " + errors[0][:90] if errors else "compiled"}
    if compiled.returncode == 0:
        row["preplang_run_s"], done = median_run([PYTHON, "output/generated.py"], folder)
        row["preplang_run"] = outcome(done)
    row["script_s"], done = median_run(script(folder), folder)
    row["script_result"] = outcome(done)
    if done.returncode == 0 and all_black(folder / "out"):
        row["script_result"] = "finished, but every image is black"
    return row


def parquet_compile_cost():
    rows = []
    for count in (1_000_000, 4_000_000, 16_000_000):
        path = DATA / f"events_{count}.parquet"
        folder = prepare(f"compile_{count}", f'LOAD "{path.as_posix()}" AS e;\nSAVE e AS "o.parquet";\n')
        compile_s, _ = median_run([str(COMPILER), "program.prep", "--explain"], folder)
        start = time.perf_counter()
        pd.read_parquet(path)
        rows.append({"rows": count, "mib": path.stat().st_size / 2**20, "compile_s": compile_s,
                     "read_all_s": time.perf_counter() - start})
    return rows


def table_cases():
    program = TABLE_PROGRAM.format(path=(DATA / "events_16000000.parquet").as_posix())

    def baseline(folder):
        return [PYTHON, str(ROOT / "eval" / "baseline.py"), "program.prep", "out"]

    cases = [
        ("tables: correct program", program),
        ("tables: misspelled column in the last step", program.replace("[id, a, b, amount]", "[id, a, b, amont]")),
        ("tables: range contract broken by the data", program.replace("BETWEEN 0 AND 999", "BETWEEN 0 AND 500")),
    ]
    return [compare(name, text, baseline) for name, text in cases]


def image_cases():
    def program(folder, extra="", image_format="JPG"):
        return IMAGE_PROGRAM.format(folder=(DATA / folder).as_posix(), extra=extra, format=image_format)

    def script(images, mode):
        def command(folder):
            return [PYTHON, str(ROOT / "eval" / "handwritten.py"), str(DATA / images), "out", mode]
        return command

    return [
        compare("images: correct program", program("images_3000"), script("images_3000", "jpg")),
        compare("images: broken file (sorted last) not removed", program("images_3000_broken"),
                script("images_3000_broken", "jpg")),
        compare("images: float pixels saved as PNG", program("images_3000", "NORMALIZE imgs RANGE 0 1;\n", "PNG"),
                script("images_3000", "float-png")),
    ]


def main():
    if not (DATA / "events_16000000.parquet").exists():
        sys.exit("run  py -3 eval/datasets.py benchmark  and  py -3 eval/datasets.py scale  first")
    WORK.mkdir(parents=True, exist_ok=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    results = {}
    for name, experiment in [("parquet_compile_cost", parquet_compile_cost), ("table_cases", table_cases),
                             ("image_cases", image_cases)]:
        print("running", name, flush=True)
        results[name] = experiment()
        print(json.dumps(results[name], indent=2), flush=True)
    (RESULTS / "scale.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    write_markdown(results)


def write_markdown(r):
    lines = [
        "# RQ5: large inputs",
        "",
        "## Compile time against a Parquet file (only the footer is read)",
        "",
        "| Rows | File size (MiB) | Compile time (s) | pandas.read_parquet (s) |",
        "|---:|---:|---:|---:|",
    ]
    for row in r["parquet_compile_cost"]:
        lines.append(f"| {row['rows']:,} | {row['mib']:.0f} | {row['compile_s']:.2f} | {row['read_all_s']:.2f} |")
    lines += [
        "",
        "## Time until a mistake is reported",
        "",
        "Tables: 16,000,000 rows (Parquet). Images: 3,000 JPG files of 640x480. Times are medians of "
        f"{REPEAT} runs and include starting the process.",
        "",
        "| Case | PrepLang compile (s) | PrepLang | PrepLang run (s) | Script (s) | Script |",
        "|---|---:|---|---:|---:|---|",
    ]
    for row in r["table_cases"] + r["image_cases"]:
        run = f"{row['preplang_run_s']:.2f}" if "preplang_run_s" in row else "-"
        lines.append(f"| {row['case']} | {row['preplang_compile_s']:.2f} | {row['preplang_report']} | {run} | "
                     f"{row['script_s']:.2f} | {row['script_result']} |")
    (RESULTS / "scale.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
