# What is new in PrepLang

**Author:** Nikhil

This file checks whether PrepLang is original, compares it with the closest existing work, and
states exactly which parts are new. The literature search was done in September 2026. No search is
complete, so the claims below say "to our knowledge".

## Short answer

**Yes, there is novelty, but it is in *what* the compiler knows and *how* it uses it, not in the
general idea of catching data errors early.** Several older ideas exist separately:

- reading data at compile time (F# type providers, 2016);
- abstract interpretation of data frames (2022, 2026);
- compile-time schema contracts (2026);
- run-time data validation (TFDV, Deequ, Great Expectations, Pandera).

To our knowledge, no existing work does the following **together**:

1. It computes **value-level facts** from **every cell of the real input files and the headers of
   every image**, in one state, and carries them through each transformation.
2. It uses those facts to **prove, refute or defer data contracts at compile time**.
3. It **removes run-time checks safely**, because the generated program refuses to run on inputs
   that differ from the ones the proofs were made for (fingerprint guard).
4. It treats **tables and images in the same analysis**, so it finds image mistakes such as
   "float pixels saved as PNG" before any pixel is decoded.
5. It separates **optimizations that keep the output exact** from **ones that change it**; the
   second kind runs only with `--approx`.
6. It finds, for each compiled program, the **data certificate**: the few facts about the inputs
   that its proofs and removed operations really need, by analysing the program again under weaker
   facts. The generated program then accepts **new batches of data** that satisfy the certificate,
   without compiling again, and names the proof that a refused batch would break.

One-sentence version, for the paper or the viva:

> PrepLang is a compiler for data-preprocessing programs that reads the real input data (every CSV
> cell and every image header) at compile time, tracks an abstract state of types, missing values,
> value ranges and image properties through the pipeline, and uses it to prove or refute data
> contracts and to optimize the program. A fingerprint guard binds every proof to the exact input
> files.

## What is *not* new (and must be admitted)

| Idea | Who did it first | What PrepLang adds |
|---|---|---|
| Inferring types from the data at compile time | F# type providers (Petricek, Guerra, Syme, PLDI 2016) | Types only, from a sample, fixed at the start. PrepLang also tracks missing values and value ranges, updates them after every statement, and uses them for contracts and optimization. |
| Abstract interpretation of data frames | Zhuang & Lu (IEEE Access 2022): column types from code; Gerstl, Sihler & Tichy (arXiv, July 2026): shapes of R data frames, more precise when the datasets are read | These infer column names, types or shapes. They do not track missing values or value ranges, prove contracts, or handle images. |
| Checking data contracts before the pipeline runs | Mirji (arXiv, April 2026): compile-time contracts for Scala/Spark; Bauplan: schema contracts as Python types | Both check **structure** (column names and types) from **declared types**, not from the data. PrepLang proves **value** contracts (`BETWEEN`, `NOT_NULL`, `UNIQUE`, `REFERENCES`, `MATCH_RATE`) from the data. |
| Declarative data contracts | TFDV (Breck et al., MLSys 2019), Deequ (Schelter et al., VLDB 2018), Great Expectations, Pandera (SciPy 2020) | These check data **while the pipeline runs**, separately from the code that transforms it. PrepLang decides the contract at a specific point of the program, at compile time, and generates a check only when it cannot decide. |
| Inferring hidden assumptions of data scripts | Sihler, Pfrenger, Gerstl & Tichy (arXiv, July 2026) | They make assumptions explicit and check them at run time. PrepLang proves them before running. |
| Checking images for broken files and sizes | Many scripts and tools (e.g. PIL `verify()`, dataset checkers) | These are separate scripts that run on the data. They do not follow how later operations change the images, and do not check whether the output format can hold the pixels. |
| Fusing image operations | Halide (Ragan-Kelley et al., PLDI 2013) | Halide's fusion is far more advanced. PrepLang's streaming is simple; what is new is that its optimizations are justified by facts about the data (for example, `GRAYSCALE` is removed because every image already has 1 channel). |

**The closest work** is Gerstl, Sihler & Tichy (2026). Like PrepLang, it analyzes a data program
together with the datasets it reads. But it infers only **shape** (column names, row and column
counts) for R scripts. It does not infer types, missing values or value ranges, and it has no
contracts, images, check removal or optimization.

## Comparison with the closest work

| | Reads the real data at compile time | Value facts (missing values, ranges) | Follows facts through the pipeline | Contracts proven at compile time | Images | Removes run-time checks safely | Data-driven optimization |
|---|---|---|---|---|---|---|---|
| F# type providers (2016) | a sample | missing values only | no | no | no | – | no |
| Zhuang & Lu (2022) | no | no | yes (column types) | no | no | – | no |
| Gerstl, Sihler & Tichy (2026) | optional | no (shape only) | yes | no | no | – | no |
| Mirji (2026), Bauplan | no (declared types) | no | yes (schemas) | structure only | no | no | no |
| TFDV, Deequ, GX, Pandera | at run time | yes | no | no (checked at run time) | TFDV: some image statistics | – | no |
| **PrepLang** | **every cell + every image header** | **yes** | **yes** | **yes: PROVEN / VIOLATED / DEFERRED** | **yes** | **yes (fingerprint guard)** | **yes (exact vs. `--approx`)** |

## The novel parts in detail

### N1. One abstract state for tables and images, computed from the real data

- **What:** for each column: type, whether values can be missing, and an interval of the values,
  with a flag saying whether the interval is **exact** (both ends are real values). For each image:
  width, height, channels, broken or not; for the whole set: pixel type, pixel range, output
  format. For linked data: how many images and labels match.
- **Why it is new:** existing analyses infer either names, types or shapes from code, or statistics
  from data at run time. None keeps value ranges, missing values and image properties in one state
  that changes statement by statement.
- **Why the exact flag matters:** it is what allows a compile-time **VIOLATED** verdict. If the
  interval is exact and goes outside `BETWEEN a AND b`, a bad value really exists, so the compiler
  can name the row. If the interval is only an over-approximation (after a `FILTER`), the compiler
  must defer instead.
- **Code:** `src/sema/SymbolTable.h` (`ColumnState`, `ImageState`, `LinkState`),
  `src/schema/SchemaProvider.cpp` (`readPngHeader`, `readJpegHeader`: sizes without decoding).

### N2. Three-valued contract decision, with deferred contracts used as facts

- **What:** every `EXPECT` becomes **PROVEN** (no check generated), **VIOLATED** (compile error
  naming the exact rows or files) or **DEFERRED** (a check is generated). A deferred contract is
  also added to the state, because the code after the check only runs if it passed. This can prove
  later contracts.
- **Why it is new:** run-time tools always check; compile-time schema tools only prove structure.
  Deciding **value** contracts at a program point, and falling back to a generated check only when
  needed, is not in the works we found.
- **Code:** `src/sema/ContractAnalyzer.cpp` (`between`, `unique`, `references`, `imageContract`,
  `matchRate`).
- **Evidence:** 15 of the 16 contracts in the examples are decided at compile time (94%), see
  `eval/results/rq1.md`.

### N3. Fingerprint guard: proofs are tied to the exact input files

- **What:** the compiler stores a CRC-32 checksum of each CSV file and of each image folder
  listing. The generated program recomputes it and stops if an input changed after compiling.
- **Why it matters:** without the guard, removing the check of a proven contract would be unsafe,
  because someone could change the data after compiling. With it, every run that proceeds uses the
  inputs the proofs were made for.
- **Why it is new:** Mirji (2026) keeps a run-time schema check at the output for the same reason
  (data can drift after compiling). PrepLang instead guards the **inputs**, which is what lets it
  **delete** the checks. We did not find this in data-pipeline work.
- **Code:** `crc32` in `src/schema/SchemaProvider.cpp`; `read_csv_checked` in
  `src/backend/PandasCodegen.cpp`; `list_images_checked` in `src/backend/OpenCVCodegen.cpp`.

### N4. Image mistakes found at compile time, before decoding any pixel

- **What:** broken files, sizes and channel counts come from file headers only. The compiler then
  follows `RESIZE`, `GRAYSCALE`, `NORMALIZE`, `CONVERT` through the pipeline and checks the save:
  - FLOAT32 pixels cannot be saved as JPG or PNG (they come out black);
  - 4-channel images cannot become JPG;
  - 16-bit images cannot become JPG.

  The error names the statement that caused the problem.
- **Why it is new:** image checkers look at the input files only. They do not know what the
  pipeline will do to the images or whether the output format fits.
- **Code:** `saveImages` in `src/sema/SemanticAnalyzer.cpp`.
- **Evidence:** mutants I5 and L2 (float pixels saved as JPG/PNG). A normal script finishes with
  no error and writes black images; PrepLang stops at compile time.

### N5. Optimizations justified by data facts, with an exact/approximate boundary

- **What:** operations the state proves useless are removed. Examples: `FILL_NULL` on a column with
  no missing values, `GRAYSCALE` on images that are already 1 channel, `CONVERT` to the current
  format, the checks of proven contracts. Unread columns are not loaded, and image steps become one
  streaming loop. These never change the output. RESIZE fusion does change pixels, so it is only
  suggested unless `--approx` is given.
- **Why it is new:** usual compilers optimize using facts about the **code**. Here the facts come
  from the **data**, and the compiler says openly which rewrites are exact.
- **Code:** `src/optimizer/RedundantOpPass.cpp`, `src/optimizer/ColumnPruningPass.cpp`,
  `src/backend/OpenCVCodegen.cpp`.
- **Evidence:** a 32% shorter table-processing time, and memory for 1,000 images down from 922 MiB
  to 10 MiB. RESIZE fusion saves only 6% and changes pixels (PSNR 57 dB), which supports keeping it
  opt-in. See `eval/results/rq3.md`.

## Added after a critical review (September 2026)

A self-review rated the first version 4/10 as research: every mechanism had an ancestor, the
evaluation used our own mutants, and for CSV the analysis costs as much as running the pipeline.
These changes answer that:

- **N6. Metadata-only analysis.** For Parquet files the compiler reads only the footer (schema,
  row count, min/max, null counts, exactness flags), so compiling takes 0.02 s for 1 or 16
  million rows. The guard fingerprints the footer, which is exactly what the proofs use. Using
  such statistics (zone maps, Moerkotte 1998) to *prove data contracts* is, to our knowledge, new.
- **Soundness argument.** The paper defines the concretization of the state and proves transfer
  soundness, verdict soundness and check elimination. Writing it down found and fixed a real
  unsound case (a Parquet float column marked exact although its missing values were unknown).
- **Lineage stated honestly.** Proven/violated/deferred is hybrid type checking (Flanagan 2006)
  and soft typing (Cartwright & Fagan 1991) applied to data; both are cited.
- **Real bugs, not only our mutants.** 44 data-preparation failures from GitHub issues, selected by
  17 error messages with a fixed protocol: 18 (41%) are reported before any data is processed,
  21 of 21 reproductions behave as classified, and the other 23 are listed with the reason.
- **Scale.** On 16M rows and 3,000 images, mistakes are reported in 0.02-0.26 s instead of after
  1.8-9 s, and a silent black-image mistake is caught.

**Honest rating now: about 6/10 as research** - a solid workshop or new-ideas paper, still not a
top-venue paper (the language is small, one person classified the issues, and 44 issues are few).

## Added in Phase 3 (October 2026)

### N7. Data certificates by counterfactual re-analysis

- **Problem.** The fingerprint guard (N3) makes proofs safe, but only for the exact file. A real
  pipeline runs again tomorrow on a new file with the same layout, and the guard refuses it.
- **What.** After optimizing, the compiler analyses the program again many times. Each time it
  weakens one fact about one input column: missing values allowed, or a lower/upper bound moved to
  the loosest program constant that still works, or removed. A weaker fact is kept if every proven
  contract and every removed operation survives. The facts that cannot be weakened form the
  certificate, and each one records the proofs that need it (the proofs lost when it was dropped).
  Contracts proven by reading the values themselves (`UNIQUE`, `REFERENCES`, `MATCH_RATE`) keep
  their check in the generated code, guarded so it runs only when an input changed.
- **Why it is sound.** All transfer functions are monotone once exactness is cleared, and every
  decision is "the state is inside a fixed predicate". So an input whose facts are inside the
  certificate gets at least the compiled proofs (paper, section "Data Certificates"). Before widening,
  the compiler checks that the facts without exactness still give exactly the compiled proofs;
  if not, it accepts only the exact files.
- **Why it is new.** Proof-carrying code (Necula 1997) ships proofs with code; JIT compilers guard
  speculative code and deoptimize (Hölzle et al. 1992); progressive query optimization keeps
  validity ranges for a plan (Markl et al. 2004); precondition inference (Cousot et al. 2013) and
  DeepInfer (Ahmed et al. 2024) infer conditions on inputs; TFDV validates new batches against a
  schema inferred from old data. To our knowledge none derives, for a compiled data pipeline, the
  minimal input facts its *correctness proofs and removed operations* depend on, by re-running the
  abstract interpreter, and uses them as an admission check that explains every refusal.
- **Extended after real data (October 2026).** The certificate now covers every column of an input,
  also columns the optimized program never loads (a proven check on a column that pruning removes,
  or a removed `DROP` that names a column); columns that are only carried to the output may be
  missing (the Kaggle test file has no `Survived`); strict comparisons give strict bounds; and image
  folders are certified as one box per file extension (channels, size ranges, bit depths, broken
  files allowed or not), which the analysis checks through the box's corner images (paper, Lemma
  "Corners"). The generated program reads the header of every file of a new folder.
