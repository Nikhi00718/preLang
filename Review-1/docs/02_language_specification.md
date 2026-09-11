# 02 — PrepLang Language Specification (version 1.0)

## 2.1 Design principles

1. **One statement = one preprocessing operation.** No nesting, no expressions
   beyond a single filter condition. The grammar stays LL(1) and the AST stays
   flat, which keeps every phase explainable.
2. **Every statement ends with `;`.** A fixed terminator gives the parser a
   reliable synchronisation point for panic-mode error recovery.
3. **Datasets are named, columns are qualified.** `students.age` makes the
   symbol-table lookup explicit in the surface syntax.
4. **Contracts are declarations, not code.** `EXPECT` statements state
   properties the data must satisfy; the compiler decides how to verify them.

## 2.2 Character set

| Class | Characters |
|---|---|
| Letters | `A`–`Z`, `a`–`z`, `_` |
| Digits | `0`–`9` |
| Whitespace (ignored) | space, tab, CR, LF |
| Punctuation | `.` `,` `;` `[` `]` |
| Operator characters | `<` `>` `=` `!` |
| Quotes | `"` `'` |
| Comment starters | `#`, `--` (to end of line) |

Any other character is a **lexical error**.

## 2.3 Token classes

| Token type | Description | Examples |
|---|---|---|
| `KEYWORD` | Reserved word (case-insensitive, canonicalised to uppercase) | `LOAD`, `NORMALIZE`, `BETWEEN` |
| `IDENTIFIER` | Dataset or column name, not a keyword | `students`, `age`, `emp_id` |
| `NUMBER_LITERAL` | Integer or floating-point constant | `50`, `3.14`, `100` |
| `STRING_LITERAL` | Quoted text; `"` or `'`; supports `\n \t \\ \" \'` escapes | `"students.csv"` |
| `BOOLEAN_LITERAL` | `TRUE` / `FALSE` (any casing) | `TRUE` |
| `DOT` | `.` — qualifies a column with its dataset | `.` |
| `COMMA` | `,` — separates items of a column list | `,` |
| `SEMICOLON` | `;` — statement terminator | `;` |
| `LEFT_BRACKET` | `[` | `[` |
| `RIGHT_BRACKET` | `]` | `]` |
| `RELATIONAL_OPERATOR` | Comparison in a `FILTER` condition | `>` `<` `>=` `<=` `==` `!=` |
| `EOF` | Synthetic end-of-file marker | — |

### Regular definitions (the lexer's DFA in regular-expression form)

```
letter        -> [A-Za-z_]
digit         -> [0-9]
identifier    -> letter ( letter | digit )*
number        -> digit+ ( "." digit+ )?
string        -> '"' ( any_char_except_quote | escape )* '"'
               | "'" ( any_char_except_quote | escape )* "'"
relop         -> ">" | "<" | ">=" | "<=" | "==" | "!="
comment       -> ( "#" | "--" ) any_char* newline
whitespace    -> ( " " | "\t" | "\r" | "\n" )+
```

Note the one context-sensitive rule: inside `number`, a `.` is only consumed
as a decimal point **when a digit follows it**. This single lookahead keeps
`students.age` (IDENTIFIER DOT IDENTIFIER) unambiguous against `3.14`.

## 2.4 Reserved keywords (18)

```
Statements :  LOAD  DROP_NULL  FILL_NULL  NORMALIZE  FILTER
              SELECT  DROP  SAVE  EXPECT
Modifiers  :  AS  WITH  WHERE
Contracts  :  NUMBER  STRING  BOOLEAN  BETWEEN  AND  NOT_NULL
```

Keywords are reserved: they cannot be used as dataset or column names.
`TRUE` and `FALSE` are boolean literals, not keywords.

## 2.5 Data types

The PrepLang type system has four column types plus one error type:

| Type | Meaning | Numeric? |
|---|---|---|
| `INTEGER` | Whole numbers | yes |
| `FLOAT` | Real numbers | yes |
| `STRING` | Text | no |
| `BOOLEAN` | `TRUE` / `FALSE` | no |
| `UNKNOWN` | Inference failed / error recovery | — |

Types are **inferred from the CSV file** at compile time, never declared by
the programmer. `INTEGER` widens to `FLOAT` where an operation requires it
(for example `NORMALIZE` always produces `FLOAT`).

## 2.6 Statements

| Statement | Syntax | Effect |
|---|---|---|
| Load | `LOAD "file.csv" AS name;` | Binds a dataset name to a CSV file and reads its schema |
| Drop nulls | `DROP_NULL name;` | Removes rows containing any missing value |
| Fill nulls | `FILL_NULL name.col WITH value;` | Replaces missing values in one column |
| Normalize | `NORMALIZE name.col;` | Z-score normalisation of a numeric column |
| Filter | `FILTER name WHERE col op value;` | Keeps rows satisfying the condition |
| Select | `SELECT name [ a, b, c ];` | Keeps only the listed columns |
| Drop | `DROP name [ a, b ];` | Removes the listed columns |
| Save | `SAVE name AS "out.csv";` | Writes the dataset to disk |
| Contract | `EXPECT name.col <expectation>;` | Declares a property the data must satisfy |

### Expectations

| Form | Meaning |
|---|---|
| `EXPECT d.c NUMBER;` | The column must be numeric (`INTEGER` or `FLOAT`) |
| `EXPECT d.c STRING;` | The column must be text |
| `EXPECT d.c BOOLEAN;` | The column must be boolean |
| `EXPECT d.c NOT_NULL;` | The column must contain no missing values |
| `EXPECT d.c BETWEEN lo AND hi;` | Every value must satisfy `lo <= v <= hi` |

## 2.7 Static semantic rules

These rules are enforced by the semantic analyser (Review 2). They define what
it means for a syntactically valid program to be *well formed*.

| # | Rule | Diagnostic if violated |
|---|---|---|
| S1 | A dataset must be `LOAD`ed before it is used | `SEMANTIC ERROR: undefined dataset` |
| S2 | A dataset name may be bound only once | `SEMANTIC ERROR: dataset redefined` |
| S3 | Every referenced column must exist in the dataset schema | `SCHEMA ERROR: column does not exist` + suggestions |
| S4 | A column removed by `DROP`/`SELECT` may not be used afterwards | `SEMANTIC ERROR: column no longer available` |
| S5 | `NORMALIZE` requires `INTEGER` or `FLOAT` | `TYPE ERROR: NORMALIZE requires a numerical column` |
| S6 | In `FILTER`, the column type and the literal type must be comparable | `TYPE ERROR: cannot compare INTEGER > STRING` |
| S7 | Ordering operators `< > <= >=` require numeric operands | `TYPE ERROR: ordering requires numeric operands` |
| S8 | `FILL_NULL` value type must be assignable to the column type | `TYPE ERROR: cannot fill FLOAT column with STRING` |
| S9 | `EXPECT ... BETWEEN` requires a numeric column and `lo <= hi` | `TYPE ERROR` / `SEMANTIC ERROR` |
| S10 | `SAVE` must name a loaded dataset with at least one live column | `SEMANTIC ERROR` |

## 2.8 Complete example

```text
LOAD "students.csv" AS students;

EXPECT students.age    NUMBER;
EXPECT students.marks  BETWEEN 0 AND 100;
EXPECT students.name   NOT_NULL;

DROP_NULL  students;
FILL_NULL  students.attendance WITH 0;
NORMALIZE  students.age;
FILTER     students WHERE marks >= 50;

SELECT students [ name, age, marks ];

SAVE students AS "cleaned_students.csv";
```
