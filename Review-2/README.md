# PrepLang — Review 2 (Phase 2: Core Implementation)

**PrepLang: A Schema-Aware Domain-Specific Language Compiler for Safe and
Optimized Data Preprocessing**

Course: BCSE307P — Compiler Design Laboratory · Individual project

---

## What is new in Review 2

Review 1 delivered the lexer and a syntax *recogniser*. Review 2 delivers the
**complete compiler front end**: the parser now builds an AST, the dataset
schema is inferred and loaded into a symbol table, the semantic analyser
performs schema-aware static type checking, data contracts are compiled and
validated, and the validated program is lowered to an intermediate
representation.

| Compiler phase | Review 1 | Review 2 |
|---|---|---|
| 1. Lexical analysis | done | done |
| 2. Syntax analysis | recogniser only | **builds the AST** |
| Symbol table | — | **done** |
| Schema inference | — | **done** |
| 3. Semantic analysis + type checking | — | **done** |
| Data contracts | — | **done** |
| 4. Intermediate code generation | — | **done** |
| 5. Optimisation | — | Review 3 |
| 6. Python/Pandas code generation + execution | — | Review 3 |

## Folder layout

```
Review-2/
├── README.md                    this file
├── REVIEW2_GUIDE.md             what to present + viva questions
├── main.py                      CLI: prints every compiler stage
├── docs/
│   ├── 01_problem_statement.md      problem, objectives, scope
│   ├── 02_language_specification.md tokens, types, statements, rules S1–S10
│   ├── 03_grammar.md                BNF, FIRST/FOLLOW, LL(1) proof
│   ├── 04_system_architecture.md    architecture, modules, data flow
│   ├── 05_background_and_stack.md   background study, related work, stack
│   ├── 06_semantic_analysis.md      NEW — schema inference, symbol table,
│   │                                type rules, contracts, error recovery
│   └── 07_intermediate_representation.md  NEW — the IR and why it exists
├── preplang/
│   ├── tokens.py                token categories and reserved words
│   ├── errors.py                structured diagnostics for every phase
│   ├── lexer.py                 PHASE 1 — lexical analysis
│   ├── ast_nodes.py             AST node definitions + tree printer
│   ├── parser.py                PHASE 2 — syntax analysis, builds the AST
│   ├── schema.py                the type system + CSV schema analyser
│   ├── symbol_table.py          datasets, columns, types, liveness
│   ├── semantic_analyzer.py     PHASE 3 — semantic analysis, type checking
│   ├── contracts.py             data-contract compilation and validation
│   ├── ir.py                    the intermediate representation
│   ├── ir_generator.py          PHASE 4 — intermediate code generation
│   └── pipeline.py              drives all phases, collects every artefact
├── examples/
│   ├── students.csv             12 × 6, with nulls and one out-of-range value
│   ├── students_dirty.csv       age contains "abc" → inferred as STRING
│   ├── employees.csv            200 × 30, for column pruning in Review 3
│   ├── demo_valid.prep          compiles cleanly, all contracts pass
│   ├── demo_all_statements.prep every production exercised once
│   ├── demo_type_error.prep     6 semantic / type errors
│   ├── demo_dropped_column.prep use of a column after it was removed
│   ├── demo_dirty_schema.prep   contract refuted by the schema itself
│   ├── demo_contract_violation.prep  contracts refuted by the data
│   ├── demo_wide_dataset.prep   30-column file, redundant DROP_NULL
│   ├── demo_lexical_error.prep  3 lexical errors
│   └── demo_syntax_error.prep   5 syntax errors
└── tests/
    ├── conftest.py
    ├── test_lexer.py                21 tests — Phase 1
    ├── test_parser.py               23 tests — Phase 2 + AST
    ├── test_schema_and_symbols.py   30 tests — inference + symbol table
    ├── test_semantic.py             34 tests — rules S1–S10, recovery
    └── test_contracts_and_ir.py     19 tests — contracts + Phase 4
```

## How to run

No third-party package is required (the front end uses only the standard
library).

```bash
cd Review-2

# full pipeline on the clean demo: tokens, AST, symbols, semantics, contracts, IR
python main.py examples/demo_valid.prep

# just the stages you want
python main.py examples/demo_valid.prep --stages ast,symbols,ir

# schema-aware type errors (the headline feature)
python main.py examples/demo_type_error.prep --stages semantic

# a column used after it was removed
python main.py examples/demo_dropped_column.prep

# a contract refuted by the schema, before any data is read
python main.py examples/demo_dirty_schema.prep

# contracts refuted by the actual data, with row numbers
python main.py examples/demo_contract_violation.prep --stages contracts

# compile a one-liner
python main.py -c 'LOAD "students.csv" AS s; NORMALIZE s.name;'

# every bundled example
python main.py --demo

# the test suite
python -m pytest -q
```

## The headline result

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

The compiler knows `name` is `STRING` because it read the header and sampled
the rows of `students.csv` when the `LOAD` statement was analysed. No data is
processed; the program is rejected at compile time.

## Other diagnostics worth demonstrating

| Program | Diagnostic |
|---|---|
| `NORMALIZE students.salary;` | `SCHEMA ERROR: column 'salary' does not exist` + did-you-mean |
| `FILTER students WHERE marks > "good";` | `TYPE ERROR: cannot compare INTEGER with STRING` |
| `FILTER students WHERE department > "CSE";` | `TYPE ERROR: '>' requires numerical operands` |
| `FILL_NULL students.attendance WITH "unknown";` | `TYPE ERROR: cannot fill a FLOAT column with a STRING value` |
| `SELECT students [name]; NORMALIZE students.age;` | `SEMANTIC ERROR: column 'age' is no longer available; removed at line 1` |
| `EXPECT students.age NUMBER;` on `students_dirty.csv` | `DATA CONTRACT ERROR: declared NUMBER but the schema says STRING` |
| `EXPECT students.marks BETWEEN 0 AND 100;` | `[FAIL] 1 violation in 12 rows — row 6: '150'` |

## Test results

```
127 passed
```

| Module | Tests | Covers |
|---|---|---|
| `test_lexer.py` | 21 | Token recognition, positions, all lexical error classes, recovery |
| `test_parser.py` | 23 | One per production, AST shape, traversal, syntax errors, recovery |
| `test_schema_and_symbols.py` | 30 | Type inference (9 cases), nullability, symbol table, liveness |
| `test_semantic.py` | 34 | Rules S1–S10, suggestions, warnings, cascade suppression |
| `test_contracts_and_ir.py` | 19 | Static + data contract checking, IR lowering, bundled examples |
