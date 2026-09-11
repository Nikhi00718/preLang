# Review 3 — Presentation Guide and Viva Preparation

Review 3 carries **35 marks** — the largest single component. It is assessed on
a *working, complete, demonstrated* system.

---

## 1. Mark-by-mark checklist

| Criterion | Marks | What to show |
|---|---|---|
| Project Completion | 6 | The objectives table O1–O16: all sixteen delivered. Every phase of the architecture diagram is implemented. |
| Functional Correctness | 6 | `python main.py examples/demo_valid.prep --run` → correct CSV out; `pytest -q` → 177 passed |
| Technical Complexity | 5 | Live-column analysis, four optimisation passes, the projection-repair pass, the soundness argument |
| Compiler Concept Integration | 4 | Walk one statement through all six phases in the IDE, in one screen |
| Testing | 4 | 177 tests, 7 modules, positive + negative per phase, plus the property test that optimisation preserves output |
| Demonstration | 4 | `python ui.py` — ten tabs, toggle the optimiser live |
| Viva | 4 | Section 3 below |
| Documentation | 2 | `docs/01`–`09`, README, this guide, module docstrings |

## 2. Suggested 12-minute demonstration

**(1) One minute of context.** "A dataset schema is a type environment.
`NORMALIZE students.name` where `name : STRING` is the same error as
`int x = "hello"` — and my compiler catches it before reading a single row."

**(2) The IDE — the centrepiece.**

```bash
python ui.py
```

* Press **F5**. Walk the tabs left to right: Tokens → AST → Symbols & Schema →
  Semantics → Contracts → IR → Optimised IR → Python. One sentence per tab.
  *The whole compiler is on one screen.*
* Press **F6**. The Output tab shows the contract results, the processed
  dataset and the file that was written.
* Edit the source: change `NORMALIZE students.age` to `students.name`, press
  F5. The Errors tab opens with the type error and the suggested numeric
  columns. **This is the headline.**

**(3) The optimiser, live.**

Load `demo_wide_dataset.prep` from the drop-down (30-column file).

* With **Optimise** ticked: look at the Python tab —
  `usecols=['name', 'age', 'salary', 'department', 'rating']`, and only one
  `DROP_NULL`.
* Untick **Optimise**: the second `DROP_NULL` reappears and `usecols` is gone.
* Optimised IR tab: the report — 30 → 5 columns, 83.3 % pruned.

**(4) The numbers.**

```bash
python benchmarks/performance.py --rows 50000
```

Point at the last section: *"the two outputs are byte-for-byte identical. An
optimisation that changes the answer is a bug, so I check it rather than claim
it."* Then the memory row: 51.0 → 7.2 MiB.

**(5) Tests.**

```bash
python -m pytest -q          # 177 passed
```

## 3. Viva questions and answers

**Q. Walk me through what happens to one statement.**
`NORMALIZE students.age;` → the lexer produces five tokens; the parser builds a
`NormalizeNode`; the analyser looks `age` up in the symbol table, finds
`INTEGER`, accepts it and records that the column is now `FLOAT`; the IR
generator emits `IR_NORMALIZE students age`; the optimiser checks that `age`
is live afterwards and adds it to the load set; the code generator emits the
z-score expression. Six phases, one statement — and the IDE shows all six at
once.

**Q. What optimisations did you implement, and what drives them?**
One analysis and four passes. The analysis is a backwards live-column
analysis: a column is live at a point if a later instruction reads it. Pass 1
collapses two identical idempotent operations. Pass 2 removes an operation that
writes a column nobody reads. Pass 3 takes the live set at the LOAD — which is
exactly the required column set — and emits it as `usecols=`. Pass 4 repairs
`SELECT`/`DROP` so they only mention columns that are still loaded.

**Q. Why is pass 4 necessary?**
Because pruning changes the shape of the dataframe. Without it, a `DROP` of a
column that was never loaded would raise a `KeyError` at run time. It is the
kind of bug you only find by actually executing the generated code — which is
why the test suite runs it.

