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
import hashlib
import shutil
import sys
import zipfile
from pathlib import Path

PAPER = Path(__file__).resolve().parent
ROOT = PAPER.parent


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
        "PrepLang analyzes preparation pipelines using an available input batch",
        "Item certificates restore removed instructions when their input facts fail",
        f"Reuse matched fresh compilation in {number('RealRuns')} pipeline-batch trials",
        "Lean proves a restricted core; differential tests check generated programs",
        "Stored results, reproduction scripts and user documentation are provided",
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
    document.add_paragraph("PrepLang highlights", style="Title")
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
    members = [f"{stem}.tex", f"{stem}.pdf", "highlights.docx"]
    if kind == "osp":
        # SCP asks for documentary material as part of the submission, not only repository links.
        documentation = {
            "README.md": "documentation/README.md",
            "LANGUAGE.md": "documentation/LANGUAGE.md",
            "ARCHITECTURE.md": "documentation/ARCHITECTURE.md",
            "CONTRIBUTING.md": "documentation/CONTRIBUTING.md",
            "LICENSE": "documentation/LICENSE",
            "eval/README.md": "documentation/eval/README.md",
            "proofs/README.md": "documentation/proofs/README.md",
        }
        for source, destination in documentation.items():
            target = out / destination
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / source, target)
            members.append(destination)
        source_zip = out / "PrepLang_LaTeX_sources.zip"
        with zipfile.ZipFile(source_zip, "w", zipfile.ZIP_DEFLATED) as bundle:
            bundle.write(out / f"{stem}.tex", f"{stem}.tex")
        members.append(source_zip.name)
        instructions = out / "SUBMISSION_README.md"
        instructions.write_text(
            "# PrepLang submission files\n\n"
            "Article type: Original Software Publication, Science of Computer Programming.\n\n"
            "Upload preplang-osp.pdf as the manuscript; PrepLang_LaTeX_sources.zip as the LaTeX "
            "source; highlights.docx as highlights; and documentation/ as documentary material. "
            "The outer archive is a convenience bundle, not the source-only upload.\n\n"
            "The flattened source embeds the numerical macros, bibliography and TikZ figure. "
            "It needs elsarticle and the packages named in its preamble.\n\n"
            "The manuscript describes release v2.1.0 (C1 and C2 of the metadata table) and its Docker "
            "image (C3); check that both are public before submitting.\n\n"
            "Before submission, both authors must approve this final version, author roles, "
            "funding and conflict statements, and the named tool-use disclosure.\n",
            encoding="utf-8",
        )
        members.append(instructions.name)
        manifest = out / "SHA256SUMS.txt"
        manifest.write_text("".join(
            f"{hashlib.sha256((out / name).read_bytes()).hexdigest()}  {name}\n"
            for name in members), encoding="utf-8")
        members.append(manifest.name)
    with zipfile.ZipFile(out / zip_name, "w", zipfile.ZIP_DEFLATED) as bundle:
        for name in members:
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
