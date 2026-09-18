# PrepLang

**Author:** Nikhil

PrepLang is a small language for preparing datasets — CSV tables, folders of CSV tables, and
image datasets — and this is its compiler, written in C++17. It reads the data a program uses
**at compile time** (every CSV cell, and the header of every image file), follows what is known
about the data through every statement, proves or refutes the program's data contracts before
anything runs, optimizes the pipeline, and generates one Python program that uses pandas for
tables and OpenCV for images.

```text
.prep -> Lexer -> Parser -> AST -> Semantic analysis over the abstract dataset state -> IR -> Optimizer
         (tokens)  (LL(1))          (CSV cells + image headers read here)                 (3 passes)
      -> Execution planner -> pandas code + OpenCV code -> output/generated.py (+ run manifest)
```

For example, change `CONVERT imgs TO NPY;` to `CONVERT imgs TO PNG;` in
`examples/images/linked.prep` and compile it:

```text
error[type]: cannot save FLOAT32 pixels as PNG: NORMALIZE at line 10 scaled them to [0, 1]
   --> examples/images/linked.prep:13:1
   |
13 | SAVE_IMAGES imgs TO "processed/ml_ready/";
   | ^
   = hint: image files store 8-bit pixels, so these would come out almost black; add CONVERT imgs TO NPY; to keep the float values
```

A hand-written pandas/OpenCV script with the same mistake runs without an error and writes black
images.

## Contents

