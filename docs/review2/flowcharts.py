import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Polygon

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
    fig.savefig(name, dpi=200, bbox_inches="tight", facecolor="white")
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


# 1. The compiler pipeline
chain([".prep program (input)", "Lexical analysis\n(tokens)", "Syntax analysis\n(parser builds the AST)",
       "Semantic analysis\n(reads data, checks types)", "Intermediate code\n(three address code)",
       "Code optimization", "Code generation\n(Python: pandas + OpenCV)", "generated.py (output)"],
      "flow_pipeline.png")

# 2. How one statement is checked in semantic analysis
fig, ax = canvas(9, 1.9, 8.9)
box(ax, 4, 8.4, "Next statement")
diamond(ax, 4, 7.0, "Dataset and columns\ndefined?")
box(ax, 7.7, 7.0, "Name error", w=2.0)
diamond(ax, 4, 5.3, "Types fit the\noperation?")
box(ax, 7.7, 5.3, "Type error", w=2.0)
box(ax, 4, 3.8, "Update the symbol table\n(types, missing values, ranges)", w=4.4, h=0.8)
box(ax, 4, 2.4, "Go to the next statement")
arrow(ax, 4, 8.1, 4, 7.55)
arrow(ax, 6.2, 7.0, 6.7, 7.0)
ax.text(6.3, 7.18, "no", fontdict=SMALL)
arrow(ax, 4, 6.45, 4, 5.85, "yes")
arrow(ax, 6.2, 5.3, 6.7, 5.3)
ax.text(6.3, 5.48, "no", fontdict=SMALL)
arrow(ax, 4, 4.75, 4, 4.2, "yes")
arrow(ax, 4, 3.4, 4, 2.7)
save(fig, "flow_semantic.png")

# 3. How an EXPECT contract is decided
fig, ax = canvas(9.5, 2.1, 7.9)
box(ax, 4.25, 7.4, "EXPECT contract")
diamond(ax, 4.25, 6.0, "Known facts say it\nis always true?")
box(ax, 8.1, 6.0, "PROVEN\n(no check needed)", w=2.4, h=0.8)
diamond(ax, 4.25, 4.2, "Facts are exact and\nshow it is false?")
box(ax, 8.1, 4.2, "VIOLATED\n(compile error)", w=2.4, h=0.8)
box(ax, 4.25, 2.6, "Checked when the program runs", w=4.4)
arrow(ax, 4.25, 7.1, 4.25, 6.55)
arrow(ax, 6.45, 6.0, 6.9, 6.0)
ax.text(6.5, 6.18, "yes", fontdict=SMALL)
arrow(ax, 4.25, 5.45, 4.25, 4.75, "no")
arrow(ax, 6.45, 4.2, 6.9, 4.2)
ax.text(6.5, 4.38, "yes", fontdict=SMALL)
arrow(ax, 4.25, 3.65, 4.25, 2.9, "no")
save(fig, "flow_contract.png")

# 4. Demo steps
chain(["Open the PrepLang folder in VS Code", "Build: Ctrl + Shift + B", "Open a .prep file",
       "Run task: compile and run", "Check the output folder"], "flow_demo.png", box_w=4.6)
print("done")
