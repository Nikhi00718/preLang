# The paper

*Compile Once, Reuse Safely: Data Certificates and Selective Deoptimization for Preprocessing
Pipelines*, in two formats that share one text: ACM `acmart` (`PrepLang_Paper.pdf`) and Elsevier
`elsarticle` for *Science of Computer Programming* (`PrepLang_Paper_SCP.pdf`).

| Path | Contents |
|---|---|
| `preplang.tex` | the ACM version: front matter and the long abstract |
| `preplang-scp.tex` | the SCP version: front matter, a 250-word abstract, keywords and the declarations |
| `sections/body.tex` | the text of both, introduction to conclusion |
| `sections/` | the two abstracts, evaluation, related work, references (numbered in citation order) |
| `make_scp_submission.py` | writes `scp-submission/`: one self-contained `.tex`, the PDF, `highlights.docx`, a zip |
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

The SCP version builds the same way from `preplang-scp.tex`. For a submission, build it, then:

```bash
py -3 make_scp_submission.py
```

The journal asks for highlights (3 to 5 bullets of at most 85 characters, in a separate file),
an abstract of at most 250 words, 1 to 7 keywords, numbered references in citation order, and, if
generative AI tools were used in preparing the manuscript, a declaration before the references
(`preplang-scp.tex` has Elsevier's wording, commented out). The declarations of competing interest,
funding and data availability in `preplang-scp.tex` must be confirmed by the author before
submitting.
