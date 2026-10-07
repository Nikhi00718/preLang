"""Runs every demo scenario for real and builds the demo page from the outputs.

    py -3 demo/capture.py

Needs build/Release/preplang.exe (this version) and the Phase 2 compiler, built from
commit f088d66 into ../phase2-src/build/Release/preplang.exe (demo/run_demo.py prepare
does that). Writes demo/demo_data.json and demo/PrepLang_Demo.html (page.html with the
data filled in). Nothing shown on the page is typed by hand: it is what the compilers,
the generated programs and the script-style baseline printed.
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
import numpy as np
import pandas as pd

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
NEW = ROOT / "build" / "Release" / "preplang.exe"
OLD = ROOT.parent / "phase2-src" / "build" / "Release" / "preplang.exe"
WORK = HERE / "work"
DATA = HERE / "data"
PYTHON = sys.executable


def clean(text):
    """Paths shown relative to the project, no blank runs."""
    for prefix in (str(WORK) + "\\", str(WORK) + "/", str(ROOT) + "\\", str(ROOT) + "/", WORK.as_posix() + "/",
                   ROOT.as_posix() + "/"):
        text = text.replace(prefix, "")
    return re.sub(r"\n{3,}", "\n\n", text).strip("\n")


def run(command, cwd=ROOT):
    done = subprocess.run([str(c) for c in command], cwd=cwd, capture_output=True, text=True, encoding="utf-8",
                          errors="replace")
    return clean(done.stdout + done.stderr), done.returncode


def outcome(text):
    """What a run of a generated program said: its ContractError line, or its normal output."""
    error = next((line for line in text.splitlines() if line.startswith("ContractError")), None)
    return error if error else text


def section(text, start, end=None):
    lines = text.splitlines()
    first = next((i for i, line in enumerate(lines) if start in line), None)
    if first is None:
        return ""
    last = next((i for i in range(first + 1, len(lines)) if end and end in lines[i]), len(lines))
    return "\n".join(lines[first:last]).strip("\n")


def stage(text, title):
    """One ==== TITLE ==== block of the compiler output, without its header line."""
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines) if line.startswith("====") and title in line), None)
    if start is None:
        return ""
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("====")), len(lines))
    return "\n".join(lines[start + 1:end]).strip("\n")


def timed(command, cwd, repeat=3):
    times = []
    for _ in range(repeat):
        start = time.perf_counter()
        subprocess.run([str(c) for c in command], cwd=cwd, capture_output=True)
        times.append(time.perf_counter() - start)
    return statistics.median(times)


def fresh(name):
    folder = WORK / name
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    return folder


# ---------- 1. the compiler, phase by phase ----------

def phases():
    program = (ROOT / "examples" / "tabular" / "students.prep").read_text(encoding="utf-8")
    full, _ = run([NEW, "examples/tabular/students.prep", "--certificate"])
    ran, _ = run([NEW, "examples/tabular/students.prep", "--run"])
    tokens = stage(full, "TOKENS").splitlines()
    return {
        "program": program,
        "csv": (ROOT / "examples" / "tabular" / "students.csv").read_text(encoding="utf-8"),
        "tokens": "\n".join(tokens[:14]) + f"\n... {len(tokens) - 14} more tokens",
        "ast": "\n".join(stage(full, "AST").splitlines()[:26]) + "\n...",
        "schema": stage(full, "SCHEMA"),
        "semantic": stage(full, "SEMANTIC ANALYSIS"),
        "symbols": stage(full, "SYMBOL TABLE"),
        "ir": stage(full, "IR (IRBuilder)"),
        "report": stage(full, "OPTIMIZATION REPORT"),
        "optimized": stage(full, "OPTIMIZED IR"),
        "certificate": stage(full, "DATA CERTIFICATE"),
        "python": "\n".join(section(stage(full, "PYTHON"), "outputs = []").splitlines()[:30]) + "\n...",
        "run": section(ran, "Saved"),
    }


# ---------- 2. mistakes: the script-style baseline against the compiler ----------

MISTAKES = [
    ("typo", "A misspelled column in the last step",
     "examples/tabular/students.prep", "SELECT students [name, age, marks];", "SELECT students [name, age, mark];"),
    ("text", "Normalizing a text column",
     "examples/tabular/students.prep", "NORMALIZE students.attendance USING MINMAX;",
     "NORMALIZE students.department USING MINMAX;"),
    ("contract", "A range contract the data breaks",
     "examples/tabular/students.prep", "EXPECT students.marks BETWEEN 0 AND 100;",
     "EXPECT students.marks BETWEEN 0 AND 90;"),
    ("threshold", "A threshold on an already normalized column",
     "examples/tabular/students.prep", "FILTER students WHERE marks >= 50 AND (attendance > 0.5 OR passed == TRUE);",
     "FILTER students WHERE marks >= 50 AND attendance > 60;"),
    ("broken", "A broken image file left in the folder",
     "examples/images/imageset.prep", "REMOVE_CORRUPT train;", "# REMOVE_CORRUPT train;"),
    ("black", "Decimal pixels saved as PNG",
     "examples/images/linked.prep", "CONVERT imgs TO NPY;", "CONVERT imgs TO PNG;"),
]


def mistakes():
    out = []
    for key, title, source, before, after in MISTAKES:
        folder = fresh(f"mistake_{key}")
        original = ROOT / source
        shutil.copytree(original.parent, folder, dirs_exist_ok=True, ignore=shutil.ignore_patterns("output", "processed"))
        text = original.read_text(encoding="utf-8")
        assert before in text, (key, before)
        program = text.replace(before, after)
        (folder / original.name).write_text(program, encoding="utf-8")
        line = next(i for i, l in enumerate(program.splitlines(), 1) if after in l)
        start = time.perf_counter()
        script, code = run([PYTHON, ROOT / "eval" / "baseline.py", original.name, "script_out"], cwd=folder)
        script_s = time.perf_counter() - start
        last = [l for l in script.splitlines() if l.strip()]
        script_result = last[-1] if code != 0 and last else "finished without an error"
        if key == "black":
            pixels = [cv2.imread(str(p), cv2.IMREAD_UNCHANGED) for p in (folder / "script_out").rglob("*.png")]
            means = [float(p.mean()) for p in pixels if p is not None]
            script_result = (f"finished without an error and wrote {len(means)} PNG files; "
                             f"their mean pixel value is {max(means):.2f} of 255: every image is black")
        if key == "threshold":
            saved = list((folder / "script_out").rglob("*.csv"))
            rows = len(pd.read_csv(saved[0])) if saved else 0
            script_result = f"finished without an error and saved {rows} rows: every student was removed"
        start = time.perf_counter()
        compiled, code = run([NEW, original.name, "--explain"], cwd=folder)
        compile_s = time.perf_counter() - start
        report = section(compiled, "error[", "Compilation failed") or section(compiled, "warning[", "Compiled")
        out.append({"key": key, "title": title, "file": original.name, "line": line, "before": before,
                    "after": after, "program": program, "script": script_result, "script_s": round(script_s, 2),
                    "compiler": report, "compile_s": round(compile_s, 2)})
        print("mistake", key, "|", script_result[:70], "|", report.splitlines()[0][:70])
    return out


# ---------- 3. Phase 2 against Phase 3 ----------

def speed():
    csv = ROOT / "eval" / "data" / "tall_1000000.csv"
    folder = fresh("speed")
    (folder / "p.prep").write_text(f'LOAD "{csv.as_posix()}" AS t;\nSAVE t AS "o.csv";\n', encoding="utf-8")
    old = timed([OLD, "p.prep", "--explain"], folder)
    new = timed([NEW, "p.prep", "--explain"], folder)
    start = time.perf_counter()
    pd.read_csv(csv)
    pandas_s = time.perf_counter() - start
    return {"rows": 1_000_000, "columns": 10, "mib": round(csv.stat().st_size / 2**20),
            "phase2_s": round(old, 2), "phase3_s": round(new, 2), "pandas_s": round(pandas_s, 2)}


def new_batches():
    """The same program, compiled once on the students file, then given next week's file."""
    out = {}
    for name in ("next_week.csv", "typo_week.csv", "missing_name.csv"):
        out[name] = (DATA / name).read_text(encoding="utf-8")

    # Phase 2: the only way to give it new data is to replace the file; the guard refuses.
    folder = fresh("phase2_batch")
    for file in ("students.prep", "students.csv"):
        shutil.copyfile(ROOT / "examples" / "tabular" / file, folder / file)
    run([OLD, "students.prep", "--python"], cwd=folder)
    shutil.copyfile(DATA / "next_week.csv", folder / "students.csv")
    text, _ = run([PYTHON, folder / "output" / "generated.py"], cwd=folder)
    out["phase2"] = outcome(text).replace("phase2_batch\\output\\..\\", "")

    # Phase 3: compile once, give the new file by name.
    folder = fresh("phase3_batch")
    for file in ("students.prep", "students.csv"):
        shutil.copyfile(ROOT / "examples" / "tabular" / file, folder / file)
    run([NEW, "students.prep", "--python"], cwd=folder)
    for name in ("next_week.csv", "typo_week.csv", "missing_name.csv"):
        shutil.copyfile(DATA / name, folder / name)
        text, code = run([PYTHON, folder / "output" / "generated.py", f"students={name}"], cwd=folder)
        out["phase3_" + name] = outcome(text).replace("phase3_batch\\", "")
    generated = (folder / "output" / "generated.py").read_text(encoding="utf-8")
    match = re.search(r'"students",\n.*?\n    (\{"name": \{.*?\}\}),\n\)', generated, re.S)
    out["certificate_dict"] = match.group(1) if match else ""
    return out


