"""The live demo of PrepLang in a terminal. Every step runs the real compiler and pauses.

    py -3 demo/run_demo.py prepare          # once: Phase 2 compiler, sample images, big CSV, Titanic data
    py -3 demo/run_demo.py                  # the walkthrough, pausing after each step (press Enter)
    py -3 demo/run_demo.py --step 5         # start at step 5
    py -3 demo/run_demo.py --no-pause       # rehearse without stopping

Steps (the same order as demo/PrepLang_Demo.html):
  1 The problem: a plain script against the compiler
  2 The compiler, phase by phase
  3 Error messages
  4 Phase 2 against Phase 3: speed
  5 Data certificates: next week's file
  6 Image folders
  7 An existing pandas script
  8 The tests
"""
import shutil
import subprocess
import sys
import time
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
NEW = ROOT / "build" / "Release" / "preplang.exe"
PHASE2_SRC = ROOT.parent / "phase2-src"
OLD = PHASE2_SRC / "build" / "Release" / "preplang.exe"
WORK = HERE / "work" / "live"
PYTHON = sys.executable
PAUSE = "--no-pause" not in sys.argv
BOLD, PLAIN = ("\033[1m", "\033[0m") if sys.stdout.isatty() else ("", "")


# ---------- presenting ----------

def title(number, text):
    line = "=" * 78
    print(f"\n{BOLD}{line}\n  {number}. {text}\n{line}{PLAIN}")


def say(text):
    print(f"\n{BOLD}>{PLAIN} {text}")


def pause():
    if PAUSE:
        input(f"\n{BOLD}[Enter]{PLAIN} ")


def show_file(path, first=None):
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    print(f"\n--- {Path(path).name} ---")
    for number, line in enumerate(lines[:first] if first else lines, 1):
        print(f"{number:3} | {line}")


def run(command, cwd=ROOT, keep=None, limit=40):
    """Runs a command and prints it like a terminal would. `keep` filters the output lines."""
    names = {str(PYTHON): "py -3", str(OLD): "phase2/preplang.exe", str(NEW): "preplang.exe"}
    shown = " ".join(names.get(str(c), Path(c).name if isinstance(c, Path) else str(c)) for c in command)
    print(f"\n{BOLD}$ {shown}{PLAIN}")
    start = time.perf_counter()
    done = subprocess.run([str(c) for c in command], cwd=cwd, capture_output=True, text=True, encoding="utf-8",
                          errors="replace")
    seconds = time.perf_counter() - start
    lines = (done.stdout + done.stderr).splitlines()
    lines = [line for line in lines if "WARN" not in line]  # OpenCV's notes about its encoders
    if keep:
        lines = [line for line in lines if keep(line)]
    for line in lines[:limit]:
        print("  " + line.replace(str(WORK) + "\\", "").replace(str(ROOT) + "\\", ""))
    if len(lines) > limit:
        print(f"  ... ({len(lines) - limit} more lines)")
    print(f"  ({seconds:.2f} s)")
    return done, seconds


def stage(name):
    """Keeps the lines of one ==== NAME ==== block of the compiler output."""
    inside = [False]

    def keep(line):
        if line.startswith("===="):
            inside[0] = name in line
            return inside[0]
        return inside[0] and line.strip() != ""
    return keep


def fresh(name, source=None):
    folder = WORK / name
    if folder.exists():
        shutil.rmtree(folder)
    if source:
        shutil.copytree(source, folder, ignore=shutil.ignore_patterns("output", "processed"))
    else:
        folder.mkdir(parents=True)
    return folder


# ---------- steps ----------

def step1():
    title(1, "The problem: a plain script against the compiler")
    folder = fresh("black", ROOT / "examples" / "images")
    program = (folder / "linked.prep").read_text(encoding="utf-8").replace("CONVERT imgs TO NPY;", "CONVERT imgs TO PNG;")
    (folder / "linked.prep").write_text(program, encoding="utf-8")
    show_file(folder / "linked.prep")
    say("Images are normalized to decimals between 0 and 1, then saved as PNG. One word is wrong: PNG instead of NPY.")
    pause()
    say("A plain pandas/OpenCV script running the same steps:")
    run([PYTHON, ROOT / "eval" / "baseline.py", "linked.prep", "script_out"], cwd=folder)
    pixels = [cv2.imread(str(p), cv2.IMREAD_UNCHANGED) for p in (folder / "script_out").rglob("*.png")]
    print(f"  -> it wrote {len(pixels)} PNG files; brightest mean pixel: "
          f"{max(float(p.mean()) for p in pixels if p is not None):.2f} of 255. Every image is black, and no error.")
    pause()
    say("The PrepLang compiler on the same program:")
    run([NEW, "linked.prep", "--explain"], cwd=folder, keep=lambda l: not l.startswith("====") and l.strip(),
        limit=12)
    say("It knows the pixel type at line 13 because it read every image header at line 2. Nothing ran.")
    pause()


