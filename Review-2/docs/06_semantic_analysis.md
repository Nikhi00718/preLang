# 06 — Semantic Analysis, Schema Inference and Data Contracts

This document explains the phase that is new in Review 2 and that carries the
project's central idea.

## 6.1 The central claim

> A dataset schema is a type environment.

A conventional compiler builds its type environment from *declarations* in the
program:

```c
int x;                 /* declaration: x : int  */
x = "hello";           /* TYPE ERROR            */
```

PrepLang builds the same environment from the *data*:

```text
students.csv header + rows   ⇒   name : STRING, age : INTEGER, marks : INTEGER,
                                 attendance : FLOAT, passed : BOOLEAN
NORMALIZE students.name;     ⇒   TYPE ERROR
```

Everything else in this phase follows mechanically from that one substitution.

## 6.2 Schema inference (`schema.py`)

`infer_schema(path)` reads the CSV header for names and a sample of rows
(default 2000) for types.

```
for each column:
    values = non-empty cells in the sample
    if every value is TRUE/FALSE/YES/NO      -> BOOLEAN
    elif every value parses as an integer    -> INTEGER
    elif every value parses as a real number -> FLOAT
    else                                     -> STRING
    nullable = at least one cell was empty
```

Two properties of this algorithm matter:

* **It is conservative.** One bad value demotes the whole column to `STRING`.
  That is the behaviour that makes `students_dirty.csv` — where `age` contains
  `abc` — reject `NORMALIZE students.age` *at compile time*.
* **It only uses the standard library.** The whole front end is therefore
  dependency-free; Pandas is needed only to execute the generated code.

Null tokens recognised: empty, whitespace, `NA`, `N/A`, `NaN`, `null`, `none`,
`-`.

## 6.3 The symbol table (`symbol_table.py`)

PrepLang has a two-level name space:

```
global scope                      SymbolTable
  └── dataset scope               DatasetSymbol  (bound by LOAD)
        └── column declarations   ColumnSymbol   (from the schema)
```

A `ColumnSymbol` records `name`, `type`, `nullable`, `active` and `history`.

The design decision worth defending in the viva: **a dropped column is marked
inactive, not deleted.** Deleting it would make

```text
SELECT students [ name, age ];
NORMALIZE students.attendance;
```

report "column `attendance` does not exist", which is misleading — the column
does exist in the file; it was removed by line 1. Keeping the symbol lets the
analyser say:

```
Column 'attendance' of dataset 'students' is no longer available;
it was removed at line 8.
```

This is a *lifetime* diagnostic, the data-preprocessing analogue of using a
variable after its scope has ended.

## 6.4 Flow-sensitivity

The analyser walks statements in source order and **updates** the symbol table
as it goes, so the type environment at line *n* reflects the statements before
it:

| Statement | Effect on the symbol table |
|---|---|
| `LOAD` | Creates the dataset scope and populates it from the schema |
| `DROP_NULL` | Every live column becomes non-nullable |
| `FILL_NULL d.c` | Column `c` becomes non-nullable |
| `NORMALIZE d.c` | Column `c`'s type becomes `FLOAT` (a z-score is real) |
| `DROP d [..]` | The listed columns become inactive |
| `SELECT d [..]` | Every column *not* listed becomes inactive |

This is why the compiler can warn:

```text
FILL_NULL on students.age has no effect: the column contains no missing values.
```

after a preceding `DROP_NULL` — a conclusion that requires tracking state
across statements, not just checking each one in isolation.

## 6.5 The type rules

| Operation | Rule | Written as |
|---|---|---|
| `NORMALIZE d.c` | `type(c) ∈ {INTEGER, FLOAT}` | S5 |
| `FILTER d WHERE c op v` | `comparable(type(c), type(v))` | S6 |
| `FILTER` with `< > <= >=` | `type(c)` numeric | S7 |
| `FILL_NULL d.c WITH v` | `assignable(type(v), type(c))` | S8 |
| `EXPECT d.c BETWEEN lo AND hi` | `type(c)` numeric ∧ `lo ≤ hi` | S9 |

with

```
assignable(v, t)  =  v = t  ∨  (t = FLOAT ∧ v = INTEGER)
comparable(a, b)  =  (numeric(a) ∧ numeric(b))  ∨  a = b
```

`INTEGER → FLOAT` is the one implicit coercion in the language — the classical
numeric widening — which is why `FILL_NULL students.attendance WITH 0` is
accepted although `attendance` is `FLOAT` and `0` is `INTEGER`.

## 6.6 Semantic error recovery

Two mechanisms prevent cascading diagnostics:

1. **A failed `LOAD` still binds the name.** If `students.csv` cannot be read,
   the dataset symbol is created with an empty scope, so later statements do
   not each report "undefined dataset".
2. **An unresolved dataset suppresses column diagnostics.** When a dataset's
   schema is missing, column lookups return quietly instead of reporting
   "column does not exist" for every subsequent line.

The result: a missing file produces exactly **one** diagnostic, not one per
statement.

## 6.7 Data contracts

`EXPECT` is checked in two stages.

### Stage 1 — statically, from the schema

| Contract | Statically decidable when |
|---|---|
| `EXPECT c NUMBER/STRING/BOOLEAN` | Always — compare with the inferred type |
| `EXPECT c BETWEEN lo AND hi` | Partially — the column must be numeric and `lo ≤ hi` |
| `EXPECT c NOT_NULL` | Satisfied statically when the schema reports no nulls |

A statically refuted contract is a **compile error**; no data is read:

```text
DATA CONTRACT ERROR - Line 7

Problem:
    Contract violated: students.age is declared NUMBER but the dataset
    schema says STRING.
```

### Stage 2 — against the data

What survives stage 1 is checked by streaming the CSV **once per dataset**, no
matter how many contracts refer to it, and reporting the offending spreadsheet
row:

```text
[FAIL]  line   9  students.marks  --  0 <= value <= 100
          1 violation(s) in 12 rows
            - row 6: '150'
```

Note the separation of concerns: a *missing* value never violates a type or
range contract — that is what `NOT_NULL` is for. Each contract makes exactly
one claim, so each diagnostic is about exactly one thing.

## 6.8 Semantic facts

Besides diagnostics, the analyser records, for every statement, the questions
it asked and the answers it got:

```text
line  16  Normalize(students.age)   [OK]
          Dataset exists    : YES (students.csv)
          Column exists     : YES
          Column datatype   : INTEGER
          Operation allowed : YES
          Result datatype   : FLOAT (z-score)
```

These facts cost almost nothing to produce and they turn the phase from a
black box into something that can be shown and explained — which is the whole
point of a compiler-visualisation project.
