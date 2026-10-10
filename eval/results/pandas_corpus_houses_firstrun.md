# RQ9: data certificates for real pandas scripts

Corpus: 461 Python files from GitHub code search `read_csv fillna LotFrontage language:python` (first 500 results).

- does not read the training file with pd.read_csv(path): 240
- lifted: 168
- not Python 3: 30
- reads other data files: 23

Of the 168 scripts whose training-file cleaning was lifted: 208 of 377 cleaning statements translated; 48 scripts lifted completely.

## New batches

- runs: 1848 (11 per script)
- admitted by the certificate: 1841
- admitted, finished and identical to the script's own result: 1819
- admitted, finished, but different from the script, or the script crashed (unsound): 22
- admitted, stopped by a run-time contract check: 0
- refused, and the script crashes on it too: 7
- refused, but the script runs (conservative): 0
- ran with a removed check or operation re-enabled (deoptimized): 3

Without deoptimization (--no-deopt, the full certificate):
- admitted: 1838
- admitted, finished and identical: 1816
- admitted, finished, different (unsound): 22
- refused, but the script runs: 3

| Batch | Runs | Admitted | Identical to the script | Script crashes |
|---|---:|---:|---:|---:|
| kaggle test file | 168 | 161 | 159 | 7 |
| resampled 0 | 168 | 168 | 166 | 0 |
| missing LotFrontage | 168 | 168 | 166 | 0 |
| resampled 1 | 168 | 168 | 166 | 0 |
| missing MasVnrArea | 168 | 168 | 166 | 0 |
| resampled 2 | 168 | 168 | 166 | 0 |
| missing GarageYrBlt | 168 | 168 | 166 | 0 |
| resampled 3 | 168 | 168 | 166 | 0 |
| missing MSZoning | 168 | 168 | 166 | 0 |
| resampled 4 | 168 | 168 | 166 | 0 |
| missing KitchenQual | 168 | 168 | 166 | 0 |
