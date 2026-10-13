# Proofs

Lean 4 (v4.34.1, core library only, no Mathlib). Build with `lake build` in this folder.

- `Deopt.lean`: selective deoptimization is exact on every admitted input (`deopt_correct`), for any
  analysis that is monotone and sound; a removal whose item certificate contains the previous stage's
  guarantee is never deoptimized (`compose_never_deopts`); the greedy certificate search ends only on
  certificates the re-analysis accepted, and only weakens (`greedy_verified`, `greedy_weakens`).
- `Core.lean`: the analysis of a core of PrepLang (numeric columns abstracted by missing values and an
  interval; FILL_NULL, FILTER with >= and <=, DROP_NULL, DERIVE of a sum, BETWEEN and NOT_NULL
  contracts). It proves the transfer functions sound, every no-effect proof valid, the analysis
  monotone, and the run-time check of the generated code sound and complete for a certificate; from
  these, exactness (`core_deopt_exact`) and composition (`core_compose`) end to end.
- `Cells.lean`: for programs of the core without DERIVE, what the analysis proves depends on the data
  only through which cells, cut at the program's numbers, hold a row: whether a column has a missing
  value, has a value, and for each number k a value below k and a value above k (`cells_decide`,
  `provesAt_stateEq`). This is what a private compilation releases (paper, Section 7).

The theorems use only the axioms `propext` and `Quot.sound` (no classical choice); the build prints
them. The C++ compiler is tested against these properties (`tests/test_certificate.cpp`), not proven.
