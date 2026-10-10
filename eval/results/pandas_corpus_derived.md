# RQ9: data certificates for real pandas scripts

Corpus: 372 Python files from GitHub code search `read_csv fillna Embarked language:python` (first 500 results).

- lifted: 183
- does not read the training file with pd.read_csv(path): 153
- not Python 3: 16
- reads other data files: 13
- compile error: 7

Of the 190 scripts whose training-file cleaning was lifted: 321 of 518 cleaning statements translated; 39 scripts lifted completely.

## New batches

- runs: 2013 (11 per script)
- admitted by the certificate: 2001
- admitted, finished and identical to the script's own result: 2001
- admitted, finished, but different from the script, or the script crashed (unsound): 0
- admitted, stopped by a run-time contract check: 0
- refused, and the script crashes on it too: 12
- refused, but the script runs (conservative): 0
- ran with a removed check or operation re-enabled (deoptimized): 18

Without deoptimization (--no-deopt, the full certificate):
- admitted: 1983
- admitted, finished and identical: 1983
- admitted, finished, different (unsound): 0
- refused, but the script runs: 18

| Batch | Runs | Admitted | Identical to the script | Script crashes |
|---|---:|---:|---:|---:|
| kaggle test file | 183 | 171 | 171 | 12 |
| resampled 0 | 183 | 183 | 183 | 0 |
| missing Age | 183 | 183 | 183 | 0 |
| resampled 1 | 183 | 183 | 183 | 0 |
| missing Fare | 183 | 183 | 183 | 0 |
| resampled 2 | 183 | 183 | 183 | 0 |
| missing Embarked | 183 | 183 | 183 | 0 |
| resampled 3 | 183 | 183 | 183 | 0 |
| missing Sex | 183 | 183 | 183 | 0 |
| resampled 4 | 183 | 183 | 183 | 0 |
| missing Pclass | 183 | 183 | 183 | 0 |
