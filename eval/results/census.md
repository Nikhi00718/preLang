# Optimization effectiveness, performance and overhead on real programs

Programs: examples/ and the programs lifted from GitHub pandas scripts (eval/compilation/census.py). Run times are of the generated program on the program's own data, in one Python process (pandas imported once), median of 3.

## Work removed by the optimizer

| Corpus | Programs | Instructions | Run-time checks | Columns read | Programs with something removed |
|---|---:|---:|---:|---:|---:|
| examples | 8 | 100 -> 83 (-17%) | 13 -> 7 (-46%) | 51 -> 42 (-18%) | 6 (75%) |
| Titanic scripts | 184 | 784 -> 684 (-13%) | 0 | 2957 -> 2736 (-7%) | 78 (42%) |
| House Prices scripts | 168 | 836 -> 721 (-14%) | 0 | 24092 -> 23907 (-1%) | 45 (27%) |
| traffic scripts | 41 | 191 -> 175 (-8%) | 0 | 369 -> 362 (-2%) | 7 (17%) |
| bike scripts (days) | 19 | 76 -> 68 (-11%) | 0 | 304 -> 287 (-6%) | 6 (32%) |
| bike scripts (hours) | 12 | 45 -> 39 (-13%) | 0 | 204 -> 193 (-5%) | 6 (50%) |
| monthly pipelines | 3 | 38 -> 19 (-50%) | 13 -> 0 (-100%) | 45 -> 21 (-53%) | 3 (100%) |
| **all** | 435 | 2070 -> 1789 (-14%) | 26 -> 7 (-73%) | 28022 -> 27548 (-2%) | 151 (35%) |

What the passes did, over all programs: column pruning 381, no-effect elimination 38, contract check elimination 19, dead store elimination 8, contract check made conditional 5, dead image operation 1.

## Run time of the generated programs

- --O0 and optimized wrote identical files (or failed the same way): 435 / 435
- speedup of the optimized program (--O0 time / optimized time), median over the programs: 1.02x (geometric mean 1.02x, range 0.41x to 1.92x)
- over the 151 programs the optimizer changed: median 1.07x, geometric mean 1.08x

| Corpus | Programs | --O0 run (s), median | optimized run (s), median | compile (s), median | compiler peak memory (MiB), max |
|---|---:|---:|---:|---:|---:|
| examples | 8 | 0.028 | 0.026 | 0.025 | 6.4 |
| Titanic scripts | 184 | 0.029 | 0.029 | 0.026 | 6.4 |
| House Prices scripts | 168 | 0.198 | 0.195 | 0.217 | 7.8 |
| traffic scripts | 41 | 0.025 | 0.025 | 0.026 | 6.4 |
| bike scripts (days) | 19 | 0.011 | 0.011 | 0.026 | 6.4 |
| bike scripts (hours) | 12 | 0.015 | 0.016 | 0.027 | 6.4 |
| monthly pipelines | 3 | 0.033 | 0.019 | 0.030 | 6.4 |

## The compiler on large inputs

| Input | Size (MiB) | Compile time (s), warm | First compile (s) | Compiler peak memory (MiB) |
|---|---:|---:|---:|---:|
| CSV, 10,000 rows x 10 columns | 0.6 | 0.02 | 0.05 | 6.1 |
| CSV, 100,000 rows x 10 columns | 5.9 | 0.03 | 0.05 | 11.2 |
| CSV, 1,000,000 rows x 10 columns | 59.1 | 0.12 | 0.19 | 64.6 |
| Parquet, 1,000,000 rows (footer only) | 21.6 | 0.02 | 0.03 | 6.0 |
| Parquet, 16,000,000 rows (footer only) | 346.0 | 0.02 | 0.04 | 6.0 |
| 100 JPG images, 640x480 | 2.6 | 0.02 | 0.93 | 6.1 |
| 1,000 JPG images, 640x480 | 26.3 | 0.09 | 12.40 | 6.4 |
| 3,000 JPG images, 640x480 | 78.8 | 0.31 | 11.93 | 8.7 |

Warm: median of the runs after the first. First compile: the first run of the measurement, which reads the inputs from disk when they are not in the file cache. On the Windows machine of these results the first pass over an image folder was much slower than the next ones; the cause (file cache or antivirus scanning of each file) was not measured.

Not compiled: 7 (Titanic scripts/2182346853b23377, Titanic scripts/79b5c160bffac35d, Titanic scripts/87f79015808a6cb1, Titanic scripts/b73d14a6985d1388, Titanic scripts/d7f9579ae7348e6f, Titanic scripts/edff8df216a5444a, traffic scripts/edc16bead0055e14)
