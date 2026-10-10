# Evaluation

Every number in the paper comes from `results/` through `paper_numbers.py`. Run the scripts from the
repository root with the Release build (`build/Release/preplang.exe`); each one writes a `.json` and a
`.md` file to `results/`. Downloaded data goes to `data/` and scratch folders to `work/`; neither is
committed.

## `compilation/`: what data-aware compilation finds and costs (RQ1, RQ2)

| Script | What it does | Results |
|---|---|---|
| `run_eval.py` | the 24 injected mistakes (`mutants.py`), compiled and run as a script would (`baseline.py`) | `rq1.*` |
| `bench.py` | optimizations, image streaming (against `handwritten.py`), compile-time cost | `rq3.*` |
| `scale.py` | time to the first error on 16M-row Parquet tables and 3,000 images | `scale.*` |
| `run_realbugs.py` | the 44 failures from GitHub issues (`realbugs_catalog.py`) | `realbugs.*` |
| `datasets.py` | builds the example images and the benchmark and scale data | |

## `reuse/`: reusing a compiled program on new batches (RQ3 to RQ7)

| Script | What it does | Results |
|---|---|---|
| `policies.py` | shared: compiling, running, scoring against a fresh compilation, running the validators | |
| `baselines.py` | pandera, Great Expectations and TFDV set up on a file and checking batches | |
| `certificates.py` | edited batches of the examples and a sensor pipeline (RQ3) | `certificates.*` |
| `imagebatches.py` | new image folders for a compiled image pipeline (RQ3) | `imagebatches.*` |
| `realbatches.py` | real monthly traffic, bike and taxi data, every policy and validator (RQ4) | `realbatches.*` |
| `pandas_corpus.py` | Titanic and House Prices scripts from GitHub, lifted and run on new batches (RQ5) | `pandas_corpus*.*` |
| `drift_corpus.py` | other people's traffic and bike scripts, run on every real month (RQ5) | `drift_corpus_*.*` |
| `composition.py` | two compiled stages over the real months (RQ6) | `composition.*` |
| `overhead.py`, `certcost.py`, `certsize.py` | the cost of admission and certification, and certificate sizes (RQ7) | `overhead.*`, `certcost.*`, `certsize.*` |

Result files keep the numbering of earlier drafts in their names and titles (`rq3.md` is the paper's
RQ2, for instance); the tables above give the paper's numbering. `*_firstrun.*` are earlier runs kept
for the record (for House Prices, the run before the `None` fix that RQ5 describes), and `*_derived.*`
the same corpora lifted with the second lifter version.

The validators run in their own Python 3.11 environment (`.venv-baselines/`, not committed); TFDV runs
in WSL. `reuse/tfdv.Dockerfile` is an alternative for machines with Docker.
