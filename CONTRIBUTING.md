# Contributing

Bug reports, test cases and fixes are welcome. Please open an issue first for a new statement or
contract, so the language stays small.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows use the Visual Studio generator (`-G "Visual Studio 17 2022"`); on Linux and macOS any
C++17 compiler works (GCC 15 and MSVC 19.44 are tested in CI). The generated programs and
`tools/release_facts.py` need Python 3.10+ with `numpy`, `pandas` (2.0 or newer, 3.x tested),
`pyarrow` and `opencv-python`. `docker build -t preplang .` gives the same environment.

## Before sending a change

1. Every change comes with a test in `tests/` that fails without it.
2. The build has no warnings (`/W4` on MSVC, `-Wall -Wextra -pedantic` on GCC and Clang).
3. A change to the optimizer or a backend must keep the output the same. Run the differential test,
   which compares the optimized program with `--O0` and with a reference interpreter on random
   programs:
   ```bash
   python eval/compilation/differential.py --programs 600
   ```
   The result must say that both agree on every program.
4. Keep code explainable: one phase per folder, a comment at the top of each header saying what it
   does, no unused code.
5. Numbers in the paper come from `eval/results/` through `eval/paper_numbers.py`; never edit them
   by hand.

## Reporting a bug

Include the `.prep` program, a small input file that shows the problem, the compiler output, and the
Python and pandas versions if the generated program misbehaves.
