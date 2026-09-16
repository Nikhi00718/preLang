# How to Demonstrate PrepLang to Your Teacher

Run the commands from the repository root. First build the three small
executables:

```bash
cmake -S . -B build
cmake --build build
```

On Windows, use `build\Debug\preplang_r1.exe` when the generator places
executables in a `Debug` folder. The other two names are `preplang_r2.exe` and
`preplang_r3.exe`.

## 1. Present the idea

Say:

> PrepLang is a small language for data-preprocessing instructions. I am
> implementing it as a compiler in C++. Review 1 handles tokens and grammar,
> Review 2 adds the AST, symbol table, semantic analysis and IR, and Review 3
> adds optimization and execution.

Show this short program:

```text
LOAD "students.csv" AS students;
EXPECT students.age NUMBER;
NORMALIZE students.age;
FILTER students WHERE marks >= 50;
SAVE students AS "cleaned.csv";
```

## 2. Demonstrate Review 1

```bash
build/Release/preplang_r1 Review-1/examples/demo_valid.prep
build/Release/preplang_r1 Review-1/examples/demo_lexical_error.prep --no-tokens
build/Release/preplang_r1 Review-1/examples/demo_syntax_error.prep --no-tokens
build/Release/preplang_r1 --demo
```

Explain the output in this order:

1. `Lexer` reads characters and groups them into tokens.
2. Each token stores its type, value, line and column.
3. `Parser` uses one-token lookahead because every statement starts with a
   different keyword.
4. `ParseTree` shows the structure of a valid statement.
5. `ErrorReporter` prints the problem and continues after an error by skipping
   to the next semicolon or statement keyword.

Open these files while explaining:

```text
src/Token.h
src/Lexer.cpp
src/Parser.cpp
src/ParseTree.cpp
src/Diagnostics.cpp
Review-1/main.cpp
```

## 3. Demonstrate Review 2

```bash
build/Release/preplang_r2 Review-2/examples/front_end.prep
build/Release/preplang_r2 Review-2/examples/semantic_error.prep --no-tokens
```

Say:

> The parser result is now stored as an AST instead of only printing grammar
> steps. `EXPECT students.name STRING` inserts a symbol. When the semantic
> analyzer sees `NORMALIZE students.name`, it checks the symbol-table type and
> reports a semantic error because normalization needs a NUMBER column.

Then show the three-address IR. It is one simple instruction per valid
statement, which makes the later optimizer easy to understand.

## 4. Demonstrate Review 3

```bash
build/Release/preplang_r3 Review-3/examples/full_pipeline.prep --run
```

Point out:

- the second `DROP_NULL` is removed by the optimizer;
- the generated C++-style plan mirrors the PrepLang statements; and
- the executor loads the CSV, fills the missing attendance value, filters the
  rows, selects columns and saves `Review-3/examples/cleaned_students.csv`.

## Simple viva answers

**Why did you write the lexer by hand?**

The grammar is small, so a hand-written lexer makes token recognition and
maximal munch visible and easy to explain.

**Why is the parser LL(1)?**

Each statement starts with a different keyword. One token of lookahead selects
the correct parsing function, so there is no backtracking.

**What is the symbol table doing?**

It stores the dataset names and the column types declared by `EXPECT`. The
semantic analyzer uses it to validate later statements.

**Why use an intermediate representation?**

IR separates language checking from execution. The optimizer can change the IR
without changing the lexer or parser.

**What is not implemented?**

This is a small teaching compiler. It supports simple CSV files and a limited
set of preprocessing statements; joins, groups, a full type inference system
and industrial error recovery are future work.
