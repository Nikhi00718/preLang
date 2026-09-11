# PrepLang
## A Schema-Aware DSL Compiler for Safe and Optimized Data Preprocessing

---

## 1. Project Title

**PrepLang: A Schema-Aware Domain-Specific Language Compiler for Safe and Optimized Data Preprocessing**

---

## 2. One-Line Idea

PrepLang is a small domain-specific programming language (DSL) that allows users to describe data-preprocessing pipelines using simple commands. The compiler performs lexical analysis, parsing, AST construction, symbol-table management, schema-aware semantic analysis, intermediate representation generation, optimization, and finally generates executable Python/Pandas code.

---

## 3. Core Problem

Data preprocessing is usually written directly in Python/Pandas.

For example:

```python
import pandas as pd

df = pd.read_csv("students.csv")
df = df.dropna()

df["age"] = (
    df["age"] - df["age"].mean()
) / df["age"].std()

df = df[["age", "marks", "attendance"]]
df.to_csv("cleaned.csv", index=False)
```

This creates several problems:

- Beginners must know Python and Pandas syntax.
- Errors such as using a string column in a numeric operation are often discovered only during execution.
- Undefined columns may cause runtime failures.
- Invalid preprocessing pipelines can silently produce incorrect results.
- Data validation and preprocessing are usually written separately.
- Repeated preprocessing code is verbose.
- There is normally no compiler-style explanation of how a preprocessing pipeline is interpreted or optimized.

PrepLang solves this by creating a small language specifically for preprocessing.

---

# 4. Basic Example

Instead of writing Pandas code:

```text
LOAD "students.csv" AS students;

DROP_NULL students;

NORMALIZE students.age;

SELECT students [
    age,
    marks,
    attendance
];

SAVE students AS "cleaned.csv";
```

The compiler converts the program through several compiler phases and produces equivalent Python/Pandas code.

Pipeline:

```text
PrepLang Source
      ↓
Lexical Analysis
      ↓
Parsing
      ↓
AST
      ↓
Symbol Table
      ↓
Schema Analysis
      ↓
Semantic Analysis
      ↓
Intermediate Representation
      ↓
Optimization
      ↓
Python Code Generation
      ↓
Pandas Execution
      ↓
Processed Dataset
```

---

# 5. Main Innovation

The main idea is NOT simply:

> "Convert a custom language into Pandas."

The key contribution is:

> **Static schema-aware validation of data-preprocessing programs before execution.**

Traditional compiler:

```text
int x = "hello";
```

produces a type error.

PrepLang applies the same compiler idea to datasets.

Example:

```text
NORMALIZE students.name;
```

If the dataset schema is:

```text
name   → STRING
age    → INTEGER
marks  → INTEGER
```

PrepLang produces:

```text
TYPE ERROR

NORMALIZE requires a numerical column.

Column:
    students.name

Detected type:
    STRING

Suggested numerical columns:
    age
    marks
```

This connects classical compiler semantic analysis directly with real-world data preprocessing.

---

# 6. Why This Project Is Special

PrepLang combines:

- Compiler Design
- Domain-Specific Language design
- Static type checking
- Dataset schema reasoning
- Data validation
- Data contracts
- Intermediate Representation
- Compiler optimization
- Code generation
- Data preprocessing
- Simple compiler visualization

The project remains understandable because the preprocessing operations are familiar and simple.

---

# 7. Project Objectives

The main objectives are:

1. Design a simple DSL for common preprocessing operations.
2. Implement lexical analysis for PrepLang.
3. Implement syntax analysis using a formal grammar.
4. Construct an Abstract Syntax Tree.
5. Build and maintain a symbol table.
6. Read and infer dataset schemas.
7. Perform schema-aware semantic analysis.
8. Detect preprocessing errors before execution.
9. Support basic data contracts.
10. Generate a language-independent Intermediate Representation.
11. Apply simple compiler optimizations.
12. Generate Python/Pandas target code.
13. Execute generated code safely.
14. Produce clear compiler errors.
15. Visualize major compiler stages for demonstration and learning.

---

# 8. Scope

## Included

- CSV datasets
- Numeric columns
- String columns
- Boolean columns
- Missing values
- Basic filtering
- Column selection
- Column dropping
- Missing-value handling
- Numeric normalization
- Data validation
- Schema checking
- Data contracts
- IR generation
- Basic IR optimization
- Python/Pandas code generation
- Compiler visualization