- **Code:** `src/certify/Certifier.cpp` (`certify`, `admissionProblems`),
  `src/schema/SchemaProvider.h` (`setOverrides`), `admit` in `src/backend/PandasCodegen.cpp`,
  `onlyIfInputChanged` in `src/optimizer/RedundantOpPass.cpp`.
- **Evidence.** `tests/test_certificate.cpp`: 400 random inputs; every admitted one, compiled on
  its own, keeps every proof. `eval/reuse/certificates.py` (RQ7): seven compiled programs were given 244 new batches (other days and a warmer season of a sensor process; resampled rows, drifting values, missing values and out-of-range values for the examples). The certificate admits 209 (86%); all facts of the compiled file, as a TFDV-style schema, admit 96 (39%); the fingerprint admits 0. All 209 admitted runs end exactly like a fresh compilation of that batch, and every refused batch also fails a fresh compilation. See `eval/results/certificates.md`.

### N8. A compile-time CSV analysis faster than reading the file with pandas

Not a new idea, but it removes the main cost argument against reading the data at compile time.
The file is read once and cut into one piece per core at line breaks; each thread splits cells in
place (`std::string_view`), types them and parses numbers in one pass (Clinger's exact fast path,
falling back to `std::from_chars`); the CRC-32 (slicing-by-8) runs on its own thread; pieces are
merged in file order, so the facts and the reported line numbers equal a sequential pass.
One million rows now take 0.13 s (2.04 s before): 4.7 times faster than `pandas.read_csv` reads the file, and about as fast as pandas with the multi-threaded pyarrow reader, while also inferring every type, range and missing value. Checked by `tests/test_scanner.cpp` (1 to 16 pieces, 20,000 random numbers against
`std::from_chars`) and by a differential test against the old reader (60 random files, 0
differences). Code: `src/schema/CsvScanner.cpp`, `src/schema/Checksum.cpp`.

