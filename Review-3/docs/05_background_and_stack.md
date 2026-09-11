# 05 — Background Study, Related Work and Technology Stack

## 5.1 Background: compiler concepts used

| Concept | Standard definition | Application in PrepLang |
|---|---|---|
| Lexical analysis | Grouping characters into tokens using a DFA | `lexer.py`; regular definitions in doc 02 |
| Regular definitions | RE description of token classes | `identifier`, `number`, `string`, `relop` |
| Maximal munch | Longest lexeme wins | `>=` vs `>`; `DROP_NULL` vs `DROP` |
| Context-free grammar | Productions describing valid programs | BNF in doc 03 |
| FIRST / FOLLOW, LL(1) | Predictive-parsing decision procedure | Proof in doc 03 §3.4 |
| Recursive-descent parsing | One function per non-terminal | `parser_basic.py` |
| Panic-mode recovery | Skip to a synchronising token after an error | Synchronising set `{';'} ∪ FIRST(statement)` |
| Abstract Syntax Tree | Condensed parse tree | `ast_nodes.py` (Review 2) |
| Symbol table | Name → attribute mapping | Datasets + columns + types + liveness |
| Type system / type checking | Rejecting ill-typed operations | Schema types drive `NORMALIZE`, `FILTER`, `FILL_NULL` rules |
| Type inference | Deducing types not written by the programmer | Column types inferred from CSV data |
| Semantic analysis | Static checks beyond grammar | Rules S1–S10 in doc 02 §2.7 |
| Intermediate representation | Machine/target-independent code | Linear IR instruction list |
| Machine-independent optimisation | Improving IR before code generation | Column pruning, redundancy and dead-op elimination |
| Live-variable analysis | Which values are needed later | Live-column analysis driving column pruning |
| Dead-code elimination | Removing computations with no effect | `NORMALIZE` on a later-dropped column |
| Target code generation | IR → executable form | Python/Pandas emitter |
| Error reporting and recovery | Useful diagnostics, continued analysis | `errors.py`, six error classes |

## 5.2 Related work

| System | What it is | How PrepLang differs |
|---|---|---|
| **Pandas** (Python library) | Imperative dataframe API | An API, not a language: no static schema checking, no compiler phases, no optimisation of the user's pipeline |
| **SQL engines** | Declarative query language with a real optimiser | SQL optimises queries over a *declared* schema in a database; PrepLang targets flat CSV files, infers the schema, and specialises in preprocessing rather than querying |
| **Great Expectations / Pandera** | Python data-validation libraries | Validation happens at run time, expressed in Python; PrepLang makes contracts *statements of the language*, checked partly at compile time |
| **dbt** | SQL transformation framework with tests | Focused on warehouse SQL and orchestration, not a compiler with its own lexer/parser/IR |
| **Apache Spark Catalyst** | Query optimiser with logical/physical plans | Industrial scale; conceptually the closest relative — PrepLang is the same idea reduced to a teachable size |
| **Kubeflow / pipeline DSLs** | Workflow specification languages | Orchestrate *steps*; they do not reason about column types inside the data |

**Originality position.** Data-processing DSLs exist, and PrepLang does not
claim to be the first. Its contribution is the specific combination of
*(a)* static schema-aware type checking of preprocessing operations,
*(b)* data contracts as first-class language statements,
*(c)* IR-level preprocessing optimisation (column pruning), and
*(d)* full compiler-stage visualisation — implemented as one small, complete,
individually built compiler.

## 5.3 Technology stack

| Layer | Choice | Justification |
|---|---|---|
| Implementation language | **Python 3.10+** | Fast development, dataclasses model AST nodes cleanly, `csv` in the standard library, permitted by the instruction manual |
| Lexer | **Hand-written DFA scanner** | The lab is about understanding the phase; a hand-written scanner makes the automaton explicit and explainable in the viva |
| Parser | **Hand-written recursive descent (LL(1))** | One function per non-terminal maps 1:1 onto the BNF; no generator black box; full control of error recovery |
| Schema inference | **`csv` module (standard library)** | Keeps the *front end* dependency-free; Pandas is needed only to execute the generated code |
| Execution backend | **Pandas** | The generated target code is ordinary Pandas, so the output is useful outside the project |
| Visualisation | **Tkinter** (standard library) | No installation required for the demo machine; each compiler stage gets one tab |
| Testing | **pytest** | Concise assertions; one test module per compiler phase |
| Version control | **Git** | Development log and phase-wise history |

### Deliberately *not* used

* **Lex/Flex/Yacc/Bison/ANTLR** — permitted by the manual, but a generated
  parser would hide exactly the logic the project is meant to demonstrate. The
  grammar is small enough (34 productions) that hand-writing it is both
  feasible and more instructive.
* **LLVM** — the target language is Python source, not machine code.

## 5.4 Development environment

```
OS               : Windows 11
Python           : 3.10 or newer   (developed on 3.14)
Editor           : Visual Studio Code
Required package : pandas   (execution backend only, Review 3)
Test runner      : pytest
Version control  : Git
```

Installation for Review 1 (no third-party package is needed):

```bash
cd Review-1
python main.py examples/demo_valid.prep
python -m pytest -q
```

## 5.5 References

1. A. V. Aho, M. S. Lam, R. Sethi, J. D. Ullman. *Compilers: Principles,
   Techniques and Tools* (2nd ed.), Pearson, 2007. — Chapters 3 (lexical
   analysis), 4 (syntax analysis), 6 (intermediate code), 8–9 (code generation
   and optimisation).
2. K. D. Cooper, L. Torczon. *Engineering a Compiler* (2nd ed.), Morgan
   Kaufmann, 2011.
3. T. Parr. *Language Implementation Patterns*, Pragmatic Bookshelf, 2010.
4. R. Nystrom. *Crafting Interpreters*, Genever Benning, 2021 — recursive
   descent and panic-mode recovery.
5. M. Fowler. *Domain-Specific Languages*, Addison-Wesley, 2010.
6. W. McKinney. "Data Structures for Statistical Computing in Python",
   *Proc. 9th Python in Science Conference*, 2010 (Pandas).
7. M. Armbrust et al. "Spark SQL: Relational Data Processing in Spark",
   *ACM SIGMOD*, 2015 — the Catalyst optimiser.
8. Pandas documentation — https://pandas.pydata.org/docs/
9. Python `csv` module documentation — https://docs.python.org/3/library/csv.html
10. Great Expectations documentation — https://docs.greatexpectations.io/
