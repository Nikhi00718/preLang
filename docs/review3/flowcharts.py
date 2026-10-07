"""Black-and-white flowcharts for the Review 3 report.

    py -3 docs/review3/flowcharts.py      (writes the PNG files next to this script)
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Polygon

HERE = Path(__file__).resolve().parent
FONT = {"family": "DejaVu Sans", "size": 11, "color": "black"}
SMALL = {**FONT, "size": 10}


def box(ax, x, y, text, w=3.6, h=0.62, round_=True):
    style = "round,pad=0.02,rounding_size=0.12" if round_ else "square,pad=0.02"
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h, boxstyle=style,
                                facecolor="white", edgecolor="black", linewidth=1.3))
    ax.text(x, y, text, ha="center", va="center", fontdict=FONT)


def diamond(ax, x, y, text, w=4.4, h=1.1):
    ax.add_patch(Polygon([(x, y + h / 2), (x + w / 2, y), (x, y - h / 2), (x - w / 2, y)],
                         facecolor="white", edgecolor="black", linewidth=1.3))
    ax.text(x, y, text, ha="center", va="center", fontdict=FONT)


def arrow(ax, x1, y1, x2, y2, label=None):
    ax.annotate("", xy=(x2, y2), xytext=(x1, y1), arrowprops=dict(arrowstyle="-|>", color="black", lw=1.2))
    if label:
        ax.text((x1 + x2) / 2 + 0.12, (y1 + y2) / 2, label, ha="left", va="center", fontdict=SMALL)


def canvas(width, y_low, y_high):
    fig, ax = plt.subplots(figsize=(width, y_high - y_low))
    ax.set_xlim(0, width)
    ax.set_ylim(y_low, y_high)
    ax.axis("off")
    return fig, ax


def save(fig, name):
    fig.savefig(HERE / name, dpi=200, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def chain(steps, name, width=6, box_w=3.6):
    fig, ax = canvas(width, 0, len(steps) * 1.35 + 0.4)
    ys = [len(steps) * 1.35 - i * 1.35 for i in range(len(steps))]
    half = lambda text: 0.4 if "\n" in text else 0.3  # noqa: E731
    for i, (text, y) in enumerate(zip(steps, ys)):
        box(ax, width / 2, y, text, w=box_w, h=2 * half(text), round_=(i in (0, len(steps) - 1)))
        if i:
            arrow(ax, width / 2, ys[i - 1] - half(steps[i - 1]), width / 2, y + half(text))
    save(fig, name)


# 1. The complete compiler pipeline
chain([".prep program (input)", "Lexical analysis\n(tokens)", "Syntax analysis\n(LL(1) parser builds the AST)",
       "Semantic analysis\n(reads the data, proves contracts)", "Intermediate code\n(three address code)",
       "Code optimization\n(3 passes)", "Data certificate\n(facts the proofs need)",
       "Code generation\n(Python: pandas + OpenCV)", "generated.py (output)"],
      "flow_pipeline.png", box_w=4.2)

# 2. How the data certificate is found (counterfactual re-analysis)
fig, ax = canvas(10, 0.6, 11.2)
box(ax, 4.5, 10.7, "Proofs P of the compiled program", w=4.6)
box(ax, 4.5, 9.5, "Take the next fact of a column\n(no missing values, lower bound, upper bound)", w=5.6, h=0.8)
box(ax, 4.5, 8.2, "Make it weaker: allow missing values,\nor move the bound to a program constant or infinity",
    w=5.6, h=0.8)
box(ax, 4.5, 6.9, "Run semantic analysis again with the weaker fact", w=5.6)
diamond(ax, 4.5, 5.5, "Every proof in P\nstill holds?", w=4.2)
box(ax, 8.6, 5.5, "Keep the weaker fact", w=2.4)
box(ax, 4.5, 4.0, "Keep the fact, and note the\nproofs that need it", w=4.6, h=0.8)
diamond(ax, 4.5, 2.6, "More facts?", w=3.0, h=0.9)
box(ax, 4.5, 1.2, "Certificate (written into generated.py)", w=5.0)
arrow(ax, 4.5, 10.4, 4.5, 9.9)
arrow(ax, 4.5, 9.1, 4.5, 8.6)
arrow(ax, 4.5, 7.8, 4.5, 7.2)
arrow(ax, 4.5, 6.6, 4.5, 6.05)
arrow(ax, 6.6, 5.5, 7.4, 5.5)
ax.text(6.75, 5.7, "yes", fontdict=SMALL)
arrow(ax, 4.5, 4.95, 4.5, 4.4, "no")
arrow(ax, 4.5, 3.6, 4.5, 3.05)
arrow(ax, 8.6, 5.2, 8.6, 2.6)
arrow(ax, 8.6, 2.6, 6.0, 2.6)
ax.plot([3.0, 1.0, 1.0], [2.6, 2.6, 9.5], color="black", lw=1.2)
arrow(ax, 1.0, 9.5, 1.7, 9.5)
ax.text(1.1, 2.8, "yes", fontdict=SMALL)
arrow(ax, 4.5, 2.15, 4.5, 1.5, "no")
save(fig, "flow_certificate.png")

# 3. What the generated program does with an input file
fig, ax = canvas(10.8, 0.6, 9.4)
box(ax, 4.5, 8.9, "Read the input file", w=4.0)
diamond(ax, 4.5, 7.5, "Same CRC-32 as\nthe compiled file?", w=4.2)
box(ax, 8.7, 7.5, "Use every proof\n(no checks needed)", w=2.6, h=0.8)
diamond(ax, 4.5, 5.7, "Satisfies the\ndata certificate?", w=4.2)
box(ax, 8.7, 5.7, "Refuse: name the row\nand the proof", w=2.6, h=0.8)
box(ax, 4.5, 4.1, "Admit: proofs still hold; checks of\ncontracts proven from values run again",
    w=5.6, h=0.8)
box(ax, 4.5, 2.6, "Run the cleaning steps", w=4.0)
box(ax, 4.5, 1.3, "Write outputs and manifest", w=4.0)
arrow(ax, 4.5, 8.6, 4.5, 8.05)
arrow(ax, 6.6, 7.5, 7.4, 7.5)
ax.text(6.75, 7.7, "yes", fontdict=SMALL)
arrow(ax, 4.5, 6.95, 4.5, 6.25, "no")
arrow(ax, 6.6, 5.7, 7.4, 5.7)
ax.text(6.75, 5.9, "no", fontdict=SMALL)
arrow(ax, 4.5, 5.15, 4.5, 4.5, "yes")
arrow(ax, 4.5, 3.7, 4.5, 2.9)
arrow(ax, 4.5, 2.3, 4.5, 1.6)
ax.plot([10.0, 10.5, 10.5, 6.7], [7.5, 7.5, 2.6, 2.6], color="black", lw=1.2)
arrow(ax, 6.7, 2.6, 6.5, 2.6)
save(fig, "flow_admission.png")

# 4. The parallel CSV scanner
chain(["Read the whole file once", "Start the CRC-32 on its own thread", "Cut the rows into one piece per core\n(at line breaks)",
       "Each thread: split cells in place,\ntype, missing values, min and max",
       "Merge the pieces in file order\n(first problem and first text cell win)", "Column facts for the analysis"],
      "flow_scanner.png", box_w=4.6)
print("done")