### N9. Certificates for existing pandas scripts

`frontend/pandas2prep.py` lifts the longest prefix of a pandas script's cleaning that PrepLang
expresses exactly (fillna with constants or statistics, dropna, drop, selection, filters,
normalization written as arithmetic, asserts, to_csv), so that the script can be compiled against
its data and certified for new data. This is an engineering contribution that makes N7 apply to real
code; the lifting itself is a syntax-directed translation, not a new technique.

### Evidence on real data and real code

- **RQ8, real monthly data** (UCI traffic 2012-2018, UCI bike rentals 2011-2012, NYC green taxi
  Parquet files 2022-2024): 238 runs; certificates admit 205 (86%), the compiled month's own facts 2
  (1%), the fingerprint 0. All 205 admitted runs end exactly like a fresh compilation of that month;
  0 unsound. Refusals name real events: 0 K temperatures (sensor fault), a 9,831 mm rain reading
  and a 0% humidity day that the removed cleaning filter would have dropped, the NYC TLC schema
  change of February 2023 (`passenger_count` double -> int64), and one fare of $2,020 (a known
  limit: certificates bound input columns, not rows after a filter).
- **RQ9, real pandas scripts**: 372 Titanic scripts from GitHub code search; the first 100 were the
  development set, the other 272 were held out. 130 held-out scripts lift, 124 compile; on 1,364 runs
  over real new batches (the Kaggle test file, resamples, extra missing values) 1,347 are admitted and
  all 1,347 reproduce the script's own pandas result exactly; 0 differ. 7 refused batches also make
  the script fail; 10 are conservative (the compiler removed a `fillna` on `Fare` because the training
  file has no missing `Fare`).
