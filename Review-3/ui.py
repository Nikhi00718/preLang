"""
ui.py -- the PrepLang IDE: a compiler-stage visualiser.

    python ui.py

A two-pane window. On the left, a PrepLang editor with the example programs
in a drop-down. On the right, one tab per compiler artefact:

    Tokens | AST | Symbols & Schema | Semantics | Contracts |
    IR | Optimised IR | Python | Output | Errors

Pressing *Compile* runs every phase and refreshes every tab, so the whole
pipeline is visible at once; *Run* additionally executes the generated Pandas
code and shows the processed dataset.

Tkinter is used because it is part of the Python standard library: the
demonstration machine needs no installation beyond Pandas (and Pandas is only
required for the Run button, not for compiling).
"""

from __future__ import annotations

import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from preplang.ast_nodes import format_ast
from preplang.lexer import format_token_table
from preplang.pipeline import CompilationResult, compile_source

BASE_DIR = Path(__file__).resolve().parent
EXAMPLES = BASE_DIR / "examples"

MONO = ("Consolas", 10)
MONO_SMALL = ("Consolas", 9)
UI_FONT = ("Segoe UI", 9)

TABS = [
    ("tokens", "Tokens"),
    ("ast", "AST"),
    ("symbols", "Symbols & Schema"),
    ("semantic", "Semantics"),
    ("contracts", "Contracts"),
    ("ir", "IR"),
    ("opt", "Optimised IR"),
    ("code", "Python"),
    ("output", "Output"),
    ("errors", "Errors"),
]

DEFAULT_PROGRAM = """# Welcome to PrepLang.
# Press Compile to see every compiler phase, or Run to execute the result.
# Try breaking it:  NORMALIZE students.name;   <- a schema-aware type error

LOAD "students.csv" AS students;

EXPECT students.age NUMBER;
EXPECT students.attendance BETWEEN 0 AND 100;

FILL_NULL students.attendance WITH 0;
DROP_NULL students;

NORMALIZE students.age;
FILTER students WHERE marks >= 50;

SELECT students [ name, age, marks ];

SAVE students AS "cleaned_students.csv";
"""


