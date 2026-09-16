# PrepLang — C++ Compiler Project

PrepLang is a small, human-readable language for describing data-preprocessing
steps. The project is split into three review milestones and uses only C++17
and the standard library.

```text
Review 1: Lexer → Parser → Parse Tree → Error Handling
Review 2: AST → Symbol Table → Semantic Analysis → IR
Review 3: Optimizer → C++-style Code Generator → CSV Executor
```

The implementation follows the possible compiler components in the supplied
course material: lexical analysis, syntax analysis, AST construction, semantic
analysis, symbol-table management, intermediate code, execution, and error
handling.

## Build everything

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

This creates `preplang_r1`, `preplang_r2`, and `preplang_r3` in the build
folder. On Windows they have an `.exe` suffix.

## Quick demonstrations

```bash
build/Release/preplang_r1 Review-1/examples/demo_valid.prep
build/Release/preplang_r1 --demo

build/Release/preplang_r2 Review-2/examples/front_end.prep
build/Release/preplang_r2 Review-2/examples/semantic_error.prep --no-tokens

build/Release/preplang_r3 Review-3/examples/full_pipeline.prep --run
```

The full teacher script is in [`TEACHER_DEMO.md`](TEACHER_DEMO.md).

## Repository layout

| Location | Purpose |
|---|---|
| [`src/`](src/) | Separate C++ modules shared by the three milestones |
| [`Review-1/`](Review-1/) | Lexer, parser, parse tree and first-review demo |
| [`Review-2/`](Review-2/) | AST, symbols, semantic checks and IR demo |
| [`Review-3/`](Review-3/) | Optimization, generated plan and execution demo |
| [`PLAN.md`](PLAN.md) | Component-to-review plan based on the course screenshot |
| [`PrepLang_Project_Idea.md`](PrepLang_Project_Idea.md) | Original long-term project idea |

Each review has its own `main.cpp`, examples and README. The shared modules
keep the behaviour in one place, which makes the code easier to explain and
maintain.
