# PrepLang

**Author:** Nikhil

PrepLang is a small language for data preprocessing, and this is its compiler, written in C++17.
It reads the CSV files a program uses **at compile time**, checks every operation against the real
column types, missing values and value ranges, proves data contracts where it can, optimizes the
pipeline, and generates a pandas script.

```text
.prep -> Lexer -> Parser -> AST -> Semantic analysis (+ CSV schema) -> IR -> Optimizer -> Python/pandas
```

## Build (Windows, Visual Studio 2022 Build Tools)

```bash
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build
```

## Run

```bash
./build/Debug/preplang.exe examples/students.prep --run
```

The script is always written to `output/generated.py`. `--run` also executes it (`py -3` on Windows,
`python3` elsewhere; pandas must be installed). With no flags, every stage is printed.

| Flag         | Module (files)                                   | What it shows                                        |
|--------------|--------------------------------------------------|------------------------------------------------------|
| `--source`   | `common/SourceManager`                           | The program with line numbers                        |
| `--tokens`   | `lexer/Token.h`, `lexer/Lexer`                   | Token table                                          |
| `--ast`      | `parser/Parser`, `ast/AST.h`, `ast/ASTPrinter`   | Syntax tree                                          |
| `--schema`   | `schema/Schema.h`, `schema/SchemaProvider`       | Column types, missing values, ranges, file hash      |
| `--semantic` | `sema/SemanticAnalyzer`, `sema/SymbolTable`      | What is known after each statement, final symbols    |
| `--ir`       | `ir/IR.h`, `ir/IRBuilder`, `optimizer/Optimizer` | IR, optimization report, optimized IR                |
| `--python`   | `backend/PandasCodegen`                          | Generated pandas code                                |
| `--run`      | `main.cpp`                                       | Runs the generated script                            |
| (always)     | `common/Diagnostics`                             | Errors and warnings with line, caret and hint        |

Phases stop at the first phase that reports errors, so one mistake is not reported again later.

## Test

```bash
ctest --test-dir build -C Debug --output-on-failure
```

68 tests: `test_lexer` (15), `test_parser` (17), `test_semantic` (25), `test_optimizer` (11).

## Examples

| File                          | Shows                                                                         |
|-------------------------------|-------------------------------------------------------------------------------|
| `examples/students.prep`      | Full pipeline; proven contracts, one contract checked at run time             |
| `examples/optimize.prep`      | Every optimization, and why DROP_NULL prevents column pruning                 |
| `examples/semantic_error.prep`| 8 type, name and contract errors found in one run                             |
| `examples/invalid.prep`       | 4 syntax errors with recovery and hints                                       |
| `examples/lexical_error.prep` | 3 lexical errors                                                              |

## What makes it different

**1. Types come from the data.** `LOAD` reads the CSV while compiling. `NORMALIZE students.name` is a
type error because `name` is STRING. If a column that looks numeric is STRING, the error says which
line of the CSV made it so.

**2. Facts flow through the program.** After each statement the compiler updates what it knows:
`DROP_NULL` means no column has missing values, `FILTER marks >= 50` means marks is in [50, 95],
`NORMALIZE ... USING MINMAX` means the column is in [0, 1]. Using a column after `DROP` is an error,
like use-after-free. This is abstract interpretation with an interval domain.

**3. Contracts are proven, not just checked.** Every `EXPECT` is one of:
- **PROVEN**: the known facts imply it, so no run-time check is generated;
- **VIOLATED**: the data breaks it, so compilation fails;
- **checked at run time**: it cannot be decided, so the script checks it.

The script stores a hash of each CSV and refuses to run if the file changed after compiling,
because the proofs were made for that exact file.

**4. Optimizations never change the output.**
- *no-effect elimination*: removes operations the analysis proved useless
- *contract check elimination*: removes checks for proven contracts
- *dead code elimination*: removes unused temporaries
- *dead store elimination*: removes `FILL_NULL`/`NORMALIZE` whose results are never used
- *column pruning*: `LOAD` reads only live columns

Column pruning is live-variable analysis on columns. `DROP_NULL` counts as reading every column:
pruning a column before it would change which rows are removed.

**5. Missing values follow SQL rules.** The generated code uses pandas nullable types
(`Int64`, `Float64`, `boolean`, `string`), so a comparison with a missing value is unknown and
`FILTER` drops that row. The analyzer reasons with the same rule.

## Grammar

```ebnf
program     = { statement } EOF ;
statement   = load | drop_null | fill_null | normalize | filter
            | select | drop | save | expect ;

load        = "LOAD" STRING_LITERAL "AS" IDENTIFIER ";" ;
drop_null   = "DROP_NULL" IDENTIFIER ";" ;
fill_null   = "FILL_NULL" column_ref "WITH" literal ";" ;
normalize   = "NORMALIZE" column_ref [ "USING" ( "ZSCORE" | "MINMAX" ) ] ";" ;
filter      = "FILTER" IDENTIFIER "WHERE" or_expr ";" ;
select      = "SELECT" IDENTIFIER column_list ";" ;
drop        = "DROP" IDENTIFIER column_list ";" ;
save        = "SAVE" IDENTIFIER "AS" STRING_LITERAL ";" ;
expect      = "EXPECT" column_ref constraint ";" ;
constraint  = "NUMBER" | "STRING" | "BOOLEAN" | "NOT_NULL"
            | "BETWEEN" number "AND" number ;

or_expr     = and_expr { "OR" and_expr } ;
and_expr    = not_expr { "AND" not_expr } ;
not_expr    = "NOT" not_expr | primary ;
primary     = "(" or_expr ")" | IDENTIFIER comparison_op literal ;

column_ref  = IDENTIFIER "." IDENTIFIER ;
column_list = "[" IDENTIFIER { "," IDENTIFIER } "]" ;
literal     = number | STRING_LITERAL | "TRUE" | "FALSE" ;
number      = [ "-" ] NUMBER_LITERAL ;
```

Tokens: `IDENTIFIER` is `[A-Za-z_][A-Za-z0-9_]*` or `` `any text` ``; `NUMBER_LITERAL` is
`[0-9]+(.[0-9]+)?`; `STRING_LITERAL` is `"text on one line"`; `comparison_op` is one of
`== != < <= > >=`; comments start with `#`. Keywords are case-insensitive. Every statement starts
with a different keyword, so the grammar is LL(1).

## Paths

- `LOAD` paths are relative to the folder of the `.prep` file.
- `SAVE` paths are relative to `output/`.