1. [Run it in VS Code (step by step)](#run-it-in-vs-code-step-by-step)
2. [Run it from a terminal](#run-it-from-a-terminal)
3. [The language](#the-language)
4. [What the compiler knows and proves](#what-the-compiler-knows-and-proves)
5. [Optimizations](#optimizations)
6. [Examples](#examples)
7. [Evaluation](#evaluation)
8. [Project layout](#project-layout)

## Run it in VS Code (step by step)

**One-time setup**

1. Install **Visual Studio 2022 Build Tools** with the *Desktop development with C++* workload
   (this gives the MSVC compiler), and **CMake** 3.16 or newer.
2. Install **Python 3** and the libraries the generated programs use (pandas 2.0 or newer):
   ```bash
   py -3 -m pip install pandas numpy opencv-python
   ```
3. Open the `PrepLang` folder in VS Code (*File > Open Folder...*). Accept the recommended
   extensions when asked: C/C++, CMake Tools and Python (listed in `.vscode/extensions.json`).

**Every day**

4. **Build:** press `Ctrl+Shift+B` (task *PrepLang: build*). The first build configures CMake into
   `build/`; `build/Debug/preplang.exe` is the compiler.
5. **Compile and run a program:** open a `.prep` file, for example `examples/tabular/students.prep`,
   then *Terminal > Run Task... > PrepLang: compile and run the open .prep file*. The terminal shows
   every compiler phase, then runs `output/generated.py`; the cleaned data appears in `output/`.
6. **See the plan without running anything:** *Run Task... > PrepLang: explain the open .prep file*
   prints the execution plan and the data lineage, and writes no files.
7. **Debug the compiler:** set a breakpoint in any `src/` file, open a `.prep` file and press `F5`
   (*Debug PrepLang on the open .prep file*).
8. **Tests:** *Run Task... > PrepLang: run all tests* (115 unit tests in 6 executables).
9. **See the errors:** open any file in `examples/errors/` and run step 5. Every error shows the
   line, a caret and a hint.

For the image examples, first create the sample images once:
`py -3 eval/datasets.py examples`.

## Run it from a terminal

```bash
cmake -S . -B build -G "Visual Studio 17 2022"
```

```bash
cmake --build build
```

```bash
./build/Debug/preplang.exe examples/tabular/students.prep --run
```

The generated program is always `output/generated.py`, and `SAVE` paths are relative to `output/`.
`LOAD` paths are relative to the folder of the `.prep` file.

| Flag | Shows (module) |
|---|---|
| `--source` | the program with line numbers (`common/SourceManager`) |
| `--tokens` | token table (`lexer/`) |
| `--ast` | syntax tree (`parser/`, `ast/ASTPrinter`); `--dot` also writes `output/ast.dot` |
| `--schema` | column types, missing values, ranges; image sizes, channels, broken files (`schema/`) |
| `--semantic` | the dataset state after every statement and the symbol table (`sema/`) |
| `--lineage` | where every output file comes from (`dataset/LineageTracker`) |
| `--ir` | IR, optimization report, optimized IR (`ir/`, `optimizer/`) |
| `--python` | the generated program (`backend/`) |
| `--explain` | execution plan: inputs, contract verdicts, optimizations, backends; writes nothing |
| `--dry-run` | every file running would read and write; writes nothing |
| `--run` | runs the generated program afterwards (`py -3` on Windows, `python3` elsewhere) |
| `--O0` | turns the optimizer off |
| `--approx` | also applies approximate rewrites (RESIZE fusion) |

With no flags every stage except `--lineage` is printed. Compilation stops after the first phase
that reports an error, so one mistake is not reported again by later phases.

## The language

A program is a list of statements. Keywords are case-insensitive; `#` starts a comment.

| Data | Load | Save |
|---|---|---|
| TABLE (one CSV) | `LOAD "students.csv" AS s;` | `SAVE s AS "clean.csv";` |
| TABLESET (a folder of CSVs) | `LOAD_TABLES "company/" AS company;` | `SAVE_TABLES company TO "out/";` |
| IMAGE (one file) | `LOAD_IMAGE "cat.png" AS cat;` | `SAVE_IMAGE cat AS "cat.jpg";` |
| IMAGESET (a folder) | `LOAD_IMAGES "dataset/" AS imgs RECURSIVE;` | `SAVE_IMAGES imgs TO "out/";` |
| DATASET (images + labels) | `LINK imgs WITH labels ON filename AS training;` | |

**Table operations:** `DROP_NULL`, `FILL_NULL ... WITH value`, `NORMALIZE ... USING ZSCORE|MINMAX`,
`FILTER t WHERE condition`, `SELECT t [columns]`, `DROP t [columns]`.

**Image operations:** `RESIZE i TO w h [KEEP_ASPECT]`, `GRAYSCALE i`, `NORMALIZE i RANGE low high`,
`CONVERT i TO JPG|PNG|NPY`, `COMPRESS i QUALITY q`, `REMOVE_CORRUPT i`.

**Selectors** name many columns at once, in every table of a TABLESET:

```text
company.*.salary                                     # salary in every table
company.*.* WHERE TYPE NUMERIC EXCEPT [id, year]     # every numeric column except the keys
company.*.* WHERE NULLABLE                           # every column with missing values
FILL_NULL company.*.age WITH 0 IF_EXISTS;            # skip the tables without the column
```

**Contracts** (`EXPECT`) state what must hold about the data at that point of the program:

| Tables | Across tables | Images | Linked datasets |
|---|---|---|---|
| `NUMBER`, `STRING`, `BOOLEAN`, `NOT_NULL`, `BETWEEN a AND b`, `UNIQUE` | `REFERENCES ds.table.column`, `SCHEMA_SAME_AS t [EXCEPT [...]]` | `CHANNELS n`, `MIN_WIDTH n`, `MIN_HEIGHT n` | `MATCH_RATE p%` |

### Grammar

```ebnf
program        = { statement } EOF ;
statement      = load | drop_null | fill_null | normalize | filter | select | drop | save | expect
               | resize | grayscale | compress | convert | remove_corrupt | link ;

load           = ( "LOAD" | "LOAD_TABLES" | "LOAD_IMAGE" | "LOAD_IMAGES" ) STRING "AS" IDENT [ "RECURSIVE" ] ";" ;
save           = "SAVE" table_ref "AS" STRING ";"  | "SAVE_TABLES" IDENT "TO" STRING ";"
               | "SAVE_IMAGE" IDENT "AS" STRING ";" | "SAVE_IMAGES" IDENT "TO" STRING ";" ;
drop_null      = "DROP_NULL" table_selector ";" ;
fill_null      = "FILL_NULL" column_selector "WITH" literal [ "IF_EXISTS" ] ";" ;
normalize      = "NORMALIZE" column_selector ( "RANGE" number number
                                             | [ "USING" ( "ZSCORE" | "MINMAX" ) ] ) [ "IF_EXISTS" ] ";" ;
filter         = "FILTER" table_ref "WHERE" or_expr ";" ;
select         = "SELECT" table_ref column_list ";" ;
drop           = "DROP" table_selector column_list [ "IF_EXISTS" ] ";" ;
expect         = "EXPECT" column_selector constraint [ "IF_EXISTS" ] ";" ;
constraint     = "NUMBER" | "STRING" | "BOOLEAN" | "NOT_NULL" | "BETWEEN" number "AND" number | "UNIQUE"
               | "REFERENCES" path | "SCHEMA_SAME_AS" table_ref [ "EXCEPT" column_list ]
               | "CHANNELS" integer | "MIN_WIDTH" integer | "MIN_HEIGHT" integer | "MATCH_RATE" number "%" ;
resize         = "RESIZE" IDENT "TO" integer integer [ "KEEP_ASPECT" ] ";" ;
grayscale      = "GRAYSCALE" IDENT ";" ;
compress       = "COMPRESS" IDENT "QUALITY" integer ";" ;
convert        = "CONVERT" IDENT "TO" ( "JPG" | "PNG" | "NPY" ) ";" ;
remove_corrupt = "REMOVE_CORRUPT" IDENT ";" ;
link           = "LINK" IDENT "WITH" IDENT "ON" IDENT "AS" IDENT ";" ;

or_expr        = and_expr { "OR" and_expr } ;
and_expr       = not_expr { "AND" not_expr } ;
not_expr       = "NOT" not_expr | primary ;
primary        = "(" or_expr ")" | IDENT comparison_op literal ;

table_ref       = IDENT [ "." IDENT ] ;
table_selector  = IDENT [ "." ( IDENT | "*" ) ] ;
column_selector = IDENT { "." ( IDENT | "*" ) }
                  [ "WHERE" ( "TYPE" ( "NUMERIC" | "NUMBER" | "STRING" | "BOOLEAN" ) | "NULLABLE" ) ]
                  [ "EXCEPT" column_list ] ;
path            = IDENT { "." IDENT } ;
column_list     = "[" IDENT { "," IDENT } "]" ;
literal         = number | STRING | "TRUE" | "FALSE" ;
number          = [ "-" ] NUMBER ;
```

Every statement starts with a different keyword, so one token of lookahead chooses the rule: the
grammar is LL(1) and the parser is hand-written recursive descent with panic-mode recovery.
`IDENT` is `[A-Za-z_][A-Za-z0-9_]*` or `` `any text` `` (for column names such as `` `number` ``).

## What the compiler knows and proves

**1. One abstract state for every kind of data.** After each statement the compiler knows, for
every table column, its type, whether it can be missing, and an interval of its values; for every
image dataset, the size and channel count of each image, the pixel type and value range, the file
format, and the files that are broken; for linked datasets, which images and labels match. Each
statement has a transfer function over this state: `FILTER marks >= 50` narrows `marks` to
[50, 95], `GRAYSCALE` makes every image 1 channel, `NORMALIZE ... RANGE 0 1` makes pixels FLOAT32
in [0, 1], and `DROP` makes a later use an error, like use-after-free.

**2. Contracts get a verdict at compile time.** Every `EXPECT` is **PROVEN** (no run-time check is
generated), **VIOLATED** (compilation fails, naming the exact rows or files), or **DEFERRED** (a
check is generated, and the compiler then assumes the contract holds for the rest of the program).

**3. Proofs are tied to the exact input files.** The generated program recomputes a CRC-32
fingerprint of every CSV file and of every image folder listing, and refuses to run if any input
changed after compiling. Without this guard, removing a proven check would be unsafe.

**4. Batch errors.** One mistake in a wildcard is reported once, with a note for each table or
image it affects.

**5. SQL-style missing values.** The generated code uses pandas nullable types (`Int64`, `Float64`,
`boolean`, `string`): a comparison with a missing value is unknown and `FILTER` drops the row. The
analyzer reasons with the same rule.

## Optimizations

| Pass | What it removes or changes | Exact? |
|---|---|---|
| no-effect elimination | operations the state proves do nothing (`FILL_NULL` on a column without missing values, `GRAYSCALE` on 1-channel images, `CONVERT` to the current format) | yes |
| contract check elimination | run-time checks of PROVEN contracts | yes (fingerprint guard) |
| dead code elimination | unused temporaries; image operations after the last `SAVE_IMAGES` | yes |
| dead store elimination + column pruning | `FILL_NULL`/`NORMALIZE` whose result is never used; `LOAD` reads only live columns | yes |
| image streaming | all image operations up to a save become one loop that decodes, transforms and writes one image at a time | yes |
| RESIZE fusion | two resizes become one | **no**: only with `--approx`; otherwise it is reported as a suggestion |

Column pruning is live-variable analysis on columns. `DROP_NULL` counts as reading every column,
because pruning a column before it would change which rows are removed.

## Examples

| File | Shows |
|---|---|
| `examples/tabular/students.prep` | whole table pipeline; proven, deferred contracts |
| `examples/tabular/optimize.prep` | every table optimization, and why `DROP_NULL` blocks pruning |
| `examples/tabular/tableset.prep` | a folder of tables, wildcards, type selectors, cross-table contracts |
| `examples/tabular/ml_split.prep` | `SCHEMA_SAME_AS` between a train and a test split |
| `examples/images/imageset.prep` | image dataset: broken file, proven size and channel contracts |
| `examples/images/linked.prep` | images linked to labels, `MATCH_RATE`, float pixels saved as NPY |
| `examples/images/optimize_images.prep` | image optimizations; add `--approx` for RESIZE fusion |
| `examples/errors/*.prep` | lexical, syntax, semantic, table-set and image errors |

## Evaluation

The `eval/` folder holds a small, reproducible evaluation (run with the Release build).

```bash
cmake --build build --config Release
```

```bash
py -3 eval/run_eval.py build/Release/preplang.exe
```

```bash
py -3 eval/datasets.py benchmark
```

```bash
py -3 eval/bench.py
```

- `eval/mutants.py` injects 24 realistic mistakes into the example programs (wrong names, wrong
  types, use after removal, broken contracts, broken files, silently wrong values).
- `eval/baseline.py` runs the same programs the way a hand-written script would: statement by
  statement with pandas and OpenCV, with `EXPECT` as `assert`.
- Results are written to `eval/results/` (`rq1.md`, `rq3.md` and JSON files).

**RQ1 (finding mistakes):** PrepLang reports all 24 injected mistakes before any data is processed
(23 errors, 1 warning), with no false alarms on the 5 original programs. The script-style baseline
crashes part-way through on 20 of them and silently writes wrong output for the other 4.

**RQ2 (contracts):** 15 of the 16 contracts in the example programs are proven at compile time.

**RQ3 (optimizations):** on a 200,000-row, 40-column CSV, column pruning and check elimination cut
the processing time by 32% (1.30 s vs. 1.54 s end to end, including 0.79 s of Python start-up).
Streaming 1,000 images needs 10 MiB of memory instead of 922 MiB. RESIZE fusion (`--approx`) saves
only 6% and changes pixels slightly (PSNR 57 dB), which is why it is opt-in.

**RQ4 (compile-time cost):** reading a 1,000,000-row CSV at compile time takes 1.75 s (pandas needs
0.77 s to read it); reading the headers of 3,000 images takes 0.17 s, 25x less than decoding them.

Full tables: `eval/results/rq1.md` and `eval/results/rq3.md`. The paper draft is `paper/preplang.tex`
(IEEE conference format; compile it with pdflatex or upload it to Overleaf).

## Project layout

```text
src/common      source text, diagnostics (line, caret, hint, notes), did-you-mean
src/lexer       tokens and the DFA-style lexer
src/parser      recursive-descent parser with error recovery
src/ast         AST, tree printer, Graphviz export
src/schema      CSV schema inference, PNG/JPEG header reading, CRC-32 fingerprints
src/sema        abstract dataset state, selectors, contracts, the semantic analyzer
src/dataset     lineage and image-label linking
src/ir          three-address IR
src/optimizer   pass manager and the optimization passes
src/backend     pandas code, OpenCV code, and the planner that joins them
src/explain     --explain, --dry-run, --lineage and the run manifest
tests/          unit tests (115)
examples/       example programs and data
eval/           mutants, baseline, benchmarks and results
paper/          conference paper draft (LaTeX)
```
