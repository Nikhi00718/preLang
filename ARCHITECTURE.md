# Architecture

PrepLang is one C++17 library (`preplang_core`) shared by the compiler executable (`src/main.cpp`)
and the unit tests. A program goes through the phases below in order; each phase is a folder under
`src/`, and each header starts with a comment that says what it does.

```text
.prep source
  -> lexer/      tokens                    Lexer: hand-written scanner, bad characters reported and skipped
  -> parser/     AST (ast/)                Parser: LL(1) recursive descent, one method per grammar rule,
                                           panic-mode recovery at statement keywords
  -> schema/     facts about the inputs    SchemaProvider reads the data at compile time:
                                             CsvScanner    every CSV cell, in parallel (types, missing values, ranges, dates)
                                             ParquetFooter the footer only (schema and per-column statistics)
                                             ImageSchema   image file headers (no pixels decoded)
                                             ReleasedFacts private compilation: facts released by the data owner
  -> sema/       abstract dataset state    SemanticAnalyzer follows the facts through every statement;
                                             SymbolTable        tables, columns and image sets in scope
                                             SelectorResolver   company.*.age, WHERE TYPE / NULLABLE, EXCEPT
                                             ContractAnalyzer   EXPECT: proven, violated, or checked at run time
  -> dataset/    LINK and lineage          DatasetLinker (images with label rows), LineageTracker (--lineage)
  -> ir/         flat IR                   IRBuilder: one instruction per resolved target; FILTER conditions
                                           as three-address code
  -> optimizer/  optimized IR              PassManager runs, in order:
                                             RedundantOpPass   operations proven to change nothing
                                                               (with --approx also RESIZE fusion)
                                             DeadOpPass        unused temporaries, dead image operations
                                             ColumnPruningPass backward liveness over columns: dead stores, LOAD pruning
                                           (--O0 runs no pass)
  -> certify/    data certificate          Certifier: the facts each proof and each removal needs, so the
                                           compiled program can be reused on another input (admission and
                                           selective deoptimization)
  -> backend/    Python                    ExecutionPlanner: table instructions to PandasCodegen,
                                           image instructions to OpenCVCodegen (one streaming pass per image)
  -> explain/    reports                   --explain, --lineage, and the run manifest the program writes
```

`common/` holds what every phase uses: `Diagnostics` (errors and warnings with source positions),
`SourceManager` (the program text for error messages) and `Dates` (ISO dates as seconds).

## Where to change what

| To add | Change |
|---|---|
| a statement | `lexer/Token.h` (keyword), `parser/Parser.cpp` (rule), `ast/AST.h` (node), `sema/SemanticAnalyzer.cpp` (its effect on the facts), `ir/IRBuilder.cpp`, `backend/PandasCodegen.cpp` or `OpenCVCodegen.cpp`, the grammar in `README.md`, a test |
| a contract | `sema/ContractAnalyzer.cpp` (prove or refute it), `certify/Certifier.cpp` (the facts its proof needs), the run-time check in the backend |
| an optimization | a pass in `optimizer/`, registered in `PassManager.cpp`; it must keep the output the same, which `eval/compilation/differential.py` checks against `--O0` and a reference interpreter |
| an input format | `schema/SchemaProvider.cpp` and the reader in the backend |

## Tests

`tests/test_<phase>.cpp`, one executable per phase, registered with CTest in `CMakeLists.txt`;
`tests/test_release_facts.py` tests the data owner's release tool (`tools/release_facts.py`). The
evaluation in `eval/` exercises the whole compiler; see `eval/README.md`.

## Other folders

| Folder | What it holds |
|---|---|
| `frontend/` | `pandas2prep.py`: lifts the cleaning steps of a pandas script into a `.prep` program |
| `tools/` | `release_facts.py`: the data owner's side of private compilation |
| `proofs/` | Lean 4 mechanization of the core analysis (soundness, deoptimization, composition) |
| `studio/` | a browser editor for `.prep` programs |
| `demo/` | the scripted demonstration |
| `paper/` | the papers and the scripts that build them |
