# The paper

*Compile Once, Reuse Safely: Data Certificates and Selective Deoptimization for Preprocessing
Pipelines*, in ACM `acmart` format. The built paper is `PrepLang_Paper.pdf`.

| Path | Contents |
|---|---|
| `preplang.tex` | the main file: introduction to implementation, conclusion |
| `sections/` | evaluation, related work, references |
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