def step2():
    title(2, "The compiler, phase by phase (examples/tabular/students.prep)")
    show_file(ROOT / "examples" / "tabular" / "students.prep")
    show_file(ROOT / "examples" / "tabular" / "students.csv")
    pause()
    program = "examples/tabular/students.prep"
    for flag, name, words in [("--tokens", "TOKENS", "Phase 1, lexer: characters become tokens with line and column."),
                              ("--ast", "AST", "Phase 2, LL(1) recursive-descent parser: the syntax tree."),
                              ("--schema", "SCHEMA", "Phase 3: the CSV is read at compile time, one piece per core."),
                              ("--semantic", "SEMANTIC", "Phase 4: facts flow through every statement; contracts get verdicts."),
                              ("--ir", "IR", "Phases 5 and 6: three-address code, then the optimizer removes proven checks.")]:
        say(words)
        run([NEW, program, flag], keep=stage(name) if flag != "--ir" else None, limit=34)
        pause()
    say("Phase 7, the data certificate: the facts the proofs need. Phase 8 writes Python and runs it.")
    run([NEW, program, "--certificate"], keep=stage("CERTIFICATE"))
    run([NEW, program, "--run"], keep=lambda l: l.startswith(("Saved", "Wrote")))
    pause()


def step3():
    title(3, "Error messages: line, caret, hint")
    for name in ("lexical_error.prep", "invalid.prep", "semantic_error.prep"):
        run([NEW, f"examples/errors/{name}", "--explain"], keep=lambda l: not l.startswith("====") and l.strip(),
            limit=22)
        pause()


def step4():
    title(4, "Phase 2 against Phase 3: one million CSV rows")
    csv = ROOT / "eval" / "data" / "tall_1000000.csv"
    folder = fresh("speed")
    (folder / "big.prep").write_text(f'LOAD "{csv.as_posix()}" AS t;\nSAVE t AS "o.csv";\n', encoding="utf-8")
    say(f"{csv.stat().st_size / 2**20:.0f} MiB of CSV. Both compilers check every cell of it.")
    run([OLD, "big.prep", "--explain"], cwd=folder, keep=lambda l: "Compiled" in l)
    run([NEW, "big.prep", "--explain"], cwd=folder, keep=lambda l: "Compiled" in l)
    say("Phase 3 reads the file once, cuts it into one piece per core at line breaks, and looks at cells in place.")
    pause()


def step5():
    title(5, "Data certificates: compile once, run on next week's file")
    for name in ("next_week.csv", "typo_week.csv", "missing_name.csv"):
        show_file(HERE / "data" / name)
    pause()
    say("Phase 2: compile, then next week's file arrives (it replaces students.csv).")
    old = fresh("phase2")
    for file in ("students.prep", "students.csv"):
        shutil.copyfile(ROOT / "examples" / "tabular" / file, old / file)
    run([OLD, "students.prep", "--python"], cwd=old, keep=lambda l: "Compiled" in l)
    shutil.copyfile(HERE / "data" / "next_week.csv", old / "students.csv")
    run([PYTHON, old / "output" / "generated.py"], cwd=old, keep=lambda l: l.startswith("ContractError"))
    say("Refused: Phase 2 trusts its proofs only for the byte-identical file.")
    pause()
    say("Phase 3: the compiler finds the facts its proofs need.")
    new = fresh("phase3")
    for file in ("students.prep", "students.csv"):
        shutil.copyfile(ROOT / "examples" / "tabular" / file, new / file)
    for name in ("next_week.csv", "typo_week.csv", "missing_name.csv"):
        shutil.copyfile(HERE / "data" / name, new / name)
    run([NEW, "students.prep", "--certificate", "--python"], cwd=new, keep=stage("CERTIFICATE"))
    say("The data had marks from 35 to 95; the program only needs 0 to 100. Next week has 12 and 100.")
    pause()
    for name in ("next_week.csv", "typo_week.csv", "missing_name.csv"):
        run([PYTHON, new / "output" / "generated.py", f"students={name}"], cwd=new,
            keep=lambda l: l.startswith(("Admitted", "Saved", "ContractError")))
        pause()