## Not Required in Version 1

- Distributed data processing
- GPU acceleration
- Full SQL support
- Full machine-learning training
- Complex joins
- Advanced query optimization
- Full Python language support
- Large-scale production deployment

The goal is a focused compiler project, not a replacement for Pandas.

---

# 9. Proposed PrepLang Commands

A small initial language is enough.

## 9.1 LOAD

```text
LOAD "students.csv" AS students;
```

Purpose:

Loads a CSV file and creates a dataset identifier.

---

## 9.2 DROP_NULL

```text
DROP_NULL students;
```

Purpose:

Removes rows containing missing values.

---

## 9.3 FILL_NULL

```text
FILL_NULL students.salary WITH 0;
```

Purpose:

Replaces missing values in a particular column.

---

## 9.4 NORMALIZE

```text
NORMALIZE students.age;
```

Purpose:

Applies numerical normalization to a column.

---

## 9.5 FILTER

```text
FILTER students WHERE age > 21;
```

Purpose:

Keeps rows satisfying a condition.

---

## 9.6 SELECT

```text
SELECT students [
    name,
    age,
    marks
];
```

Purpose:

Keeps only specified columns.

---

## 9.7 DROP

```text
DROP students [
    phone,
    address
];
```

Purpose:

Removes specified columns.

---

## 9.8 SAVE

```text
SAVE students AS "cleaned.csv";
```

Purpose:

Saves the processed dataset.

---

# 10. Data Contract Commands

Data contracts make PrepLang more distinctive.

## 10.1 Type Contract

```text
EXPECT students.age NUMBER;
```

Meaning:

The column must contain numerical values.

---

## 10.2 Range Contract

```text
EXPECT students.marks BETWEEN 0 AND 100;
```

Meaning:

Every value must lie between 0 and 100.

---

## 10.3 Not-Null Contract

```text
EXPECT students.name NOT_NULL;
```

Meaning:

The column must not contain missing values.

---

# 11. Complete Example Program

```text
LOAD "students.csv" AS students;

EXPECT students.age NUMBER;
EXPECT students.marks BETWEEN 0 AND 100;
EXPECT students.name NOT_NULL;

DROP_NULL students;

NORMALIZE students.age;

FILTER students WHERE marks >= 50;

SELECT students [
    name,
    age,
    marks
];

SAVE students AS "cleaned_students.csv";
```

---

# 12. Compiler Architecture

```text
                  PrepLang Source Code
                          │
                          ▼
                 ┌─────────────────┐
                 │      Lexer      │
                 └─────────────────┘
                          │
                        Tokens
                          │
                          ▼
                 ┌─────────────────┐
                 │     Parser      │
                 └─────────────────┘
                          │
                          ▼
                 Abstract Syntax Tree
                          │
             ┌────────────┴────────────┐
             │                         │
             ▼                         ▼
      ┌─────────────┐           ┌─────────────┐
      │Symbol Table │           │CSV Schema   │
      └─────────────┘           └─────────────┘
             │                         │
             └────────────┬────────────┘
                          ▼
                 ┌─────────────────┐
                 │ Semantic        │
                 │ Analyzer        │
                 └─────────────────┘
                          │
                          ▼
                 Intermediate Code
                          │
                          ▼
                 ┌─────────────────┐
                 │   Optimizer     │
                 └─────────────────┘
                          │
                          ▼
                 Optimized IR
                          │
                          ▼
                 ┌─────────────────┐
                 │ Code Generator  │
                 └─────────────────┘
                          │
                          ▼
                    Python/Pandas
                          │
                          ▼
                   Processed Dataset
```

---

# 13. Compiler Phase 1 — Lexical Analysis

The lexer converts source characters into tokens.

Input:

```text
NORMALIZE students.age;
```

Possible token stream:

```text
NORMALIZE       KEYWORD
students        IDENTIFIER
.               DOT
age             IDENTIFIER
;               SEMICOLON
```

Another example:

```text
FILTER students WHERE marks >= 50;
```

Tokens:

```text
FILTER          KEYWORD
students        IDENTIFIER
WHERE           KEYWORD
marks           IDENTIFIER
>=              RELATIONAL_OPERATOR
50              NUMBER
;               SEMICOLON
```

