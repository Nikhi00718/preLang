# Review 1 — Presentation and Viva Guide

Review 1 demonstrates the problem, language design, grammar, and the first two
compiler phases. The implementation is a small C++17 program in `main.cpp`.

## What to show

| Criterion | What to show |
|---|---|
| Topic relevance | PrepLang turns data-preprocessing instructions into a real compiler example. |
| Problem statement | A normal data script may discover a wrong column or type only while running. |
| Language design | The statements and contracts listed in `docs/02_language_specification.md`. |
| Grammar | The BNF, FIRST/FOLLOW sets and LL(1) argument in `docs/03_grammar.md`. |
| Architecture | The compiler pipeline in `docs/04_system_architecture.md`. |
| Prototype | The valid example, both error examples, and the `--demo` run. |

## Suggested demo

```bash
cd Review-1
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp -o preplang
./preplang examples/demo_valid.prep
./preplang examples/demo_lexical_error.prep --no-tokens
./preplang examples/demo_syntax_error.prep --no-tokens
./preplang --demo
```

On Windows with Visual C++:

```bat
cl /std:c++17 /EHsc /W4 main.cpp /Fe:preplang.exe
preplang.exe examples/demo_valid.prep
```

## Important viva points

**What does the lexer do?**

`Lexer::tokenize()` reads one character at a time, uses maximal munch, and
creates tokens with a type, value, line and column. It continues after a bad
character and records a lexical diagnostic.

**How are `students.age` and `3.14` distinguished?**

A dot is part of a number only when a digit follows it. Otherwise it becomes a
separate `DOT` token, so a column reference stays unambiguous.

**Why is `DROP_NULL` one keyword?**

The lexer consumes the complete identifier before checking the keyword table.
This is the maximal-munch rule.

**Is the grammar LL(1)?**

Yes. Each statement starts with a different keyword, each expectation starts
with a different keyword, and the grammar has no left recursion. One token of
lookahead is enough to choose a production.

**What is panic-mode recovery?**

`Parser::synchronize()` skips tokens until it reaches a semicolon or the start
of another statement. The parser can therefore report multiple independent
errors in one run.

**Which phases are implemented now?**

Only lexical analysis and syntax analysis. The syntax phase is a recogniser: it
records a compact statement summary instead of building an AST. AST creation,
semantic analysis, data processing and code generation are outside Review 1.

**Why C++?**

C++17 makes the compiler structure visible without third-party packages. The
lexer, parser and diagnostics use only simple structs, vectors and strings, so
the code is easy to compile and explain.
