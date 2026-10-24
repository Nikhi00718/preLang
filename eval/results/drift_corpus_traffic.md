# Other people's pandas scripts on real monthly data: traffic

Corpus: 241 Python files from GitHub code search `Metro_Interstate_Traffic_Volume read_csv language:python` (first 500 results). Compiled on 2012-10, run on 62 later months.

- does not read the data file with pd.read_csv(path): 149
- lifted: 45
- no cleaning statement lifted: 36
- not Python 3: 7
- reads other data files: 2
- compile error: 2

Of the 47 scripts with lifted cleaning: 110 of 148 cleaning statements translated; 9 lifted completely.

## Later months

- runs: 2790
- finished and identical to the script: 2790
- finished, but different from the script, or the script crashed (wrong): 0
- stopped by a contract check, and the script crashes too: 0
- stopped by a contract check, but the script runs: 0
- refused, and the script crashes too: 0
- refused, but the script runs: 0
- ran with removals re-enabled: 6

Without deoptimization (--no-deopt, the full certificate):
- finished and identical: 2784
- finished, different (wrong): 0
- refused, but the script runs: 6

## Removals re-enabled

- 4 x line 3: removed because the condition is true for every row of df
- 2 x line 4: removed because the condition is true for every row of df