- **Bugs the evaluation found in our own implementation** (all fixed, all with regression tests):
  the run-time MATCH_RATE check; pruned columns not covered; type relaxations that were unsound
  against fresh compilation; columns named only by a removed `DROP`; and a lifter that translated a
  `drop` pandas rejects. The last two were found on the held-out set; the first run's results are kept
  in `eval/results/pandas_corpus_firstrun.*`.

### Strict rating (October 2026)

Rated as a submission to a top venue (PLDI, OOPSLA, SIGMOD, VLDB, ICSE main track):

| Criterion | Score | Why |
|---|---:|---|
| Novelty of the idea | 6.5/10 | Certificates for a compiled data pipeline's proofs, found by counterfactual re-analysis and used as an explained admission check, are new to our knowledge. But each ingredient has a clear ancestor (abstract interpretation, sufficient/necessary precondition inference, proof-carrying code, guards with validity ranges), and the search is a simple greedy enumeration over a small lattice. |
| Technical depth | 5/10 | The soundness argument is a monotonicity argument written as proof sketches, not mechanized; the language is small (no joins, loops or functions); certificates are column-level intervals and nullability, not relational facts. |
| Evaluation | 6.5/10 | Real data (three public monthly datasets) and real code (272 held-out GitHub scripts) with ground truth and 0 unsound runs is solid. Weaknesses: one task for RQ9 (Titanic), short lifted prefixes (189 of 328 cleaning statements, 30 scripts lifted completely), a TFDV-style emulation rather than the real tools as baselines, one machine, one author. |
| Honesty and artifact | 8/10 | Every number is generated from the results; five bugs found by the evaluation are reported; dev/test split for the corpus. |

**Overall: about 6/10 as research.** Realistic outcome at a top venue's main track: weak reject or,
at best, borderline (reviewers would ask for a mechanized or more formal soundness proof, lifting of
real pipelines beyond short prefixes, more datasets and tasks, and comparison with the actual TFDV,
Great Expectations and Deequ constraint-suggestion tools). Realistic outcome at a good workshop or a
short-paper/demo track (e.g. SIGMOD DEEM, VLDB workshops, SOAP): likely accept. As a Compiler Design
course project, the innovation is far beyond what the rubric expects.

## Evidence summary (from `eval/`)

| Question | Result |
|---|---|
| Injected mistakes found before processing | 24 of 24, 0 false alarms |
| Contracts decided at compile time | 15 of 16 (94%) |
| Optimizations | table processing 37% faster; images 922 MiB -> 10 MiB |
| Analysis cost | CSV 4.7x faster than one pandas read; Parquet footer 0.02 s at any size; headers about 17x faster than decoding |
| Large inputs | mistakes reported in 0.02-0.26 s vs. 1.8-9 s for the scripts |
| Real GitHub failures | 18 of 44 (41%) reported before processing |
| Real monthly batches (RQ8) | certificate admits 205 of 238 (86%), file facts 2 (1%); 0 unsound |
| Real pandas scripts (RQ9, held out) | 1,347 of 1,364 runs admitted, all identical to the script; 0 unsound |
| New batches (RQ7) | certificate admits 209 of 244 (86%), file facts 96 (39%), fingerprint 0; 0 unsound, 0 needless refusals |
| CSV analysis, 1M rows | 0.13 s (was 2.04 s); pandas reads it in 0.62 s, pyarrow in 0.11 s |

## Sources

- T. Petricek, G. Guerra, D. Syme, "Types from data: Making structured data first-class citizens
  in F#", PLDI 2016. https://ar5iv.labs.arxiv.org/html/1605.02941 and
  https://fsprojects.github.io/FSharp.Data/library/CsvProvider.html
- Y. Y. Zhuang, M.-Y. Lu, "Enabling type checking on columns in data frame libraries by abstract
  interpretation", IEEE Access, 2022. https://ieeexplore.ieee.org/document/9691374/
- O. Gerstl, F. Sihler, M. Tichy, "Inferring the shape of data frames in R programs using abstract
  interpretation", arXiv:2607.03889, 2026. https://arxiv.org/abs/2607.03889
- F. Sihler, L. Pfrenger, O. Gerstl, M. Tichy, "Towards automatically inferring constraints to
  identify implicit assumptions in data analysis", arXiv:2607.03114, 2026.
  https://arxiv.org/abs/2607.03114
- V. Mirji, "Shift schema drift left: policy-aware compile-time contracts for typed JVM and Spark
  pipelines", arXiv:2604.16986, 2026. https://arxiv.org/abs/2604.16986
- Bauplan, "Data contracts as types: compile-time schema enforcement for Python data pipelines".
  https://bauplanlabs.com/agentic-resources/data-contracts-as-types-how-bauplan-brings-compile-time-schema-enforcement-to-data-pipelines
- E. Breck et al., "Data validation for machine learning", MLSys 2019.
  https://mlsys.org/Conferences/2019/doc/2019/167.pdf
