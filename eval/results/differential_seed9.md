# Correctness: compiled programs against a reference interpreter

2000 random table programs over random CSV files (eval/compilation/differential.py), each compiled with --O0 and with the optimizer, run, and compared with an interpreter of the language written without pandas or compiler code.

| Measure | Value |
|---|---:|
| programs | 2000 |
| reference saves a table / a contract fails | 1374 / 626 |
| rows saved by the reference, in all | 11,509 |
| --O0 agrees with the reference | 2000 / 2000 |
| optimized agrees with the reference | 2000 / 2000 |
| --O0 and optimized give byte-identical outputs or the same failure | 2000 / 2000 |
| failing contracts reported at compile time (before any data is processed) | 400 / 626 |
| programs with a JOIN (of them MANY) / an AGGREGATE | 636 (245) / 723 |
| time for the whole run (s) | 75 |

## Reuse on a second batch

The optimized program of every program that compiled, run on another random batch with the same columns (`t=data_b.csv u=lookup_b.csv`), against the reference on that batch.

| Outcome | Programs |
|---|---:|
| ran, same rows as the reference | 1170 |
| stopped at the line where the reference fails | 275 |
| refused (the batch breaks the admission certificate) | 155 |
| wrong (other rows, another line, or a crash) | 0 |
| runs that re-enabled at least one removed check or operation | 333 |

In 459 of these batches one numeric column had the other numeric type (INTEGER values made x.5, or FLOAT values rounded): 262 ran with the reference's rows, 63 stopped where it fails, 134 were refused (15 of the batches do not compile at all), 0 were wrong.

