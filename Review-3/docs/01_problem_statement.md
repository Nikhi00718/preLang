# 01 — Problem Statement, Motivation and Objectives

## 1.1 Problem area

Data preprocessing is the first and most repeated step of every data-analysis
and machine-learning workflow. In practice it is written directly in Python
with the Pandas library:

```python
import pandas as pd

df = pd.read_csv("students.csv")
df = df.dropna()
df["age"] = (df["age"] - df["age"].mean()) / df["age"].std()
df = df[["age", "marks", "attendance"]]
df.to_csv("cleaned.csv", index=False)
```

This code is *syntactically* valid Python, so the Python compiler accepts it
unconditionally. Every question that actually matters for correctness is left
to run time:

| Question | When is it answered today? |
|---|---|
| Does the column `age` exist in the file? | At run time, as a `KeyError` |
| Is `age` numeric, so that the mean is meaningful? | At run time, or never |
| Is `marks` within the legal range 0–100? | Never, unless hand-coded |
| Are the 30 unused columns of the file loaded into memory? | Always, silently |

## 1.2 Problem statement

> Data-preprocessing pipelines written in general-purpose languages such as
> Python/Pandas are not statically checked against the structure of the data
> they operate on. Column-existence errors, datatype errors and data-quality
> violations are discovered only during execution — or produce silently
> incorrect results — and no compile-time optimisation of the pipeline is
> performed. There is no small, dedicated language in which a preprocessing
> pipeline can be *declared*, *statically validated against the dataset
> schema*, *optimised* and *then* translated into executable code.

## 1.3 Motivation

1. **Compiler theory applies directly.** A dataset schema is a type
   environment. `NORMALIZE students.name` where `name : STRING` is the exact
   analogue of `int x = "hello";` — a static type error. Once that analogy is
   made, the whole classical front end (lexer → parser → AST → symbol table →
   semantic analyser) becomes applicable to data preprocessing.
2. **Errors are caught before any data is touched.** Failing at compile time
   costs milliseconds; failing after loading a large CSV costs minutes and may
   corrupt an output file that is already half written.
3. **Declarative code can be optimised.** Because a PrepLang program *declares*
   what is required rather than *how* to compute it, the compiler can apply
   real optimisations — column pruning, redundant-operation elimination, dead
   operation removal — that are impossible to apply automatically to arbitrary
   Python.
4. **Educational value.** Every compiler phase becomes visible and explainable
   on a program that a non-compiler person can still read.

## 1.4 Objectives

**Primary objective** — design and implement PrepLang, a domain-specific
language and compiler that statically validates data-preprocessing programs
against the schema of the datasets they use, and generates optimised
Python/Pandas code.

**Specific objectives**

| # | Objective | Phase |
|---|---|---|
| O1 | Design a minimal, readable DSL for common preprocessing operations | 1 |
| O2 | Define the complete lexical specification (tokens, keywords, literals) | 1 |
| O3 | Define an unambiguous LL(1) grammar in BNF | 1 |
| O4 | Implement a hand-written lexical analyser with error recovery | 1 |
| O5 | Implement a recursive-descent parser with panic-mode recovery | 1 |
| O6 | Construct an Abstract Syntax Tree | 2 |
| O7 | Build a symbol table for datasets and columns | 2 |
| O8 | Infer the dataset schema from CSV files | 2 |
| O9 | Perform schema-aware semantic analysis and static type checking | 2 |
| O10 | Support declarative data contracts (type, range, not-null) | 2 |
| O11 | Generate a language-independent Intermediate Representation | 2 |
| O12 | Apply IR-level optimisations (column pruning, redundancy elimination) | 3 |
| O13 | Generate executable Python/Pandas target code | 3 |
| O14 | Execute the generated code and produce the processed dataset | 3 |
| O15 | Visualise every compiler stage in an IDE for demonstration | 3 |
| O16 | Validate the system with a comprehensive test suite and benchmarks | 3 |

## 1.5 Scope

**Included:** CSV datasets; numeric, string and boolean columns; missing-value
handling; row filtering; column selection and dropping; normalisation; data
contracts; schema inference; static type checking; IR generation; IR
optimisation; Python/Pandas code generation; execution; compiler-stage
visualisation.

**Deliberately excluded (version 1):** distributed processing, GPU
acceleration, full SQL, joins and group-by, machine-learning training,
multi-file pipelines, and production deployment. The project is a compiler
project, not a Pandas replacement.

## 1.6 Expected outcome

A working compiler that, given a PrepLang program and a CSV file, either

* rejects the program with a precise, located, actionable diagnostic, **or**
* accepts it and emits optimised Python/Pandas code plus the processed CSV,

while exposing the token stream, AST, symbol table, schema, IR, optimised IR
and generated code for inspection at every step.
