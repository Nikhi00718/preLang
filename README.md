# PrepLang — Compiler Design Laboratory Project

**PrepLang: A Schema-Aware Domain-Specific Language Compiler for Safe and
Optimized Data Preprocessing**

Course: BCSE307P — Compiler Design Laboratory (Fall Semester 2026–27)
Individual project · Three phases · Three reviews

---

## The idea in one sentence

> A dataset schema is a type environment — so `NORMALIZE students.name` where
> `name : STRING` is exactly the error `int x = "hello"` is, and a compiler can
> reject it before a single row of data is read.

PrepLang is a small language for describing data-preprocessing pipelines, and a
complete compiler for it: lexical analysis, LL(1) recursive-descent parsing,
AST construction, symbol-table management, CSV schema inference, schema-aware
semantic analysis and static type checking, data-contract validation,
intermediate code generation, machine-independent optimisation, and
Python/Pandas target code generation with an execution engine.

```text
LOAD "students.csv" AS students;

EXPECT students.age NUMBER;
EXPECT students.attendance BETWEEN 0 AND 100;

FILL_NULL students.attendance WITH 0;
DROP_NULL students;
NORMALIZE students.age;
FILTER students WHERE marks >= 50;

SELECT students [ name, age, marks ];
SAVE students AS "cleaned_students.csv";
```

## One folder per review

| Folder | Phase | Contents | Tests |
|---|---|---|---|
| **[Review-1/](Review-1/)** | Phase 1 — Problem definition and design | Problem statement, objectives, background study, full language and grammar specification with the LL(1) proof, system architecture, **+ working lexer and syntax recogniser**. Contains **`PrepLang_Review1_Report.docx`**, the document to upload. | 46 |
| **[Review-2/](Review-2/)** | Phase 2 — Core implementation | The complete compiler **front end**: AST, symbol table, CSV schema inference, semantic analysis with static type checking, data contracts, intermediate representation | 127 |
| **[Review-3/](Review-3/)** | Phase 3 — Final implementation and testing | The **back end**: four optimisation passes driven by live-column analysis, Python/Pandas code generation, execution engine, the visual IDE, benchmarks | 177 |

Each folder is **self-contained and runnable on its own** — its own
`preplang/` package, examples, tests, docs, README and a presentation/viva
guide. That way each review demonstrates exactly the state of the project at
that point, and nothing from a later phase leaks into an earlier demo.

## Start here

```bash
# Review 1 — lexical analysis and syntax validation
cd Review-1  && python main.py examples/demo_valid.prep && python -m pytest -q

# Review 2 — the complete front end, all stages printed
cd Review-2  && python main.py examples/demo_valid.prep && python -m pytest -q

# Review 3 — the complete compiler, plus the IDE
cd Review-3  && pip install pandas
              python main.py examples/demo_valid.prep --run
              python ui.py
              python benchmarks/performance.py --rows 50000
              python -m pytest -q
```

## The headline results

**Schema-aware static type checking** — no data is read before the program is
rejected:

```text
TYPE ERROR - Line 7

Problem:
    NORMALIZE requires a numerical column.

Source:
    NORMALIZE students.name;
              ^

Expected:
    a column of type INTEGER or FLOAT

Found:
    students.name : STRING

Did you mean:
    age
    marks
    attendance
```

**IR-level optimisation** — on a 30,000 × 30 dataset:

| Metric | Unoptimised | Optimised | Change |
|---|---|---|---|
| IR instructions | 9 | 8 | −11.1 % |
| Columns read from disk | 30 | 5 | −83.3 % |
| Execution time | 1227.8 ms | 1070.8 ms | −12.8 % |
| Peak memory | 51.0 MiB | 7.2 MiB | −85.9 % |
| Output | 28 914 × 4 | 28 914 × 4 | byte-identical |

## What makes it a compiler project, not a Pandas wrapper

| Compiler concept | Where it lives |
|---|---|
| Lexical analysis, DFA, maximal munch | `lexer.py` |
| CFG, FIRST/FOLLOW, LL(1), recursive descent | `parser.py`, `docs/03_grammar.md` |
| Panic-mode error recovery | `parser.py::_synchronise` |
| Abstract Syntax Tree | `ast_nodes.py` |
| Symbol table, scope, liveness | `symbol_table.py` |
| Type inference | `schema.py` (from the CSV, not from declarations) |
| Type checking, semantic rules S1–S10 | `semantic_analyzer.py` |
| Intermediate representation | `ir.py`, `ir_generator.py` |
| Live-variable analysis | `optimizer.py::analyse_liveness` |
| Dead-code elimination, redundancy elimination | `optimizer.py` |
| Machine-independent optimisation (projection push-down) | `optimizer.py::prune_columns` |
| Target code generation | `code_generator.py` |
| Runtime environment | `executor.py` |
| Seven-class structured error reporting | `errors.py` |

## Source material

* `PrepLang_Project_Idea.md` — the original project concept.
* `FALLSEM2026-27_VL_BCSE307P_00100_LO_2026-09-03_Compiler-lab-project-instruction.pdf`
  — the laboratory instruction manual the deliverables follow.
