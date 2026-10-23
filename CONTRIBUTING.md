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
C++17 compiler works (CI tests MSVC on Windows and GCC on Ubuntu and macOS). The generated programs and
`tools/release_facts.py` need Python 3 (3.12 tested) with `numpy`, `pandas` (2.0 or newer, 3.x tested),
`pyarrow` and `opencv-python`. `docker build -t preplang .` gives the same environment.

`ctest` runs the unit tests, compiles every program in `examples/` end to end
(`tests/test_examples.py`), and replays the fuzz corpora (below).

## Checks CI runs (`.github/workflows/ci.yml`)

| Job | Run it locally (Linux or WSL) |
|---|---|
| Sanitizers | `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DPREPLANG_SANITIZE=ON && cmake --build build-asan && ctest --test-dir build-asan` |
| Coverage (at least 85% of `src/` lines) | `cmake -S . -B build-cov -DPREPLANG_COVERAGE=ON && cmake --build build-cov && ctest --test-dir build-cov && gcovr -r . --filter src/ build-cov` |
| Format | `pip install clang-format==19.1.7`, then `clang-format -i src/*.cpp src/*/*.cpp src/*/*.h tests/*.cpp fuzz/*.cpp` |
| Static analysis | `pip install clang-tidy==19.1.0`; configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then `clang-tidy -p build src/*/*.cpp` |
| Fuzzing | with Clang: `CXX=clang++ cmake -S . -B build-fuzz -DPREPLANG_FUZZ=ON && cmake --build build-fuzz --target fuzz_parser`, then `./build-fuzz/fuzz_parser fuzz/corpus/parser examples -max_total_time=60` (also `fuzz_csv`, `fuzz_parquet`) |
| Docker | `docker build -t preplang . && docker run --rm preplang` |
| Proofs | `cd proofs && lake build` |

A crash a fuzzer finds goes into `fuzz/corpus/<target>/` with the fix, so every build replays it.

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
5. The code is formatted with `.clang-format` and clean under `.clang-tidy`; hand-aligned tables
   may be kept between `// clang-format off` and `// clang-format on`.
6. Numbers in the paper come from `eval/results/` through `eval/paper_numbers.py`; never edit them
   by hand. `eval/reproduce.sh check` confirms they match the committed results.

## Reporting a bug

Include the `.prep` program, a small input file that shows the problem, the compiler output, and the
Python and pandas versions if the generated program misbehaves.