---

# 14. Token Types

Possible tokens include:

```text
KEYWORD
IDENTIFIER
STRING_LITERAL
NUMBER_LITERAL
BOOLEAN_LITERAL
DOT
COMMA
SEMICOLON
LEFT_BRACKET
RIGHT_BRACKET
ASSIGNMENT_OPERATOR
RELATIONAL_OPERATOR
ARITHMETIC_OPERATOR
```

Keywords may include:

```text
LOAD
AS
DROP_NULL
FILL_NULL
WITH
NORMALIZE
FILTER
WHERE
SELECT
DROP
SAVE
EXPECT
NUMBER
STRING
BOOLEAN
BETWEEN
AND
NOT_NULL
```

---

# 15. Compiler Phase 2 — Parsing

The parser verifies that tokens follow the PrepLang grammar.

Correct:

```text
NORMALIZE students.age;
```

Incorrect:

```text
NORMALIZE . age students;
```

Possible compiler response:

```text
Syntax Error — Line 3

Expected dataset identifier after NORMALIZE.

Found:
    '.'
```

---

# 16. Example Grammar

A simplified grammar could be:

```bnf
program         -> statement*

statement       -> load_stmt
                 | drop_null_stmt
                 | fill_null_stmt
                 | normalize_stmt
                 | filter_stmt
                 | select_stmt
                 | drop_stmt
                 | save_stmt
                 | expect_stmt

load_stmt       -> LOAD STRING AS IDENTIFIER SEMICOLON

drop_null_stmt  -> DROP_NULL IDENTIFIER SEMICOLON

fill_null_stmt  -> FILL_NULL IDENTIFIER DOT IDENTIFIER
                   WITH literal SEMICOLON

normalize_stmt  -> NORMALIZE IDENTIFIER DOT IDENTIFIER SEMICOLON

filter_stmt     -> FILTER IDENTIFIER WHERE condition SEMICOLON

select_stmt     -> SELECT IDENTIFIER LEFT_BRACKET
                   identifier_list RIGHT_BRACKET SEMICOLON

drop_stmt       -> DROP IDENTIFIER LEFT_BRACKET
                   identifier_list RIGHT_BRACKET SEMICOLON

save_stmt       -> SAVE IDENTIFIER AS STRING SEMICOLON

expect_stmt     -> EXPECT IDENTIFIER DOT IDENTIFIER
                   expectation SEMICOLON

condition       -> IDENTIFIER relational_operator literal

expectation     -> NUMBER
                 | STRING
                 | BOOLEAN
                 | NOT_NULL
                 | BETWEEN NUMBER AND NUMBER

identifier_list -> IDENTIFIER
                 | IDENTIFIER COMMA identifier_list

literal         -> NUMBER
                 | STRING
                 | BOOLEAN
```

---

# 17. Compiler Phase 3 — Abstract Syntax Tree

Example:

```text
FILTER students WHERE marks > 50;
```

AST:

```text
            FilterNode
            /        \
       students       >
                     / \
                  marks  50
```

Possible internal structure:

```text
FilterNode
│
├── dataset: students
│
└── condition:
    ├── operator: >
    ├── column: marks
    └── value: 50
```

---

# 18. AST Node Types

Possible AST nodes:

```text
ProgramNode
LoadNode
DropNullNode
FillNullNode
NormalizeNode
FilterNode
SelectNode
DropNode
SaveNode
ExpectTypeNode
ExpectRangeNode
ExpectNotNullNode
BinaryConditionNode
LiteralNode
IdentifierNode
```

---

# 19. Symbol Table

When:

```text
LOAD "students.csv" AS students;
```

PrepLang creates a symbol-table entry.

Example:

| Identifier | Category | Source |
|---|---|---|
| students | Dataset | students.csv |

After reading the dataset schema:

| Column | Type | Nullable |
|---|---|---|
| name | STRING | Yes |
| age | INTEGER | Yes |
| marks | INTEGER | No |
| department | STRING | Yes |

The symbol table lets the semantic analyzer answer questions such as:

- Does the dataset exist?
- Does the column exist?
- What is the column datatype?
- Can this operation be applied?
- Has a column already been removed?

---

# 20. Schema Inference

When a dataset is loaded, PrepLang inspects its schema.

Example:

