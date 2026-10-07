"""Output evidence for the Review 3 report: real compiler and program output,
captured by running the commands and drawn as black-on-white terminal images.

    py -3 docs/review3/evidence.py      (after building build/Release)
"""
import json
import subprocess
import textwrap
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
COMPILER = str(ROOT / "build" / "Release" / "preplang.exe")
WORK = ROOT / "eval" / "work" / "evidence"


def run(command, cwd=ROOT):
    done = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    return (done.stdout + done.stderr).replace(str(ROOT) + "\\", "").replace(str(WORK) + "\\", "")


def section(text, start, end=None):
    """The lines of `text` from the first line containing `start` up to (not including) `end`."""
    lines = text.splitlines()
    first = next(i for i, line in enumerate(lines) if start in line)
    last = next((i for i in range(first + 1, len(lines)) if end and end in lines[i]), len(lines))
    return "\n".join(lines[first:last]).rstrip()


def terminal(text, name, width=100):
    lines = []
    for line in text.splitlines():  # long lines wrap at a space, continued with an indent
        lines += textwrap.wrap(line, width, subsequent_indent="  ", drop_whitespace=False) or [""]
    fig = plt.figure(figsize=(width * 0.085, 0.165 * len(lines) + 0.25))
    fig.text(0.01, 0.98, "\n".join(lines), family="Consolas", size=9.5, va="top", ha="left", color="black",
             linespacing=1.25)
    fig.savefig(HERE / name, dpi=200, facecolor="white")
    plt.close(fig)
    print("wrote", name)


def main():
    WORK.mkdir(parents=True, exist_ok=True)

    tests = run(["ctest", "--test-dir", "build", "-C", "Release"])
    terminal("> ctest --test-dir build -C Release\n" + section(tests, "Start 1:"), "evidence_tests.png")

    explain = run([COMPILER, "examples/tabular/students.prep", "--explain"])
    terminal("> preplang examples/tabular/students.prep --explain\n" + section(explain, "PREPLANG EXECUTION PLAN"),
             "evidence_explain.png")

    errors = run([COMPILER, "examples/errors/semantic_error.prep", "--semantic"])
    terminal("> preplang examples/errors/semantic_error.prep\n" + section(errors, "error[", "Compilation failed"),
             "evidence_errors.png")

    linked = (ROOT / "examples" / "images" / "linked.prep").read_text(encoding="utf-8")
    (ROOT / "examples" / "images" / "float_png.prep").write_text(
        linked.replace("CONVERT imgs TO NPY;", "CONVERT imgs TO PNG;"), encoding="utf-8")
    image = run([COMPILER, "examples/images/float_png.prep", "--explain"])
    (ROOT / "examples" / "images" / "float_png.prep").unlink()
    terminal("> preplang float_png.prep   (linked.prep with CONVERT imgs TO PNG)\n" +
             section(image, "error[", "Compilation failed"), "evidence_image_error.png")

    certificate = run([COMPILER, "examples/tabular/students.prep", "--certificate"])
    terminal("> preplang examples/tabular/students.prep --certificate\n" +
             section(certificate, "DATA CERTIFICATE", "Compiled successfully"), "evidence_certificate.png")

    # Compile once, then give the program two new batches.
    run([COMPILER, "examples/tabular/students.prep", "--python"])
    students = pd.read_csv(ROOT / "examples" / "tabular" / "students.csv", keep_default_na=False)
    good = students.copy()
    good["marks"] = [min(100, m + 4) for m in good["marks"]]
    good.to_csv(WORK / "next_week.csv", index=False)
    bad = students.copy()
    bad.loc[3, "marks"] = 104
    bad.to_csv(WORK / "typo.csv", index=False)
    script = str(ROOT / "output" / "generated.py")
    ok = run([sys.executable, script, f"students={WORK / 'next_week.csv'}"])
    refused = run([sys.executable, script, f"students={WORK / 'typo.csv'}"])
    terminal("> py -3 output/generated.py students=next_week.csv\n" + ok.strip() +
             "\n\n> py -3 output/generated.py students=typo.csv\n" + section(refused, "ContractError:"),
             "evidence_admission.png")

    # Compile time against reading the file, from eval/results/rq3.json
    rq3 = json.loads((ROOT / "eval" / "results" / "rq3.json").read_text(encoding="utf-8"))
    rows = rq3["compile_cost"][:3]
    labels = ["10k rows", "100k rows", "1M rows"]
    fig, ax = plt.subplots(figsize=(6.5, 3.4))
    width = 0.26
    for k, (key, name, hatch) in enumerate([("compile_s", "PrepLang compile (all checks)", ""),
                                            ("read_arrow_s", "pandas read, pyarrow engine", "//"),
                                            ("read_all_s", "pandas read, C engine", "..")]):
        ax.bar([i + (k - 1) * width for i in range(3)], [r[key] for r in rows], width, label=name,
               color="white" if k else "black", edgecolor="black", hatch=hatch)
    ax.set_xticks(range(3), labels)
    ax.set_ylabel("seconds")
    ax.legend(frameon=False)
    ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(HERE / "chart_compile_time.png", dpi=200, facecolor="white")
    plt.close(fig)
    print("wrote chart_compile_time.png")


if __name__ == "__main__":
    main()
