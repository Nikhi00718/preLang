# PrepLang C++ Review Plan

This is the simple implementation plan for the project. Each review adds one
compiler idea and leaves the previous idea visible in the output.

| Course component | Review | C++ module |
|---|---:|---|
| Token recognition and classification | 1 | `src/Token.h`, `src/Lexer.cpp` |
| Keyword and identifier recognition | 1 | `src/Lexer.cpp` |
| Lexical error detection | 1 | `src/Diagnostics.cpp`, `src/Lexer.cpp` |
| Grammar processing and syntax validation | 1 | `src/Parser.cpp` |
| Parse-tree generation and visualization | 1 | `src/ParseTree.cpp` |
| AST construction and traversal | 2 | `src/Ast.cpp` |
| Symbol insertion and lookup | 2 | `src/SymbolTable.cpp` |
| Type and declaration checking | 2 | `src/SemanticAnalyzer.cpp` |
| Three-address intermediate code | 2 | `src/IR.cpp` |
| Simple optimization | 3 | `src/Optimizer.cpp` |
| C++-style target plan | 3 | `src/CodeGenerator.cpp` |
| Interpreter/execution engine | 3 | `src/Executor.cpp` |
| Lexical, syntax, semantic and runtime errors | 1–3 | `src/Diagnostics.cpp` |

## Review boundaries

### Review 1

Input is a `.prep` source file. The lexer creates tokens, the recursive-descent
parser checks the grammar, and the parse tree is printed. No AST or data
processing is needed yet.

### Review 2

The same source is converted into an AST. `EXPECT` statements populate the
symbol table with dataset columns and types. The semantic analyzer catches
mistakes such as normalizing a STRING column. Valid statements become a small
three-address IR.

### Review 3

The optimizer removes an obvious repeated operation. The code generator prints
the equivalent C++-style plan, and the executor performs the plan on a small
CSV file using the C++ standard library.

The code is intentionally not a large industrial compiler. Every module has a
small interface and a single job, so the implementation can be explained in a
viva without hiding the important logic behind a framework.
