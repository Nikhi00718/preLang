"""Builds the files for a submission to Science of Computer Programming in paper/scp-submission/:

- preplang-scp.tex: preplang-scp.tex with every \\input expanded, so it compiles alone (the
  submission system builds LaTeX from files in one folder);
- preplang-scp.pdf: the built paper (build it first: pdflatex preplang-scp.tex, three times);
- highlights.docx: the highlights, 3 to 5 bullets of at most 85 characters each, as the journal
  asks for them, in a separate editable file;
- PrepLang_SCP_submission.zip: all three.

Run from any folder: py -3 paper/make_scp_submission.py
"""
import re
import shutil
import sys
import zipfile
from pathlib import Path

PAPER = Path(__file__).resolve().parent
OUT = PAPER / "scp-submission"


def number(name):
    """A number the paper prints, from generated/numbers.tex."""
    text = (PAPER / "generated" / "numbers.tex").read_text(encoding="utf-8")
    found = re.search(r"\\newcommand\{\\" + name + r"\}\{([^}]*)\}", text)
    if not found:
        sys.exit(f"generated/numbers.tex has no {name}: run eval/paper_numbers.py")
    return found.group(1)


HIGHLIGHTS = [
    "Data certificates name the input facts that a data-aware compiler's proofs rely on",
    "Selective deoptimization reruns only the removed checks that a new batch breaks",
    "Exactness on every admitted batch is mechanized in Lean 4 for a core language",
    f"Compiled pipelines handle {number('RealDeoptCorrectPct')}% of real new months exactly, without recompiling",
    f"Compiling from differentially private facts kept all {number('PrivRuns')} runs exact",
]


def flatten(path, depth=0):
    """The text of `path` with every \\input{...} outside a comment replaced by its file."""
    if depth > 10:
        sys.exit(f"\\input nested too deeply at {path}")
    lines = []
    for line in path.read_text(encoding="utf-8").split("\n"):
        found = re.match(r"^(\s*)\\input\{([^}]+)\}\s*$", line)
        if found:
            name = found.group(2)
            child = PAPER / (name if name.endswith(".tex") else name + ".tex")
            lines.append(f"% ---- {name}.tex ----")
            lines.append(flatten(child, depth + 1).rstrip("\n"))
            lines.append(f"% ---- end of {name}.tex ----")
        else:
            if "\\input{" in line.split("%")[0]:
                sys.exit(f"{path.name}: an \\input that is not alone on its line: {line.strip()}")
            lines.append(line)
    return "\n".join(lines) + "\n"


def highlights_docx(target):
    import docx  # python-docx
    document = docx.Document()
    document.add_heading("Highlights", level=1)
    for item in HIGHLIGHTS:
        document.add_paragraph(item, style="List Bullet")
    document.save(target)


def main():
    too_long = [h for h in HIGHLIGHTS if len(h) > 85]
    if too_long or not 3 <= len(HIGHLIGHTS) <= 5:
        sys.exit(f"highlights must be 3 to 5 bullets of at most 85 characters: {too_long}")
    pdf = PAPER / "preplang-scp.pdf"
    if not pdf.exists():
        sys.exit("build preplang-scp.pdf first (pdflatex preplang-scp.tex, three times)")
    OUT.mkdir(exist_ok=True)
    (OUT / "preplang-scp.tex").write_text(flatten(PAPER / "preplang-scp.tex"), encoding="utf-8", newline="\n")
    shutil.copyfile(pdf, OUT / "preplang-scp.pdf")
    highlights_docx(OUT / "highlights.docx")
    with zipfile.ZipFile(OUT / "PrepLang_SCP_submission.zip", "w", zipfile.ZIP_DEFLATED) as bundle:
        for name in ("preplang-scp.tex", "preplang-scp.pdf", "highlights.docx"):
            bundle.write(OUT / name, name)
    for h in HIGHLIGHTS:
        print(f"{len(h):3d}  {h}")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
