# 03 — PrepLang Grammar (BNF) and LL(1) Analysis

## 3.1 Context-free grammar

Terminals are written in UPPERCASE or quoted; non-terminals in lowercase.
`ε` denotes the empty string.

```bnf
(1)  program          -> statement_list EOF

(2)  statement_list   -> statement statement_list
(3)                    | ε

(4)  statement        -> load_stmt
(5)                    | drop_null_stmt
(6)                    | fill_null_stmt
(7)                    | normalize_stmt
(8)                    | filter_stmt
(9)                    | select_stmt
(10)                   | drop_stmt
(11)                   | save_stmt
(12)                   | expect_stmt

(13) load_stmt        -> LOAD STRING AS IDENTIFIER ';'

(14) drop_null_stmt   -> DROP_NULL IDENTIFIER ';'

(15) fill_null_stmt   -> FILL_NULL column_ref WITH literal ';'

(16) normalize_stmt   -> NORMALIZE column_ref ';'

(17) filter_stmt      -> FILTER IDENTIFIER WHERE condition ';'

(18) select_stmt      -> SELECT IDENTIFIER '[' identifier_list ']' ';'

(19) drop_stmt        -> DROP IDENTIFIER '[' identifier_list ']' ';'

(20) save_stmt        -> SAVE IDENTIFIER AS STRING ';'

(21) expect_stmt      -> EXPECT column_ref expectation ';'

(22) column_ref       -> IDENTIFIER '.' IDENTIFIER

(23) condition        -> IDENTIFIER RELOP literal

(24) expectation      -> NUMBER
(25)                   | STRING_TYPE
(26)                   | BOOLEAN_TYPE
(27)                   | NOT_NULL
(28)                   | BETWEEN NUMBER_LIT AND NUMBER_LIT

(29) identifier_list  -> IDENTIFIER identifier_list_tail
(30) identifier_list_tail -> ',' IDENTIFIER identifier_list_tail
(31)                   | ε

(32) literal          -> NUMBER_LIT
(33)                   | STRING
(34)                   | BOOLEAN_LIT
```

Terminal note: `NUMBER`, `STRING_TYPE` and `BOOLEAN_TYPE` in rules (24)–(26)
are the *keywords* `NUMBER`, `STRING`, `BOOLEAN` used as contract type names;
`NUMBER_LIT`, `STRING`, `BOOLEAN_LIT` are literal tokens.

## 3.2 FIRST sets

| Non-terminal | FIRST |
|---|---|
| `program` | { LOAD, DROP_NULL, FILL_NULL, NORMALIZE, FILTER, SELECT, DROP, SAVE, EXPECT, ε } |
| `statement_list` | { LOAD, DROP_NULL, FILL_NULL, NORMALIZE, FILTER, SELECT, DROP, SAVE, EXPECT, ε } |
| `statement` | { LOAD, DROP_NULL, FILL_NULL, NORMALIZE, FILTER, SELECT, DROP, SAVE, EXPECT } |
| `load_stmt` | { LOAD } |
| `drop_null_stmt` | { DROP_NULL } |
| `fill_null_stmt` | { FILL_NULL } |
| `normalize_stmt` | { NORMALIZE } |
| `filter_stmt` | { FILTER } |
| `select_stmt` | { SELECT } |
| `drop_stmt` | { DROP } |
| `save_stmt` | { SAVE } |
| `expect_stmt` | { EXPECT } |
| `column_ref` | { IDENTIFIER } |
| `condition` | { IDENTIFIER } |
| `expectation` | { NUMBER, STRING, BOOLEAN, NOT_NULL, BETWEEN } |
| `identifier_list` | { IDENTIFIER } |
| `identifier_list_tail` | { ',' , ε } |
| `literal` | { NUMBER_LIT, STRING, BOOLEAN_LIT } |

## 3.3 FOLLOW sets (for the nullable non-terminals)