**Q. Is your optimisation semantics-preserving? Prove it.**
The risky operator is `DROP_NULL`, because pruning changes which rows contain
a missing value. Two things make it sound. First, the language defines
`DROP_NULL` over the columns live at that point, not over every column in the
file — the same move a relational optimiser makes when it pushes a projection
below a filter. Second, the code generator emits that live set as
`dropna(subset=[...])` in both pipelines, so the optimiser changes how much
data is read, never what is computed. And I verify it: the benchmark runs both
versions and compares the output files byte for byte.

**Q. Why not push the FILTER before the NORMALIZE? That would be faster.**
Because it would change the answer. Normalisation subtracts the mean and
divides by the standard deviation of the rows it sees; filtering first changes
both statistics. It is a valid *cost-based* rewrite only if you accept
different output, so I deliberately did not implement it. It is listed under
future work with that caveat.

**Q. Why generate Python source instead of interpreting the IR?**
Three reasons. The output is ordinary Pandas, so it is useful outside the
project — you can save it with `--emit` and run it anywhere. It makes the
optimisation visible: `usecols=` in the generated file *is* the pruning pass.
And it keeps a clean back-end boundary, so a Polars or DuckDB backend would be
a new code generator and nothing else.

**Q. What does your executor actually do, and is it safe?**
It runs the generated program in a fresh namespace containing only Pandas,
with the working directory set to the program's directory so relative paths
mean what the programmer wrote. It captures stdout for the contract messages,
keeps the dataframes for the IDE preview and verifies the output files. It is
not a sandbox against hostile code, and it does not need to be: the code was
produced by this compiler from a checked program, and PrepLang has no statement
that can emit arbitrary Python.

**Q. How do you know the compiler is correct?**
177 tests across seven modules, one per phase, each with positive and negative
cases. Three are property tests rather than output tests: optimised and
unoptimised pipelines must produce identical output; one fault must produce
exactly one diagnostic, not a cascade; and all twelve bundled example programs
must behave exactly as their comments claim.

**Q. What is the peak-memory result and why is it so large?**
51.0 MiB down to 7.2 MiB, an 85.9 % reduction, because the optimised pipeline
never materialises the 25 columns it does not need. The time saving (about
13 %) is smaller and machine-dependent; the memory saving is structural.

**Q. What are the limitations?**
CSV only; one condition per `FILTER`; no joins or group-by; schema inference
samples rows rather than reading the whole file, so a bad value beyond the
sample would be missed; and the optimiser does no cost-based reordering. All
are listed as scope exclusions or future work, not discovered afterwards.

**Q. If you had more time?**
A Polars backend first, because it exercises the IR boundary and needs no
front-end change at all. Then compound `FILTER` conditions, which turns the
grammar into a real expression sub-language with precedence — the natural next
compiler-design step.

## 4. Files to have open

| If asked about | Open |
|---|---|
| Liveness analysis | `preplang/optimizer.py` → `analyse_liveness` |
| The four passes | `preplang/optimizer.py` → `eliminate_redundant`, `eliminate_dead`, `prune_columns`, `rewrite_projections` |
| Soundness | `preplang/optimizer.py` module docstring |
| Emitters | `preplang/code_generator.py` → `_load`, `_normalize` |
| Execution | `preplang/executor.py` → `execute` |
| Phase orchestration | `preplang/pipeline.py` → `compile_source` |
| Correctness check | `benchmarks/performance.py` → section 4 |

## 5. Traps

* Have `pandas` installed on the demo machine, and run `python ui.py` once
  *before* the review — the IDE is the demonstration.
* When you toggle the optimiser in the IDE, say what you expect to change
  *before* you click. Predicting the output is what shows understanding.
* If asked "is this just a Pandas wrapper?", the answer is the error tabs: a
  wrapper cannot reject `NORMALIZE students.name` before reading the file, and
  cannot prune 25 columns it was never told about.
