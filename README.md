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
      -> Data certificate -> Execution planner -> pandas code + OpenCV code -> output/generated.py
         (facts the proofs need)                                                (+ run manifest)
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

**What is new** compared with existing work (F# type providers, data-frame abstract interpretation,
compile-time schema contracts, TFDV/Deequ/Great Expectations) is explained in
[NOVELTY.md](NOVELTY.md).

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
8. **Tests:** *Run Task... > PrepLang: run all tests* (207 unit tests in 9 executables).
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
| `--certificate` | the data certificate: the facts about the inputs the proofs need, and which proof needs each one (`certify/`) |
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
| TABLE (one CSV or Parquet file) | `LOAD "students.csv" AS s;` | `SAVE s AS "clean.csv";` |
| TABLESET (a folder of CSVs) | `LOAD_TABLES "company/" AS company;` | `SAVE_TABLES company TO "out/";` |
| IMAGE (one file) | `LOAD_IMAGE "cat.png" AS cat;` | `SAVE_IMAGE cat AS "cat.jpg";` |
| IMAGESET (a folder) | `LOAD_IMAGES "dataset/" AS imgs RECURSIVE;` | `SAVE_IMAGES imgs TO "out/";` |
| DATASET (images + labels) | `LINK imgs WITH labels ON filename AS training;` | |

**Table operations:** `DROP_NULL t` or `DROP_NULL t [columns]` (only rows missing one of these),
`FILL_NULL ... WITH value|MEAN|MEDIAN|MODE` (the statistic is computed from the data when the program
runs; MEAN and MEDIAN make an INTEGER column FLOAT), `NORMALIZE ... USING ZSCORE|MINMAX`,
`FILTER t WHERE condition`, `SELECT t [columns]`, `DROP t [columns]`.

**Column types** are BOOLEAN, INTEGER, FLOAT and STRING, inferred from the CSV cells, plus DATETIME
for the date and timestamp columns of Parquet files. A DATETIME column can be kept, dropped, saved
and checked with `NOT_NULL`; comparing or filling it is a type error, as the language has no date
values.

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
drop_null      = "DROP_NULL" table_selector [ column_list ] ";" ;
fill_null      = "FILL_NULL" column_selector "WITH" ( literal | "MEAN" | "MEDIAN" | "MODE" ) [ "IF_EXISTS" ] ";" ;
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

**3. Proofs are tied to the facts they use, not to one file.** The generated program recomputes a
CRC-32 fingerprint of every input. If it is the file the compiler analysed, every proof holds as it
is. Any other file (tomorrow's batch, a resampled split) is accepted only if it satisfies the
program's **data certificate**, described next; another image folder is admitted the same way.

**4. Data certificates: compile once, run on new data.** After optimizing, the compiler analyses
the program again many times with weaker facts about the inputs: missing values allowed, a lower or
upper bound moved to one of the program's own constants or removed. A weaker fact is kept when every
proof and every removed operation survives it. What remains is the certificate: the smallest set of
facts (found greedily) that the compiled program relies on, each with the proofs that need it.

```text
students  (compiled with examples/tabular/students.csv)
  name        STRING   no missing values   <- line 6: EXPECT students.name NOT_NULL
  age         INTEGER  (any values)
  marks       INTEGER  >= 0                <- line 5: EXPECT students.marks BETWEEN 0 AND 100
                       <= 100              <- line 5: EXPECT students.marks BETWEEN 0 AND 100
```

The data had marks in [35, 95]; the certificate needs only [0, 100]. Give the program another file
when you run it:

```bash
py -3 output/generated.py students=new_students.csv
```

What the certificate covers:

- **every column of the file**, not only the ones the program reads: a proven `NOT_NULL` on a column
  that column pruning then removes still needs that column without missing values, so the generated
  program loads it only to check it;
- **which columns may be missing**: a column no instruction names, which is only carried to the
  output, may be absent from a new input if the analysis without it keeps every proof (the Kaggle
  Titanic test file has no `Survived`);
- **strict bounds** for strict comparisons (`FILTER t WHERE rain < 100` needs `rain < 100`, not
  `<= 100`);
- **image folders**, as one box per file extension (channel counts, width and height ranges, bit
  depths, whether unreadable files are allowed). The analysis checks the box through its corner
  images, and the generated program reads the header of every file of a new folder (no pixel is
  decoded) and admits the folder if every header fits.

A file that breaks the certificate is refused before anything is written, with the row and the
proof: `marks is 104 in data row 4, above 100; the compiled program relies on this for line 5:
EXPECT students.marks BETWEEN 0 AND 100`. Contracts that were proven by reading the values
(`UNIQUE`, `REFERENCES`, `MATCH_RATE`) keep their check, which runs only when an input changed.
Because every transfer function is monotone, an input inside the certificate gets at least the
compiled proofs; `tests/test_certificate.cpp` checks this on 400 random inputs, and
`eval/certificates.py` on 244 new batches against a fresh compilation of each.

**5. Batch errors.** One mistake in a wildcard is reported once, with a note for each table or
image it affects.

**6. SQL-style missing values.** The generated code uses pandas nullable types (`Int64`, `Float64`,
`boolean`, `string`): a comparison with a missing value is unknown and `FILTER` drops the row. The
analyzer reasons with the same rule.

**7. Fast analysis of large CSV files.** The compile-time CSV reader reads the file once, cuts it
into one piece per core at line breaks, and analyses the pieces in parallel without copying cells
(`std::string_view`); numbers are parsed in one pass with an exact fast path. The pieces are merged
in file order, so the facts and error lines are the same as for one sequential pass (checked by
`tests/test_scanner.cpp`). One million rows take 0.13 s to analyse (2.04 s before), 4.7 times less
than pandas needs just to read them with its default parser, and about as long as pandas with the
multi-threaded pyarrow reader.

**8. Certificates for existing pandas scripts.** `frontend/pandas2prep.py` lifts the cleaning
part of a pandas script into PrepLang: it follows each DataFrame read with `pd.read_csv(path)` and
translates `fillna` (constants, dictionaries, `mean()`, `median()`, `mode()[0]`), `dropna`,
`drop`, column selection, boolean-mask filters, min-max and z-score normalization, `assert`s and
`to_csv`, until the first statement it cannot express exactly:

```bash
py -3 frontend/pandas2prep.py my_script.py > lifted.prep
```

## Optimizations

| Pass | What it removes or changes | Exact? |
|---|---|---|
| no-effect elimination | operations the state proves do nothing (`FILL_NULL` on a column without missing values, `GRAYSCALE` on 1-channel images, `CONVERT` to the current format) | yes |
| contract check elimination | run-time checks of PROVEN contracts; a contract proven from the values keeps its check, which runs only if an input changed | yes (fingerprint guard and certificate) |
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
- `eval/scale.py` compares time-to-first-error on 16M-row Parquet tables and 3,000 images
  (`py -3 eval/datasets.py scale` first).
- `eval/run_realbugs.py` runs the study of real GitHub issues (`eval/realbugs_catalog.py`).
- `eval/certificates.py` gives compiled programs new batches of data and compares each run with a
  fresh compilation for that batch (RQ7).
- `eval/realbatches.py` does the same with real monthly data: UCI traffic and bike-sharing data and
  NYC green-taxi Parquet files (RQ8; `py -3 eval/realbatches.py download` first).
- `eval/pandas_corpus.py` lifts real Titanic scripts from GitHub, compiles them, and compares the
  compiled program with the script's own pandas statements on new batches (RQ9;
  `py -3 eval/pandas_corpus.py collect` first, which needs `gh` signed in).
