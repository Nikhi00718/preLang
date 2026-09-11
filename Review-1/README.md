# PrepLang — Review 1 (Phase 1: Problem Definition and Design)

**PrepLang: A Schema-Aware Domain-Specific Language Compiler for Safe and
Optimized Data Preprocessing**

Course: BCSE307P — Compiler Design Laboratory · Individual project

---

## What this folder contains

Phase 1 of the project: the problem definition, the complete language and
grammar specification, the system architecture, **and a working prototype** of
the first two compiler phases.

| | Status in Review 1 |
|---|---|
| Phase 1 — Lexical analysis | **Implemented** (`preplang/lexer.py`) |
| Phase 2 — Syntax analysis | **Implemented as a recogniser** (`preplang/parser_basic.py`) |
| AST, symbol table, schema, semantic analysis, IR | Designed (see `docs/`), built in Review 2 |
| Optimiser, code generator, executor, IDE | Designed, built in Review 3 |

## Folder layout

```
Review-1/
├── README.md                      this file
├── REVIEW1_GUIDE.md               what to present + viva questions
├── PrepLang_Review1_Report.docx   ← the document to upload
├── main.py                        command line driver
├── docs/
│   ├── 01_problem_statement.md    problem, motivation, objectives, scope
│   ├── 02_language_specification.md  tokens, keywords, types, statements, semantic rules
│   ├── 03_grammar.md              BNF, FIRST/FOLLOW, LL(1) proof, recovery strategy
│   ├── 04_system_architecture.md  architecture diagram, modules, data flow, methodology
│   └── 05_background_and_stack.md background study, related work, technology stack
├── preplang/
│   ├── __init__.py
│   ├── tokens.py                  token categories and reserved words
│   ├── errors.py                  structured diagnostics for every phase
│   ├── lexer.py                   PHASE 1 — hand-written DFA scanner
│   └── parser_basic.py            PHASE 2 — LL(1) recursive-descent recogniser
├── examples/
│   ├── students.csv               main demo dataset (12 rows, 6 columns)
│   ├── students_dirty.csv         dataset with type + range violations
│   ├── employees.csv              200 rows × 30 columns (for column pruning later)
│   ├── demo_valid.prep            the Review 1 demo program (compiles cleanly)
│   ├── demo_all_statements.prep   every grammar production exercised once
│   ├── demo_lexical_error.prep    deliberate lexical errors
│   └── demo_syntax_error.prep     deliberate syntax errors
└── tests/
    ├── test_lexer.py              21 tests
    └── test_parser_basic.py       25 tests
```

## How to run

No third-party packages are required for Review 1 (standard library only).

```bash
cd Review-1

# compile the demo program: shows tokens + recognised statements
python main.py examples/demo_valid.prep

# show error detection and recovery
python main.py examples/demo_lexical_error.prep --no-tokens
python main.py examples/demo_syntax_error.prep --no-tokens

# compile a one-liner
python main.py -c "NORMALIZE students.age;"

# run every bundled example in sequence (the review demo)
python main.py --demo

# run the test suite
python -m pytest -q
```

## What the prototype demonstrates

1. **Lexical analysis** — a hand-written scanner that produces a fully
   annotated token stream (type, lexeme, semantic value, line, column),
   handles keywords case-insensitively, distinguishes `students.age` from
   `3.14`, skips `#` and `--` comments, and reports unknown characters,
   malformed numbers, unterminated strings and invalid operators — recovering
   after each so all of them are reported in one run.

2. **Syntax analysis** — an LL(1) recursive-descent recogniser covering all
   nine statement forms and all five contract forms, producing located
   diagnostics of the form *expected / found / hint*, with panic-mode recovery
   that lets one run report several independent syntax errors.

3. **Diagnostic infrastructure** — one `CompilerError` hierarchy shared by all
   six error classes, with a caret pointing at the offending column and
   "did you mean" suggestions ready for the semantic phase.

## Example output

```
--- PHASE 1: LEXICAL ANALYSIS (TOKENS) -----------------------------
LINE:COL   LEXEME               TOKEN TYPE           VALUE
6:1        LOAD                 KEYWORD              'LOAD'
6:6        "students.csv"       STRING_LITERAL       'students.csv'
6:21       AS                   KEYWORD              'AS'
6:24       students             IDENTIFIER           'students'
6:32       ;                    SEMICOLON            ';'
...

--- PHASE 2: SYNTAX ANALYSIS (RECOGNISED STATEMENTS) ---------------
#   LINE   STATEMENT     DETAILS
1   6      LOAD          file='students.csv', dataset='students'
2   8      EXPECT        dataset='students', column='age', contract='TYPE', type='NUMBER'
...
10 statement(s) recognised
```

```
SYNTAX ERROR - Line 3

Problem:
    Unexpected 'students' (IDENTIFIER).

Source:
    LOAD "students.csv" students;
                        ^

Expected:
    keyword AS

Found:
    'students' (IDENTIFIER)
```

## Test results

```
46 passed
```

21 lexer tests (token recognition, positions, comments, every error class and
recovery) and 25 parser tests (one per grammar production, every syntax error
class, recovery behaviour, and the bundled example programs).