| Non-terminal | FOLLOW |
|---|---|
| `program` | { $ } |
| `statement_list` | { EOF } |
| `identifier_list_tail` | { ']' } |
| `column_ref` | { WITH, ';', NUMBER, STRING, BOOLEAN, NOT_NULL, BETWEEN } |
| `literal` | { ';' } |
| `expectation` | { ';' } |

## 3.4 Why the grammar is LL(1)

Three conditions must hold for each non-terminal with more than one
production:

1. **Disjoint FIRST sets.**
   For `statement`, the nine alternatives begin with nine *distinct* keywords,
   so `FIRST(4) ∩ FIRST(5) ∩ … ∩ FIRST(12) = ∅`.
   For `expectation`, the five alternatives begin with `NUMBER`, `STRING`,
   `BOOLEAN`, `NOT_NULL`, `BETWEEN` — again pairwise disjoint.
   For `literal`, the three alternatives are three distinct token classes.

2. **For nullable non-terminals, FIRST ∩ FOLLOW = ∅.**
   `statement_list` is nullable; `FIRST(statement_list)` contains only
   statement keywords while `FOLLOW(statement_list) = { EOF }` — disjoint.
   `identifier_list_tail` is nullable; `FIRST = { ',' }` and
   `FOLLOW = { ']' }` — disjoint.

3. **No left recursion.** Every recursive production (`statement_list`,
   `identifier_list_tail`) is *right* recursive.

Therefore a predictive parser needs exactly **one token of lookahead** and
never backtracks. This is why `parser_basic.py` can dispatch on
`self.current` with a plain dictionary lookup — that dictionary *is* the LL(1)
parsing table.

## 3.5 LL(1) parsing table (statement level)

| Lookahead token | Production applied |
|---|---|
| `LOAD` | (13) `load_stmt` |
| `DROP_NULL` | (14) `drop_null_stmt` |
| `FILL_NULL` | (15) `fill_null_stmt` |
| `NORMALIZE` | (16) `normalize_stmt` |
| `FILTER` | (17) `filter_stmt` |
| `SELECT` | (18) `select_stmt` |
| `DROP` | (19) `drop_stmt` |
| `SAVE` | (20) `save_stmt` |
| `EXPECT` | (21) `expect_stmt` |
| `EOF` | (3) `statement_list -> ε` |
| anything else | **syntax error** → panic-mode recovery |

Note that `DROP` and `DROP_NULL` are two *separate* keyword tokens, decided by
the lexer's maximal-munch identifier rule, so the parser never has to
distinguish them by lookahead — a small but deliberate design decision that
preserves LL(1).

## 3.6 Error recovery strategy

The parser uses **panic-mode recovery**:

1. Record the located diagnostic (expected / found / hint).
2. Discard tokens until either
   * a `;` has been consumed — the current statement is abandoned, or
   * the lookahead is a statement keyword — the current statement is abandoned
     without consuming the keyword.
3. Resume parsing at the next statement.

The synchronising set is therefore `{ ';' } ∪ FIRST(statement)`. Because every
statement is terminated by `;`, recovery is precise: one bad statement costs
exactly one error message, and all later statements are still checked.

## 3.7 Derivation example

Input: `NORMALIZE students.age;`

```
program
=> statement_list EOF                                   (1,2)
=> statement statement_list EOF                         (2)
=> normalize_stmt statement_list EOF                    (7)
=> NORMALIZE column_ref ';' statement_list EOF          (16)
=> NORMALIZE IDENTIFIER '.' IDENTIFIER ';' statement_list EOF   (22)
=> NORMALIZE students . age ; statement_list EOF
=> NORMALIZE students . age ; EOF                       (3)
```

Parse tree:

```
                    program
                       |
                   statement
                       |
                normalize_stmt
            /        |        \        \
    NORMALIZE   column_ref     ';'     ...
                /   |   \
        students   '.'   age
```

After AST construction (Review 2) the syntactic noise disappears:

```
NormalizeNode
├── dataset : students
└── column  : age
```
