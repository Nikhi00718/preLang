# PrepLang — Review 3

Review 3 completes the small C++ compiler pipeline:

- `Optimizer`: removes a repeated `DROP_NULL` instruction;
- `CodeGenerator`: prints the equivalent C++-style execution plan; and
- `Executor`: loads, checks, transforms and saves a small CSV table.

## Build and run

```bash
cmake -S . -B build
cmake --build build
build/Release/preplang_r3 Review-3/examples/full_pipeline.prep --run
```

The duplicate `DROP_NULL` statements are intentional. The optimizer removes
one and explains why. The executor then writes
`Review-3/examples/cleaned_students.csv`.
