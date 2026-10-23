# Correctness: compiled programs against a reference interpreter

2000 random table programs over random CSV files (eval/compilation/differential.py), each compiled with --O0 and with the optimizer, run, and compared with an interpreter of the language written without pandas or compiler code.

| Measure | Value |
|---|---:|
| programs | 2000 |
| reference saves a table / a contract fails | 1365 / 635 |
| rows saved by the reference, in all | 11,492 |
| --O0 agrees with the reference | 2000 / 2000 |
| optimized agrees with the reference | 2000 / 2000 |
| --O0 and optimized give byte-identical outputs or the same failure | 2000 / 2000 |
| failing contracts reported at compile time (before any data is processed) | 445 / 635 |
| programs with a JOIN / an AGGREGATE | 647 / 699 |
| time for the whole run (s) | 110 |

## Reuse on a second batch

The optimized program of every program that compiled, run on another random batch with the same columns (`t=data_b.csv u=lookup_b.csv`), against the reference on that batch.

| Outcome | Programs |
|---|---:|
| ran, same rows as the reference | 1241 |
| stopped at the line where the reference fails | 286 |
| refused (the batch breaks the admission certificate) | 28 |
| wrong (other rows, another line, or a crash) | 0 |
| runs that re-enabled at least one removed check or operation | 356 |