- S. Schelter et al., "Automating large-scale data quality verification", PVLDB 11(12), 2018.
- N. Bantilan, "pandera: Statistical data validation of pandas dataframes", SciPy 2020.
- Great Expectations. https://greatexpectations.io
- J. Ragan-Kelley et al., "Halide", PLDI 2013.
- C. Flanagan, "Hybrid type checking", POPL 2006.
- R. Cartwright, M. Fagan, "Soft typing", PLDI 1991.
- G. Moerkotte, "Small materialized aggregates", VLDB 1998.
- G. C. Necula, "Proof-carrying code", POPL 1997.
- U. Hölzle, C. Chambers, D. Ungar, "Debugging optimized code with dynamic deoptimization", PLDI 1992.
- V. Markl et al., "Robust query processing through progressive optimization", SIGMOD 2004.
- P. Cousot, R. Cousot, M. Fähndrich, F. Logozzo, "Automatic inference of necessary preconditions",
  VMCAI 2013. https://cs.nyu.edu/~pcousot/publications.www/CousotCousotFahndrichLogozzo-VMCAI-LNCS-7737-pp128-148-Jan-2013.pdf
- S. Ahmed, H. Gao, H. Rajan, "Inferring data preconditions from deep learning models for
  trustworthy prediction in deployment", ICSE 2024. https://arxiv.org/abs/2401.14628
- W. D. Clinger, "How to read floating point numbers accurately", PLDI 1990.

## Added in October 2026: selective deoptimization, composition, and a stricter prior-art check

### What was added

- **N10. Selective deoptimization.** The optimizer removes proven-useless checks and operations
  *speculatively*: they stay in the generated program behind guards. The certifier finds a full
  certificate (every proof), an admission certificate (only what no run-time code can stand in for:
  types, contracts without a run-time check, no compile errors) and one item certificate per removal.
  A new batch outside admission is refused; otherwise each removal whose item certificate the batch
  breaks runs again. Theorem: on every admitted batch this computes exactly what the source program
  computes. Mechanized in Lean 4 (`proofs/Deopt.lean`, core only, axioms: `propext`).
- **N11. Composition.** Every compilation writes the guarantee of each table it saves (the analysis
  run on the admission certificate). `tools/contracts.py` checks it against the next stage's
  certificates (assume-guarantee), names the facts it does not imply with the `EXPECT` stage 1 would
  need, and synthesizes a producer contract from several consumers.
- **Evaluation against real validators:** pandera (`infer_schema`), Great Expectations 0.18
  (onboarding data assistant) and TFDV 1.21 (`infer_schema`), set up on the compiled file, on every
  column and on the columns read; a second real-script corpus (Kaggle House Prices).

### Prior work found in the October check (all now cited in the paper)

| Work | What it does | Difference |
|---|---|---|
| Urban, "What Programs Want" (arXiv 2020); Urban & Müller (ESOP 2018) | infers *necessary* conditions on the input data of Python programs by abstract interpretation; input data usage | certificates are *sufficient*, per compilation, found with the data at hand, and used for admission and deoptimization |
| Meijer, Sandahl, Varró, "Data-aware Static Analysis" (ICSE-NIER 2026) | loads a data sample during static analysis to check ML API contracts | a fault finder; no compilation, no removal, no reuse on new data |
| Tuplex (SIGMOD 2021); Schiavio et al. (PVLDB 2020) | speculative compilation of data pipelines on a sample, per-row or per-region fallback / deoptimization | speculation for speed; no proofs, no per-obligation certificates, no exactness theorem |
| Hölzle et al. (PLDI 1992); Flückiger et al. (POPL 2018) | deoptimization; correctness of speculative optimizations | general-purpose JITs; not data facts or batch-level admission |
| Markl et al., progressive optimization (SIGMOD 2004) | validity ranges of plan operators, re-optimization outside them | ranges on cardinalities for performance; item certificates are validity ranges for correctness obligations |
| Dillig et al., abductive inference (PLDI 2012); Miné (SCP 2014) | weakest facts that discharge a proof; sufficient preconditions by backward under-approximation | used for error diagnosis or C programs; here a forward analyzer is used as a black box over a finite constant-anchored lattice |
| Stulova et al. (SCP 2018); gradual verification (POPL 2024) | residual run-time checks after static analysis | a check is residual for every input; here only for batches that break its certificate |
| Abstraction-carrying code (LPAR 2004); Benveniste et al., contracts (2018); Avro schema compatibility | certificates checked by the consumer; assume-guarantee composition; writer/reader compatibility | both sides of the containment are derived from the stages' proofs |
| Auto-Validate (SIGMOD 2021), Auto-Validate-by-History (KDD 2023) | validation rules for recurring pipelines from data lakes or past runs | from the data, not from the program |

**Honest rating after these additions:** the combination (proof-derived per-removal certificates,
batch-level checks on cheap statistics, exact selective re-enabling with a mechanized theorem, and
composition of the resulting certificates) was not found in prior work; each ingredient has a clear
precedent, listed above. The implementation's monotonicity and soundness are tested, not proven.

## Added in October 2026 (second round): a mechanized core, dates and derived columns, other people's pipelines

What a reviewer at a top venue would raise, and what was done about it:

- **"The analysis itself is only tested."** `proofs/Core.lean` defines the analysis of a core of
  PrepLang (numeric columns abstracted by missing values and an interval; FILL_NULL, FILTER with >= and
  <=, DROP_NULL, DERIVE of a sum, BETWEEN and NOT_NULL contracts) and proves the transfer functions
  sound, every no-effect proof valid, the analysis monotone, the run-time check of the generated code
  sound and complete for a certificate, and from these the exactness and composition theorems end to
  end. Axioms: `propext` and `Quot.sound` only (no classical choice). The C++ implementation is still
  tested, not proven, for the full language.
