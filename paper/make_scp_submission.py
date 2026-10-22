"""Builds the files for submissions to Science of Computer Programming:

  research  paper/scp-submission/      the research article (preplang-scp.tex)
  osp       paper/scp-osp-submission/  the Original Software Publication for the Software Track
                                       (preplang-osp.tex)

Each folder gets the article's .tex with every \\input expanded, so it compiles alone (the
submission system builds LaTeX from the files of one folder); its built PDF (build it first:
pdflatex three times, and for the OSP osp_references.py after the first run); highlights.docx, 3 to
5 bullets of at most 85 characters each, as the journal asks for them, in a separate editable file;
and a zip of the three.

Run from any folder: py -3 paper/make_scp_submission.py [research|osp]  (default: both)
"""
import re
import shutil
import sys
import zipfile
from pathlib import Path

PAPER = Path(__file__).resolve().parent


def number(name):
    """A number the paper prints, from generated/numbers.tex."""
    text = (PAPER / "generated" / "numbers.tex").read_text(encoding="utf-8")
    found = re.search(r"\\newcommand\{\\" + name + r"\}\{([^}]*)\}", text)
    if not found:
        sys.exit(f"generated/numbers.tex has no {name}: run eval/paper_numbers.py")
    return found.group(1)


ARTICLES = {
    "research": ("preplang-scp", "scp-submission", "PrepLang_SCP_submission.zip", lambda: [
        "Data certificates name the input facts that a data-aware compiler's proofs rely on",
        "Selective deoptimization reruns only the removed checks that a new batch breaks",
        "Exactness on every admitted batch is mechanized in Lean 4 for a core language",
        f"Compiled pipelines handle {number('RealDeoptCorrectPct')}% of real new months exactly, without recompiling",
        f"Compiling from differentially private facts kept all {number('PrivRuns')} runs exact",
    ]),
    "osp": ("preplang-osp", "scp-osp-submission", "PrepLang_SCP_OSP_submission.zip", lambda: [
        "PrepLang compiles data-preparation pipelines against the data they first run on",
        "Generated programs rerun only the removed checks that a new batch makes unsafe",
        f"Compiled pipelines handled {number('RealDeoptCorrectPct')}% of real new months exactly, none wrongly",
        "Lean 4 proofs for a core and randomized tests back the exactness guarantee",
        "One script regenerates every reported number from the raw evaluation results",
    ]),
}


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


def highlights_docx(target, highlights):
    import docx  # python-docx
    document = docx.Document()
    document.add_heading("Highlights", level=1)
    for item in highlights:
        document.add_paragraph(item, style="List Bullet")
    document.save(target)


def build(kind):
    stem, folder, zip_name, highlights = ARTICLES[kind]
    highlights = highlights()
    too_long = [h for h in highlights if len(h) > 85]
    if too_long or not 3 <= len(highlights) <= 5:
        sys.exit(f"highlights must be 3 to 5 bullets of at most 85 characters: {too_long}")
    pdf = PAPER / f"{stem}.pdf"
    if not pdf.exists():
        sys.exit(f"build {stem}.pdf first (pdflatex {stem}.tex, three times)")
    out = PAPER / folder
    out.mkdir(exist_ok=True)
    (out / f"{stem}.tex").write_text(flatten(PAPER / f"{stem}.tex"), encoding="utf-8", newline="\n")
    shutil.copyfile(pdf, out / f"{stem}.pdf")
    highlights_docx(out / "highlights.docx", highlights)
    with zipfile.ZipFile(out / zip_name, "w", zipfile.ZIP_DEFLATED) as bundle:
        for name in (f"{stem}.tex", f"{stem}.pdf", "highlights.docx"):
            bundle.write(out / name, name)
    for h in highlights:
        print(f"{len(h):3d}  {h}")
    print(f"wrote {out}")


def main():
    kinds = sys.argv[1:] or list(ARTICLES)
    for kind in kinds:
        if kind not in ARTICLES:
            sys.exit(f"unknown article {kind!r}: research or osp")
        build(kind)


if __name__ == "__main__":
    main()
