# Correctness: compiled programs against a reference interpreter

2000 random table programs over random CSV files (eval/compilation/differential.py), each compiled with --O0 and with the optimizer, run, and compared with an interpreter of the language written without pandas or compiler code.

| Measure | Value |
|---|---:|
| programs | 2000 |
| reference saves a table / a contract fails | 1365 / 635 |
| rows saved by the reference, in all | 11,492 |
| --O0 agrees with the reference | 2000 / 2000 |
| optimized agrees with the reference | 1999 / 2000 |
| --O0 and optimized give byte-identical outputs or the same failure | 1999 / 2000 |
| failing contracts reported at compile time (before any data is processed) | 445 / 635 |
| programs with a JOIN / an AGGREGATE | 647 / 699 |
| time for the whole run (s) | 192 |

## Reuse on a second batch

The optimized program of every program that compiled, run on another random batch with the same columns (`t=data_b.csv u=lookup_b.csv`), against the reference on that batch.

| Outcome | Programs |
|---|---:|
| ran, same rows as the reference | 1241 |
| stopped at the line where the reference fails | 285 |
| refused (the batch breaks the admission certificate) | 29 |
| wrong (other rows, another line, or a crash) | 0 |
| runs that re-enabled at least one removed check or operation | 341 |

## Disagreements

### p0975

```text
LOAD "data.csv" AS t;
LOAD "lookup.csv" AS u;
EXPECT t.id NOT_NULL;
FILTER t WHERE y <= 40.1;
DROP t [cat];
JOIN t WITH u ON k;
FILL_NULL t.k WITH MEDIAN;
FILL_NULL t.flag WITH TRUE;
FILL_NULL t.x WITH MEAN;
FILTER t WHERE b == 91;
FILL_NULL t.k WITH MEDIAN;
FILTER t WHERE w != 107;
AGGREGATE t BY [b, label] MIN k AS d1;
FILL_NULL t.b WITH -4;
SAVE t AS "out.csv";
```

- O0: contract error; agrees (ContractError: line 9: t.x has no values to compute the MEAN from)
- optimized: saved; saved, but the reference fails at line 9

