# Review 2 — Presentation Guide and Viva Preparation

Review 2 carries **25 marks** and is assessed on *implementation progress*, not
on slides. The instruction manual is explicit: "Simply presenting source code
without execution or demonstration is not sufficient." Everything below is
something you run.

---

## 1. Mark-by-mark checklist

| Criterion | Marks | What to show |
|---|---|---|
| Implementation Progress | 7 | Four phases now complete: AST, symbol table + schema, semantic analysis + type checking, IR. Show `python main.py examples/demo_valid.prep` — every stage prints. |
| Technical Correctness | 5 | The diagnostics table below: each error class demonstrated live on a real file, at the right line, with the right message |
| Compiler Concept Application | 4 | Say where each concept lives: AST (`ast_nodes.py`), symbol table + scope (`symbol_table.py`), type inference (`schema.py`), type checking (`semantic_analyzer.py`), IR (`ir.py`) |
| Code Quality | 3 | One module per phase, narrow interfaces, every module's docstring states its phase and its algorithm |
| Testing | 3 | `python -m pytest -q` → 127 passed; one test module per phase, positive **and** negative cases |
| Individual Understanding | 3 | The viva answers below |

## 2. Suggested 10-minute demonstration

**(1) The clean run — everything works**

```bash
python main.py examples/demo_valid.prep
```

Walk the output top to bottom: tokens → AST → symbol table with the *inferred*
schema → the checks the analyser performed → contract report → IR. Say one
sentence per stage. This single command shows four complete compiler phases.

**(2) The headline: schema-aware type checking**

```bash
python main.py examples/demo_type_error.prep --stages semantic
```

Six errors from one run. Point at `NORMALIZE students.name` and say: *"the
compiler read students.csv when it processed LOAD, inferred `name : STRING`,
and rejected the program before touching a single row — this is exactly
`int x = "hello"` in a normal compiler."*

**(3) The symbol table earning its keep**

```bash
python main.py examples/demo_dropped_column.prep
```

*"`attendance` does exist in the file — it was removed by the SELECT on line 8.
The analyser can say so because dropped columns are marked inactive, not
deleted."*

**(4) Contracts, both kinds**

```bash
python main.py examples/demo_dirty_schema.prep --stages semantic     # refuted statically
python main.py examples/demo_contract_violation.prep --stages contracts   # refuted by the data
```

*"The first is rejected at compile time from the schema alone. The second
compiles, then the validator streams the file once and reports row 6, value
150."*

**(5) Tests**

```bash
python -m pytest -q
```

## 3. Viva questions and answers

**Q. What does your semantic analyser actually check?**
Ten static rules. Name resolution: a dataset must be loaded before use (S1),
may be bound only once (S2), a referenced column must exist (S3) and must not
have been removed earlier (S4). Type rules: `NORMALIZE` needs a numeric column
(S5), `FILTER` operands must be comparable (S6) and ordering operators need
numbers (S7), a `FILL_NULL` value must be assignable to the column type (S8),
a range contract needs a numeric column and `low ≤ high` (S9), and `SAVE`
needs a dataset with at least one live column (S10).

**Q. Where does the type information come from? You never declare types.**
From `schema.py`. When `LOAD` is analysed, the schema analyser reads the CSV
header for the column names and samples up to 2000 rows to infer each column's
type — BOOLEAN, then INTEGER, then FLOAT, else STRING — plus nullability. That
inferred schema is loaded into the symbol table and becomes the type
environment. A conventional compiler gets its type environment from
declarations in the program; mine gets it from the data.

**Q. Why does one bad value make the whole column STRING?**
Because inference is conservative, and that conservatism is the feature. In
`students_dirty.csv`, `age` contains `abc`, so the column is `STRING`, so
`NORMALIZE students.age` is a compile-time type error. If I inferred INTEGER
and ignored the outlier, the error would surface at run time instead — which is
the problem the project exists to solve.

