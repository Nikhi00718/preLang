# PrepLang — Review 1

Review 1 is the small, explainable C++ front end. It implements the two first
compiler phases and a parse-tree display:

1. **Lexical analysis**: characters become tokens.
2. **Syntax analysis**: the LL(1) parser checks the grammar.
3. **Parse tree**: valid statements are printed as a tree.
4. **Error handling**: lexical and syntax errors include a line and column.

Review 1 does not build a full AST, check dataset types, generate IR, or run
CSV operations. Those pieces are added in Review 2 and Review 3.

## Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

The target is `build/Release/preplang_r1.exe` with the Visual Studio generator
or `build/preplang_r1` with a single-configuration generator.

## Demonstrate

```bash
build/preplang_r1 Review-1/examples/demo_valid.prep
build/preplang_r1 Review-1/examples/demo_lexical_error.prep --no-tokens
build/preplang_r1 Review-1/examples/demo_syntax_error.prep --no-tokens
build/preplang_r1 --demo
```

When explaining the code, open the files in this order:

```text
src/Token.h        what a token stores
src/Lexer.cpp      how characters are grouped
src/Parser.cpp     how grammar rules are checked
src/ParseTree.cpp  how the parse tree is printed
src/Diagnostics.cpp how errors are shown
Review-1/main.cpp  how the phases are called
```
