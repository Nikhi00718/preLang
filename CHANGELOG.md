# Changelog

## 2.0.0 (unreleased)

### Added
- Speculative removal with per-removal certificates and selective deoptimization: a compiled program
  admitted on a new input runs again exactly the removed checks and operations whose item
  certificates that input breaks.
- Composition of compiled stages.
- Private compilation (`--queries`, `--facts`): compiling against facts the data owner releases with
  differential privacy (`tools/release_facts.py`), never deciding admission from a released fact.
- `PARSE_DATE` and `DERIVE` (dates from ISO text, derived columns with interval arithmetic and
  calendar parts); the lifter `frontend/pandas2prep.py` lifts them too.
- Lean 4 mechanization of the core analysis (`proofs/`).
- Continuous integration on Ubuntu (GCC) and Windows (MSVC); a Dockerfile.
- `eval/compilation/differential.py`: random programs compared with `--O0` and a reference
  interpreter; `eval/compilation/census.py`: work removed, run time and overhead on lifted programs.

### Fixed
- Column pruning kept a `DROP` of a column that a pruned `SELECT` before it no longer had, so the
  optimized program failed with `KeyError`.
- With pandas 3, a `FILTER` combining a text comparison with another condition failed with
  `TypeError` on data with missing values (string comparisons are `bool[pyarrow]`).
- `FILL_NULL ... WHERE NULLABLE` in a certificate, and path-keyed tables in the release tool.
- GCC warnings in `dateText` and in the private-compilation test.

## 1.0.0 (2026-10-08)

First release: the compiler from `.prep` to pandas and OpenCV, compile-time reading of CSV, Parquet
footers and image headers (CSV in parallel), contracts proven or refuted before anything runs,
three optimization passes, data certificates for reusing a compiled program on another input,
`DATETIME` columns from Parquet, the pandas lifter `frontend/pandas2prep.py`,
`--explain`, `--lineage`, `--dry-run`, run manifests, and the evaluation in `eval/`.
