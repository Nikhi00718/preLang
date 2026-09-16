# PrepLang — Review 1 in C++

PrepLang is a small language for writing data-preprocessing instructions. For
the first review, the project contains only the two front-end phases required
to demonstrate the language:

1. **Lexical analysis** — source text is converted into tokens.
2. **Syntax analysis** — a simple LL(1) recursive-descent parser checks the
   grammar and reports errors.

The parser recognises statements; it does not build an AST or process CSV data
yet. Those features belong to later reviews and are intentionally out of this
small first-review program.

## Files

```text
Review-1/
├── README.md                   this file
├── main.cpp                    lexer, parser, diagnostics and CLI
├── examples/                   small .prep programs and CSV examples
├── docs/                       language, grammar and architecture notes
├── REVIEW1_GUIDE.md            presentation and viva guide
└── PrepLang_Review1_Report.docx
```

## Build

The program uses only the C++17 standard library.

```bash
cd Review-1
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp -o preplang
```

On Windows with Visual C++:

```bat
cd Review-1
cl /std:c++17 /EHsc /W4 main.cpp /Fe:preplang.exe
```

## Run

```bash
# valid program: tokens + recognised statements
./preplang examples/demo_valid.prep

# show only diagnostics
./preplang examples/demo_lexical_error.prep --no-tokens
./preplang examples/demo_syntax_error.prep --no-tokens

# compile a short program directly
./preplang -c "NORMALIZE students.age;"

# run all bundled examples
./preplang --demo
```

On Windows, use `preplang.exe` instead of `./preplang`.

## What the first review demonstrates

- case-insensitive keywords such as `load` and `LOAD`;
- identifiers, numbers, strings, booleans and relational operators;
- comments beginning with `#` or `--`;
- line and column tracking for every token;
- the nine statement forms in the grammar;
- precise lexical and syntax diagnostics; and
- panic-mode recovery, so later statements are still checked after an error.

The implementation is deliberately kept in one readable C++ file so each
compiler step can be explained easily during the review.