```text
students.csv
```

Schema:

```text
students
│
├── name          STRING
├── age           INTEGER
├── marks         INTEGER
├── attendance    FLOAT
└── passed        BOOLEAN
```

This schema becomes part of the compiler's semantic information.

---

# 21. Compiler Phase 4 — Semantic Analysis

Semantic analysis is one of the main features of PrepLang.

---

## 21.1 Undefined Dataset

Code:

```text
NORMALIZE result.age;
```

If `result` was never loaded:

```text
SEMANTIC ERROR

Undefined dataset:
    result
```

---

## 21.2 Undefined Column

Code:

```text
NORMALIZE students.salary;
```

But `salary` does not exist.

Compiler:

```text
SEMANTIC ERROR

Column 'salary' does not exist in dataset 'students'.

Available columns:
    name
    age
    marks
    attendance
```

---

## 21.3 Invalid Datatype

Code:

```text
NORMALIZE students.name;
```

Schema:

```text
name → STRING
```

Compiler:

```text
TYPE ERROR

NORMALIZE requires a numerical column.

Column:
    students.name

Detected type:
    STRING
```

---

## 21.4 Invalid Comparison

Code:

```text
FILTER students WHERE marks > "excellent";
```

Compiler detects:

```text
marks       → INTEGER
"excellent" → STRING
```

Compiler response:

```text
TYPE ERROR

Cannot compare:

INTEGER > STRING
```

---

## 21.5 Invalid Fill Value

Code:

```text
FILL_NULL employees.salary WITH "unknown";
```

Schema:

```text
salary → FLOAT
```

Compiler:

```text
TYPE ERROR

Cannot fill FLOAT column with STRING value.

Column:
    salary : FLOAT

Value:
    "unknown" : STRING
```

---

# 22. Data Contract Validation

Example CSV:

```text
name,age,marks
Nikhil,20,85
Rahul,21,95
Sam,abc,150
```

Program:

```text
EXPECT students.age NUMBER;
EXPECT students.marks BETWEEN 0 AND 100;
```

Output:

```text
DATA CONTRACT VALIDATION

✓ name   → STRING

✗ age
Expected:
    NUMBER

Violation:
    Row 4 contains "abc"

✗ marks
Expected:
    0 <= marks <= 100

Violation:
    Found 150
```

This is an important signature feature of PrepLang.

---

# 23. Intermediate Representation

After semantic analysis, PrepLang converts the AST into a simple Intermediate Representation.

Input:

```text
LOAD "students.csv" AS data;

DROP_NULL data;

NORMALIZE data.age;

SAVE data AS "clean.csv";
```

Possible IR:

```text
IR_LOAD        "students.csv"   data
IR_DROP_NULL   data
IR_NORMALIZE   data age
IR_SAVE        data "clean.csv"
```

Another representation:

```text
LOAD students.csv -> data
DROP_NULL data
NORMALIZE data.age
SAVE data -> clean.csv
```

The IR separates the source language from the final target language.

---

# 24. Why Use an IR?

Without IR:

```text
PrepLang
    ↓
Direct Python
```

With IR:

```text
PrepLang
    ↓
AST
    ↓
IR
    ↓
Optimization
    ↓
Python
```

Advantages:

- Easier optimization
- Easier debugging
- Cleaner compiler design
- Possible future support for multiple backends
- Easier visualization
- Better separation between frontend and backend

---

# 25. Compiler Optimization

Optimization happens on the IR before Python generation.

---

## 25.1 Column Pruning

Suppose the dataset contains 30 columns but the program uses only:

```text
age
salary
```

Naive code:

```python
df = pd.read_csv("employees.csv")
```

Optimized code:

```python
df = pd.read_csv(
    "employees.csv",
    usecols=["age", "salary"]
)
```

Compiler report:

```text
OPTIMIZATION REPORT

Available columns:   30
Required columns:     2
Pruned columns:      28

Optimization:
    COLUMN PRUNING
```

---

## 25.2 Redundant Operation Elimination

Input:

```text
DROP_NULL data;
DROP_NULL data;
```

Before optimization:

```text
IR_DROP_NULL data
IR_DROP_NULL data
```

After optimization:

```text
IR_DROP_NULL data
```

---

## 25.3 Dead Operation Detection

Example:

```text
NORMALIZE data.salary;

DROP data [
    salary
];
```

