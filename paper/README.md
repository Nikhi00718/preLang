# The paper

The paper is the software article for the Science of Computer Programming Software Track (an Original
Software Publication): `preplang-osp.tex`, built as **`PrepLang_Software_SCP.pdf`**.

The longer research article, *Compile Once, Reuse Safely: Data Certificates and Selective
Deoptimization for Preprocessing Pipelines*, is kept as source only (`preplang.tex` in ACM `acmart`,
`preplang-scp.tex` in Elsevier `elsarticle`): it holds the theory and the full evaluation the software
article points to, and is not submitted. Build it as below to read it as a PDF.

| Path | Contents |
|---|---|
| `preplang.tex` | the ACM version: front matter and the long abstract |
| `preplang-scp.tex` | the SCP version, in Elsevier's two-column journal layout (permitted for LaTeX submissions): front matter, a 250-word abstract, keywords and the declarations |
| `sections/body.tex` | the text of both, introduction to conclusion |
| `sections/` | the two abstracts, evaluation, related work, references (numbered in citation order) |
| `preplang-osp.tex` | the software article for the SCP Software Track (Original Software Publication): code metadata table, motivation, description, example, impact; `PrepLang_Software_SCP.pdf` |
| `osp_references.py` | writes `sections/references-osp.tex`, the entries of `sections/references.tex` the software article cites |
| `make_scp_submission.py` | writes `scp-submission/` and `scp-osp-submission/`: one self-contained `.tex`, the PDF, `highlights.docx`, a zip |
| `figures/` | the hand-written figures (architecture, admission policies) |
| `generated/` | every measured number (`numbers.tex`), the result tables and the timeline figure |
| `archive/` | the earlier IEEE-format draft |

`generated/` is written by `eval/paper_numbers.py` from `eval/results/`; do not edit it by hand. A
number the paper uses that no result defines is printed as **??** and listed by the script.

To build, from this folder (three runs settle the references):

```bash
py -3 ../eval/paper_numbers.py
pdflatex preplang.tex
pdflatex preplang.tex
pdflatex preplang.tex
```

The SCP version builds the same way from `preplang-scp.tex`. The software article needs its references
written between the first and the second run:

```bash
pdflatex preplang-osp.tex
py -3 osp_references.py
pdflatex preplang-osp.tex
pdflatex preplang-osp.tex
```

The Software Track requires the code in a public repository under an OSI-approved licence, named in
the article's metadata table (fields C1, C2 and C4 are marked in red until they are filled in).

For a submission, build the articles, then:

```bash
py -3 make_scp_submission.py
```

The journal asks for highlights (3 to 5 bullets of at most 85 characters, in a separate file),
an abstract of at most 250 words, 1 to 7 keywords, numbered references in citation order, and, if
generative AI tools were used in preparing the manuscript, a declaration before the references
(`preplang-scp.tex` has Elsevier's wording, commented out). The declarations of competing interest,
funding and data availability in `preplang-scp.tex` must be confirmed by the author before
submitting.