**Q. What is in your symbol table, and why keep dropped columns?**
A global scope of datasets; each dataset is itself a scope of columns. Each
column symbol stores its type, nullability, liveness and history. Dropped
columns are marked inactive rather than deleted so the analyser can
distinguish a typo ("column does not exist") from a lifetime error ("column is
no longer available; it was removed at line 8"). That is the same distinction
a C compiler makes between an undeclared identifier and one that is out of
scope.

**Q. Is your analysis flow-sensitive?**
Yes. The symbol table is updated as statements are processed: `DROP_NULL`
clears nullability on every live column, `NORMALIZE` changes a column's type to
FLOAT, `SELECT` deactivates the columns not listed. That is why the compiler
can warn that a `FILL_NULL` after a `DROP_NULL` has no effect — a conclusion
that needs state across statements, not a per-statement check.

**Q. Show me a type rule.**
`assignable(v, t) = (v = t) ∨ (t = FLOAT ∧ v = INTEGER)` — one implicit
coercion, the classical numeric widening. That is why
`FILL_NULL students.attendance WITH 0` is accepted although `attendance` is
FLOAT and the literal is INTEGER, while `WITH "unknown"` is rejected.

**Q. How do you avoid a wall of cascading errors?**
Two mechanisms. A failed `LOAD` still binds the dataset name with an empty
scope, so later statements do not each report "undefined dataset". And when a
dataset's schema is missing, column lookups return quietly instead of reporting
"column does not exist" for every following line. A missing CSV file produces
exactly one diagnostic — there is a test for it.

**Q. Why are data contracts part of the language rather than a library?**
Because then the compiler can decide them. `EXPECT students.age NUMBER` is
resolved *statically* against the inferred schema and never reads the data. A
validation library could only check it at run time. What cannot be decided
statically — whether a value is inside a range, whether a nullable column
actually contains a null — is checked by streaming the file once per dataset,
with the spreadsheet row number reported.

**Q. Why did you add an IR? Why not generate Pandas from the AST?**
Three reasons. Optimisation: column pruning and redundancy elimination are
list rewrites over the IR and would be far harder as tree rewrites. Retarget-
ability: no IR instruction mentions Pandas, so a Polars or DuckDB backend needs
only a new code generator. And intermediate code generation is an examinable
phase in its own right.

**Q. What is `same_effect_as` for?**
Two IR instructions are equal in effect when their opcode, dataset and operands
match. Combined with the `IDEMPOTENT_OPS` set (`DROP_NULL`, `NORMALIZE`,
`SELECT`), that is exactly what the Review 3 redundant-operation elimination
pass needs: two consecutive `DROP_NULL` collapse into one.

**Q. What is left for Review 3?**
The back end: the three optimisation passes with an optimisation report,
Python/Pandas code generation, the execution engine, the visual IDE showing
every stage side by side, and the performance evaluation.

## 4. Files you must be able to open and explain

| If asked about | Open |
|---|---|
| Type inference | `preplang/schema.py` → `infer_column_type` |
| The symbol table | `preplang/symbol_table.py` → `DatasetSymbol.keep_only` |
| The headline error | `preplang/semantic_analyzer.py` → `_normalize` |
| Comparison rules | `preplang/semantic_analyzer.py` → `_comparable`, `_assignable` |
| Contract checking | `preplang/contracts.py` → `_violates`, `validate_contracts` |
| AST construction | `preplang/parser.py` → `_filter_stmt`, `_condition` |
| IR lowering | `preplang/ir_generator.py` → `_lower` |

## 5. Traps

* Do not claim optimisation or code generation — that is Review 3. Claiming
  unfinished work is the fastest way to lose viva marks.
* Be precise that contracts are checked in *two* stages; examiners often ask
  "so is it compile time or run time?" The answer is: both, deliberately.
* When asked "what is novel", do not say "a preprocessing DSL". Say: schema
  inference used as a type environment, contracts as language statements, and
  IR-level column pruning, combined in one small compiler.