- `eval/paper_numbers.py` writes `paper/numbers.tex`, so every number in the paper comes from
  `eval/results/`.

**RQ1 (injected mistakes):** all 24 reported before any data is processed, no false alarms; the
script-style baseline crashes part-way on 20 and silently writes wrong output on 4.

**RQ2 (contracts):** 15 of the 16 contracts in the examples are proven at compile time.

**RQ3 (optimizations):** column pruning and check elimination cut table processing time by 40%;
streaming needs 10 MiB for 1,000 images (like a careful hand-written script) instead of 922 MiB;
RESIZE fusion gains little and changes pixels, so it is opt-in (`--approx`).

**RQ4 (analysis cost):** analysing every cell of a 1M-row CSV takes 0.13 s, 4.7x faster than
`pandas.read_csv` reads it and about as fast as its pyarrow engine; a Parquet footer costs 0.02 s for
1 or 16 million rows; image headers are about 17x faster than decoding.

**RQ5 (large inputs):** mistakes in a 16M-row pipeline are reported in 0.02 s (the script fails
after 1.8-2.8 s); a float-pixel mistake on 3,000 images is reported in 0.26 s, while the script
runs 8.5 s and writes 3,000 black images.

**RQ6 (real bugs):** of 44 data-preparation failures from GitHub issues, PrepLang reports 18 (41%)
before any data is processed; all 21 reproductions behave as classified. Details:
`eval/results/realbugs.md`.

**RQ8 (real monthly batches)** and **RQ9 (real pandas scripts):** see
`eval/results/realbatches.md` and `eval/results/pandas_corpus.md`.

**RQ7 (data certificates):** seven compiled programs get 244 new batches, and each batch is also
compiled fresh as the ground truth. The certificate admits 209 (86%), the compiled file's own facts
(a TFDV-style schema) 96 (39%), the fingerprint none. All 209 admitted runs end exactly like the
fresh compilation, and every refused batch also fails a fresh compilation. Details:
`eval/results/certificates.md`.

The paper is `paper/preplang.tex` with `paper/numbers.tex` (IEEE format; upload both to Overleaf).
What is new compared with existing work is in [NOVELTY.md](NOVELTY.md).

## Project layout

```text
src/common      source text, diagnostics (line, caret, hint, notes), did-you-mean
src/lexer       tokens and the DFA-style lexer
src/parser      recursive-descent parser with error recovery
src/ast         AST, tree printer, Graphviz export
src/schema      parallel CSV scanner, Parquet footers, PNG/JPEG headers, CRC-32 fingerprints
src/sema        abstract dataset state, selectors, contracts, the semantic analyzer
src/dataset     lineage and image-label linking
src/ir          three-address IR
src/optimizer   pass manager and the optimization passes
src/certify     data certificates: counterfactual re-analysis and admission (tables and image folders)
frontend/       pandas2prep.py: lifts the cleaning part of pandas scripts into PrepLang
src/backend     pandas code, OpenCV code, and the planner that joins them
src/explain     --explain, --dry-run, --lineage and the run manifest
tests/          unit tests (207)
examples/       example programs and data
eval/           mutants, baseline, benchmarks and results
paper/          conference paper draft (LaTeX)
```