- **"The language has no dates or computed columns."** `PARSE_DATE` turns a text column of ISO dates
  into DATETIME (the compiler reads the column's values only for such a statement, so no other
  program pays for it, and every earlier result is unchanged: the same re-analysis counts for all 11
  programs and the same randomized test results). `DERIVE t.c = e` computes a column with + - * / or
  YEAR/MONTH/DAY/HOUR/WEEKDAY of a date. New compile-time errors: a division by a value that can be 0,
  and text that is not a date (with its row). Certificates gain "this column holds dates" (admission)
  and date bounds widened to calendar boundaries (a FILTER on MONTH compiled on October data needs dates
  from 1 October to 31 December of one year, and deoptimizes in January).
- **"The pipelines of the real-data experiment are the authors' own."** GitHub scripts for the same UCI
  traffic and bike-sharing data, lifted with the second lifter version (traffic scripts as the
  development set, bike scripts held out), compiled on the first real month and run on every later
  month against the scripts' own pandas statements (`eval/reuse/drift_corpus.py`). Result: 72 scripts
  compile on the first month (80 DERIVE, 52 PARSE_DATE statements lifted); all 3,255 runs on later
  months end as the scripts' own statements do, none refused, none wrong. Only 4 programs have a
  removal that can run again; two traffic scripts filter impossible readings themselves (temp > 0,
  rain_1h < 500), and those removed filters run again in exactly the three months with 0 K or
  9,831 mm of rain, where the full certificate would refuse the month.

Still open: joins; a proof for the full C++ analysis; showing reuse saving time (compilation is cheap,
so the paper does not claim it).

## Added in October 2026 (third round): compiling against data you cannot see

### How the idea was chosen

Two strict reviewers (one as a PLDI/OOPSLA PC member, one as a SIGMOD/VLDB/MLSys PC member) rated
the project as it stood at **5/10 for novelty**, lower than the 6.5 above, and named three ancestors
that were not cited: semantic query optimization with soft constraints that hold in the current data
(Godfrey, Gryz and Zuzarte, SIGMOD 2001), BHUNT (Brown and Haas, VLDB 2003), and verified speculation
and deoptimization (Barrière et al., POPL 2021). All three are now cited. Eight candidate ideas were
checked against prior work; the scores were: robustness margins 4, library-upgrade safety 4, engine
divergence certificates 4 (6-8 strengthened), drift repair 4, row-level partitioned execution 3,
prediction-invariance certificates 7.5, retroactive edit certificates 7-7.5, and private compilation
6-8, the highest.

### N12. Private compilation

- **Problem.** A data-aware compiler leaks its data through what it writes: the generated program
  embeds the file's checksum (which confirms a guessed file), every removal states a fact (no missing
  `age`, no mark below 10), a violated contract names a row, the contract file records bounds. Yet in
  validation servers, OpenSAFELY and data clean rooms the analyst who writes the pipeline may not see
  the data.
- **Observation 1: guards are necessary.** The facts a data-aware compiler optimizes with are emptiness
  facts (no value is missing, none is below k). No differentially private release can certify one: an
  epsilon-DP mechanism says whatever it says on a clean table, with probability at least exp(-k epsilon)
  times as often, on a table that breaks the fact in k rows. One-sided noise, which keeps Shrinkwrap and
  SPECIAL exact when they drop padding tuples, cannot make a removal safe. A program optimized from
  released facts must check them on the data it runs on.
- **Observation 2: selective deoptimization provides those guards.** Its exactness theorem has no
  hypothesis about the facts the compiler starts from (in Lean, `deopt_correct` quantifies over arbitrary
  removals and certificates); a wrong fact costs a deoptimization, never a wrong result.
- **What.** The data owner runs `preplang --queries` (the schema and the numbers written in the program)
  and `tools/release_facts.py --schema-declared`. The numbers cut each column into cells (below k1,
  exactly k1, between k1 and k2, ..., missing); every decision of the analysis depends only on which cells
  hold some row. The tool releases one Laplace histogram per column over those cells (sensitivity 1), for
  the columns the program names only; each table gets the whole epsilon, split over its columns (a
  neighbouring dataset differs in one row of one table, so tables compose in parallel). No released fact
  enters the admission certificate: a removed check keeps its column loaded, so it can always run again. The analyst compiles with `--facts`; the compiler opens no input
  file, treats nothing as exact (no contract is reported violated; contracts that read values are checked
  at run time; a statement shown useless "probably has no effect" and stays, guarded), embeds no
  checksum, and warns "likely violated" where the released evidence shows a contract broken. One release
  can serve many programs over the same files.
- **Guarantees.** (1) For a schema fixed independently of the data, everything the compiler writes is
  epsilon-differentially private (sequential composition over columns, non-adaptive, post-processing).
  (2) Every run on every admitted batch is exact, for any facts file, true, noisy or false. (3) With
  probability 1 - beta every occupied cell holds a row and every cell with at least 2t + 1 rows is
  occupied: every "likely violated" warning names a real violation, a released fact is false only if
  fewer than c(2t + 1) rows break it (c cells), and for programs without `DERIVE` whose columns each have
  an occupied value cell the private compilation removes at least what a normal compilation removes.
  (4) For a program that does not divide in `DERIVE`, whether a run finishes does not depend on the
  facts: the programs generated from any two facts files refuse the same batches (those that break a
  declared type); the facts change only which removed instructions run again, that is, the run time. In
  a private compilation nothing is exact, so no contract is reported violated, and the one compile error
  left that depends on bounds is a division in `DERIVE` by a range that contains 0. Without this, a
  refusal caused by a false admission fact would let an analyst who chooses the program's numbers learn
  a bit about a row per submission.
