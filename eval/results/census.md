# Optimization effectiveness, performance and overhead on real programs

Programs: examples/ and the programs lifted from GitHub pandas scripts (eval/compilation/census.py). Run times are of the generated program on the program's own data, in one Python process (pandas imported once), median of 3.

## Work removed by the optimizer

| Corpus | Programs | Instructions | Run-time checks | Columns read | Programs with something removed |
|---|---:|---:|---:|---:|---:|
| examples | 10 | 120 -> 100 (-17%) | 16 -> 7 (-56%) | 65 -> 54 (-17%) | 8 (80%) |
| Titanic scripts | 184 | 784 -> 684 (-13%) | 0 | 2957 -> 2736 (-7%) | 78 (42%) |
| House Prices scripts | 168 | 836 -> 721 (-14%) | 0 | 24092 -> 23907 (-1%) | 45 (27%) |
| traffic scripts | 46 | 208 -> 192 (-8%) | 0 | 414 -> 407 (-2%) | 7 (15%) |
| bike scripts (days) | 26 | 102 -> 92 (-10%) | 0 | 416 -> 397 (-5%) | 7 (27%) |
| bike scripts (hours) | 14 | 51 -> 44 (-14%) | 0 | 238 -> 226 (-5%) | 7 (50%) |
| monthly pipelines | 5 | 66 -> 30 (-55%) | 28 -> 1 (-96%) | 71 -> 42 (-41%) | 5 (100%) |
| **all** | 453 | 2167 -> 1863 (-14%) | 44 -> 8 (-82%) | 28253 -> 27769 (-2%) | 157 (35%) |

What the passes did, over all programs: column pruning 391, no-effect elimination 40, contract check elimination 36, dead store elimination 8, contract check made conditional 6, dead image operation 1.

## Run time of the generated programs

- --O0 and optimized wrote identical files (or failed the same way): 453 / 453
- speedup of the optimized program (--O0 time / optimized time), median over the programs: 1.01x (geometric mean 1.02x, range 0.42x to 2.12x)
- over the 157 programs the optimizer changed: median 1.07x, geometric mean 1.07x

| Corpus | Programs | --O0 run (s), median | optimized run (s), median | compile (s), median | compiler peak memory (MiB), max |
|---|---:|---:|---:|---:|---:|
| examples | 10 | 0.021 | 0.018 | 0.026 | 6.6 |
| Titanic scripts | 184 | 0.015 | 0.014 | 0.026 | 6.6 |
| House Prices scripts | 168 | 0.099 | 0.099 | 0.114 | 8.4 |
| traffic scripts | 46 | 0.013 | 0.013 | 0.027 | 6.5 |
| bike scripts (days) | 26 | 0.010 | 0.010 | 0.027 | 6.5 |
| bike scripts (hours) | 14 | 0.014 | 0.014 | 0.027 | 6.5 |
| monthly pipelines | 5 | 0.014 | 0.013 | 0.147 | 24.2 |

## The compiler on large inputs

| Input | Size (MiB) | Compile time (s), warm | First compile (s) | Compiler peak memory (MiB) |
|---|---:|---:|---:|---:|
| CSV, 10,000 rows x 10 columns | 0.6 | 0.02 | 0.04 | 6.2 |
| CSV, 100,000 rows x 10 columns | 5.9 | 0.03 | 0.05 | 11.2 |
| CSV, 1,000,000 rows x 10 columns | 59.1 | 0.12 | 0.14 | 64.6 |
| Parquet, 1,000,000 rows (footer only) | 21.6 | 0.02 | 0.02 | 6.2 |
| Parquet, 16,000,000 rows (footer only) | 346.0 | 0.02 | 0.02 | 6.2 |
| 100 JPG images, 640x480 | 2.6 | 0.02 | 0.04 | 6.0 |
| 1,000 JPG images, 640x480 | 26.3 | 0.07 | 0.09 | 6.5 |
| 3,000 JPG images, 640x480 | 78.8 | 0.25 | 0.28 | 9.0 |

Warm: median of the runs after the first. First compile: the first run of the measurement, which reads the inputs from disk when they are not in the file cache. On the Windows machine of these results the first pass over an image folder was much slower than the next ones; the cause (file cache or antivirus scanning of each file) was not measured.

Not compiled: 8 (Titanic scripts/2182346853b23377, Titanic scripts/79b5c160bffac35d, Titanic scripts/87f79015808a6cb1, Titanic scripts/b73d14a6985d1388, Titanic scripts/d7f9579ae7348e6f, Titanic scripts/edff8df216a5444a, traffic scripts/13459e5d1432d91b, traffic scripts/edc16bead0055e14)