If the normalized salary is never used afterwards, PrepLang can warn:

```text
OPTIMIZER WARNING

The operation:

    NORMALIZE data.salary

does not affect the final output because
'salary' is removed later.
```

This can initially remain an optional enhancement.

---

# 26. Generated Python Code

PrepLang:

```text
LOAD "students.csv" AS students;

DROP_NULL students;

NORMALIZE students.age;

SAVE students AS "clean.csv";
```

Generated Python:

```python
import pandas as pd

students = pd.read_csv("students.csv")

students = students.dropna()

students["age"] = (
    students["age"] - students["age"].mean()
) / students["age"].std()

students.to_csv("clean.csv", index=False)
```

---

# 27. Target Backend

Version 1:

```text
PrepLang
    ↓
Python/Pandas
```

Possible future backends:

```text
PrepLang
├── Pandas
├── Polars
├── DuckDB
└── Spark
```

Multi-backend support is a future enhancement, not required for the first version.

---

# 28. Error Categories

PrepLang can produce different classes of errors.

## Lexical Errors

```text
Unknown character '$'
```

## Syntax Errors

```text
Expected ';' after statement.
```

## Semantic Errors

```text
Undefined dataset 'students2'.
```

## Type Errors

```text
NORMALIZE requires a numerical column.
```

## Schema Errors

```text
Column 'salary' does not exist.
```

## Data Contract Errors

```text
marks must be between 0 and 100.
```

## Runtime Errors

```text
Unable to open file "students.csv".
```

---

# 29. Error Message Philosophy

Errors should explain:

1. What failed
2. Where it failed
3. Why it failed
4. What was expected
5. What was found
6. A possible correction

Example:

```text
TYPE ERROR — Line 5

Operation:
    NORMALIZE students.name

Problem:
    NORMALIZE requires a numerical column.

Found:
    students.name : STRING

Possible alternatives:
    students.age
    students.marks
```

---

# 30. Proposed User Interface

A simple educational IDE can make the compiler easy to demonstrate.

```text
┌──────────────────────────────────────────────────────┐
│                    PrepLang IDE                      │
├─────────────────────────┬────────────────────────────┤
│ Source Code             │ Compiler Output            │
│                         │                            │
│ LOAD "data.csv"         │ Compilation successful    │
│ AS data;                │                            │
│                         │ Schema loaded              │
│ NORMALIZE data.age;     │ 5 columns detected         │
│                         │                            │
│ SAVE data AS            │ 1 optimization applied    │
│ "output.csv";           │                            │
├─────────────────────────┴────────────────────────────┤
│ Tokens | AST | Symbols | Schema | IR | Optimized IR │
│ Generated Python | Errors                           │
├──────────────────────────────────────────────────────┤
│             Compile              Run                │
└──────────────────────────────────────────────────────┘
```

The UI can be built using a simple Python interface such as Streamlit, Tkinter, or Flask.

A complex frontend is not necessary.

---

# 31. Compiler Visualization

For:

```text
NORMALIZE students.age;
```

The interface can display:

## Tokens

```text
NORMALIZE       KEYWORD
students        IDENTIFIER
.               DOT
age             IDENTIFIER
;               SEMICOLON
```

## AST

```text
NormalizeNode
├── dataset: students
└── column: age
```

## Semantic Information

```text
Dataset exists:       YES
Column exists:        YES
Column datatype:      INTEGER
Operation allowed:    YES
```

## IR

```text
IR_NORMALIZE students age
```

## Generated Python

```python
students["age"] = (
    students["age"] -
    students["age"].mean()
) / students["age"].std()
```

This makes the compiler pipeline visually understandable.

---

# 32. Project Modules

Suggested folder structure:

```text
preplang/
│
├── lexer.py
├── tokens.py
├── parser.py
├── ast_nodes.py
├── symbol_table.py
├── schema_analyzer.py
├── semantic_analyzer.py
├── contracts.py
├── ir.py
├── ir_generator.py
├── optimizer.py
├── code_generator.py
├── executor.py
├── errors.py
├── ui.py
├── main.py
│
├── tests/
│   ├── test_lexer.py
│   ├── test_parser.py
│   ├── test_semantic.py
│   ├── test_optimizer.py
│   └── test_codegen.py
│
├── examples/
│   ├── students.csv
│   ├── employees.csv
│   └── sample.prep
│
└── README.md
```

