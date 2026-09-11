# 04 — System Architecture and Methodology

## 4.1 Full compiler architecture

```
                       PrepLang source program (.prep)
                                   │
        ┌──────────────────────────▼──────────────────────────┐
        │  PHASE 1   LEXICAL ANALYSIS            lexer.py     │   ← Review 1
        │  characters ──► tokens   (+ lexical errors)         │
        └──────────────────────────┬──────────────────────────┘
                                token stream
        ┌──────────────────────────▼──────────────────────────┐
        │  PHASE 2   SYNTAX ANALYSIS             parser.py    │   ← Review 1 (recogniser)
        │  tokens ──► parse decisions ──► AST  (+ syntax errs)│     Review 2 (AST)
        └──────────────────────────┬──────────────────────────┘
                          Abstract Syntax Tree
                 ┌─────────────────┴─────────────────┐
                 ▼                                   ▼
    ┌────────────────────────┐          ┌─────────────────────────┐
    │ SYMBOL TABLE           │◄────────►│ SCHEMA ANALYSER         │   ← Review 2
    │ symbol_table.py        │          │ schema_analyzer.py      │
    │ datasets, columns,     │          │ reads CSV header + rows,│
    │ types, live/dropped    │          │ infers column datatypes │
    └───────────┬────────────┘          └────────────┬────────────┘
                └──────────────┬─────────────────────┘
                               ▼
        ┌──────────────────────────────────────────────────────┐
        │  PHASE 3   SEMANTIC ANALYSIS      semantic_analyzer.py│  ← Review 2
        │  scope + existence checks, static type checking,      │
        │  contract compatibility  (+ semantic / type errors)   │
        └──────────────────────────┬───────────────────────────┘
                          validated AST
        ┌──────────────────────────▼──────────────────────────┐
        │  PHASE 4   INTERMEDIATE CODE GEN    ir_generator.py  │  ← Review 2
        │  AST ──► linear IR instruction list                  │
        └──────────────────────────┬──────────────────────────┘
                                  IR
        ┌──────────────────────────▼──────────────────────────┐
        │  PHASE 5   OPTIMISATION              optimizer.py    │  ← Review 3
        │  column pruning · redundant-op elimination ·         │
        │  dead-operation elimination  (+ optimisation report) │
        └──────────────────────────┬──────────────────────────┘
                             optimised IR
        ┌──────────────────────────▼──────────────────────────┐
        │  PHASE 6   TARGET CODE GEN        code_generator.py  │  ← Review 3
        │  IR ──► Python / Pandas source code                  │
        └──────────────────────────┬──────────────────────────┘
                          generated Python
        ┌──────────────────────────▼──────────────────────────┐
        │  EXECUTION ENGINE                     executor.py    │  ← Review 3
        │  runs the generated code, validates data contracts   │
        └──────────────────────────┬──────────────────────────┘
                                   ▼
                          processed CSV dataset

        ┌──────────────────────────────────────────────────────┐
        │  CROSS-CUTTING:  errors.py (diagnostics for every     │
        │  phase)  ·  ui.py / main.py (stage visualisation)     │
        └──────────────────────────────────────────────────────┘
```

## 4.2 The two inputs

PrepLang is unusual among teaching compilers in that it consumes **two**
inputs:

| Input | Consumed by | Role |
|---|---|---|
| The `.prep` program | Lexer, parser | Defines *what* to compute |
| The `.csv` dataset header + sample rows | Schema analyser | Defines the *type environment* |

This is exactly the compiler notion of a symbol table populated from
declarations — except the declarations come from the data file rather than
from the program text. It is the core idea of the project.

## 4.3 Module inventory

