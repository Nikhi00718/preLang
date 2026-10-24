# RQ9: data certificates for real pandas scripts

Corpus: 461 Python files from GitHub code search `read_csv fillna LotFrontage language:python` (first 500 results).

- lifted: 225
- does not read the training file with pd.read_csv(path): 175
- reads other data files: 31
- not Python 3: 30

Of the 225 scripts whose training-file cleaning was lifted: 269 of 535 cleaning statements translated; 51 scripts lifted completely.

## New batches

- runs: 2475 (11 per script)
- admitted by the certificate: 2467
- admitted, finished and identical to the script's own result: 2467
- admitted, finished, but different from the script, or the script crashed (unsound): 0
- admitted, stopped by a run-time contract check: 0
- refused, and the script crashes on it too: 8
- refused, but the script runs (conservative): 0
- ran with a removed check or operation re-enabled (deoptimized): 3

Without deoptimization (--no-deopt, the full certificate):
- admitted: 2464
- admitted, finished and identical: 2464
- admitted, finished, different (unsound): 0
- refused, but the script runs: 3

| Batch | Runs | Admitted | Identical to the script | Script crashes |
|---|---:|---:|---:|---:|
| kaggle test file | 225 | 217 | 217 | 8 |
| resampled 0 | 225 | 225 | 225 | 0 |
| missing LotFrontage | 225 | 225 | 225 | 0 |
| resampled 1 | 225 | 225 | 225 | 0 |
| missing MasVnrArea | 225 | 225 | 225 | 0 |
| resampled 2 | 225 | 225 | 225 | 0 |
| missing GarageYrBlt | 225 | 225 | 225 | 0 |
| resampled 3 | 225 | 225 | 225 | 0 |
| missing MSZoning | 225 | 225 | 225 | 0 |
| resampled 4 | 225 | 225 | 225 | 0 |
| missing KitchenQual | 225 | 225 | 225 | 0 |