- **Why it is new, to our knowledge.** The rule that a hint may be wrong if it is cheap to check is
  Lampson's (1983), and JIT guards apply it to speculation (Hölzle 1992; Flückiger 2018; Barrière 2021).
  Differential privacy has answered an analyst's queries (PrivateSQL, elastic sensitivity, APEx, also for
  cleaning), sized secure query processing (Shrinkwrap, SPECIAL, exact through one-sided noise), chosen DP
  algorithms (Pythia), profiled deployed software (Zhang et al., OOPSLA 2020), and been proven of programs
  (Fuzz, Duet). We found no work in which differentially private facts decide which checks and operations
  a compiler removes, with guards that keep every run exact, and with the released cells chosen by the
  program's own numbers.
- **Code.** `src/schema/ReleasedFacts.{h,cpp}`, `SchemaProvider::setReleased`, `--queries` and `--facts`
  in `src/main.cpp`, `likelyViolated` in `src/sema/ContractAnalyzer.cpp`, `noEffectText` in
  `src/sema/SemanticAnalyzer.cpp`, `tools/release_facts.py`, `tests/test_private.cpp`,
  `eval/reuse/private.py`, `eval/reuse/private_baselines.py`.
- **Evidence** (`eval/results/private.md`, `eval/results/private_diagnostics.md`, paper RQ8).
  - 3,287 compilations (the RQ4 pipelines on real months and 184 lifted Titanic scripts; the release at
    epsilon 0.1-100, per row or per ticket, one release for all scripts, no noise, and the baselines below) ran 42,825 times:
    none unsound, no wrong stop. All 688 refusals of months a fresh compilation runs are the taxi schema
    change, which the normal compilation refuses too.
  - Without noise, the program's cells give exactly the normal removals. At epsilon 1 the taxi and
    traffic pipelines keep all of them at full precision; fixed logarithmic bins with the same budget keep
    60% (taxi) and 83% (traffic), because the program's numbers (1 and 265, 230 and 320) are not on the
    grid. Synthetic data sampled from those bins and compiled normally fails on all 15 pipeline
    compilations, with contracts the real data satisfies. The optimistic facts ("nothing is missing",
    no data) reach 60% recall at 11% precision.
  - On the Titanic training file removal precision is 17% at epsilon 1 and 31% at epsilon 10 (fixed bins
    and synthetic data: 20%); the false facts are mostly "no missing `Embarked`" (2 of 891 rows), which no
    release can reveal, and the run deoptimizes them. One release for all 184 scripts at a total epsilon
    of 1 gives 17% precision, about what each script gets from its own release at epsilon 1, which
    together spend 184 times the budget.
  - On a 1,000,000-row file with a mistake injected into m rows, the program's cells report the broken
    contract in every run from m = 100 at epsilon 1 and m = 10 at epsilon 10, with 0 false reports in
    1,818 compilations. Fixed bins and synthetic data reach the same sizes in every run but less often
    between them (at epsilon 0.3, 100 bad rows in 50-60% of runs against 80-100%).
  - `tests/test_private.cpp`: 300 random facts files with random inputs, 0 runs that differ from the
    source program; the reader trusts no input without a checksum; 200 pairs of tables with the same
    occupied cells and identical compilations (`testCellsDecide`).
- **Found by this round in the existing compiler.** A fill of a column dropped right after was removed
  both as a dead store and as having no effect, and the certifier kept "no missing values" as an
  admission fact of a column the program never loads, so batches were refused for nothing; a dead store
  now needs no data fact (`testDeadStoreNeedsNoFact`). The earlier RQs were measured before this fix,
  which only removes admission facts.
- **Strict re-review of the first version** (AboveThreshold release): PL 5.5/10 overall (from 5), private
  compilation alone 5; DB/privacy 5/10 overall, private compilation alone 4. They found real flaws, all
  fixed in this version: inferred column types are facts about the data (the release now requires
  `--schema-declared`); the seed was written to the facts file; AboveThreshold was the wrong mechanism
  (now Laplace histograms, thresholds about ten times lower); `PREPLANG_TRUST_INPUTS` bypassed the guards;
  "has no effect" was stated as fact; the utility lemma lost a hypothesis; SPECIAL was described wrongly;
  there were no baselines.
- **Final strict re-review of this version** (commit 1299b69): PL 5.5/10 overall, private compilation
  alone 5.5; DB/privacy 5.5/10 overall, private compilation alone 5. Both: 9/10 is not reachable for this
  project; the realistic ceiling is about 6.5-7. The DB reviewer found one new flaw, now fixed: a false
  released fact could enter the admission certificate (a proven check of a pruned column), so a refusal
  the analyst sees could reveal a bit (guarantee 4 above). It also showed that tables compose in parallel
  (now used), and both said the baselines are weak: the program's cells beat a decade grid and synthetic
  data sampled from it, but the real competitor is workload-optimized DP (HDMM, AIM) given the program's
  comparisons as its workload, and on the third-party Titanic scripts the program's cells buy little at
  epsilon 1. Their suggested next steps: derive the release from the abstract domain and prove that the
  compiler's output depends only on which cells are occupied, and compare with HDMM/AIM; or bound each
  person's contribution and set thresholds for run time rather than warnings.