---

# 33. Module Responsibilities

## lexer.py

- Read source characters
- Recognize keywords
- Recognize identifiers
- Recognize numbers
- Recognize strings
- Recognize operators
- Produce tokens
- Detect lexical errors

## parser.py

- Consume token stream
- Validate grammar
- Generate AST
- Produce syntax errors

## ast_nodes.py

Contains AST classes such as:

```text
LoadNode
NormalizeNode
FilterNode
SelectNode
SaveNode
ExpectNode
```

## symbol_table.py

Stores:

- Dataset names
- Dataset sources
- Column names
- Column datatypes
- Column state

## schema_analyzer.py

Reads CSV metadata and determines:

```text
column name
datatype
nullable status
```

## semantic_analyzer.py

Checks:

- Undefined dataset
- Undefined column
- Invalid operation
- Datatype mismatch
- Invalid comparisons
- Contract compatibility

## contracts.py

Implements:

- Type contracts
- Range contracts
- Not-null contracts

## ir_generator.py

Converts AST into IR instructions.

## optimizer.py

Performs:

- Column pruning
- Redundant-operation removal
- Optional dead-operation detection

## code_generator.py

Converts IR into Python/Pandas source code.

## executor.py

Executes generated Python safely.

## errors.py

Defines structured compiler errors.

## ui.py

Displays compiler stages.

---

# 34. Technology Stack

Recommended:

```text
Language:
    Python

Data Processing:
    Pandas

Parser:
    Hand-written recursive descent parser
    OR PLY/Lark if required

Visualization:
    Streamlit / Tkinter / Flask

AST visualization:
    Graphviz

Testing:
    pytest

Version Control:
    Git + GitHub
```

For a Compiler Design laboratory, implementing the lexer/parser logic manually is preferable if manageable because it makes the compiler concepts easier to explain.

---

# 35. Why Python Is Suitable

Python makes the compiler easier to implement because:

- String handling is simple.
- AST nodes are easy to model using classes/dataclasses.
- CSV inspection is easy.
- Pandas provides the execution backend.
- Testing is simple.
- Development is fast.
- The project remains focused on compiler concepts rather than low-level implementation issues.

---

# 36. Review 1 Plan

## Goal

Problem definition, language design, architecture, lexer, and a basic parser.

## Deliverables

- Project title
- Abstract
- Problem statement
- Motivation
- Objectives
- Scope
- Literature/background study
- Compiler concepts involved
- PrepLang language specification
- Keywords
- Grammar
- Architecture diagram
- Technology stack
- Initial prototype
- Lexer
- Basic parser

## Review 1 Demo

Input:

```text
LOAD "students.csv" AS students;
NORMALIZE students.age;
```

Show:

```text
Source
    ↓
Tokens
    ↓
Basic syntax validation
```

---

# 37. Review 2 Plan

## Goal

Complete the compiler frontend and semantic system.

Implement:

- Complete parser
- AST
- Symbol table
- Dataset schema analysis
- Semantic analysis
- Type checking
- Data-contract validation
- Error reporting
- Initial IR generation

## Review 2 Demo

Valid:

```text
NORMALIZE students.age;
```

Invalid:

```text
NORMALIZE students.name;
```

Output:

```text
TYPE ERROR

NORMALIZE requires a numerical column.

name : STRING
```

---

# 38. Review 3 Plan

## Goal

Complete the entire compiler.

Implement:

- Final IR
- Optimizer
- Python/Pandas code generator
- Execution engine
- Compiler-stage visualization
- Full testing
- Performance comparison
- Documentation

## Final Demo

```text
PrepLang Source
       ↓
Tokens
       ↓
AST
       ↓
Symbol Table + Schema
       ↓
Semantic Validation
       ↓
IR
       ↓
Optimized IR
       ↓
Generated Python
       ↓
Processed CSV
```

---

# 39. Testing Strategy

Testing should include:

## Valid Programs

- Correct LOAD
- Valid NORMALIZE
- Correct FILTER
- Valid SELECT
- Valid SAVE
- Valid data contracts

## Lexical Errors

```text
NORMALIZE data$age;
```

## Syntax Errors

```text
NORMALIZE . age;
```

## Undefined Dataset

