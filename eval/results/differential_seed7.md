# Correctness: compiled programs against a reference interpreter

2000 random table programs over random CSV files (eval/compilation/differential.py), each compiled with --O0 and with the optimizer, run, and compared with an interpreter of the language written without pandas or compiler code.

| Measure | Value |
|---|---:|
| programs | 2000 |
| reference saves a table / a contract fails | 1410 / 590 |
| rows saved by the reference, in all | 11,858 |
| --O0 agrees with the reference | 2000 / 2000 |
| optimized agrees with the reference | 2000 / 2000 |
| --O0 and optimized give byte-identical outputs or the same failure | 2000 / 2000 |
| failing contracts reported at compile time (before any data is processed) | 389 / 590 |
| programs with a JOIN (of them MANY) / an AGGREGATE | 647 (278) / 715 |
| time for the whole run (s) | 76 |

## Reuse on a second batch

The optimized program of every program that compiled, run on another random batch with the same columns (`t=data_b.csv u=lookup_b.csv`), against the reference on that batch.

| Outcome | Programs |
|---|---:|
| ran, same rows as the reference | 1203 |
| stopped at the line where the reference fails | 245 |
| refused (the batch breaks the admission certificate) | 163 |
| wrong (other rows, another line, or a crash) | 0 |
| runs that re-enabled at least one removed check or operation | 322 |

In 486 of these batches one numeric column had the other numeric type (INTEGER values made x.5, or FLOAT values rounded): 289 ran with the reference's rows, 57 stopped where it fails, 140 were refused (14 of the batches do not compile at all), 0 were wrong.

