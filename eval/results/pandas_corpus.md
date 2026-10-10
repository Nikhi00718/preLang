# RQ9: data certificates for real pandas scripts

Corpus: 372 Python files from GitHub code search `read_csv fillna Embarked language:python` (first 500 results).

- lifted: 184
- does not read the training file with pd.read_csv(path): 153
- not Python 3: 16
- reads other data files: 13
- compile error: 6

Of the 190 scripts whose training-file cleaning was lifted: 310 of 508 cleaning statements translated; 38 scripts lifted completely.

## New batches

- runs: 2024 (11 per script)
- admitted by the certificate: 2012
- admitted, finished and identical to the script's own result: 2012
- admitted, finished, but different from the script, or the script crashed (unsound): 0
- admitted, stopped by a run-time contract check: 0
- refused, and the script crashes on it too: 12
- refused, but the script runs (conservative): 0
- ran with a removed check or operation re-enabled (deoptimized): 18

Without deoptimization (--no-deopt, the full certificate):
- admitted: 1994
- admitted, finished and identical: 1994
- admitted, finished, different (unsound): 0
- refused, but the script runs: 18

| Batch | Runs | Admitted | Identical to the script | Script crashes |
|---|---:|---:|---:|---:|
| kaggle test file | 184 | 172 | 172 | 12 |
| resampled 0 | 184 | 184 | 184 | 0 |
| missing Age | 184 | 184 | 184 | 0 |
| resampled 1 | 184 | 184 | 184 | 0 |
| missing Fare | 184 | 184 | 184 | 0 |
| resampled 2 | 184 | 184 | 184 | 0 |
| missing Embarked | 184 | 184 | 184 | 0 |
| resampled 3 | 184 | 184 | 184 | 0 |
| missing Sex | 184 | 184 | 184 | 0 |
| resampled 4 | 184 | 184 | 184 | 0 |
| missing Pclass | 184 | 184 | 184 | 0 |