def step6():
    title(6, "Image folders")
    folder = fresh("images", ROOT / "examples" / "images")
    rng = np.random.default_rng(1)
    for name, shapes in {"new_photos": [(300, 260, 3), (640, 480, 1), (250, 900, 3), (400, 300, 4)],
                         "small_photos": [(300, 300, 3), (150, 100, 3)]}.items():
        (folder / name).mkdir()
        for i, (w, h, c) in enumerate(shapes):
            pixels = rng.integers(0, 255, (h, w, c) if c > 1 else (h, w), dtype=np.uint8)
            cv2.imwrite(str(folder / name / f"photo_{i}{'.png' if c == 4 else '.jpg'}"), pixels)
    show_file(folder / "imageset.prep")
    run([NEW, "imageset.prep", "--certificate", "--python"], cwd=folder, keep=stage("CERTIFICATE"))
    pause()
    for name in ("new_photos", "small_photos"):
        run([PYTHON, folder / "output" / "generated.py", f"train={name}"], cwd=folder,
            keep=lambda l: l.startswith(("Admitted", "Saved", "ContractError")))
    say("Only the headers of the new images were read to decide.")
    pause()


def step7():
    title(7, "An existing pandas script")
    folder = fresh("titanic")
    shutil.copyfile(HERE / "titanic_clean.py", folder / "titanic_clean.py")
    for name in ("train.csv", "test.csv"):
        shutil.copyfile(ROOT / "eval" / "data" / "titanic" / name, folder / name)
    show_file(folder / "titanic_clean.py")
    pause()
    done, _ = run([PYTHON, ROOT / "frontend" / "pandas2prep.py", "titanic_clean.py"], cwd=folder)
    (folder / "lifted.prep").write_text("\n".join(l for l in done.stdout.splitlines() if not l.startswith("#")) + "\n",
                                        encoding="utf-8")
    pause()
    run([NEW, "lifted.prep", "--certificate", "--python"], cwd=folder, keep=stage("CERTIFICATE"))
    say("Kaggle's test file has no Survived column; the program only carries it, so it may be missing.")
    run([PYTHON, folder / "output" / "generated.py", "train=test.csv"], cwd=folder,
        keep=lambda l: l.startswith(("Admitted", "Saved", "ContractError")))
    pause()


def step8():
    title(8, "The tests")
    run(["ctest", "--test-dir", "build", "-C", "Release"], keep=lambda l: "Test #" in l or "tests passed" in l)
    say("Results of the evaluation: eval/results/*.md. The demo page: demo/PrepLang_Demo.html")


def prepare():
    """Builds the Phase 2 compiler from its commit and makes the demo data."""
    if not OLD.exists():
        if not PHASE2_SRC.exists():
            subprocess.run(["git", "worktree", "add", "--detach", str(PHASE2_SRC), "f088d66"], cwd=ROOT, check=True)
        subprocess.run(["cmake", "-S", str(PHASE2_SRC), "-B", str(PHASE2_SRC / "build"), "-G", "Visual Studio 17 2022"],
                       check=True)
        subprocess.run(["cmake", "--build", str(PHASE2_SRC / "build"), "--config", "Release", "--target", "preplang"],
                       check=True)
    if not (ROOT / "examples" / "images" / "dataset").exists():
        subprocess.run([PYTHON, ROOT / "eval" / "datasets.py", "examples"], check=True)
    if not (ROOT / "eval" / "data" / "tall_1000000.csv").exists():
        subprocess.run([PYTHON, ROOT / "eval" / "datasets.py", "benchmark"], check=True)
    titanic = ROOT / "eval" / "data" / "titanic"
    if not (titanic / "test.csv").exists():
        import urllib.request
        titanic.mkdir(parents=True, exist_ok=True)
        for name in ("train.csv", "test.csv"):
            urllib.request.urlretrieve(f"https://raw.githubusercontent.com/agconti/kaggle-titanic/master/data/{name}",
                                       titanic / name)
    print("Ready: Phase 2 compiler, sample images, the big CSV and the Titanic files.")


STEPS = [step1, step2, step3, step4, step5, step6, step7, step8]

if __name__ == "__main__":
    if "prepare" in sys.argv:
        prepare()
        sys.exit(0)
    first = int(sys.argv[sys.argv.index("--step") + 1]) if "--step" in sys.argv else 1
    for step in STEPS[first - 1:]:
        step()
    print("\nEnd of the demo.")
