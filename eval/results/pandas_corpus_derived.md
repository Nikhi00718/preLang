# RQ9: data certificates for real pandas scripts

Corpus: 372 Python files from GitHub code search `read_csv fillna Embarked language:python` (first 500 results).

- lifted: 217
- does not read the training file with pd.read_csv(path): 119
- not Python 3: 16
- reads other data files: 13
- compile error: 7

Of the 224 scripts whose training-file cleaning was lifted: 347 of 573 cleaning statements translated; 50 scripts lifted completely.

## New batches

- runs: 2387 (11 per script)
- admitted by the certificate: 2374
- admitted, finished and identical to the script's own result: 2363
- admitted, finished, but different from the script, or the script crashed (unsound): 0
- admitted, stopped by a run-time contract check: 11
- refused, and the script crashes on it too: 13
- refused, but the script runs (conservative): 0
- ran with a removed check or operation re-enabled (deoptimized): 19

Without deoptimization (--no-deopt, the full certificate):
- admitted: 2355
- admitted, finished and identical: 2344
- admitted, finished, different (unsound): 0
- refused, but the script runs: 19

| Batch | Runs | Admitted | Identical to the script | Script crashes |
|---|---:|---:|---:|---:|
| kaggle test file | 217 | 204 | 203 | 13 |
| resampled 0 | 217 | 217 | 216 | 0 |
| missing Age | 217 | 217 | 216 | 0 |
| resampled 1 | 217 | 217 | 216 | 0 |
| missing Fare | 217 | 217 | 216 | 0 |
| resampled 2 | 217 | 217 | 216 | 0 |
| missing Embarked | 217 | 217 | 216 | 0 |
| resampled 3 | 217 | 217 | 216 | 0 |
| missing Sex | 217 | 217 | 216 | 0 |
| resampled 4 | 217 | 217 | 216 | 0 |
| missing Pclass | 217 | 217 | 216 | 0 |
