# Review 1 — Presentation Guide and Viva Preparation

Review 1 carries **20 marks**. This file maps every mark to something concrete
you can show, and lists the questions you are most likely to be asked.

---

## 1. Mark-by-mark checklist

| Criterion | Marks | What to show |
|---|---|---|
| Topic Relevance | 3 | The pipeline diagram: this project *is* the classical compiler pipeline. Say: "a dataset schema is a type environment, so `NORMALIZE students.name` is the same error as `int x = "hello";`" |
| Problem Statement | 3 | The "before/after" slide: Pandas code where a wrong column type fails only at run time, versus PrepLang rejecting it at compile time |
| Objectives | 2 | The O1–O16 table (doc 01 §1.4), mapped to Review 1 / 2 / 3 |
| System Design | 4 | The architecture diagram (doc 04 §4.1) + the module inventory table + the single-statement data-flow table (§4.4) |
| Compiler Concepts | 3 | The concepts table (doc 05 §5.1); be ready to point at the *line of code* for any concept named |
| Innovation | 2 | Three signature features: schema-aware static typing, data contracts as language statements, IR-level column pruning |
| Prototype | 3 | Live run: `python main.py examples/demo_valid.prep`, then the two error demos, then `python -m pytest -q` (46 passed) |

## 2. Suggested 8–10 minute flow

1. **(1 min) The problem.** Show the five-line Pandas snippet. Ask: which
   column names exist? Which are numeric? Nobody knows until it runs.
2. **(1 min) The idea.** Same pipeline in PrepLang. State the one-line pitch:
   *"a small language for preprocessing whose compiler knows the dataset
   schema and rejects invalid pipelines before any data is read."*
3. **(1 min) Language design.** Nine statements, five contracts, 18 keywords —
   show the statement table from doc 02.
4. **(1.5 min) Grammar.** Show the BNF and say why it is LL(1): every
   statement starts with a distinct keyword, so FIRST sets are pairwise
   disjoint and one token of lookahead suffices.
5. **(2 min) Architecture.** Walk the diagram once, then walk
   `NORMALIZE students.age;` through the data-flow table.
6. **(2.5 min) Live prototype.**
   ```bash
   python main.py examples/demo_valid.prep         # tokens + statements
   python main.py examples/demo_lexical_error.prep --no-tokens
   python main.py examples/demo_syntax_error.prep --no-tokens
   python -m pytest -q
   ```
   Emphasise: five errors are reported from one run — that is panic-mode
   recovery, not a crash on the first error.
7. **(1 min) Plan.** Review 2 = AST + symbol table + schema + semantic
   analysis + contracts + IR. Review 3 = optimiser + Pandas code generation +
   execution + IDE + benchmarks.

## 3. Viva questions and answers

**Q. What exactly does your lexer do?**
It implements a DFA by hand. `tokenize()` is the start state; it dispatches on
the first character to an accepting routine (`_scan_word`, `_scan_number`,
`_scan_string`, `_scan_operator`). Each routine consumes the longest valid
lexeme (maximal munch) and emits a `Token` carrying type, lexeme, semantic
value, line and column.

**Q. How do you distinguish `students.age` from the number `3.14`?**
One character of lookahead inside `_scan_number`: a `.` is consumed as a
decimal point only if the character after it is a digit. Otherwise the `.` is
emitted as a `DOT` token. Without that rule the grammar would be ambiguous.

**Q. Why is `DROP_NULL` not parsed as `DROP` followed by `NULL`?**
Because the scanner uses maximal munch on identifiers: it consumes letters,
digits and underscores greedily, so it reads the whole lexeme `DROP_NULL` and
only then looks it up in the keyword table.

**Q. Is your grammar LL(1)? Prove it.**
Yes. Three conditions: (1) for `statement` the nine alternatives begin with
nine distinct keywords, so the FIRST sets are pairwise disjoint; same for the
five `expectation` alternatives and the three `literal` alternatives. (2) The
nullable non-terminals `statement_list` and `identifier_list_tail` have
FIRST ∩ FOLLOW = ∅ ({keywords} vs {EOF}, and {','} vs {']'}). (3) There is no
left recursion — both recursive rules are right-recursive. So one token of
lookahead always selects the production.

**Q. Why recursive descent instead of Lex/Yacc or ANTLR?**
The grammar is 34 productions — small enough to hand-write. A hand-written
parser makes each non-terminal a readable function, gives complete control
over error messages and recovery, and means I can explain every line. A
generated parser would hide exactly the logic the lab is assessing.

**Q. What is panic-mode recovery and where is it in your code?**
`BasicParser._synchronise()`. On a syntax error the parser records the
diagnostic, then discards tokens until it has consumed a `;` or sees a
statement keyword, then resumes. The synchronising set is
`{';'} ∪ FIRST(statement)`. That is why `demo_syntax_error.prep` reports five
errors instead of one.

**Q. Your lexer found `$` invalid — why does it not stop there?**
It records a `LexicalError`, skips the character and continues scanning, so
one run reports every lexical problem. Stopping at the first error is the
behaviour of a toy; reporting all of them is the behaviour of a compiler.

**Q. Which compiler phases are actually implemented right now?**
Lexical analysis, fully. Syntax analysis as a recogniser: it enforces the
entire grammar and reports errors, but builds a flat `StatementInfo` summary
rather than the full AST. AST construction is the first task of Review 2.

**Q. What is the innovation? Preprocessing DSLs already exist.**
I do not claim the first preprocessing DSL. The contribution is the
combination: schema-aware *static* type checking (the compiler reads the CSV
header and sample rows and treats the inferred column types as a type
environment), data contracts as first-class statements, IR-level column
pruning driven by live-column analysis, and full stage-by-stage
visualisation — in one small compiler built individually.

**Q. How will your semantic analyser know `age` is an integer?**
`schema_analyzer.py` (Review 2) reads the CSV header for column names and a
sample of rows to infer each column's type, producing a `Schema` that is
loaded into the symbol table by the `LOAD` statement. Type checking then works
against that environment exactly like a normal compiler's declarations.

**Q. What optimisation will you actually implement?**
Three IR-level passes: (1) column pruning — a live-column analysis computes
the set of columns the program actually needs, and `usecols=` is attached to
the `read_csv` call so unused columns are never loaded; (2) redundant-operation
elimination — repeated identical operations such as two consecutive
`DROP_NULL` collapse to one; (3) dead-operation elimination — a `NORMALIZE` on
a column that is later dropped and never read is removed with a warning.

**Q. Why generate Python instead of interpreting directly?**
Because code generation is a compiler phase and the IR makes it replaceable.
The IR is target-independent, so a Polars or DuckDB backend would only require
a new code generator — the whole front end stays untouched.

**Q. What are the limitations?**
Version 1 handles CSV only, one condition per `FILTER`, no joins or group-by,
and schema inference samples rows rather than reading the whole file. These
are listed as scope exclusions and future enhancements, not oversights.

## 4. Common traps

* Do not call PrepLang "a Pandas wrapper" — the point is the *static
  validation* that a wrapper cannot do.
* Do not say the parser "builds an AST" in Review 1 — it validates syntax; the
  AST is Review 2. Claiming work you have not shown is the fastest way to lose
  viva marks.
* Be able to open any file you are asked about and explain a specific function.
  `lexer.py::_scan_number` and `parser_basic.py::_synchronise` are the two most
  likely targets.
