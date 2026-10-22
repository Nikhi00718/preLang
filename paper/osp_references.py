"""Writes sections/references-osp.tex, the bibliography of the software article (preplang-osp.tex):
the entries of sections/references.tex it cites, in the order it first cites them.

Run from this folder after a first pdflatex preplang-osp.tex (which writes the .aux file):

    py -3 osp_references.py
"""
import re
import sys
from pathlib import Path

PAPER = Path(__file__).resolve().parent


def main():
    aux = PAPER / "preplang-osp.aux"
    if not aux.exists():
        sys.exit("run pdflatex preplang-osp.tex first")
    order = []
    for group in re.findall(r"\\citation\{([^}]*)\}", aux.read_text(encoding="latin-1")):
        for key in group.split(","):
            if key.strip() and key.strip() not in order:
                order.append(key.strip())
    text = (PAPER / "sections" / "references.tex").read_text(encoding="utf-8")
    items = text[text.index("\\bibitem{"):text.index("\\end{thebibliography}")]
    entries = {}
    for chunk in re.split(r"(?=\\bibitem\{)", items):
        if chunk.strip():
            entries[re.match(r"\\bibitem\{([^}]*)\}", chunk).group(1)] = chunk.rstrip() + "\n\n"
    missing = [key for key in order if key not in entries]
    if missing:
        sys.exit(f"cited but not in sections/references.tex: {missing}")
    out = ("% Written by osp_references.py from sections/references.tex; do not edit by hand.\n"
           "\\begin{thebibliography}{99}\n\\footnotesize\n" + "".join(entries[k] for k in order) +
           "\\end{thebibliography}\n")
    (PAPER / "sections" / "references-osp.tex").write_text(out, encoding="utf-8", newline="\n")
    print(len(order), "references")


if __name__ == "__main__":
    main()
