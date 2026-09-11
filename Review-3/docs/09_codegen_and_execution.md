# 09 — Target Code Generation, Execution and Testing

## 9.1 Code generation

The back end emits **Python/Pandas source**, one emitter function per opcode,
so the mapping is a table rather than a tangle of special cases.

| IR instruction | Emitted Python |
|---|---|
| `IR_LOAD d "f.csv"` | `d = pd.read_csv("f.csv", usecols=[...])` |
| `IR_DROP_NULL d` | `d = d.dropna(subset=[...])` |
| `IR_FILL_NULL d c <- v` | `d["c"] = d["c"].fillna(v)` |
| `IR_NORMALIZE d c` | `d["c"] = (d["c"] - d["c"].mean()) / d["c"].std()` |
| `IR_FILTER d c op v` | `d = d[d["c"] op v]` |
| `IR_SELECT d [a, b]` | `d = d[["a", "b"]]` |
| `IR_DROP d [a, b]` | `d = d.drop(columns=["a", "b"])` |
| `IR_SAVE d "o.csv"` | `d.to_csv("o.csv", index=False)` |
| `IR_EXPECT_TYPE d c t` | `_expect_type(d, "d", "c", "t", line)` |
| `IR_EXPECT_RANGE d c lo hi` | `_expect_range(d, "d", "c", lo, hi, line)` |
| `IR_EXPECT_NOT_NULL d c` | `_expect_not_null(d, "d", "c", line)` |

Every emitted block carries a comment naming the IR instruction and the source
line it came from, so the generated file can be read against the original
program.

### Why compile to source rather than interpret the IR?

* The output is ordinary Pandas: it can be saved, read, reviewed, modified and
  run by someone who has never heard of PrepLang.
* It makes the optimisation visible — `usecols=` in the generated file *is* the
  column-pruning pass.
* Interpreting the IR would fuse the back end into the compiler and remove the
  clean boundary that makes a second backend (Polars, DuckDB) a drop-in
  replacement.

### Contracts are compiled, not bolted on

The prelude of every generated program defines three validation helpers, and
each `EXPECT` becomes a call to one of them. Contract checking is therefore
part of the compiled pipeline:

```text
[PASS] emp.age: every value must be NUMBER (200 rows checked)
[PASS] emp.rating: 1 <= value <= 5 (200 rows checked)
wrote clean_employees.csv - 188 rows x 4 columns
```

## 9.2 Example output

Input:

```text
LOAD "employees.csv" AS emp;
EXPECT emp.rating BETWEEN 1 AND 5;
DROP_NULL emp;
DROP_NULL emp;
NORMALIZE emp.salary;
FILTER emp WHERE age > 21;
SELECT emp [ name, age, salary, department ];
SAVE emp AS "clean_employees.csv";
```

Generated (body; the prelude is omitted here):

```python
# line 2: IR_LOAD "employees.csv"
emp = pd.read_csv(
    'employees.csv',
    usecols=['name', 'age', 'salary', 'department', 'rating'],   # column pruning
)

# line 3: IR_EXPECT_RANGE rating in [1, 5]
_expect_range(emp, 'emp', 'rating', 1, 5, 3)

# line 4: IR_DROP_NULL
emp = emp.dropna(subset=['name', 'age', 'salary', 'department'])

# line 6: IR_NORMALIZE salary
emp['salary'] = (
    emp['salary'] - emp['salary'].mean()
) / emp['salary'].std()

# line 7: IR_FILTER age > 21
emp = emp[emp['age'] > 21]

# line 8: IR_SELECT [name, age, salary, department]
emp = emp[['name', 'age', 'salary', 'department']]

# line 9: IR_SAVE "clean_employees.csv"
emp.to_csv('clean_employees.csv', index=False)
```

Note what is *not* there: the second `DROP_NULL` (pass 1), and 25 of the 30
columns (pass 3).

## 9.3 The execution engine

`executor.py` runs the generated program in a fresh namespace containing only
Pandas, with the working directory set to the directory of the PrepLang
program so that relative paths mean what the programmer wrote. It captures
standard output (the contract messages), keeps the resulting dataframes for
the IDE preview, and checks that the expected output files were written.

Because the compiler has already proved the program well formed, anything that
fails here is a genuine **runtime** error and is reported as one:

```text
RUNTIME ERROR

Problem:
    The generated program could not open a file.

Found:
    [Errno 2] No such file or directory: 'definitely_missing.csv'

Hint:
    Check the paths used in the LOAD and SAVE statements.
```

This completes the error taxonomy: lexical, syntax, semantic, type, schema,
contract and runtime — the seventh and last class.

## 9.4 The visual IDE

`ui.py` is a Tkinter application with the source on the left and one tab per
compiler artefact on the right:

```
Tokens | AST | Symbols & Schema | Semantics | Contracts |
IR | Optimised IR | Python | Output | Errors
```

*Compile* (F5) runs every phase and refreshes every tab; *Run* (F6) also
executes the generated code and shows the processed dataset. Checkboxes toggle
the optimiser and contract validation, so the effect of optimisation can be
shown live by comparing the IR and Python tabs with the box on and off.

Tkinter is part of the standard library, so the demonstration machine needs no
installation beyond Pandas — and Pandas is needed only for the Run button.

## 9.5 Testing strategy

One test module per phase, with positive tests (the phase accepts what it
should) and negative tests (the phase rejects what it should, at the right
place, with the right message).

| Module | Tests | Covers |
|---|---|---|
| `test_lexer.py` | 21 | Token recognition, positions, comments, four lexical error classes, recovery |
| `test_parser.py` | 23 | One test per grammar production, AST shape, traversal, seven syntax error classes, panic-mode recovery |
| `test_schema_and_symbols.py` | 30 | Type inference (nine cases), nullability, missing files, symbol table, liveness |
| `test_semantic.py` | 34 | Rules S1–S10, did-you-mean suggestions, warnings, cascade suppression |
| `test_contracts_and_ir.py` | 19 | Static and data contract checking with row numbers, IR lowering |
| `test_optimizer.py` | 24 | All four passes, liveness, fixed point, `usecols` ordering, report |
| `test_codegen_and_execution.py` | 26 | Every emitter, generated code validity, execution, end-to-end examples |
| **Total** | **177** | |

Three tests deserve particular mention because they check properties rather
than outputs:

* `test_optimised_and_unoptimised_pipelines_produce_identical_output` —
  optimisation must not change the answer.
* `test_a_missing_csv_file_is_reported_once_without_cascading` — a single
  fault must produce a single diagnostic.
* `test_every_valid_example_compiles_and_runs` /
  `test_every_broken_example_is_still_rejected` — the twelve bundled example
  programs behave exactly as their comments claim.

## 9.6 Performance evaluation

`benchmarks/performance.py` generates a dataset of a chosen size, compiles the
same pipeline with and without optimisation, runs both, and reports
compilation time per phase, the static optimisation effect, execution time and
peak memory, and finally a byte-for-byte comparison of the two outputs. See
document 08 §8.7 for the measured results.
