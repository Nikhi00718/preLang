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

## `reuse/`: reusing a compiled program on new batches (RQ3 to RQ8)

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
| `private.py`, `private_baselines.py`, `private_dp_baselines.py` | private compilation: released, noisy, absent, optimistic and random facts and five baselines (fixed bins, synthetic data from them, HDMM, AIM's facts, AIM's synthetic data) on the real months and the Titanic scripts; with `diagnostics`, warnings on a 1M-row file with injected mistakes (RQ8) | `private.*`, `private_diagnostics.*` |

Result files keep the numbering of earlier drafts in their names and titles (`rq3.md` is the paper's
RQ2, for instance); the tables above give the paper's numbering. `*_firstrun.*` are earlier runs kept
for the record (for House Prices, the run before the `None` fix that RQ5 describes), and `*_derived.*`
the same corpora lifted with the second lifter version.

The validators run in their own Python 3.11 environment (`.venv-baselines/`, not committed); TFDV runs
in WSL. `reuse/tfdv.Dockerfile` is an alternative for machines with Docker.

HDMM and AIM (`reuse/private_dp_baselines.py`) run in two Python 3.12 environments, not committed, with
the authors' code at fixed commits. HDMM needs numpy 1.x; AIM needs JAX, which needs numpy 2:

```bash
uv venv eval/.venv-hdmm --python 3.12
uv pip install --python eval/.venv-hdmm/Scripts/python.exe numpy==1.26.4 scipy==1.11.4 pandas==2.2.3 pyarrow==17.0.0 autograd==1.7.0 future==1.0.0
uv pip install --python eval/.venv-hdmm/Scripts/python.exe --no-deps "hdmm @ git+https://github.com/dpcomp-org/hdmm.git@5aa9df93a46e"
uv venv eval/.venv-dp --python 3.12
uv pip install --python eval/.venv-dp/Scripts/python.exe "mbi @ git+https://github.com/ryan112358/private-pgm.git@57e22b46fa" pandas pyarrow matplotlib==3.10.7
```

and the AIM script with its two helpers from the same commit, in `eval/.venv-dp/aim/`:

```bash
mkdir -p eval/.venv-dp/aim && for f in aim.py cdp2adp.py mechanism.py; do gh api "repos/ryan112358/private-pgm/contents/mechanisms/$f?ref=57e22b46fa" --jq .content | base64 -d > eval/.venv-dp/aim/$f; done
```

`private_dp_baselines.py` changes two lines of HDMM at run time, both for current platforms and neither
for its results: a template's random seed is drawn below 2^31 (numpy's default integer is 32 bits on
Windows), and `EkteloMatrix.sensitivity` calls `abs` instead of `np.abs`, which scipy's `LinearOperator`
no longer allows.