```text
NORMALIZE xyz.age;
```

## Undefined Column

```text
NORMALIZE students.salary;
```

## Type Error

```text
NORMALIZE students.name;
```

## Comparison Error

```text
FILTER students WHERE marks > "good";
```

## Contract Error

```text
EXPECT students.marks BETWEEN 0 AND 100;
```

where a value is 150.

## Optimization Tests

Verify:

```text
DROP_NULL data;
DROP_NULL data;
```

becomes one IR instruction.

---

# 40. Performance Evaluation

Possible metrics:

```text
Number of IR instructions before optimization
Number of IR instructions after optimization
Columns loaded before optimization
Columns loaded after column pruning
Compilation time
Execution time
Memory usage
Number of detected semantic errors
Number of detected data-contract violations
```

Example result:

```text
Dataset columns:              30
Columns required:              4
Columns pruned:               26

IR instructions before:       10
IR instructions after:         8

Optimization reduction:       20%
```

---

# 41. Example Viva Explanation

## What is PrepLang?

PrepLang is a domain-specific programming language and compiler for safe data preprocessing.

## What does the lexer do?

The lexer converts PrepLang source code into tokens such as keywords, identifiers, literals, operators, and delimiters.

## What does the parser do?

The parser verifies whether the token sequence follows the PrepLang grammar and builds an Abstract Syntax Tree.

## What is the AST?

The AST is a hierarchical representation of the program structure without unnecessary syntax symbols.

## What is the symbol table?

The symbol table stores datasets, their columns, datatypes, and related metadata.

## What is semantic analysis in PrepLang?

It checks whether datasets and columns exist, whether operations are valid for their datatypes, whether comparisons are type-compatible, and whether data contracts are satisfied.

## Why use an IR?

The IR separates source-language parsing from target-language generation and provides a clean stage for optimization.

## What optimization is implemented?

PrepLang initially supports column pruning and redundant-operation elimination.

## Why not directly translate the DSL to Python?

A direct translator would skip proper compiler phases. PrepLang uses AST, semantic analysis, IR, and optimization so that invalid programs can be rejected and valid programs can be improved before target-code generation.

---

# 42. Originality Position

The general concept of data-processing DSLs already exists.

Examples include:

- Data-processing APIs such as Pandas
- Pipeline DSLs such as Kubeflow pipelines
- Data transformation languages such as ADTL
- Data-analysis DSLs such as Pivotal

Therefore, PrepLang should NOT claim:

> "This is the first data preprocessing DSL."

Its originality comes from the specific combination of:

```text
Educational compiler
        +
Schema-aware semantic analysis
        +
Data contracts
        +
IR-level preprocessing optimization
        +
Compiler-stage visualization
```

This makes PrepLang distinctive enough for a Compiler Design project while remaining realistic for an individual implementation.

---

# 43. Main Signature Features

The three most important features are:

## 1. Static Schema-Aware Type Checking

Example:

```text
NORMALIZE students.name;
```

Compiler knows `name` is STRING and rejects it before execution.

---

## 2. Data Contracts

Example:

```text
EXPECT students.marks BETWEEN 0 AND 100;
```

The compiler/runtime validation system checks the dataset against explicit constraints.

---

## 3. IR-Level Preprocessing Optimization

Example:

```text
SELECT data [
    age,
    salary
];
```

The compiler performs column pruning so unnecessary columns are never loaded.

---

# 44. Core Compiler Concepts Used

PrepLang directly demonstrates:

- Lexical Analysis
- Tokenization
- Syntax Analysis
- Formal Grammar
- Parsing
- Abstract Syntax Tree
- Symbol Table
- Semantic Analysis
- Type Checking
- Schema Validation
- Intermediate Code Generation
- Optimization
- Target Code Generation
- Interpretation / Execution
- Error Detection
- Error Reporting

---

# 45. Strong Project Pitch

> PrepLang is a schema-aware domain-specific compiler for safe and optimized data preprocessing. Users write simple preprocessing instructions instead of raw Pandas code. The compiler performs lexical and syntax analysis, builds an AST and symbol table, infers dataset schemas, statically validates preprocessing operations, checks data contracts, generates intermediate representation, applies preprocessing optimizations such as column pruning and redundant-operation elimination, and finally generates executable Python/Pandas code. The project demonstrates classical compiler-design concepts in a practical data-processing application.

