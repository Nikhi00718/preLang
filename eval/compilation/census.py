"""Optimization effectiveness, performance and overhead on programs other people wrote.

The corpus is every program of examples/ and the programs lifted from GitHub pandas
scripts by the reuse experiments (eval/work/pandas_corpus, pandas_corpus_houses,
drift_corpus_*, realbatches/*/fresh: run those first). For each program

    work removed    instructions, run-time contract checks and columns read, before and
                    after the optimizer, and what each pass removed (from --ir)
    run time        the --O0 and the optimized program on the program's own data, in this
                    process (pandas imported once), median of 3; the outputs must be equal
    overhead        compile time and the compiler's peak memory, median of 3

and, for the cost of reading the data at compile time, the compiler's time and peak memory
on the large inputs of eval/data (CSV up to 1M rows, Parquet up to 16M rows, 3,000 images).

    py -3 eval/compilation/census.py [--quick]

Writes eval/results/census.json and .md.
"""
import argparse
import contextlib
import io
import json
import os
import re
import runpy
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "eval" / "work" / "census"
DATA = ROOT / "eval" / "data"
# PREPLANG_RESULTS: another folder for the results (eval/evaluate.py --quick)
RESULTS = Path(os.environ.get("PREPLANG_RESULTS") or ROOT / "eval" / "results")
COMPILER = ROOT / "build" / ("Release/preplang.exe" if os.name == "nt" else "preplang")
SOURCES = {  # corpus -> folders holding program.prep and its data
    "examples": None,
    "Titanic scripts": ROOT / "eval" / "work" / "pandas_corpus",
    "House Prices scripts": ROOT / "eval" / "work" / "pandas_corpus_houses",
    "traffic scripts": ROOT / "eval" / "work" / "drift_corpus_traffic",
    "bike scripts (days)": ROOT / "eval" / "work" / "drift_corpus_bikes",
    "bike scripts (hours)": ROOT / "eval" / "work" / "drift_corpus_hours",
    "monthly pipelines": ROOT / "eval" / "work" / "realbatches",
}
REPEAT = 3


def measured(command, cwd):
    """Runs a command; returns (seconds, peak memory in MiB, completed process)."""
    start = time.perf_counter()
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
                (name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                                                     "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
                                                     "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")]
        process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        out, _ = process.communicate()
        seconds = time.perf_counter() - start
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        ctypes.windll.psapi.GetProcessMemoryInfo(int(process._handle), ctypes.byref(counters), counters.cb)
        peak = counters.PeakWorkingSetSize
        code = process.returncode
    else:
        process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        out = process.stdout.read()
        _, status, usage = os.wait4(process.pid, 0)
        seconds = time.perf_counter() - start
        peak = usage.ru_maxrss * 1024  # KiB on Linux
        code = os.waitstatus_to_exitcode(status)
        process.returncode = code
    return seconds, peak / 2**20, subprocess.CompletedProcess(command, code, out)


def corpus():
    """(corpus, name, program path) for every program."""
    for path in sorted((ROOT / "examples").rglob("*.prep")):
        if "errors" not in path.parts:
            yield "examples", path.stem, path
    for name, folder in SOURCES.items():
        if folder is None or not folder.exists():
            continue
        programs = folder.glob("*/fresh/program.prep") if folder.name == "realbatches" else folder.glob("*/program.prep")
        for program in sorted(programs):
            yield name, program.parent.name if folder.name != "realbatches" else program.parent.parent.name, program


def ir_counts(text):
    """Instructions, run-time checks and columns read, before and after the optimizer."""
    def section(title):
        return re.search(r"=+ " + re.escape(title) + r" =+\n(.*?)(?:\n=+ |\Z)", text, re.S).group(1)
    original, optimized = section("IR (IRBuilder)"), section("OPTIMIZED IR")
    def count(listing):
        live = [line for line in listing.splitlines() if re.match(r"\s+\d+\s", line) and "(speculative" not in line]
        loads = [re.search(r"\[(.*)\]", line) for line in live if re.match(r"\s+\d+\s+LOAD ", line)]
        return {"instructions": len(live),
                "checks": sum(1 for line in live if re.match(r"\s+\d+\s+CHECK ", line)),
                "columns": sum(len(m.group(1).split(", ")) for m in loads if m)}
    report = text.split("OPTIMIZATION REPORT (PassManager)")[1].split("Instructions:")[0]
    kinds = {}
    for line in report.splitlines():
        found = re.match(r"- ([a-z -]+?):", line)
        if found:
            kinds[found.group(1)] = kinds.get(found.group(1), 0) + 1
    return count(original), count(optimized), kinds