class PrepLangIDE(tk.Tk):
    """The compiler-stage visualiser."""

    def __init__(self) -> None:
        super().__init__()
        self.title("PrepLang IDE  --  schema-aware compiler for data preprocessing")
        self.geometry("1380x860")
        self.minsize(1000, 640)

        self.optimise = tk.BooleanVar(value=True)
        self.validate_data = tk.BooleanVar(value=True)
        self.result: CompilationResult | None = None
        self.views: dict[str, tk.Text] = {}

        self._build_toolbar()
        self._build_body()
        self._build_statusbar()
        self.editor.insert("1.0", DEFAULT_PROGRAM)
        self.compile_program()

    # ------------------------------------------------------------------
    # Layout
    # ------------------------------------------------------------------
    def _build_toolbar(self) -> None:
        bar = ttk.Frame(self, padding=(8, 6))
        bar.pack(side=tk.TOP, fill=tk.X)

        ttk.Label(bar, text="Example:", font=UI_FONT).pack(side=tk.LEFT)
        self.example_names = [p.name for p in sorted(EXAMPLES.glob("*.prep"))]
        self.example_choice = ttk.Combobox(
            bar, values=self.example_names, width=28, state="readonly", font=UI_FONT
        )
        self.example_choice.pack(side=tk.LEFT, padx=(4, 12))
        self.example_choice.bind("<<ComboboxSelected>>", self._load_example)

        ttk.Button(bar, text="Open...", command=self.open_file).pack(side=tk.LEFT)
        ttk.Separator(bar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)

        ttk.Button(bar, text="Compile  (F5)", command=self.compile_program).pack(side=tk.LEFT)
        ttk.Button(bar, text="Run  (F6)", command=self.run_program).pack(side=tk.LEFT, padx=6)
        ttk.Button(bar, text="Save generated Python...", command=self.save_code).pack(side=tk.LEFT)

        ttk.Separator(bar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)
        ttk.Checkbutton(
            bar, text="Optimise", variable=self.optimise, command=self.compile_program
        ).pack(side=tk.LEFT)
        ttk.Checkbutton(
            bar, text="Check data contracts",
            variable=self.validate_data, command=self.compile_program,
        ).pack(side=tk.LEFT, padx=6)

        self.bind("<F5>", lambda _event: self.compile_program())
        self.bind("<F6>", lambda _event: self.run_program())

    def _build_body(self) -> None:
        panes = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        panes.pack(fill=tk.BOTH, expand=True, padx=8, pady=(0, 4))

        left = ttk.LabelFrame(panes, text=" PrepLang source ", padding=4)
        self.editor = tk.Text(left, font=MONO, wrap="none", undo=True,
                              background="#1e1e1e", foreground="#e6e6e6",
                              insertbackground="#e6e6e6", tabs="4c")
        editor_scroll = ttk.Scrollbar(left, command=self.editor.yview)
        self.editor.configure(yscrollcommand=editor_scroll.set)
        editor_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.editor.pack(fill=tk.BOTH, expand=True)
        panes.add(left, weight=2)

        right = ttk.LabelFrame(panes, text=" Compiler stages ", padding=4)
        self.notebook = ttk.Notebook(right)
        self.notebook.pack(fill=tk.BOTH, expand=True)
        for key, label in TABS:
            frame = ttk.Frame(self.notebook)
            view = tk.Text(frame, font=MONO_SMALL, wrap="none", state="disabled",
                           background="#fbfbfb")
            vertical = ttk.Scrollbar(frame, command=view.yview)
            horizontal = ttk.Scrollbar(frame, orient=tk.HORIZONTAL, command=view.xview)
            view.configure(yscrollcommand=vertical.set, xscrollcommand=horizontal.set)
            vertical.pack(side=tk.RIGHT, fill=tk.Y)
            horizontal.pack(side=tk.BOTTOM, fill=tk.X)
            view.pack(fill=tk.BOTH, expand=True)
            view.tag_configure("error", foreground="#b00020")
            view.tag_configure("ok", foreground="#0a7d2c")
            self.notebook.add(frame, text=label)
            self.views[key] = view
        panes.add(right, weight=3)

    def _build_statusbar(self) -> None:
        self.status = tk.StringVar(value="ready")
        bar = ttk.Frame(self, relief=tk.SUNKEN, padding=(8, 3))
        bar.pack(side=tk.BOTTOM, fill=tk.X)
        ttk.Label(bar, textvariable=self.status, font=UI_FONT).pack(side=tk.LEFT)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    def _set(self, key: str, text: str, tag: str | None = None) -> None:
        view = self.views[key]
        view.configure(state="normal")
        view.delete("1.0", tk.END)
        view.insert("1.0", text, tag or ())
        view.configure(state="disabled")

    def _source(self) -> str:
        return self.editor.get("1.0", tk.END)

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------
    def _load_example(self, _event=None) -> None:
        name = self.example_choice.get()
        if not name:
            return
        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0", (EXAMPLES / name).read_text(encoding="utf-8"))
        self.compile_program()

    def open_file(self) -> None:
        path = filedialog.askopenfilename(
            initialdir=EXAMPLES, filetypes=[("PrepLang programs", "*.prep"), ("All files", "*.*")]
        )
        if not path:
            return
        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0", Path(path).read_text(encoding="utf-8"))
        self.compile_program()

    def save_code(self) -> None:
        if self.result is None or self.result.code is None:
            messagebox.showinfo("PrepLang", "There is no generated code to save yet.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".py", initialfile="pipeline.py",
            filetypes=[("Python", "*.py")],
        )
        if path:
            Path(path).write_text(self.result.code.source, encoding="utf-8")
            self.status.set(f"generated Python written to {path}")

    def compile_program(self, run: bool = False) -> None:
        self.status.set("compiling...")
        self.update_idletasks()
        result = compile_source(
            self._source(),
            base_dir=EXAMPLES,
            validate_data=self.validate_data.get(),
            optimise=self.optimise.get(),
            run=run,
        )
        self.result = result
        self._refresh(result)

    def run_program(self) -> None:
        self.compile_program(run=True)
        if self.result is not None and self.result.execution is not None:
            self.notebook.select(len(TABS) - 2)  # the Output tab

    # ------------------------------------------------------------------
    # Rendering
    # ------------------------------------------------------------------
    def _refresh(self, result: CompilationResult) -> None:
        self._set("tokens", format_token_table(result.tokens, show_eof=False))
        self._set("ast", format_ast(result.ast) if result.ast else "(not parsed)")
        self._set(
            "symbols",
            result.symbols.format() if result.symbols else "(no symbol table: the program did not reach semantic analysis)",
        )
        self._set(
            "semantic",
            result.analysis.format_facts() if result.analysis else "(semantic analysis not reached)",
        )
        self._set(
            "contracts",
            result.contract_report.format() if result.contract_report else "(contract validation disabled)",
        )
        self._set("ir", result.ir.format() if result.ir else "(no IR: compilation stopped earlier)")

        if result.optimised_ir is not None:
            text = result.optimised_ir.format("OPTIMISED INTERMEDIATE REPRESENTATION")
            if result.optimisation is not None:
                text += "\n\n" + result.optimisation.format()
            self._set("opt", text)
        else:
            self._set("opt", "(no optimised IR)")

        self._set(
            "code",
            result.code.source if result.code else "(no target code: compilation stopped earlier)",
        )

        if result.execution is not None:
            text = result.execution.format()
            for dataset, preview in result.execution.previews.items():
                text += f"\n\nFinal dataset '{dataset}' (first rows):\n{preview}"
            self._set("output", text, "ok" if result.execution.ok else "error")
        else:
            self._set("output", "Press Run (F6) to execute the generated Pandas code.")

        if result.reporter.errors or result.reporter.warnings:
            self._set("errors", result.reporter.render(), "error" if result.reporter.errors else None)
        else:
            self._set("errors", "No lexical, syntax, semantic or type errors.", "ok")

        if result.ok:
            self.status.set("OK  " + result.summary())
        elif not result.reporter.ok:
            self.status.set(
                f"FAILED during {result.failed_phase or result.reached_phase}  --  {result.summary()}"
            )
            self.notebook.select(len(TABS) - 1)  # the Errors tab
        else:
            failed = len(result.contract_report.failed) if result.contract_report else 0
            self.status.set(f"REJECTED  {failed} data contract(s) violated  --  {result.summary()}")
            self.notebook.select(4)  # the Contracts tab


def main() -> int:
    PrepLangIDE().mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
