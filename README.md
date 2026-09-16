# PrepLang

PrepLang is a small domain-specific language for describing data-preprocessing
steps. The current project scope is **Review 1 only**: a simple C++17 lexer and
LL(1) syntax recogniser.

## Current Review-1 pipeline

```text
PrepLang source (.prep)
        ↓
Lexer: characters → tokens
        ↓
Parser: tokens → recognised statements
        ↓
Diagnostics: lexical and syntax errors with line/column locations
```

Example:

```text
LOAD "students.csv" AS students;
EXPECT students.age NUMBER;
NORMALIZE students.age;
FILTER students WHERE marks >= 50;
SAVE students AS "cleaned_students.csv";
```

## Run Review 1

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
cd Review-1
cl /std:c++17 /EHsc /W4 main.cpp /Fe:preplang.exe
preplang.exe examples/demo_valid.prep
```

Review 1 is intentionally small. It does not build an AST, read CSV data,
perform semantic/type checking, optimise code, or generate target code yet.

## Repository layout

| Folder | Purpose |
|---|---|
| [`Review-1/`](Review-1/) | Active C++ implementation, examples and first-review documents |
| `Review-2/` | Later-phase reference material kept for future work |
| `Review-3/` | Later-phase reference material kept for future work |
| [`PrepLang_Project_Idea.md`](PrepLang_Project_Idea.md) | Original project idea and long-term plan |

See [`Review-1/README.md`](Review-1/README.md) and
[`Review-1/REVIEW1_GUIDE.md`](Review-1/REVIEW1_GUIDE.md) for the full first-review
scope and presentation steps.