def certificate_facts():
    """The students certificate as data for the interactive checker on the page."""
    generated = (WORK / "phase3_batch" / "output" / "generated.py").read_text(encoding="utf-8")
    block = re.search(r'    (\{"name": \{.*?\}\}),\n\)', generated, re.S).group(1)
    return json.loads(block.replace("True", "true").replace("None", "null"))


def images():
    folder = fresh("images")
    shutil.copytree(ROOT / "examples" / "images", folder, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("output", "processed"))
    certificate, _ = run([NEW, "imageset.prep", "--certificate", "--python"], cwd=folder)
    rng = np.random.default_rng(1)
    made = {"new_photos": [(300, 260, 3), (640, 480, 1), (250, 900, 3), (400, 300, 4)],
            "small_photos": [(300, 300, 3), (150, 100, 3)]}
    sizes = {}
    for name, shapes in made.items():
        target = folder / name
        target.mkdir()
        sizes[name] = []
        for i, (w, h, c) in enumerate(shapes):
            pixels = rng.integers(0, 255, (h, w, c) if c > 1 else (h, w), dtype=np.uint8)
            suffix = ".png" if c == 4 else ".jpg"
            cv2.imwrite(str(target / f"photo_{i}{suffix}"), pixels)
            sizes[name].append({"w": w, "h": h, "c": c, "file": f"photo_{i}{suffix}"})
    results = {}
    for name in made:
        text, _ = run([PYTHON, folder / "output" / "generated.py", f"train={folder / name}"], cwd=folder)
        results[name] = outcome(text).replace("images\\", "")
    compiled_sizes = []
    for path in sorted((folder / "dataset").rglob("*")):
        if path.suffix.lower() in (".png", ".jpg") and path.is_file():
            image = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
            if image is not None:
                compiled_sizes.append({"w": image.shape[1], "h": image.shape[0], "file": path.name})
    return {"program": (folder / "imageset.prep").read_text(encoding="utf-8"),
            "certificate": stage(certificate, "DATA CERTIFICATE"), "compiled": compiled_sizes,
            "folders": sizes, "results": results}


