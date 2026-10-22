# Correctness: compiled programs against a reference interpreter

2000 random table programs over random CSV files (eval/compilation/differential.py), each compiled with --O0 and with the optimizer, run, and compared with an interpreter of the language written without pandas or compiler code.

| Measure | Value |
|---|---:|
| programs | 2000 |
| reference saves a table / a contract fails | 1402 / 598 |
| rows saved by the reference, in all | 15,058 |
| --O0 agrees with the reference | 2000 / 2000 |
| optimized agrees with the reference | 2000 / 2000 |
| --O0 and optimized give byte-identical outputs or the same failure | 2000 / 2000 |
| failing contracts reported at compile time (before any data is processed) | 433 / 598 |
| time for the whole run (s) | 65 |