| Module | Phase | Delivered in | Responsibility |
|---|---|---|---|
| `tokens.py` | 1 | Review 1 | Token categories, reserved words, `Token` record |
| `errors.py` | all | Review 1 | Structured diagnostics + `ErrorReporter` collector |
| `lexer.py` | 1 | Review 1 | Characters → tokens; lexical error recovery |
| `parser_basic.py` | 2 | Review 1 | LL(1) recursive-descent recogniser |
| `ast_nodes.py` | 2 | Review 2 | AST node dataclasses + pretty printer |
| `parser.py` | 2 | Review 2 | Full parser producing an AST |
| `symbol_table.py` | 3 | Review 2 | Dataset/column symbols, types, liveness |
| `schema_analyzer.py` | 3 | Review 2 | CSV schema inference |
| `semantic_analyzer.py` | 3 | Review 2 | Scope, existence and type checking |
| `contracts.py` | 3 | Review 2 | Data-contract compilation and validation |
| `ir.py`, `ir_generator.py` | 4 | Review 2 | IR instruction set and AST → IR lowering |
| `optimizer.py` | 5 | Review 3 | IR-level optimisation passes + report |
| `code_generator.py` | 6 | Review 3 | IR → Python/Pandas |
| `executor.py` | — | Review 3 | Sandboxed execution of generated code |
| `pipeline.py` | — | Review 3 | Drives all phases, collects artefacts |
| `ui.py` | — | Review 3 | Tkinter IDE showing every stage |

## 4.4 Data flow of one statement

Tracing `NORMALIZE students.age;` through the whole compiler:

| Stage | Representation |
|---|---|
| Source | `NORMALIZE students.age;` |
| Tokens | `KEYWORD(NORMALIZE) IDENT(students) DOT IDENT(age) SEMICOLON` |
| AST | `NormalizeNode(dataset='students', column='age', line=7)` |
| Symbol table | `students → Dataset(students.csv)`, `age → INTEGER, nullable` |
| Semantic check | `INTEGER ∈ {INTEGER, FLOAT}` ⇒ accepted; result type becomes `FLOAT` |
| IR | `IR_NORMALIZE students age` |
| Optimised IR | unchanged, but `age` is added to the live-column set of `IR_LOAD` |
| Python | `students["age"] = (students["age"] - students["age"].mean()) / students["age"].std()` |

## 4.5 Methodology

The project follows an **incremental, phase-by-phase development method**, one
compiler phase at a time, each with its own test module:

1. **Specify before implementing.** Tokens and the BNF grammar were fixed
   (documents 02 and 03) before any parser code was written; the LL(1)
   property was verified by hand from the FIRST/FOLLOW sets.
2. **Implement each phase behind a narrow interface.** A phase receives the
   previous phase's artefact and an `ErrorReporter`, and returns its own
   artefact. No phase reaches back into an earlier one.
3. **Collect errors, never abort.** Each phase records diagnostics and
   recovers, so one compilation reports many problems.
4. **Test each phase independently.** `pytest` modules mirror the phases
   (`test_lexer.py`, `test_parser_basic.py`, …), with both positive tests
   (valid programs) and negative tests (each error class).
5. **Keep every artefact printable.** Tokens, AST, symbol table, IR and
   generated code all have a `format_*` renderer, which is what makes the
   Review 3 visual IDE a thin layer rather than a separate system.

## 4.6 Error handling architecture

All six error classes derive from one base record so the front end renders
them uniformly:

```
CompilerError
├── LexicalError    (Phase 1)  unknown char, bad literal, unterminated string
├── SyntaxError_    (Phase 2)  unexpected token, missing terminator
├── SemanticError   (Phase 3)  undefined dataset, dropped column reuse
├── TypeError_      (Phase 3)  NORMALIZE on STRING, incomparable operands
├── SchemaError     (Phase 3)  column absent from CSV, unreadable file
├── ContractError   (Phase 3)  EXPECT violated by actual data
└── RuntimeError_   (execution) file missing, Pandas failure
```

Every diagnostic carries: phase, line, column, problem, expected, found,
optional hint and optional "did you mean" suggestions computed with
`difflib`.