def run_here(folder, script):
    """Runs output/<script> in this process: (seconds, status, files written)."""
    out = folder / "output"
    for path in list(out.rglob("*")):
        if path.is_file() and not path.name.startswith("generated"):
            path.unlink()
    argv = sys.argv
    sys.argv = [str(out / script)]
    start = time.perf_counter()
    status = "finished"
    try:
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            runpy.run_path(str(out / script), run_name="__main__")
    except Exception as error:  # noqa: BLE001 - the program's failure is a result
        status = type(error).__name__
    finally:
        sys.argv = argv
    seconds = time.perf_counter() - start
    files = {p.relative_to(out).as_posix(): p.read_bytes() for p in sorted(out.rglob("*"))
             if p.is_file() and not p.name.startswith(("generated", "preplang_"))}
    return seconds, status, files


def measure(corpus_name, name, program, quick):
    folder = WORK / corpus_name.split()[0].lower() / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    _, _, done = measured([str(COMPILER), str(program), "--ir"], folder)
    if done.returncode != 0:
        return {"corpus": corpus_name, "program": name, "status": "compile error"}
    before, after, kinds = ir_counts(done.stdout)
    compiles = [measured([str(COMPILER), str(program), "--python"], folder) for _ in range(1 if quick else REPEAT)]
    shutil.copyfile(folder / "output" / "generated.py", folder / "output" / "generated_opt.py")
    measured([str(COMPILER), str(program), "--python", "--O0"], folder)
    shutil.copyfile(folder / "output" / "generated.py", folder / "output" / "generated_O0.py")
    runs = {}
    for variant in ("O0", "opt"):
        times = []
        for _ in range(1 if quick else REPEAT):
            seconds, status, files = run_here(folder, f"generated_{variant}.py")
            times.append(seconds)
        runs[variant] = {"seconds": statistics.median(times), "status": status, "files": files}
    return {"corpus": corpus_name, "program": name, "status": "compiled", "before": before, "after": after,
            "removed": kinds, "compile_s": statistics.median(c[0] for c in compiles),
            "compile_mib": max(c[1] for c in compiles),
            "run_O0_s": runs["O0"]["seconds"], "run_opt_s": runs["opt"]["seconds"],
            "run_status": runs["opt"]["status"],
            "same_output": runs["O0"]["status"] == runs["opt"]["status"] and runs["O0"]["files"] == runs["opt"]["files"]}


def large_inputs(quick):
    """Compile time and the compiler's peak memory on large inputs (--explain reads all of them)."""
    cases = [(f"CSV, {n:,} rows x 10 columns", f'LOAD "{(DATA / f"tall_{n}.csv").as_posix()}" AS t;\nSAVE t AS "o.csv";\n',
              DATA / f"tall_{n}.csv") for n in (10_000, 100_000, 1_000_000)]
    cases += [(f"Parquet, {n:,} rows (footer only)", f'LOAD "{(DATA / f"events_{n}.parquet").as_posix()}" AS t;\nSAVE t AS "o.csv";\n',
               DATA / f"events_{n}.parquet") for n in (1_000_000, 16_000_000)]
    cases += [(f"{n:,} JPG images, 640x480", f'LOAD_IMAGES "{(DATA / f"images_{n}").as_posix()}/" AS i;\nSAVE_IMAGES i TO "o/";\n',
               DATA / f"images_{n}") for n in (100, 1000, 3000)]
    rows = []
    for label, program, data in cases:
        if not data.exists():
            continue
        folder = WORK / "large" / re.sub(r"\W+", "_", label)
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "program.prep").write_text(program, encoding="utf-8")
        size = sum(p.stat().st_size for p in data.rglob("*")) if data.is_dir() else data.stat().st_size
        # The first run may read the files from disk (cold cache); the median is of the runs after it.
        first = measured([str(COMPILER), "program.prep", "--explain"], folder)
        runs = [measured([str(COMPILER), "program.prep", "--explain"], folder) for _ in range(1 if quick else REPEAT)]
        rows.append({"input": label, "mib": size / 2**20, "first_s": first[0],
                     "compile_s": statistics.median(r[0] for r in runs),
                     "compile_peak_mib": max(r[1] for r in runs + [first])})
    return rows