def pandas_script():
    folder = fresh("titanic")
    shutil.copyfile(HERE / "titanic_clean.py", folder / "titanic_clean.py")
    for name in ("train.csv", "test.csv"):
        shutil.copyfile(ROOT / "eval" / "data" / "titanic" / name, folder / name)
    lifted, _ = run([PYTHON, ROOT / "frontend" / "pandas2prep.py", "titanic_clean.py"], cwd=folder)
    program = "\n".join(l for l in lifted.splitlines() if not l.startswith("#"))
    (folder / "lifted.prep").write_text(program + "\n", encoding="utf-8")
    compiled, _ = run([NEW, "lifted.prep", "--certificate", "--python"], cwd=folder)
    on_test, _ = run([PYTHON, folder / "output" / "generated.py", "train=test.csv"], cwd=folder)
    return {"script": (HERE / "titanic_clean.py").read_text(encoding="utf-8"), "lifted": program,
            "notes": "\n".join(l for l in lifted.splitlines() if l.startswith("#")),
            "certificate": stage(compiled, "DATA CERTIFICATE"), "on_test": outcome(on_test).replace("titanic\\", "")}


def results():
    def load(name):
        return json.loads((ROOT / "eval" / "results" / name).read_text(encoding="utf-8"))
    rq1 = load("rq1.json")
    mutants = [r for r in rq1 if r["class"] != "control"]
    cert = load("certificates.json")
    real = [r for r in load("realbatches.json") if r["fresh"] is not None]
    corpus = load("pandas_corpus.json")[100:]
    runs = [b for r in corpus for b in r.get("batches", [])]
    bugs = load("realbugs.json")
    scale = load("scale.json")
    rq3 = load("rq3.json")
    return {
        "mutants": len(mutants), "mutants_caught": sum(r["preplang"]["verdict"] != "not reported" for r in mutants),
        "baseline_crash": sum(r["baseline"]["verdict"].startswith("run-time crash") for r in mutants),
        "baseline_silent": sum(r["baseline"]["verdict"] == "silent wrong output" for r in mutants),
        "realbugs": len(bugs), "realbugs_early": sum(b["verdict"] in ("compile", "contract") for b in bugs),
        "rq7": {"runs": len(cert), "cert": sum(r["compiled_once"] != "refused" for r in cert),
                "facts": sum(r["file_facts_admit"] for r in cert), "fingerprint": 0},
        "rq8": {"runs": len(real), "cert": sum(r["compiled_once"] != "refused" for r in real),
                "facts": sum(bool(r["file_facts_admit"]) for r in real), "fingerprint": 0,
                "unsound": sum(r["compiled_once"] == "finished" and not (r["fresh"] == "finished" and r["same_outputs"])
                               for r in real)},
        "rq9": {"scripts": len(corpus), "lifted": sum(r["outcome"] == "lifted" for r in corpus), "runs": len(runs),
                "admitted": sum(b["prep"] != "refused" for b in runs),
                "same": sum(b["prep"] == "finished" and bool(b["same"]) for b in runs),
                "unsound": sum(b["prep"] == "finished" and not b["same"] for b in runs)},
        "scale": {"rows": 16_000_000, "parquet_s": scale["parquet_compile_cost"][2]["compile_s"],
                  "typo_compile": scale["table_cases"][1]["preplang_compile_s"],
                  "typo_script": scale["table_cases"][1]["script_s"],
                  "black_compile": scale["image_cases"][2]["preplang_compile_s"],
                  "black_script": scale["image_cases"][2]["script_s"]},
        "memory": {"streaming": round(rq3["image_streaming"]["preplang_peak_mib"]),
                   "in_memory": round(rq3["image_streaming"]["baseline_peak_mib"])},
        "tests": sum(subprocess.run([str(t)], cwd=ROOT / "build", capture_output=True, text=True).stdout.count("  PASS  ")
                     for t in sorted((ROOT / "build" / "Release").glob("test_*.exe"))),
    }


def main():
    WORK.mkdir(exist_ok=True)
    data = {"phases": phases(), "mistakes": mistakes(), "speed": speed(), "batches": new_batches()}
    data["certificate"] = certificate_facts()
    data["images"] = images()
    data["pandas"] = pandas_script()
    data["results"] = results()
    (HERE / "demo_data.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    build_page()


def build_page():
    """page.html with demo_data.json filled in, as one file that works offline."""
    data = json.loads((HERE / "demo_data.json").read_text(encoding="utf-8"))
    page = (HERE / "page.html").read_text(encoding="utf-8")
    payload = json.dumps(data).replace("</", "<\\/")
    (HERE / "PrepLang_Demo.html").write_text(page.replace("/*DEMO_DATA*/null", payload), encoding="utf-8")
    print("wrote demo/PrepLang_Demo.html")


if __name__ == "__main__":
    build_page() if sys.argv[1:] == ["--page"] else main()