---

# 46. Short Faculty Pitch

> I am building a small programming language for data preprocessing. Similar to how a traditional compiler catches type errors such as assigning a string to an integer, my compiler understands dataset schemas and catches errors such as normalizing a string column or comparing an integer column with text. After semantic validation, it generates an intermediate representation, optimizes the preprocessing pipeline, and generates Python/Pandas code.

---

# 47. Difficulty

Estimated implementation difficulty:

```text
Lexer                 Easy
Parser                Easy–Medium
AST                   Easy
Symbol Table          Easy
Schema Analysis       Easy–Medium
Semantic Analysis     Medium
Data Contracts        Medium
IR                    Easy
Optimization          Medium
Code Generation       Easy
UI                    Easy–Medium
```

Overall:

**Approximately 4–5/10 difficulty**

while still giving strong compiler-design depth.

---

# 48. Why This Project Is a Good Choice

PrepLang is a strong project because it is:

- Easy to understand
- Easy to explain
- Strongly related to Compiler Design
- Not just a calculator/compiler toy
- Practical
- Demonstrable
- Individually implementable
- Original enough for a laboratory project
- Suitable for incremental development
- Easy to test
- Easy to present visually
- Useful as a portfolio project
- Expandable in the future

---

# 49. Minimum Viable Version

If time becomes limited, the minimum working version should support:

```text
LOAD
DROP_NULL
NORMALIZE
FILTER
SELECT
SAVE
```

and implement:

```text
Lexer
Parser
AST
Symbol Table
Schema Checking
Semantic Analysis
IR
Python Code Generation
```

This alone is enough for a proper compiler project.

---

# 50. Full Version

The final version can include:

```text
LOAD
DROP_NULL
FILL_NULL
NORMALIZE
FILTER
SELECT
DROP
SAVE

EXPECT TYPE
EXPECT RANGE
EXPECT NOT_NULL

Schema Inference
Type Checking
Data Contracts
IR Generation
Column Pruning
Redundant Operation Elimination
Python/Pandas Code Generation
Compiler Visualization
Execution
Optimization Report
```

---

# 51. Future Enhancements

Possible future extensions:

- Polars backend
- DuckDB backend
- Spark backend
- JOIN operations
- Group-by operations
- Encoding categorical values
- Train/test split command
- Feature scaling options
- Advanced data contracts
- Nullability type system
- Schema evolution checking
- Automatic preprocessing suggestions
- Cost-based optimization
- Visual pipeline graph
- IDE autocomplete
- Language Server Protocol support
- More advanced compiler error recovery

---

# 52. Final Project Identity

## Name

**PrepLang**

## Full Title

**PrepLang: A Schema-Aware Domain-Specific Language Compiler for Safe and Optimized Data Preprocessing**

## Main Theme

Compiler Design applied to data preprocessing.

## Main Novelty

Static schema-aware validation + data contracts + IR-level optimization.

## Main Output

Validated and optimized Python/Pandas preprocessing code.

## Main Compiler Pipeline

```text
Source
 ↓
Lexer
 ↓
Parser
 ↓
AST
 ↓
Symbol Table
 ↓
Schema Analysis
 ↓
Semantic Analysis
 ↓
IR
 ↓
Optimizer
 ↓
Code Generator
 ↓
Python/Pandas
 ↓
Output Dataset
```

---

# 53. Final Recommendation

The project should remain focused on the compiler.

Do not turn PrepLang into a complete machine-learning platform.

The goal should be:

> **Build a small language extremely well and clearly demonstrate every compiler phase.**

The strongest final version is:

**PrepLang = Simple Language + Schema-Aware Compiler + Data Contracts + Optimization + Python Backend + Compiler Visualization**

This provides a good balance between:

```text
Easy to build
       +
Easy to understand
       +
Easy to explain
       +
Strong compiler relevance
       +
Enough originality
       +
Good demonstration value
```

---

# 54. Final Locked Idea

> **PrepLang is a schema-aware domain-specific compiler that lets users express data-preprocessing pipelines using a simple custom language. It statically validates dataset schemas and preprocessing operations, supports explicit data contracts, converts programs into an intermediate representation, optimizes the preprocessing pipeline, and generates executable Python/Pandas code while exposing each compiler stage for visualization and learning.**