def write_markdown(results, large):
    ok = [r for r in results if r["status"] == "compiled"]
    lines = ["# Optimization effectiveness, performance and overhead on real programs", "",
             "Programs: examples/ and the programs lifted from GitHub pandas scripts (eval/compilation/census.py). "
             "Run times are of the generated program on the program's own data, in one Python process "
             "(pandas imported once), median of 3.", "",
             "## Work removed by the optimizer", "",
             "| Corpus | Programs | Instructions | Run-time checks | Columns read | Programs with something removed |",
             "|---|---:|---:|---:|---:|---:|"]
    def row(name, group):
        b = {k: sum(r["before"][k] for r in group) for k in ("instructions", "checks", "columns")}
        a = {k: sum(r["after"][k] for r in group) for k in ("instructions", "checks", "columns")}
        changed = sum(1 for r in group if r["before"] != r["after"])
        cell = lambda k: f"{b[k]} -> {a[k]} (-{100 * (b[k] - a[k]) / b[k]:.0f}%)" if b[k] else "0"
        return f"| {name} | {len(group)} | {cell('instructions')} | {cell('checks')} | {cell('columns')} | {changed} ({100 * changed / len(group):.0f}%) |"
    for name in dict.fromkeys(r["corpus"] for r in ok):
        lines.append(row(name, [r for r in ok if r["corpus"] == name]))
    lines.append(row("**all**", ok))
    kinds = {}
    for r in ok:
        for k, n in r["removed"].items():
            kinds[k] = kinds.get(k, 0) + n
    lines += ["", "What the passes did, over all programs: " +
              ", ".join(f"{k} {n}" for k, n in sorted(kinds.items(), key=lambda kv: -kv[1])) + ".", ""]
    same = sum(1 for r in ok if r["same_output"])
    speed = [r["run_O0_s"] / r["run_opt_s"] for r in ok if r["run_opt_s"] > 0 and r["run_status"] == "finished"]
    changed = [r["run_O0_s"] / r["run_opt_s"] for r in ok if r["before"] != r["after"] and r["run_status"] == "finished"]
    lines += ["## Run time of the generated programs", "",
              f"- --O0 and optimized wrote identical files (or failed the same way): {same} / {len(ok)}",
              f"- speedup of the optimized program (--O0 time / optimized time), median over the programs: "
              f"{statistics.median(speed):.2f}x (geometric mean {statistics.geometric_mean(speed):.2f}x, "
              f"range {min(speed):.2f}x to {max(speed):.2f}x)",
              f"- over the {len(changed)} programs the optimizer changed: median {statistics.median(changed):.2f}x, "
              f"geometric mean {statistics.geometric_mean(changed):.2f}x" if changed else "", "",
              "| Corpus | Programs | --O0 run (s), median | optimized run (s), median | compile (s), median | compiler peak memory (MiB), max |",
              "|---|---:|---:|---:|---:|---:|"]
    for name in dict.fromkeys(r["corpus"] for r in ok):
        group = [r for r in ok if r["corpus"] == name]
        lines.append(f"| {name} | {len(group)} | {statistics.median(r['run_O0_s'] for r in group):.3f} | "
                     f"{statistics.median(r['run_opt_s'] for r in group):.3f} | "
                     f"{statistics.median(r['compile_s'] for r in group):.3f} | {max(r['compile_mib'] for r in group):.1f} |")
    lines += ["", "## The compiler on large inputs", "",
              "| Input | Size (MiB) | Compile time (s), warm | First compile (s) | Compiler peak memory (MiB) |", "|---|---:|---:|---:|---:|"]
    for r in large:
        lines.append(f"| {r['input']} | {r['mib']:.1f} | {r['compile_s']:.2f} | {r['first_s']:.2f} | {r['compile_peak_mib']:.1f} |")
    lines += ["", "Warm: median of the runs after the first. First compile: the first run of the measurement, "
              "which reads the inputs from disk when they are not in the file cache. On the Windows machine of "
              "these results the first pass over an image folder was much slower than the next ones; the cause "
              "(file cache or antivirus scanning of each file) was not measured."]
    failed = [r for r in results if r["status"] != "compiled"]
    if failed:
        lines += ["", f"Not compiled: {len(failed)} ({', '.join(r['corpus'] + '/' + r['program'] for r in failed[:10])})"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true", help="one repetition, examples and 20 programs per corpus")
    args = parser.parse_args()
    programs = list(corpus())
    if args.quick:
        seen = {}
        programs = [p for p in programs if seen.setdefault(p[0], []).append(p) or len(seen[p[0]]) <= 20]
    results = []
    for index, (corpus_name, name, program) in enumerate(programs, 1):
        results.append(measure(corpus_name, name, program, args.quick))
        if index % 50 == 0:
            print(f"{index}/{len(programs)}", flush=True)
    large = large_inputs(args.quick)
    RESULTS.mkdir(exist_ok=True)
    (RESULTS / "census.json").write_text(json.dumps({"programs": results, "large_inputs": large}, indent=1),
                                         encoding="utf-8")
    text = write_markdown(results, large)
    (RESULTS / "census.md").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
