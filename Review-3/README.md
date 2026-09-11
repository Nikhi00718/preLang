# PrepLang — Review 3 (Phase 3: Final Implementation and Testing)

**PrepLang: A Schema-Aware Domain-Specific Language Compiler for Safe and
Optimized Data Preprocessing**

Course: BCSE307P — Compiler Design Laboratory · Individual project

---

## The complete compiler

```
PrepLang source
   → tokens → AST → symbol table + inferred schema → semantic analysis
   → data contracts → IR → optimised IR → Python/Pandas → processed CSV
```

Every phase is implemented, tested and visible.

| Compiler phase | Status |
|---|---|
| 1. Lexical analysis | done (Review 1) |
| 2. Syntax analysis + AST | done (Review 2) |
| Symbol table + schema inference | done (Review 2) |
| 3. Semantic analysis + static type checking | done (Review 2) |
| Data contracts (static + data) | done (Review 2) |
| 4. Intermediate code generation | done (Review 2) |
| **5. Optimisation (4 passes)** | **done — this phase** |
| **6. Target code generation (Python/Pandas)** | **done — this phase** |
| **Execution engine** | **done — this phase** |
| **Visual IDE (stage visualiser)** | **done — this phase** |
| **Performance evaluation** | **done — this phase** |

## Quick start

```bash
cd Review-3
pip install pandas          # only needed to *run* the generated code

# 1. the whole pipeline, every stage printed
python main.py examples/demo_valid.prep

# 2. compile AND execute
python main.py examples/demo_valid.prep --run

# 3. the optimisation showcase: 30 columns in, 5 needed
python main.py examples/demo_wide_dataset.prep --stages opt,code --run

# 4. schema-aware type errors
python main.py examples/demo_type_error.prep --stages semantic

# 5. the visual IDE  <- the demonstration
python ui.py

# 6. performance evaluation
python benchmarks/performance.py --rows 50000

# 7. the test suite
python -m pytest -q
```

## Folder layout

```
Review-3/
├── README.md                  this file
├── REVIEW3_GUIDE.md           what to present + viva questions
├── main.py                    CLI: every compiler stage, --run, --emit
├── ui.py                      the Tkinter IDE (compiler-stage visualiser)
├── docs/
│   ├── 01_problem_statement.md          problem, objectives, scope
│   ├── 02_language_specification.md     tokens, types, statements, rules S1–S10
│   ├── 03_grammar.md                    BNF, FIRST/FOLLOW, LL(1) proof
│   ├── 04_system_architecture.md        architecture, modules, data flow
│   ├── 05_background_and_stack.md       background study, related work, stack
│   ├── 06_semantic_analysis.md          schema inference, symbol table, type rules
│   ├── 07_intermediate_representation.md the IR and why it exists
│   ├── 08_optimisation.md               NEW — liveness, 4 passes, soundness, results
│   └── 09_codegen_and_execution.md      NEW — emitters, executor, IDE, testing
├── preplang/
│   ├── tokens.py · errors.py · lexer.py          Phase 1
│   ├── ast_nodes.py · parser.py                  Phase 2
│   ├── schema.py · symbol_table.py               type environment
│   ├── semantic_analyzer.py · contracts.py       Phase 3
│   ├── ir.py · ir_generator.py                   Phase 4
│   ├── optimizer.py                              PHASE 5  (new)
│   ├── code_generator.py                         PHASE 6  (new)
│   ├── executor.py                               execution engine (new)
│   └── pipeline.py                               drives every phase
├── examples/         12 PrepLang programs + 3 CSV datasets
├── tests/            7 modules, 177 tests
└── benchmarks/
    └── performance.py  compile/run, optimised vs unoptimised, byte comparison
```

## What Review 3 adds

### 1. Optimisation — four passes over one analysis

A backwards **live-column analysis** (the analogue of live-variable analysis)
drives everything:

| Pass | Effect |
|---|---|
| 1. Redundant-operation elimination | Two identical idempotent operations collapse into one |
| 2. Dead-operation elimination | An operation writing a column nobody reads is removed, with a warning |
| 3. Column pruning | Only the required columns are read: `usecols=[...]` |
| 4. Projection repair | `SELECT`/`DROP` are rewritten to match the pruned schema |

```text
OPTIMIZATION REPORT
==============================================================
IR instructions before          : 9
IR instructions after           : 8
Instructions eliminated         : 1 (11.1% reduction)

Columns available in the file(s): 30
Columns actually required       : 5
Columns pruned (never loaded)   : 25 (83.3% of the file)
```

### 2. Target code generation

```python
# line 2: IR_LOAD "employees.csv"
emp = pd.read_csv(
    'employees.csv',
    usecols=['name', 'age', 'salary', 'department', 'rating'],   # column pruning
)

# line 4: IR_DROP_NULL
emp = emp.dropna(subset=['name', 'age', 'salary', 'department'])

# line 6: IR_NORMALIZE salary
emp['salary'] = (
    emp['salary'] - emp['salary'].mean()
) / emp['salary'].std()
```

Ordinary Pandas — readable, saveable (`--emit out/pipeline.py`) and runnable
without PrepLang.

### 3. Execution engine

Runs the generated program, captures the contract messages, reports the shape
of the result and whether the output file was written, and turns any failure
into a structured `RUNTIME ERROR` — the seventh and last diagnostic class.

### 4. The visual IDE (`python ui.py`)

Source on the left; ten tabs on the right, one per artefact:
**Tokens · AST · Symbols & Schema · Semantics · Contracts · IR · Optimised IR ·
Python · Output · Errors**. F5 compiles, F6 compiles and runs. The optimiser
can be toggled live, so its effect on the IR and the generated code is visible
side by side.

### 5. Performance evaluation

On a generated 30,000 × 30 dataset (5.0 MiB):

| Metric | Unoptimised | Optimised | Change |
|---|---|---|---|
| IR instructions | 9 | 8 | −11.1 % |
| Columns read from disk | 30 | 5 | −83.3 % |
| Execution time | 1227.8 ms | 1070.8 ms | −12.8 % |
| Peak memory | 51.0 MiB | 7.2 MiB | **−85.9 %** |
| Output | 28 914 × 4 | 28 914 × 4 | **byte-identical** |

The last row is the one that matters: the benchmark compares the two output
files byte for byte, so the optimisation is shown to be sound rather than
merely claimed to be.

## The seven error classes

| Class | Example | Detected in |
|---|---|---|
| Lexical | `NORMALIZE data$age;` | Phase 1 |
| Syntax | `LOAD "s.csv" students;` | Phase 2 |
| Semantic | `NORMALIZE results.age;` (undefined dataset) | Phase 3 |
| Type | `NORMALIZE students.name;` (`name : STRING`) | Phase 3 |
| Schema | `NORMALIZE students.salary;` (no such column) | Phase 3 |
| Data contract | `EXPECT students.marks BETWEEN 0 AND 100;` with `marks = 150` | Phase 3 / validation |
| Runtime | the CSV disappeared between compiling and running | Execution |

## Test results

```
177 passed
```

| Module | Tests |
|---|---|
| `test_lexer.py` | 21 |
| `test_parser.py` | 23 |
| `test_schema_and_symbols.py` | 30 |
| `test_semantic.py` | 34 |
| `test_contracts_and_ir.py` | 19 |
| `test_optimizer.py` | 24 |
| `test_codegen_and_execution.py` | 26 |

## Requirements

* Python 3.10 or newer (developed on 3.14)
* `pandas` — only to execute the generated code; the compiler itself uses the
  standard library alone
* Tkinter (part of the standard library) for the IDE
