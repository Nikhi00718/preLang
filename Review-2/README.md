# PrepLang — Review 2

Review 2 extends the Review-1 front end with the remaining compile-time
structures:

- `Ast.h/.cpp`: a small abstract syntax tree for each statement;
- `SymbolTable.h/.cpp`: loaded datasets and declared column types;
- `SemanticAnalyzer.h/.cpp`: declaration, scope and type checks; and
- `IR.h/.cpp`: a simple three-address intermediate representation.

The code still uses only C++17 and the standard library.

## Build and run

```bash
cmake -S . -B build
cmake --build build
build/Release/preplang_r2 Review-2/examples/front_end.prep
build/Release/preplang_r2 Review-2/examples/semantic_error.prep --no-tokens
```

The first program shows the AST, symbol table and IR. The second proves that
`NORMALIZE students.name` is rejected because `name` was declared as STRING.
