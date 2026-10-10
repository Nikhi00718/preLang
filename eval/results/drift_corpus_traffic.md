# Other people's pandas scripts on real monthly data: traffic

Corpus: 241 Python files from GitHub code search `Metro_Interstate_Traffic_Volume read_csv language:python` (first 500 results). Compiled on 2012-10, run on 62 later months.

- does not read the data file with pd.read_csv(path): 166
- lifted: 41
- no cleaning statement lifted: 24
- not Python 3: 7
- reads other data files: 2
- compile error: 1

Of the 42 scripts with lifted cleaning: 104 of 139 cleaning statements translated; 7 lifted completely.

## Later months

- runs: 2542
- finished and identical to the script: 2542
- finished, but different from the script, or the script crashed (wrong): 0
- stopped by a contract check, and the script crashes too: 0
- stopped by a contract check, but the script runs: 0
- refused, and the script crashes too: 0
- refused, but the script runs: 0
- ran with removals re-enabled: 6

Without deoptimization (--no-deopt, the full certificate):
- finished and identical: 2536
- finished, different (wrong): 0
- refused, but the script runs: 6

## Removals re-enabled

- 4 x line 3: removed because the condition is true for every row of df
- 2 x line 4: removed because the condition is true for every row of df
