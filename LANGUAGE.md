# PrepLang language and compiler reference

This is the complete reference for PrepLang 2.0: the language, what the compiler checks for each
statement, the command line, and the generated program. [README.md](README.md) is the tutorial and
explains the analysis; [ARCHITECTURE.md](ARCHITECTURE.md) explains the compiler's code.

Contents

1. [Programs and files](#1-programs-and-files)
2. [Lexical structure](#2-lexical-structure)
3. [Datasets and types](#3-datasets-and-types)
4. [Selectors](#4-selectors)
5. [Statements](#5-statements)
6. [Contracts (`EXPECT`)](#6-contracts-expect)
7. [Conditions and values](#7-conditions-and-values)
8. [Grammar](#8-grammar)
9. [Command line](#9-command-line)
10. [The generated program](#10-the-generated-program)
11. [Diagnostics](#11-diagnostics)

## 1. Programs and files

A program is a `.prep` file holding a list of statements, each ending with `;`. Statements run in
order; each one changes the state of the datasets it names.

```text
LOAD "students.csv" AS students;                # read a table
EXPECT students.marks BETWEEN 0 AND 100;        # a contract: proven, refuted or checked at run time
FILTER students WHERE marks >= 50;              # an operation
SAVE students AS "passed.csv";                  # write the result
```

- `LOAD` paths are relative to the folder of the `.prep` file; absolute paths work too.
- `SAVE` paths are relative to `output/` in the folder the compiler is run from.
- The compiler reads every input at compile time (see *Datasets and types*), so an input must exist
  when the program is compiled, unless the program is compiled from released facts (`--facts`).
- The output of a compilation is `output/generated.py` (one Python program) and
  `output/preplang_contract.json` (the certificates; section 10).

## 2. Lexical structure

| Token | Form | Notes |
|---|---|---|
| Keyword | a word in the keyword list below | case-insensitive: `LOAD`, `load` and `Load` are the same |
| Identifier | `[A-Za-z_][A-Za-z0-9_]*` | case-sensitive: `Age` and `age` are different columns |
| Quoted identifier | `` `any text` `` | for names with spaces or names that are keywords (`` `number` ``); ends on the same line |
| Number | `[0-9]+(.[0-9]+)?` | a sign is a separate `-` token; there are no exponents |
| String | `"any text"` | no escapes: a backslash is an ordinary character, so `"C:\data\a.csv"` works; ends on the same line |
| Comparison | `==` `!=` `<` `<=` `>` `>=` | `=` alone is only the assignment of `DERIVE` |
| Arithmetic | `+` `-` `*` `/` | only in `DERIVE` values (`-` also as a sign) |
| Punctuation | `.` `,` `;` `[` `]` `(` `)` `*` `%` | `*` is also the wildcard of selectors |
| Comment | `#` to the end of the line | |

Spaces, tabs and line breaks separate tokens. Any other character is a lexical error (reported, then
skipped, so all lexical errors of a file are reported at once).

**Keywords.** Statement keywords: `LOAD LOAD_TABLES LOAD_IMAGE LOAD_IMAGES DROP_NULL FILL_NULL
NORMALIZE FILTER SELECT DROP PARSE_DATE DERIVE JOIN AGGREGATE SAVE SAVE_TABLES SAVE_IMAGE SAVE_IMAGES
EXPECT RESIZE GRAYSCALE COMPRESS CONVERT REMOVE_CORRUPT LINK`. Other keywords: `AS WITH TO ON USING
ZSCORE MINMAX RANGE WHERE TYPE EXCEPT IF_EXISTS RECURSIVE MEAN MEDIAN MODE NUMBER NUMERIC STRING
BOOLEAN NULLABLE NOT_NULL BETWEEN UNIQUE REFERENCES SCHEMA_SAME_AS CHANNELS MIN_WIDTH MIN_HEIGHT
MATCH_RATE KEEP_ASPECT QUALITY JPG PNG NPY AND OR NOT TRUE FALSE`.

`BY`, `COUNT`, `SUM`, `MIN`, `MAX`, `YEAR`, `MONTH`, `DAY`, `HOUR` and `WEEKDAY` are recognized by
position, not reserved: columns may have these names.

## 3. Datasets and types

Every `LOAD` defines a dataset name. A name is defined once and must be defined before it is used.

| Kind | Loaded by | What the compiler reads at compile time |
|---|---|---|
| TABLE | `LOAD "f.csv"` or `LOAD "f.parquet"` | CSV: every cell, in parallel. Parquet: only the footer (schema, row count, per-column statistics) |
| TABLESET | `LOAD_TABLES "folder/"` | every CSV and Parquet file of the folder; each file is a table named after the file |
| IMAGE | `LOAD_IMAGE "f.png"` | the header of the file (PNG or JPG) |
| IMAGESET | `LOAD_IMAGES "folder/" [RECURSIVE]` | the header of every PNG and JPG file (in subfolders too with `RECURSIVE`) |
| DATASET | `LINK images WITH labels ON column AS name` | which images and label rows match |

**Column types.** `BOOLEAN`, `INTEGER`, `FLOAT`, `STRING` and `DATETIME`. A CSV column's type is the
narrowest that fits every present value: `BOOLEAN` (`true`/`false`, any case), then `INTEGER`, then
`FLOAT`, otherwise `STRING`. `DATETIME` comes from Parquet date and timestamp columns and from
`PARSE_DATE`. A Parquet column's type comes from its schema.

**Missing values.** An empty CSV cell is a missing value. The generated code uses pandas' nullable
types (`Int64`, `Float64`, `boolean`, `string`), and missing values follow SQL: a comparison with a
missing value is unknown, so `FILTER` drops the row. The compiler reasons with the same rule.

**What the compiler knows.** After each statement, for every column: its type, whether it can have
missing values, and an interval of its values (for numbers and dates); for images: width and height,
channels, bit depth and pixel type, value range, format and the files that cannot be decoded; for a
linked dataset: the matching rate of images and labels. `--semantic` prints this state after every
statement.

## 4. Selectors

Statements that work on columns take a selector:

| Selector | Means |
|---|---|
| `t.col` | column `col` of table `t` |
| `ts.table.col` | column `col` of one table of TABLESET `ts` |
| `ts.*.col` | column `col` in every table of `ts` |
| `t.*`, `ts.*.*` | every column |
| `... WHERE TYPE NUMERIC` | only columns of that type: `NUMERIC` (INTEGER or FLOAT), `NUMBER` (the same), `STRING`, `BOOLEAN` |
| `... WHERE NULLABLE` | only columns with missing values (only in `FILL_NULL`) |
| `... EXCEPT [a, b]` | without these columns |
| `... IF_EXISTS` (at the end of the statement) | skip the tables that do not have the column instead of reporting an error |

A table selector (`DROP_NULL`, `DROP`) is `t`, `ts.table` or `ts.*`. A table reference (`FILTER`,
`SELECT`, `SAVE`, `JOIN`, `AGGREGATE`, `SCHEMA_SAME_AS`) is `t` or `ts.table`: one table.

A wildcard that matches no column is a warning. One mistake in a wildcard is reported once, with a
note for each table it affects.

## 5. Statements

Each entry gives the syntax, what the statement does, and what the compiler reports before anything
runs. *No effect* warnings mark statements the optimizer removes (section 9, `--O0`).

### Loading and saving

| Statement | Effect | Compile-time errors |
|---|---|---|
| `LOAD "path" AS t;` | reads a CSV or Parquet file as TABLE `t` | file cannot be opened, is empty, is not UTF-8 (byte and line named), has a line with the wrong number of fields (line named); Parquet footer unreadable |
| `LOAD_TABLES "folder/" AS ts;` | reads every CSV and Parquet file of a folder as TABLESET `ts` | folder cannot be opened or has no table files |
| `LOAD_IMAGE "path" AS i;` | reads one PNG or JPG file | file cannot be opened or is not PNG/JPG |
| `LOAD_IMAGES "folder/" AS i [RECURSIVE];` | reads every PNG and JPG of a folder | folder cannot be opened or has no images |
| `SAVE t AS "out.csv";` | writes a table; `.parquet` writes Parquet, anything else CSV | `t` is not a TABLE (use `SAVE_TABLES` for a TABLESET) |
| `SAVE_TABLES ts TO "folder/";` | writes every table of a TABLESET | `ts` is not a TABLESET |
| `SAVE_IMAGE i AS "out.png";` | writes one image | pixels that the format cannot store (below) |
| `SAVE_IMAGES i TO "folder/";` | writes every image, keeping subfolders | the same |

A dataset that is loaded but never saved is a warning. Saving FLOAT32 pixels (after
`NORMALIZE ... RANGE`) as PNG or JPG, 4-channel images as JPG, or 16-bit images as JPG is an error
with the statement that caused it and a hint (`CONVERT ... TO NPY` or `TO PNG`).

### Table operations

| Statement | Effect | Compile-time checks |
|---|---|---|
| `DROP_NULL t;` | removes rows with a missing value in any column | warning, no effect, if no column has missing values |
| `DROP_NULL t [a, b];` | removes rows with a missing value in one of these columns | the same, for these columns |
| `FILL_NULL sel WITH v;` | replaces missing values with a literal | the literal must have the column's kind (a number for INTEGER and FLOAT, text for STRING, `TRUE`/`FALSE` for BOOLEAN); DATETIME columns cannot be filled; no effect if there are no missing values |
| `FILL_NULL sel WITH MEAN \| MEDIAN \| MODE;` | the statistic is computed from the data when the program runs; `MEAN` and `MEDIAN` make an INTEGER column FLOAT | `MEAN`/`MEDIAN` need numeric columns |
| `NORMALIZE sel [USING ZSCORE \| MINMAX];` | z-score (the default) or min-max scaling; the column becomes FLOAT | numeric columns only; a warning that missing values stay missing |
| `FILTER t WHERE cond;` | keeps the rows where the condition is true (section 7) | unknown columns, comparing a column with a value of another kind; the condition narrows the known intervals; a filter the state proves always true is removed by the optimizer |
| `SELECT t [a, b];` | keeps only these columns, in this order | unknown or repeated columns |
| `DROP sel [a, b] [IF_EXISTS];` | removes columns | unknown or repeated columns; no effect if no table has them; a later use of a dropped column is an error |
| `PARSE_DATE t.col;` | a STRING column of ISO dates (`YYYY-MM-DD`, optionally ` HH:MM` or ` HH:MM:SS`, one layout per column) becomes DATETIME | the first value that is not a date is named; the column must be as the file has it (not changed after `LOAD`); not on Parquet columns (only the footer is read) or in a private compilation |
| `DERIVE t.col = value;` | adds or replaces a column computed row by row (section 7) | numeric columns only (and DATETIME inside a date part); a division by a value that can be 0 is an error; the new column's interval is computed |
| `JOIN t WITH u ON key;` | left lookup join: every row of `t` gets the columns of the one row of `u` with the same key, or missing values | the key must exist in both and have the same kind; a column (other than the key) in both tables is an error; a key repeated in `u` is an error naming its rows, and is checked again on new batches |
| `AGGREGATE t BY [a, b] item, ...;` | one row per group; items are `COUNT AS n` and `SUM \| MEAN \| MIN \| MAX col AS name`; rows with a missing `BY` value belong to no group | `SUM`/`MEAN`/`MIN`/`MAX` need numeric columns; names must be distinct; `COUNT` is at least 1, `SUM` at least 0 when every value is, `MEAN`/`MIN`/`MAX` stay in the column's interval |

### Image operations

| Statement | Effect | Compile-time checks |
|---|---|---|
| `RESIZE i TO w h [KEEP_ASPECT];` | resizes every image; `KEEP_ASPECT` keeps the proportions and pads with black | `w`, `h` from 1 to 65535; two resizes in a row are merged only with `--approx` |
| `GRAYSCALE i;` | makes every image 1 channel | no effect on 1-channel images |
| `NORMALIZE i RANGE low high;` | pixels become FLOAT32 in [low, high] | the whole dataset only (`i`, not `i.x`); `low` below `high`; not on folders that mix 8- and 16-bit files |
| `CONVERT i TO JPG \| PNG \| NPY;` | the format the images are saved in | pixels the format cannot store (see `SAVE_IMAGES`); no effect if already that format |
| `COMPRESS i QUALITY q;` | JPG quality `q` | `q` from 1 to 100; a warning if the images are not saved as JPG |
| `REMOVE_CORRUPT i;` | drops the files that cannot be decoded | no effect if every file decodes |
| `LINK i WITH labels ON col AS ds;` | pairs every image with the label row whose `col` is its file name | `i` is an image dataset; `labels` is a TABLE whose `col` is a STRING column (the file names) |

## 6. Contracts (`EXPECT`)

`EXPECT sel constraint [IF_EXISTS];` states what must hold at that point of the program. Each
contract gets a verdict at compile time:

- **PROVEN**: the state shows it holds. No run-time check is generated (it is kept behind a guard
  for new inputs, section 10).
- **VIOLATED**: the state shows it is broken. Compilation fails, naming the rows, files or values.
- **DEFERRED**: the state cannot decide it (for instance after a `FILTER` whose bounds are not
  enough). A run-time check is generated, and the rest of the program is analysed as if it holds.

| Constraint | On | Holds when |
|---|---|---|
| `NUMBER`, `STRING`, `BOOLEAN` | columns | the column has this type (`NUMBER`: INTEGER or FLOAT) |
| `NOT_NULL` | columns | no missing values |
| `BETWEEN a AND b` | numeric columns | every present value is in [a, b] (`a <= b`, otherwise an error) |
| `UNIQUE` | columns | no value appears twice |
| `REFERENCES t.col` or `ds.table.col` | a column | every present value appears in the other column (a foreign key) |
| `SCHEMA_SAME_AS t [EXCEPT [cols]]` | a table | the same column names and types as `t` |
| `CHANNELS n`, `MIN_WIDTH n`, `MIN_HEIGHT n` | images | every image has `n` channels, or at least that width or height |
| `MATCH_RATE p%` | a DATASET from `LINK` | at least `p`% of the images have a label row |

In a private compilation (`--facts`), no contract is reported violated: a contract the released
facts show broken gets a *likely violated* warning and is checked on the real data.

## 7. Conditions and values

**FILTER conditions** compare a column with a literal and combine comparisons with `AND`, `OR`,
`NOT` and parentheses (precedence from lowest: `OR`, `AND`, `NOT`):

```text
FILTER t WHERE (age >= 18 AND city != "Pune") OR NOT member == TRUE;
```

The literal must have the column's kind. A comparison with a missing value is unknown, and the row is
dropped. For numeric columns, the compiler narrows the interval: after `FILTER t WHERE marks >= 50`,
`marks` is known to be in [50, max].

**DERIVE values** are arithmetic over numeric columns and numbers, with `+`, `-`, `*`, `/`, a unary
`-` and parentheses (usual precedence), and the date parts `YEAR(c)`, `MONTH(c)`, `DAY(c)`, `HOUR(c)`
and `WEEKDAY(c)` (Monday = 0) of a DATETIME column:

```text
DERIVE trips.fare_per_km = fare / (distance + 0.1);
DERIVE trips.hour = HOUR(pickup);
```

The interval of the result is computed with interval arithmetic; a date part is narrower than its
full range when the dates fall in one year, month or day. A division by an interval that contains 0
is an error.

## 8. Grammar

```ebnf
program        = { statement } EOF ;
statement      = load | drop_null | fill_null | normalize | filter | select | drop | parse_date | derive
               | join | aggregate | save | expect | resize | grayscale | compress | convert | remove_corrupt
               | link ;

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
parse_date     = "PARSE_DATE" column_selector ";" ;
derive         = "DERIVE" path "=" sum ";" ;
join           = "JOIN" table_ref "WITH" table_ref "ON" IDENT ";" ;
aggregate      = "AGGREGATE" table_ref "BY" column_list aggregate_item { "," aggregate_item } ";" ;
aggregate_item = ( "COUNT" | ( "SUM" | "MEAN" | "MIN" | "MAX" ) IDENT ) "AS" IDENT ;
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
comparison_op  = "==" | "!=" | "<" | "<=" | ">" | ">=" ;

sum            = product { ( "+" | "-" ) product } ;
product        = unary { ( "*" | "/" ) unary } ;
unary          = "-" unary | operand ;
operand        = NUMBER | IDENT [ "(" IDENT ")" ] | "(" sum ")" ;   (* IDENT ( ... ): YEAR, MONTH, DAY, HOUR, WEEKDAY *)

table_ref       = IDENT [ "." IDENT ] ;
table_selector  = IDENT [ "." ( IDENT | "*" ) ] ;
column_selector = IDENT { "." ( IDENT | "*" ) }
                  [ "WHERE" ( "TYPE" ( "NUMERIC" | "NUMBER" | "STRING" | "BOOLEAN" ) | "NULLABLE" ) ]
                  [ "EXCEPT" column_list ] ;
path            = IDENT { "." IDENT } ;
column_list     = "[" IDENT { "," IDENT } "]" ;
literal         = number | STRING | "TRUE" | "FALSE" ;
number          = [ "-" ] NUMBER ;
integer         = NUMBER ;   (* without a fraction *)
```

Every statement starts with a different keyword, so one token of lookahead chooses the rule: the
grammar is LL(1). The parser (`src/parser/Parser.cpp`) is hand-written recursive descent, one method
per rule, with panic-mode recovery: a statement with a syntax error is reported and skipped up to the
next `;`, and parsing continues.

## 9. Command line

```text
preplang <file.prep> [options]
preplang --version
```

**What to print.** With none of these, every stage except `--lineage` is printed.

| Flag | Prints |
|---|---|
| `--source` | the program with line numbers |
| `--tokens` | the token table |
| `--ast` | the syntax tree; with `--dot`, also writes `output/ast.dot` (Graphviz) |
| `--schema` | what was read from every input: column types, missing values, ranges; image sizes, channels, broken files |
| `--semantic` | the state after every statement, and the symbol table |
| `--lineage` | where every output file comes from |
| `--ir` | the IR, the optimization report, the optimized IR |
| `--certificate` | the data certificates: the facts about the inputs each proof and removal needs |
| `--python` | the generated program |

**Modes.**

| Flag | Does |
|---|---|
| `--explain` | prints the execution plan (inputs, contract verdicts, optimizations, back ends); writes nothing |
| `--dry-run` | prints every file running the program would read and write; writes nothing |
| `--json` | prints only the diagnostics, as one JSON object (below); writes nothing |
| `--run` | runs the generated program afterwards (`py -3` on Windows, `python3` elsewhere) |
| `--O0` | turns the optimizer off |
| `--approx` | also applies rewrites that change the output slightly (RESIZE fusion); without it they are reported as suggestions |
| `--no-deopt` | removes proven-useless checks and operations outright, without guards: a new input must then satisfy the full certificate |
| `--queries` | for the data owner: writes `output/preplang_queries.tsv`, what the program asks about its data, for `tools/release_facts.py` |
| `--facts <file>` | compiles from released facts without opening any input file (private compilation; README section 9) |

**Exit codes.** `0`: compiled (warnings allowed). `1`: a compile error, an input or facts file that
cannot be read, or, with `--run`, a generated program that failed. `2`: bad arguments (the usage is
printed).

Compilation stops after the first phase that reports an error, so one mistake is not reported again
by later phases.

**Files written** (none with `--explain`, `--dry-run` or `--queries`):

| File | Holds |
|---|---|
| `output/generated.py` | the generated program |
| `output/preplang_contract.json` | the admission certificate, one item certificate per removal, and the guarantee for every saved table (input of `tools/contracts.py`) |
| `output/ast.dot` | with `--dot` |
| `output/preplang_manifest.json` | written by the generated program when it runs: inputs, outputs, row counts, checks |

## 10. The generated program

```text
python3 output/generated.py [name=path ...]
```

With no arguments it runs on the inputs the program was compiled with. `name=path` gives the input
`name` (the dataset name of its `LOAD`, or `ts.table` for a table of a TABLESET) another file or
folder. For each input the program:

1. checks the CRC-32 fingerprint: the compiled file runs with every compile-time proof as it is;
2. otherwise checks the **admission certificate** (column types, contracts without a run-time check,
   no compile errors) and refuses the input if it is broken, naming the fact and the statement that
   needs it;
3. checks each **item certificate**, and runs again only the removed checks and operations whose
   facts the input breaks (*deoptimization*), printing which and why.

A broken contract stops the program with one line, `ContractError: line N: ...`, and exit code 1. The
outcome on an admitted input is what a fresh compilation of that input would produce
(`proofs/Deopt.lean`; tested by `tests/test_certificate.cpp` and `eval/reuse/`).

The program needs Python 3 (3.12 is tested) with pandas 2.0 or newer (3.0 is tested; PyArrow for
Parquet) for tables, and OpenCV and NumPy for images. `PREPLANG_TRUST_INPUTS=1` skips admission and deoptimization, for
experiments only.

## 11. Diagnostics

Every error and warning names the phase, the message, the place in the program with a caret, and
usually a hint:

```text
error[type]: cannot save FLOAT32 pixels as PNG: NORMALIZE at line 10 scaled them to [0, 1]
   --> examples/images/linked.prep:13:1
   |
13 | SAVE_IMAGES imgs TO "processed/ml_ready/";
   | ^
   = hint: image files store 8-bit pixels, so these would come out almost black; add CONVERT imgs TO NPY; to keep the float values
```

| Phase | Reports |
|---|---|
| `lexical` | characters that are not tokens, strings or quoted names not closed on their line |
| `syntax` | a statement that does not follow the grammar (what was expected and what was found) |
| `schema` | inputs that cannot be read: missing files and folders, empty or non-UTF-8 CSV files, lines with the wrong number of fields, broken Parquet footers, files that are not PNG or JPG |
| `semantic` | undefined datasets, unknown tables and columns (with *did you mean*), the wrong kind of dataset for a statement, repeated names, values out of range (`RESIZE`, `QUALITY`, empty `RANGE`), statements with no effect (warnings) |
| `type` | comparing or filling a column with a value of another kind, numeric statements on text, dates, pixels a format cannot store |
| `contract` | VIOLATED contracts with the rows, values or files that break them; repeated join keys; division by zero in `DERIVE`; *likely violated* warnings in a private compilation |

The programs in `examples/errors/` show one of each kind.

**For tools.** `preplang file.prep --json` prints the same diagnostics as one JSON object and nothing
else, with the exit code above, so editors and CI can read them:

```json
{"file": "examples/errors/semantic_error.prep", "errors": 8, "warnings": 1, "diagnostics": [
  {"severity": "error", "phase": "semantic", "line": 8, "column": 11,
   "message": "column 'mark' does not exist in 'students'", "hint": "did you mean 'marks'?", "notes": []}
]}
```

Diagnostics are in source order; `line` and `column` count from 1; `hint` is `""` when there is none.
