# Changelog

## 2.1.0 (2026-10-24)

### Added
- A numeric column may change between INTEGER and FLOAT from one batch to the next. The certificate
  marks a column `NUMBER` when compiling the program with the column's other numeric type gives the
  same generated program, certificates included (`certify/NumberKinds.h`); the program then reads
  the column with the type the batch has, as a fresh compilation of that batch would. Only columns
  that no instruction combines with another column (DERIVE, JOIN, AGGREGATE, REFERENCES) are tried.
  On the real monthly data this removes the refusals of months whose pressure or temperature
  column switched between whole and decimal numbers.
- `JOIN t WITH u ON key MANY;`: a left join with every matching row of `u`; rows of `t` repeat once
  per match. Ranges and missing values of `t` still hold; `UNIQUE` and `REFERENCES` on `t` are
  then checked when the program runs.
- `AGGREGATE` items `MEDIAN`, `STD` (sample standard deviation, bounded by (max - min)/sqrt(2)) and
  `COUNT_DISTINCT`.
- The lifter (`frontend/pandas2prep.py`) follows reads with a path built from strings (`+`,
  f-strings, `os.path.join`, pathlib's `/`), the options `sep=","`, `header=0`, `encoding="utf-8"`,
  `low_memory` and `engine`, `parse_dates` (as `PARSE_DATE`) and `index_col="name"` (as `DROP`),
  reads in a function that returns the file or the cleaned DataFrame, `main()`, `try` and `with`.
  Tested in `tests/test_lifter.py`.
- `PLOT t HISTOGRAM | BAR | SCATTER | LINE ... TO "file.png";` writes a chart with matplotlib (`.png`,
  `.svg` or `.pdf`); a histogram shows the bounds of earlier `BETWEEN` contracts on its column.
  `REPORT t TO "profile.html";` writes a web page with every column as the run finds it next to
  what the compiler knew at that line, the contracts and their verdicts, which inputs were the
  compiled files, and the removed checks that ran again. Columns, their types and the file kind are
  checked at compile time; the plotted columns are kept by column pruning. `examples/tabular/charts.prep`.
- The differential test generates `MANY` joins and the new aggregates, and gives one numeric
  column of the second batch the other numeric type in 3 of 10 programs.

## 2.0.0 (2026-10-23)

### Added
- `JOIN t WITH u ON key` (a lookup join) and `AGGREGATE t BY [columns]` with `COUNT`, `SUM`, `MEAN`,
  `MIN` and `MAX`: key types, columns in both tables and repeated lookup keys are compile errors (a
  repeated key is checked again on new batches), and the new columns keep their types and ranges;
  column pruning reaches through a `JOIN` to the lookup table's `LOAD`.
- Speculative removal with per-removal certificates and selective deoptimization: a compiled program
  admitted on a new input runs again exactly the removed checks and operations whose item
  certificates that input breaks.
- Composition of compiled stages.
- Private compilation (`--queries`, `--facts`): compiling against facts the data owner releases with
  differential privacy (`tools/release_facts.py`), never deciding admission from a released fact.
- `PARSE_DATE` and `DERIVE` (dates from ISO text, derived columns with interval arithmetic and
  calendar parts); the lifter `frontend/pandas2prep.py` lifts them too.
- Lean 4 mechanization of the core analysis (`proofs/`).
- Continuous integration on Ubuntu (GCC), Windows (MSVC) and macOS (GCC); a Dockerfile.
- CI also builds with AddressSanitizer and UndefinedBehaviorSanitizer, measures coverage (at least 85%
  of the lines of `src/`), checks `clang-format` and `clang-tidy`, fuzzes the `.prep` front end, the
  CSV scanner and the Parquet footer reader with libFuzzer (`fuzz/`), builds the Docker image and
  checks the Lean proofs. Without libFuzzer the fuzz targets replay their corpus as tests.
- `tests/test_examples.py`: every example compiled end to end, as a `ctest` test.
- `preplang --version`; the MIT licence; a release workflow that publishes prebuilt compilers for
  Linux, Windows and macOS and a Docker image for every version tag.
- `LANGUAGE.md`: the complete language and command-line reference.
- `--json`: the diagnostics as one JSON object, for editors and other tools; writes nothing.
- `editors/vscode/`: a VS Code extension with syntax highlighting and the compiler's diagnostics on save.
- `pip install`: builds the compiler with scikit-build-core and installs `preplang`, `preplang-lift`,
  `preplang-contracts` and `preplang-release-facts`; releases also carry a wheel per platform.
- `frontend/pandas2prep.py` takes its arguments with argparse (`--help` printed a traceback).
- `eval/reproduce.sh` (the evaluation in three tiers), `eval/corpora.py` and `eval/corpora/` (the
  GitHub script corpora frozen as evaluated, and checksums of the downloaded inputs).
- `eval/compilation/differential.py`: random programs, with joins and groups, compared with `--O0`
  and a reference interpreter, and reused on a second batch; `eval/compilation/census.py`: work removed, run time and overhead on lifted programs.

### Fixed
- Column pruning kept a `DROP` of a column that a pruned `SELECT` before it no longer had, so the
  optimized program failed with `KeyError`.
- With pandas 3, a `FILTER` combining a text comparison with another condition failed with
  `TypeError` on data with missing values (string comparisons are `bool[pyarrow]`).
- `FILL_NULL ... WHERE NULLABLE` in a certificate, and path-keyed tables in the release tool.
- GCC warnings in `dateText` and in the private-compilation test.
- Dead store elimination removed a `FILL_NULL ... WITH MEAN` (or `MEDIAN`, `MODE`) of a column never
  used again, but that fill stops the program when the column has missing values and none to compute
  the statistic from; the optimized program then wrote a table where the source program stops. Found by
  the differential test with a new seed (1 of 2,000 programs); such a fill is now kept.
- The CSV scanner read one byte past the string literal `"true"` when a 5-byte cell started with `true`
  followed by a NUL byte (AddressSanitizer: global-buffer-overflow). Found by the CSV fuzzer in CI; the
  input is now in `fuzz/corpus/csv/` and `tests/test_scanner.cpp`.
- `eval/paper_numbers.py` read the Kaggle Titanic file and the real monthly batches, and found the
  test programs only in a Visual Studio build, so the paper's numbers could not be regenerated from
  `eval/results/` alone or on Linux and macOS; the input sizes are kept in `results/inputs.json`.
- The differential-privacy and validator baselines looked for their Python environments only in the
  Windows layout (`Scripts/python.exe`).

## 1.0.0 (2026-10-08)

First release: the compiler from `.prep` to pandas and OpenCV, compile-time reading of CSV, Parquet
footers and image headers (CSV in parallel), contracts proven or refuted before anything runs,
three optimization passes, data certificates for reusing a compiled program on another input,
`DATETIME` columns from Parquet, the pandas lifter `frontend/pandas2prep.py`,
`--explain`, `--lineage`, `--dry-run`, run manifests, and the evaluation in `eval/`.