- **After the final re-review: the release derived from the analysis (Theorem Cells).** Let K be the
  numbers a program compares columns with, and 0; they cut a numeric column into 2|K|+2 cells (each
  number, the open intervals between and around them, and missing). For a program with a declared
  schema, no `DERIVE`, no `PARSE_DATE` and no contract that reads values (`UNIQUE`, `REFERENCES`,
  `MATCH_RATE`), two tables that occupy the same cells of every column receive the same verdicts and
  the same removals. The hypotheses are needed: ids {1,2} and {1,1} occupy the same cells but differ on
  `UNIQUE`, and the point cell {30} decides `FILTER s WHERE age < 30`. Mechanized for the core in Lean
  (`cells_decide` in `proofs/Cells.lean`, 335 lines, axioms `propext` and `Quot.sound`, no `sorry`;
  the core has only >= and <=, for which "a value below k" and "a value above k" suffice); for the
  compiler, `testCellsDecide` compiles one program (two contracts, a fill, a filter with a strict
  comparison, `DROP_NULL`) for 200 pairs of different integer tables with the same occupied cells
  (other values, row counts, orders) and finds identical verdicts, removals, speculative instructions
  and certificates in all 200. Consequences: the occupied cells are all the analysis can use, so
  released exactly they give the normal removals (RQ8 confirms it on every workload) and a DP release
  of them loses only its noise; and the *decisions* of a normal compilation of such a program reveal at
  most 2|K|+2 bits per numeric column. The rest of what a normal compilation writes reveals more (the
  checksum, inferred types, messages, the bounds in the contract file and the output guarantees).
- **After the final re-review: people, not rows.** `release_facts.py --unit COLUMN --max-rows m` counts
  at most m rows per unit, the first m in file order, and no row without a unit (sensitivity m, noise
  and threshold scaled by m, all columns of all tables composed sequentially, since a unit may have rows
  everywhere): epsilon-DP for adding or removing a whole person. Contribution bounding is standard (Wilson
  et al., PoPETs 2020); here the bias it causes never costs exactness, since a fact the truncation makes
  false is one the generated program checks, but it does cost precision, as noise does. On Titanic, protecting tickets (134 of 681 tickets have several passengers,
  up to 7) with one row per ticket gives 17% precision at epsilon 1 (as rows do) and 17% at epsilon 10
  (rows: 31%), every run exact. The evaluation now has 3,287 compilations run 42,825 times, none
  unsound, no wrong stop.
- **After the final re-review: HDMM and AIM.** Both get the program's cells and the same budget
  (`eval/reuse/private_dp_baselines.py`, the authors' code at fixed commits). HDMM, given the program's
  comparisons as its workload, chose the identity measurement for all 1,064 column blocks; it differs only
  in how it splits epsilon between columns (more to columns with more cells). Its removals equal the
  release's on the pipelines and on Titanic at epsilon 1; at epsilon 10 its Titanic precision is 20%
  against 31% (most Titanic facts are about missing values, which it funds less), and on the 1M-row file
  it reports every mistake from 100 rows at epsilon 10 where the release needs 10. AIM (defaults,
  (epsilon, 1e-9), a weaker guarantee) keeps 41%/48% of the normal removals over all workloads at epsilon
  1/10 against 73%/100%, with higher precision (72%/83% against 51%/76%): its synthetic rows fill empty
  cells, so its facts err towards "maybe". The same rows give false "likely violated" warnings (34 and 35
  in 13 and 15 of 199 compilations; the release and HDMM give none), and on the 1M-row file AIM warns about
  the contract in 64% of compilations of the file without any mistake; its synthetic data compiled
  normally fails on all 15 pipeline compilations at each budget. 1,194 more compilations, 0 unsound runs.
  Reading: given the cells, a workload-optimized mechanism chooses the same measurements and a
  synthetic-data generator loses emptiness; the contribution is the cells (Theorem Cells) and the
  threshold, not the measurement. A first run of the AIM baseline had a decoder bug (non-integers in
  INTEGER columns, 171 needless refusals); it was fixed before these numbers.
- **Strict re-review of f1ad7fd (two reviewers).** PL: overall 5.5, private compilation 6.0 (both
  unchanged); DB/privacy: overall 5.5, private compilation 5.5 (+0.5). Tool venues: 45-65% by both. They
  found two real bugs, both fixed with regression tests: (1) `FILL_NULL ... WHERE NULLABLE` chose its
  columns by the compile-time facts with no certificate, so a compiled (or privately compiled) program
  left new missing values unfilled where a fresh compilation fills them; FILL_NULL now matches every
  column its value fits (guarded no-ops where nothing is missing) and the selector is an error elsewhere
  (`testNullableSelector`). (2) The release keyed tables by path, so `p.csv` and `P.CSV` were two tables
  with epsilon each; tables are keyed by file identity (`tests/test_release_facts.py`). Neither touched an
  evaluation result. They also showed the comparison overreached: HDMM's split follows from the
  L2 objective on a comparison-weighted workload (an emptiness objective gives the equal split), and AIM
  was read off sampled rows at threshold 0. AIM's model marginals read with the release's threshold
  (`aim-marginals`) were added: they keep every taxi and traffic removal with no warning, as the release
  does, but 80% of the bike removals at epsilon 10 (release 100%) at Titanic precision 16%/18% (release
  17%/31%), since AIM spends its budget over 16 rounds per column. So, read with the same threshold,
  neither HDMM nor AIM beats one Laplace histogram per column on any workload; the gains come from the
  cells and the threshold, not the mechanism. HDMM chose identity for 1,064 of 1,064 column blocks
  (now recorded in each facts file and counted by `eval/paper_numbers.py`). Totals: 4,879 compilations,
  64,179 runs, 0 unsound. Not evaluated: Gaussian/zCDP noise for wide tables, other operating points.
- **Limits.** The schema must be declared. The guarantee covers what the compiler writes, not the data
  owner's run logs or run time. Small tables give little at small epsilon, by design. Images,
  `LOAD_TABLES` and `PARSE_DATE` are not supported. There is no budget ledger across releases. The
  privacy theorem and the utility lemma are on paper; the Lean core covers Theorem Cells but not
  admission, types or compile errors, and `DERIVE` is outside it.
