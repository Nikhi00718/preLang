# Correctness: compiled programs against a reference interpreter

2000 random table programs over random CSV files (eval/compilation/differential.py), each compiled with --O0 and with the optimizer, run, and compared with an interpreter of the language written without pandas or compiler code.

| Measure | Value |
|---|---:|
| programs | 2000 |
| reference saves a table / a contract fails | 1402 / 598 |
| rows saved by the reference, in all | 15,058 |
| --O0 agrees with the reference | 1964 / 2000 |
| optimized agrees with the reference | 1891 / 2000 |
| --O0 and optimized give byte-identical outputs or the same failure | 1921 / 2000 |
| failing contracts reported at compile time (before any data is processed) | 433 / 598 |
| time for the whole run (s) | 82 |

## Disagreements

### p0009

```text
LOAD "data.csv" AS t;
SELECT t [x, b, y, a, id];
EXPECT t.id NOT_NULL;
DROP t [b];
FILL_NULL t.a WITH -4;
DROP_NULL t [x, id];
SELECT t [y, a, id];
DERIVE t.d1 = y - a;
DROP t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['b'] not found in axis"), but the reference saves 4 rows (KeyError: "['b'] not found in axis")

### p0018

```text
LOAD "data.csv" AS t;
SELECT t [y, x, a, id, b];
DROP t [y];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not found in axis"), but the reference saves 8 rows (KeyError: "['y'] not found in axis")

### p0024

```text
LOAD "data.csv" AS t;
FILTER t WHERE flag == FALSE;
NORMALIZE t.b USING MINMAX;
EXPECT t.id BETWEEN -32 AND 100;
DROP t [a];
SELECT t [b, cat, x, id, flag, y];
SELECT t [y, b, x, id, cat, flag];
SELECT t [cat, x, id];
FILTER t WHERE x <= 3.5;
FILTER t WHERE id != -10;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y', 'b'] not in index"), but the reference saves 8 rows (KeyError: "['y', 'b'] not in index")

### p0060

```text
LOAD "data.csv" AS t;
DERIVE t.d1 = b + b * 1;
SELECT t [flag, id, x, a];
DROP t [flag];
FILL_NULL t.a WITH 0;
DROP_NULL t;
EXPECT t.x NOT_NULL;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 23 rows (KeyError: "['flag'] not found in axis")

### p0071

```text
LOAD "data.csv" AS t;
SELECT t [a, id, x, y];
FILL_NULL t.id WITH 4;
NORMALIZE t.y USING ZSCORE;
DERIVE t.d1 = id * id;
DROP t [a];
DROP_NULL t;
EXPECT t.x NOT_NULL;
FILTER t WHERE ((d1 < 0 OR d1 >= -29) OR x < 63.8);
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 23 rows (KeyError: "['a'] not found in axis")

### p0075

```text
LOAD "data.csv" AS t;
NORMALIZE t.x USING ZSCORE;
SELECT t [flag, b, a, id, cat];
DROP t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 37 rows (KeyError: "['a'] not found in axis")

### p0085

```text
LOAD "data.csv" AS t;
FILTER t WHERE y < 62.0;
NORMALIZE t.a USING ZSCORE;
SELECT t [b, flag, id, x];
DROP t [b];
EXPECT t.x NOT_NULL;
EXPECT t.x BETWEEN -5 AND 209;
FILL_NULL t.x WITH MODE;
DROP_NULL t [id];
SAVE t AS "out.csv";
```

- O0: contract error; agrees (ContractError: line 7: t.x has values outside -5 to 209)
- optimized: crash; crash, but the reference fails at line 7 (KeyError: "['b'] not found in axis")

### p0087

```text
LOAD "data.csv" AS t;
FILTER t WHERE NOT (a <= 54 OR cat != "blue");
FILL_NULL t.x WITH MEAN;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p0103

```text
LOAD "data.csv" AS t;
FILTER t WHERE (flag == TRUE AND (y >= 71.7 AND x <= 78.9));
SELECT t [flag, y, cat, x, a, id];
DROP t [y];
EXPECT t.flag NOT_NULL;
NORMALIZE t.x USING MINMAX;
FILTER t WHERE (flag == TRUE OR a >= -52);
DROP t [cat];
FILTER t WHERE id <= 45;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 1 rows (KeyError: "['cat'] not found in axis")

### p0104

```text
LOAD "data.csv" AS t;
SELECT t [y, a, cat, x, b, id, flag];
NORMALIZE t.x USING ZSCORE;
FILL_NULL t.x WITH MEDIAN;
DROP t [y];
DERIVE t.d1 = id - id + 2;
EXPECT t.x BETWEEN -100 AND 182;
FILTER t WHERE (cat == "green" AND (a > -49 AND flag != FALSE));
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not found in axis"), but the reference saves 0 rows (KeyError: "['y'] not found in axis")

### p0110

```text
LOAD "data.csv" AS t;
DROP_NULL t [a, id];
FILTER t WHERE (flag == FALSE AND (x <= 65.9 OR id >= 16));
FILTER t WHERE ((b <= -7 OR x != 21.3) AND cat == "green");
DROP t [b];
DROP_NULL t [id, cat];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 6 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 6 rows (TypeError: boolean value of NA is ambiguous)

### p0111

```text
LOAD "data.csv" AS t;
EXPECT t.id NOT_NULL;
SELECT t [flag, y, id, b, a];
EXPECT t.id NOT_NULL;
DROP t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 18 rows (KeyError: "['a'] not found in axis")

### p0124

```text
LOAD "data.csv" AS t;
DROP t [a];
SELECT t [id, flag, y, cat];
DROP t [cat];
FILTER t WHERE (flag == TRUE AND flag == TRUE);
NORMALIZE t.y USING MINMAX;
FILL_NULL t.y WITH MEAN;
DERIVE t.d1 = id * y;
DERIVE t.d2 = y + d1 + 3;
DERIVE t.d3 = d2 * y * 2;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 13 rows (KeyError: "['cat'] not found in axis")

### p0186

```text
LOAD "data.csv" AS t;
SELECT t [y, id, a, flag, x, cat];
DROP_NULL t [y];
SELECT t [cat, id, y, x];
SELECT t [x, y, id];
DERIVE t.d1 = x + y;
FILTER t WHERE NOT x <= 16.2;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not in index"), but the reference saves 1 rows (KeyError: "['cat'] not in index")

### p0224

```text
LOAD "data.csv" AS t;
DERIVE t.d1 = y - id + 4;
FILTER t WHERE ((flag == TRUE OR id > -44) AND (cat != "red" AND x == 97.8));
FILTER t WHERE id != -41;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p0232

```text
LOAD "data.csv" AS t;
FILTER t WHERE ((cat != "green" AND a == 100) AND (b >= 101 OR cat == "grey"));
FILL_NULL t.b WITH 5;
SELECT t [b, x, a, flag, id, y, cat];
DROP t [flag];
SELECT t [x, id, y, b];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p0261

```text
LOAD "data.csv" AS t;
FILTER t WHERE (b <= 17 AND (flag != FALSE AND cat == "grey"));
EXPECT t.flag NOT_NULL;
FILTER t WHERE (a <= 23 AND (flag == FALSE AND flag != TRUE));
FILL_NULL t.cat WITH "none";
DROP t [x];
FILTER t WHERE (b >= -20 OR flag == TRUE);
FILTER t WHERE flag != TRUE;
DROP t [cat];
SELECT t [b, id, y, flag];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p0285

```text
LOAD "data.csv" AS t;
DROP t [flag];
SELECT t [a, y, x, cat, b, id];
DROP t [cat];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 25 rows (KeyError: "['cat'] not found in axis")

### p0288

```text
LOAD "data.csv" AS t;
NORMALIZE t.y USING MINMAX;
SELECT t [a, y, flag, b, cat, id];
SELECT t [flag, id, a, cat];
FILTER t WHERE (id != 87 OR NOT cat == "grey");
FILL_NULL t.id WITH MEDIAN;
DROP_NULL t [id, flag];
FILL_NULL t.a WITH MEAN;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 8 rows (TypeError: boolean value of NA is ambiguous)
- optimized: saved; agrees

### p0306

```text
LOAD "data.csv" AS t;
FILL_NULL t.a WITH -4;
FILTER t WHERE (flag != FALSE AND y > 49.6);
SELECT t [id, flag, x, b, a];
DROP t [x];
DROP_NULL t [id];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not found in axis"), but the reference saves 5 rows (KeyError: "['x'] not found in axis")

### p0334

```text
LOAD "data.csv" AS t;
DROP t [y];
SELECT t [id, x, b, a, flag, cat];
DROP t [a];
EXPECT t.flag NOT_NULL;
FILL_NULL t.b WITH MEDIAN;
SELECT t [cat, b, flag, x, id];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 16 rows (KeyError: "['a'] not found in axis")

### p0337

```text
LOAD "data.csv" AS t;
FILL_NULL t.flag WITH FALSE;
FILL_NULL t.id WITH MEDIAN;
SELECT t [cat, flag, id, y, b, a, x];
FILTER t WHERE ((b != 109 OR flag == TRUE) OR (id == -0.7 OR cat != "grey"));
SELECT t [x, a, cat, id];
FILTER t WHERE (x >= 30.2 OR (cat == "green" AND id >= 72.1));
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 4 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 4 rows (TypeError: boolean value of NA is ambiguous)

### p0345

```text
LOAD "data.csv" AS t;
FILTER t WHERE (id > -12 OR cat == "grey");
FILTER t WHERE ((b <= 88 AND y <= 48.6) AND (cat == "green" OR y < 69.7));
FILTER t WHERE y <= 85.4;
DERIVE t.d1 = x + b;
FILL_NULL t.a WITH MODE;
NORMALIZE t.b USING MINMAX;
NORMALIZE t.y USING MINMAX;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 2 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 2 rows (TypeError: boolean value of NA is ambiguous)

### p0354

```text
LOAD "data.csv" AS t;
FILTER t WHERE (y == 4.3 AND b >= -1);
FILL_NULL t.id WITH MEAN;
FILL_NULL t.b WITH 3;
SELECT t [b, x, flag, cat, y, id, a];
DROP t [flag];
EXPECT t.cat NOT_NULL;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 0 rows (KeyError: "['flag'] not found in axis")

### p0359

```text
LOAD "data.csv" AS t;
DERIVE t.d1 = b + x + 2;
FILL_NULL t.a WITH MEAN;
FILTER t WHERE flag == FALSE;
SELECT t [y, d1, flag, id, cat, x, b, a];
DROP t [b];
FILTER t WHERE NOT x <= 13.2;
SELECT t [x, id, flag];
FILTER t WHERE flag != TRUE;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['b'] not found in axis"), but the reference saves 8 rows (KeyError: "['b'] not found in axis")

### p0361

```text
LOAD "data.csv" AS t;
SELECT t [id, flag, a, cat, x];
FILTER t WHERE (flag != TRUE AND id > 59);
DROP t [flag];
DROP t [cat];
DERIVE t.d1 = a + a * 3;
EXPECT t.x NOT_NULL;
DROP_NULL t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 0 rows (KeyError: "['cat'] not found in axis")

### p0371

```text
LOAD "data.csv" AS t;
FILTER t WHERE (NOT x > 2.9 OR (cat != "red" AND id < -42));
FILTER t WHERE b > -20;
FILL_NULL t.cat WITH "none";
SELECT t [x, y, id, a, b, flag];
DROP t [y];
FILL_NULL t.id WITH 1;
NORMALIZE t.x USING ZSCORE;
EXPECT t.x NOT_NULL;
DROP_NULL t [flag];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not found in axis"), but the reference saves 11 rows (KeyError: "['y'] not found in axis")

### p0377

```text
LOAD "data.csv" AS t;
FILTER t WHERE NOT id == 78;
SELECT t [flag, y, b, id, x];
NORMALIZE t.b USING ZSCORE;
DROP t [b];
FILTER t WHERE x < 90.7;
FILTER t WHERE (y == 93.0 OR (x != 1.0 OR id >= 79));
FILL_NULL t.y WITH MODE;
EXPECT t.y BETWEEN -55 AND 187;
FILL_NULL t.id WITH MEAN;
SAVE t AS "out.csv";
```

- O0: contract error; agrees (ContractError: line 9: t.y has values outside -55 to 187)
- optimized: crash; crash, but the reference fails at line 9 (KeyError: "['b'] not found in axis")

### p0379

```text
LOAD "data.csv" AS t;
SELECT t [a, id, cat, flag, b];
DROP t [a];
SELECT t [id, b, flag, cat];
FILL_NULL t.cat WITH "none";
FILL_NULL t.cat WITH "none";
DERIVE t.d1 = b + b + 3;
NORMALIZE t.d1 USING ZSCORE;
DROP_NULL t [flag];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 19 rows (KeyError: "['a'] not found in axis")

### p0391

```text
LOAD "data.csv" AS t;
FILL_NULL t.x WITH 4.5;
EXPECT t.a BETWEEN -66 AND 145;
FILL_NULL t.y WITH MODE;
SELECT t [b, id, cat, x, y, flag, a];
DROP t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 22 rows (KeyError: "['a'] not found in axis")

### p0402

```text
LOAD "data.csv" AS t;
FILL_NULL t.b WITH MEDIAN;
FILTER t WHERE ((x < 42.7 OR cat == "grey") AND y == 55.7);
EXPECT t.id BETWEEN -33 AND 168;
EXPECT t.a BETWEEN -110 AND 145;
EXPECT t.a BETWEEN -74 AND 216;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p0405

```text
LOAD "data.csv" AS t;
SELECT t [a, cat, id, b];
DROP t [cat];
FILL_NULL t.id WITH MEDIAN;
FILL_NULL t.id WITH -2.5;
FILTER t WHERE id != 1.3;
FILL_NULL t.a WITH -4;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 33 rows (KeyError: "['cat'] not found in axis")

### p0417

```text
LOAD "data.csv" AS t;
SELECT t [b, cat, id, x, flag, y, a];
SELECT t [y, flag, x, id, a];
FILTER t WHERE y != 25.4;
FILTER t WHERE y >= 33.7;
SELECT t [y, x, id, a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not in index"), but the reference saves 9 rows (KeyError: "['flag'] not in index")

### p0421

```text
LOAD "data.csv" AS t;
FILL_NULL t.a WITH MODE;
DROP t [x];
EXPECT t.id BETWEEN -58 AND 86;
SELECT t [a, flag, id, b];
DROP t [flag];
FILL_NULL t.id WITH 5;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 36 rows (KeyError: "['flag'] not found in axis")

### p0429

```text
LOAD "data.csv" AS t;
FILTER t WHERE (a >= 38 OR cat != "red");
FILTER t WHERE cat != "blue";
EXPECT t.flag NOT_NULL;
DROP t [a];
FILTER t WHERE flag == FALSE;
FILL_NULL t.y WITH MEAN;
FILL_NULL t.y WITH MEAN;
SAVE t AS "out.csv";
```

- O0: crash; crash, but the reference fails at line 4 (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash, but the reference fails at line 4 (TypeError: boolean value of NA is ambiguous)

### p0435

```text
LOAD "data.csv" AS t;
SELECT t [a, flag, y, x, id, b, cat];
FILTER t WHERE (y >= 79.6 AND y == 95.1);
SELECT t [id, y, a, b, cat];
NORMALIZE t.a USING ZSCORE;
FILL_NULL t.y WITH 0.5;
SELECT t [b, a, cat, id];
FILL_NULL t.cat WITH "none";
SELECT t [a, b, id];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not in index"), but the reference saves 0 rows (KeyError: "['cat'] not in index")

### p0436

```text
LOAD "data.csv" AS t;
SELECT t [x, id, flag, b, y, a];
DROP t [a];
FILL_NULL t.id WITH 4;
SELECT t [flag, y, b, id];
DROP t [flag];
NORMALIZE t.b USING ZSCORE;
EXPECT t.b BETWEEN -50 AND 21;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 32 rows (KeyError: "['a'] not found in axis")

### p0441

```text
LOAD "data.csv" AS t;
FILTER t WHERE (y < 13.1 AND (b == -52 OR cat != "green"));
NORMALIZE t.x USING ZSCORE;
DERIVE t.d1 = y * a;
EXPECT t.y NOT_NULL;
DROP t [d1];
FILL_NULL t.b WITH MEAN;
DROP t [y];
DROP t [cat];
FILTER t WHERE a >= 30;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 7 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 7 rows (TypeError: boolean value of NA is ambiguous)

### p0461

```text
LOAD "data.csv" AS t;
NORMALIZE t.x USING MINMAX;
SELECT t [id, b, y, x, flag, cat, a];
DROP t [flag];
EXPECT t.id BETWEEN -3 AND 169;
FILTER t WHERE a > 24;
FILTER t WHERE a <= 67;
FILTER t WHERE id >= 105;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 0 rows (KeyError: "['flag'] not found in axis")

### p0466

```text
LOAD "data.csv" AS t;
DERIVE t.d1 = b + y * 2;
FILTER t WHERE (a >= -48 OR (cat == "blue" AND d1 > 11.2));
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 33 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 33 rows (TypeError: boolean value of NA is ambiguous)

### p0468

```text
LOAD "data.csv" AS t;
FILTER t WHERE ((a >= 97 AND y > 47.9) AND cat == "green");
SELECT t [x, cat, a, id, y, flag];
FILTER t WHERE id == 100;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p0474

```text
LOAD "data.csv" AS t;
NORMALIZE t.y USING ZSCORE;
FILTER t WHERE (y == 5.1 AND cat != "blue");
SELECT t [flag, id, y];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p0551

```text
LOAD "data.csv" AS t;
FILTER t WHERE a <= 117;
SELECT t [id, flag, cat, x, b];
DROP t [x];
FILTER t WHERE (id == -24 AND cat != "blue");
DROP_NULL t [id];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not found in axis"), but the reference saves 0 rows (KeyError: "['x'] not found in axis")

### p0563

```text
LOAD "data.csv" AS t;
NORMALIZE t.x USING ZSCORE;
SELECT t [id, b, a, cat, y, flag, x];
FILTER t WHERE (y <= 20.2 OR cat == "blue");
DERIVE t.d1 = id - b;
FILL_NULL t.x WITH 2.5;
EXPECT t.a NOT_NULL;
EXPECT t.id BETWEEN -21 AND 157;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 18 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 18 rows (TypeError: boolean value of NA is ambiguous)

### p0573

```text
LOAD "data.csv" AS t;
FILTER t WHERE y <= 73.6;
DERIVE t.d1 = y - a + 1;
EXPECT t.cat NOT_NULL;
DROP t [cat];
FILL_NULL t.y WITH -1.5;
EXPECT t.x NOT_NULL;
SELECT t [flag, a, y, id];
DROP t [flag];
DROP_NULL t;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 10 rows (KeyError: "['flag'] not found in axis")

### p0652

```text
LOAD "data.csv" AS t;
SELECT t [a, id, y, flag, cat, b];
DROP t [cat];
DERIVE t.d1 = a * y;
FILL_NULL t.id WITH 1;
DROP_NULL t [b, d1];
FILL_NULL t.b WITH MODE;
FILTER t WHERE id < -54;
FILL_NULL t.y WITH MEAN;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 0 rows (KeyError: "['cat'] not found in axis")

### p0700

```text
LOAD "data.csv" AS t;
NORMALIZE t.b USING ZSCORE;
SELECT t [id, b, a, y, x];
DROP t [b];
DROP t [a];
DERIVE t.d1 = x - id;
FILL_NULL t.y WITH MEDIAN;
EXPECT t.x NOT_NULL;
EXPECT t.id NOT_NULL;
DROP t [x];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['b'] not found in axis"), but the reference saves 30 rows (KeyError: "['b'] not found in axis")

### p0707

```text
LOAD "data.csv" AS t;
FILTER t WHERE ((flag != TRUE AND id <= 119) OR cat != "grey");
EXPECT t.x NOT_NULL;
FILL_NULL t.b WITH -3;
DROP_NULL t;
EXPECT t.cat NOT_NULL;
FILL_NULL t.y WITH MEDIAN;
EXPECT t.b NOT_NULL;
EXPECT t.b NOT_NULL;
DERIVE t.d1 = id * b;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 11 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 11 rows (TypeError: boolean value of NA is ambiguous)

### p0714

```text
LOAD "data.csv" AS t;
FILTER t WHERE flag == TRUE;
FILL_NULL t.b WITH MEDIAN;
NORMALIZE t.y USING MINMAX;
SELECT t [cat, flag, b, id, a, y, x];
FILTER t WHERE ((flag == TRUE AND cat != "blue") OR (id != -48 OR y <= 48.5));
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 7 rows (TypeError: boolean value of NA is ambiguous)
- optimized: saved; agrees

### p0717

```text
LOAD "data.csv" AS t;
DROP_NULL t [flag];
SELECT t [id, flag, cat, x, y, a, b];
DROP t [x];
SELECT t [a, y, cat, flag, b, id];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not found in axis"), but the reference saves 5 rows (KeyError: "['x'] not found in axis")

### p0752

```text
LOAD "data.csv" AS t;
FILTER t WHERE ((a < 40 OR cat != "grey") OR flag == TRUE);
DROP_NULL t;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 13 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 13 rows (TypeError: boolean value of NA is ambiguous)

### p0770

```text
LOAD "data.csv" AS t;
NORMALIZE t.y USING ZSCORE;
NORMALIZE t.a USING ZSCORE;
DERIVE t.d1 = id + y;
SELECT t [id, b, flag, x, d1, y, cat];
DROP t [flag];
DERIVE t.d2 = b + id;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 19 rows (KeyError: "['flag'] not found in axis")

### p0774

```text
LOAD "data.csv" AS t;
FILL_NULL t.id WITH 5;
EXPECT t.id NOT_NULL;
FILL_NULL t.b WITH -4;
SELECT t [flag, a, y, id, cat];
DROP_NULL t [id, cat];
DERIVE t.d1 = id - y;
SELECT t [flag, d1, cat, id];
DROP t [flag];
FILTER t WHERE cat == "green";
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not in index"), but the reference saves 2 rows (KeyError: "['flag'] not in index")

### p0801

```text
LOAD "data.csv" AS t;
SELECT t [x, y, b, id];
SELECT t [id, b, x, y];
DROP t [x];
DROP_NULL t;
EXPECT t.b NOT_NULL;
DROP_NULL t;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not in index"), but the reference saves 7 rows (KeyError: "['x'] not in index")

### p0826

```text
LOAD "data.csv" AS t;
EXPECT t.id NOT_NULL;
DERIVE t.d1 = a + x;
NORMALIZE t.d1 USING ZSCORE;
FILTER t WHERE (NOT b > -23 AND cat != "red");
EXPECT t.x BETWEEN -23 AND 150;
DROP_NULL t [flag];
FILTER t WHERE (flag == TRUE AND (flag != TRUE OR b <= 71));
EXPECT t.b NOT_NULL;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p0845

```text
LOAD "data.csv" AS t;
DROP t [y];
SELECT t [id, flag, a, cat, x];
DERIVE t.d1 = x + x;
DROP t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 28 rows (KeyError: "['a'] not found in axis")

### p0884

```text
LOAD "data.csv" AS t;
SELECT t [id, b, x, flag, y, cat];
FILL_NULL t.id WITH MODE;
FILL_NULL t.id WITH 5;
FILTER t WHERE y >= 62.1;
DROP t [x];
FILTER t WHERE id > -59;
SELECT t [b, flag, id];
EXPECT t.b BETWEEN -18 AND 126;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not found in axis"), but the reference saves 1 rows (KeyError: "['x'] not found in axis")

### p0888

```text
LOAD "data.csv" AS t;
FILL_NULL t.a WITH MEDIAN;
SELECT t [cat, id, flag];
EXPECT t.id NOT_NULL;
FILTER t WHERE (cat == "green" OR (id > 24 OR cat != "red"));
EXPECT t.id NOT_NULL;
FILL_NULL t.flag WITH TRUE;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 9 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 9 rows (TypeError: boolean value of NA is ambiguous)

### p0898

```text
LOAD "data.csv" AS t;
SELECT t [id, x, a, flag, b, cat];
FILL_NULL t.flag WITH FALSE;
FILTER t WHERE id == -43;
EXPECT t.x BETWEEN -117 AND 69;
NORMALIZE t.a USING ZSCORE;
FILL_NULL t.a WITH MEAN;
DROP t [cat];
EXPECT t.a BETWEEN -85 AND 182;
FILTER t WHERE ((a != 46.0 OR b <= 93) AND x >= 90.4);
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 0 rows (KeyError: "['cat'] not found in axis")

### p0964

```text
LOAD "data.csv" AS t;
DROP t [a];
SELECT t [b, id, x, cat, flag];
DROP t [b];
FILTER t WHERE cat != "red";
FILTER t WHERE id > 57;
FILTER t WHERE (cat != "red" AND id < 84);
FILL_NULL t.cat WITH "none";
EXPECT t.x BETWEEN -64 AND 102;
FILL_NULL t.x WITH 5.5;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['b'] not found in axis"), but the reference saves 0 rows (KeyError: "['b'] not found in axis")

### p1000

```text
LOAD "data.csv" AS t;
EXPECT t.cat NOT_NULL;
SELECT t [x, id, a, flag];
DROP t [x];
FILTER t WHERE flag != TRUE;
FILTER t WHERE a < 78;
FILL_NULL t.flag WITH TRUE;
DERIVE t.d1 = id + id + 3;
FILL_NULL t.d1 WITH -2;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not found in axis"), but the reference saves 2 rows (KeyError: "['x'] not found in axis")

### p1044

```text
LOAD "data.csv" AS t;
EXPECT t.id BETWEEN -20 AND 179;
FILTER t WHERE x > 63.9;
DROP_NULL t [b, a];
NORMALIZE t.b USING ZSCORE;
SELECT t [b, a, cat, y, flag, id];
EXPECT t.a NOT_NULL;
DROP t [y];
DROP t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not found in axis"), but the reference saves 3 rows (KeyError: "['y'] not found in axis")

### p1070

```text
LOAD "data.csv" AS t;
EXPECT t.cat NOT_NULL;
FILL_NULL t.id WITH 1;
SELECT t [b, id, a, cat, flag];
NORMALIZE t.b USING MINMAX;
NORMALIZE t.b USING ZSCORE;
DROP t [b];
DROP_NULL t [a, flag];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['b'] not found in axis"), but the reference saves 14 rows (KeyError: "['b'] not found in axis")

### p1071

```text
LOAD "data.csv" AS t;
SELECT t [x, flag, y, id, a];
FILTER t WHERE (flag == FALSE OR x < 18.8);
FILL_NULL t.a WITH 1;
FILL_NULL t.y WITH 2.5;
EXPECT t.a NOT_NULL;
NORMALIZE t.y USING MINMAX;
DROP t [a];
DERIVE t.d1 = x * y;
DERIVE t.d2 = x * id;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 28 rows (KeyError: "['a'] not found in axis")

### p1076

```text
LOAD "data.csv" AS t;
FILTER t WHERE (y > 57.5 AND (flag == FALSE AND y != -0.5));
NORMALIZE t.a USING ZSCORE;
SELECT t [flag, a, id, y, cat, b];
EXPECT t.id BETWEEN -105 AND 157;
FILL_NULL t.cat WITH "none";
DROP t [a];
DROP t [cat];
FILL_NULL t.id WITH 5;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 5 rows (KeyError: "['a'] not found in axis")

### p1098

```text
LOAD "data.csv" AS t;
SELECT t [id, cat, flag];
FILTER t WHERE cat == "blue";
DERIVE t.d1 = id - id;
DROP t [flag];
DROP_NULL t [d1, cat];
FILTER t WHERE cat == "red";
FILTER t WHERE cat == "blue";
NORMALIZE t.d1 USING MINMAX;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 0 rows (KeyError: "['flag'] not found in axis")

### p1116

```text
LOAD "data.csv" AS t;
FILL_NULL t.id WITH MEDIAN;
FILTER t WHERE x != 61.2;
FILTER t WHERE flag != TRUE;
SELECT t [x, y, id, a];
FILL_NULL t.id WITH MODE;
DROP t [a];
DERIVE t.d1 = y * y;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 9 rows (KeyError: "['a'] not found in axis")

### p1123

```text
LOAD "data.csv" AS t;
FILL_NULL t.x WITH MODE;
FILTER t WHERE (b == 98 AND cat == "blue");
FILTER t WHERE ((x == 10.1 OR b > 101) OR (y <= 18.9 OR id >= 54));
FILTER t WHERE y <= 68.6;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p1160

```text
LOAD "data.csv" AS t;
DERIVE t.d1 = id * id;
DROP t [x];
DERIVE t.d2 = id - id * 4;
FILL_NULL t.d2 WITH MODE;
FILTER t WHERE (NOT y != 37.2 AND cat != "red");
FILTER t WHERE (d2 < 117 OR d2 > 41);
FILTER t WHERE y > 58.1;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p1168

```text
LOAD "data.csv" AS t;
NORMALIZE t.x USING ZSCORE;
SELECT t [y, flag, x, id, b, cat, a];
NORMALIZE t.y USING MINMAX;
DROP t [a];
NORMALIZE t.x USING MINMAX;
DROP t [y];
FILTER t WHERE b >= -17;
DROP t [flag];
EXPECT t.b NOT_NULL;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 22 rows (KeyError: "['a'] not found in axis")

### p1173

```text
LOAD "data.csv" AS t;
SELECT t [id, cat, y, x];
FILL_NULL t.id WITH 2;
SELECT t [y, id, x, cat];
EXPECT t.id NOT_NULL;
DROP t [y];
FILL_NULL t.x WITH MEAN;
EXPECT t.id NOT_NULL;
NORMALIZE t.x USING ZSCORE;
FILL_NULL t.id WITH -5;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not in index"), but the reference saves 7 rows (KeyError: "['y'] not in index")

### p1181

```text
LOAD "data.csv" AS t;
FILL_NULL t.b WITH MODE;
DERIVE t.d1 = y - y;
SELECT t [b, x, id, flag, y, cat, a, d1];
SELECT t [flag, a, y, id, cat];
FILL_NULL t.y WITH -2.5;
DROP t [y];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not in index"), but the reference saves 28 rows (KeyError: "['y'] not in index")

### p1202

```text
LOAD "data.csv" AS t;
FILTER t WHERE ((x != 60.9 AND b < 114) AND (flag == FALSE OR cat != "green"));
FILTER t WHERE b < 14;
NORMALIZE t.y USING ZSCORE;
FILL_NULL t.id WITH MODE;
FILL_NULL t.y WITH -2.5;
SELECT t [b, id, cat, flag, y, x];
DROP t [y];
NORMALIZE t.b USING MINMAX;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not found in axis"), but the reference saves 6 rows (KeyError: "['y'] not found in axis")

### p1206

```text
LOAD "data.csv" AS t;
SELECT t [id, x, y, a];
SELECT t [x, y, a, id];
NORMALIZE t.a USING MINMAX;
DROP t [x];
FILTER t WHERE ((a >= 92.2 OR id <= 53) OR y >= 20.0);
FILL_NULL t.id WITH MODE;
DROP_NULL t [id, y];
EXPECT t.id NOT_NULL;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not in index"), but the reference saves 16 rows (KeyError: "['x'] not in index")

### p1235

```text
LOAD "data.csv" AS t;
FILTER t WHERE b != -45;
FILTER t WHERE (x > 83.9 AND NOT cat != "blue");
FILTER t WHERE (NOT id > 3 AND y <= 48.9);
FILL_NULL t.b WITH MODE;
EXPECT t.a BETWEEN -86 AND 63;
FILL_NULL t.flag WITH FALSE;
DROP t [a];
DROP_NULL t;
SELECT t [id, x, cat, b, y, flag];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p1236

```text
LOAD "data.csv" AS t;
FILTER t WHERE (flag != FALSE AND (cat == "red" OR flag != TRUE));
FILTER t WHERE (a >= 34 OR y >= 22.9);
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 2 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 2 rows (TypeError: boolean value of NA is ambiguous)

### p1243

```text
LOAD "data.csv" AS t;
FILTER t WHERE ((y != 0.3 AND a <= 76) OR (cat == "red" OR y < 15.1));
DROP t [b];
SELECT t [flag, cat, x, y, a, id];
FILL_NULL t.cat WITH "none";
FILTER t WHERE NOT (flag != TRUE OR id > 43);
EXPECT t.a NOT_NULL;
DROP t [flag];
DROP t [a];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 12 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 12 rows (TypeError: boolean value of NA is ambiguous)

### p1248

```text
LOAD "data.csv" AS t;
FILTER t WHERE ((x > 30.4 AND x < 83.5) AND cat == "grey");
NORMALIZE t.x USING ZSCORE;
SELECT t [id, x, cat, flag, b, y, a];
FILL_NULL t.y WITH 2.5;
DROP_NULL t [a, x];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p1287

```text
LOAD "data.csv" AS t;
FILTER t WHERE id < 61;
SELECT t [y, b, flag, x, id, cat];
FILL_NULL t.id WITH MODE;
DROP t [y];
DROP t [flag];
EXPECT t.id BETWEEN -101 AND 163;
FILTER t WHERE x == 17.2;
EXPECT t.x BETWEEN -98 AND 50;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not found in axis"), but the reference saves 0 rows (KeyError: "['y'] not found in axis")

### p1292

```text
LOAD "data.csv" AS t;
FILL_NULL t.b WITH -5;
DROP t [y];
SELECT t [cat, id, x, a];
DROP t [cat];
DROP_NULL t;
DERIVE t.d1 = a * x + 3;
FILL_NULL t.id WITH -4;
NORMALIZE t.a USING MINMAX;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 11 rows (KeyError: "['cat'] not found in axis")

### p1306

```text
LOAD "data.csv" AS t;
FILTER t WHERE id <= 30;
DROP_NULL t [flag];
FILTER t WHERE b >= 43;
EXPECT t.id BETWEEN -50 AND 244;
FILTER t WHERE (b > -24 OR (cat == "blue" OR id >= 38));
EXPECT t.x NOT_NULL;
SAVE t AS "out.csv";
```

- O0: crash; crash, but the reference fails at line 7 (TypeError: boolean value of NA is ambiguous)
- optimized: contract error; agrees (ContractError: line 7: t.x has missing values)

### p1315

```text
LOAD "data.csv" AS t;
DERIVE t.d1 = b + a;
SELECT t [x, a, y, id];
DROP t [a];
FILTER t WHERE x == 2.4;
NORMALIZE t.y USING ZSCORE;
FILL_NULL t.id WITH 4;
FILL_NULL t.y WITH 1.5;
FILTER t WHERE (id == 50 AND NOT x >= 64.2);
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 0 rows (KeyError: "['a'] not found in axis")

### p1330

```text
LOAD "data.csv" AS t;
FILL_NULL t.b WITH MODE;
SELECT t [id, a, flag, b];
EXPECT t.b NOT_NULL;
NORMALIZE t.b USING MINMAX;
DROP t [flag];
FILL_NULL t.a WITH MODE;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 10 rows (KeyError: "['flag'] not found in axis")

### p1341

```text
LOAD "data.csv" AS t;
DROP_NULL t [y, id];
SELECT t [x, b, id, a, flag, cat, y];
SELECT t [y, a, id, cat, x, b, flag];
FILL_NULL t.b WITH MODE;
FILTER t WHERE NOT (cat != "blue" AND id > 77);
SELECT t [id, b, a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x', 'flag'] not in index"), but the reference saves 18 rows (KeyError: "['x', 'flag'] not in index")

### p1355

```text
LOAD "data.csv" AS t;
FILTER t WHERE (x >= 47.5 OR (cat != "green" AND x <= 60.4));
NORMALIZE t.x USING ZSCORE;
DROP t [flag];
FILTER t WHERE cat == "blue";
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 5 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 5 rows (TypeError: boolean value of NA is ambiguous)

### p1392

```text
LOAD "data.csv" AS t;
FILL_NULL t.b WITH MEAN;
DROP t [b];
SELECT t [a, y, flag, x, id, cat];
FILTER t WHERE ((cat == "blue" AND y <= 50.4) AND a <= -16);
DROP t [x];
FILL_NULL t.y WITH MODE;
FILL_NULL t.id WITH MEDIAN;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not found in axis"), but the reference saves 0 rows (KeyError: "['x'] not found in axis")

### p1429

```text
LOAD "data.csv" AS t;
DROP_NULL t [a];
FILTER t WHERE (b >= 73 OR cat == "blue");
FILTER t WHERE flag == TRUE;
FILL_NULL t.x WITH -3.5;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 3 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 3 rows (TypeError: boolean value of NA is ambiguous)

### p1454

```text
LOAD "data.csv" AS t;
NORMALIZE t.x USING MINMAX;
EXPECT t.flag NOT_NULL;
DERIVE t.d1 = x + y;
FILTER t WHERE (id > 91 OR (cat != "grey" OR a <= 51));
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 3 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 3 rows (TypeError: boolean value of NA is ambiguous)

### p1518

```text
LOAD "data.csv" AS t;
SELECT t [y, id, cat, a];
EXPECT t.id NOT_NULL;
FILTER t WHERE ((y > 64.7 OR cat == "blue") AND y != 3.8);
NORMALIZE t.y USING MINMAX;
FILL_NULL t.y WITH 2.5;
FILTER t WHERE (id < -5 AND y == 71.8);
FILL_NULL t.a WITH 2;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p1533

```text
LOAD "data.csv" AS t;
SELECT t [b, cat, id, x, y, a];
FILL_NULL t.y WITH MEDIAN;
DROP t [y];
DERIVE t.d1 = x + a;
DROP_NULL t [id];
DERIVE t.d2 = a - id + 1;
EXPECT t.d2 NOT_NULL;
NORMALIZE t.d2 USING MINMAX;
DROP_NULL t;
SAVE t AS "out.csv";
```

- O0: contract error; agrees (ContractError: line 8: t.d2 has missing values)
- optimized: crash; crash, but the reference fails at line 8 (KeyError: "['y'] not found in axis")

### p1538

```text
LOAD "data.csv" AS t;
SELECT t [cat, x, a, id, b, flag, y];
NORMALIZE t.b USING ZSCORE;
FILL_NULL t.cat WITH "none";
DERIVE t.d1 = a * y;
DROP t [flag];
SELECT t [cat, id, y, d1, a, x, b];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 39 rows (KeyError: "['flag'] not found in axis")

### p1580

```text
LOAD "data.csv" AS t;
SELECT t [y, id, x, flag, a];
DROP t [x];
DERIVE t.d1 = a + y * 4;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not found in axis"), but the reference saves 13 rows (KeyError: "['x'] not found in axis")

### p1590

```text
LOAD "data.csv" AS t;
EXPECT t.x NOT_NULL;
DROP t [cat];
FILL_NULL t.flag WITH TRUE;
NORMALIZE t.b USING MINMAX;
DROP t [b];
SELECT t [y, x, flag, id, a];
NORMALIZE t.a USING MINMAX;
NORMALIZE t.y USING MINMAX;
DROP t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not found in axis"), but the reference saves 4 rows (KeyError: "['a'] not found in axis")

### p1595

```text
LOAD "data.csv" AS t;
SELECT t [flag, x, y, cat, id];
SELECT t [x, id, y, flag];
DROP t [y];
EXPECT t.flag NOT_NULL;
FILL_NULL t.id WITH 4;
DERIVE t.d1 = id - x * 3;
FILTER t WHERE flag == TRUE;
NORMALIZE t.d1 USING ZSCORE;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not in index"), but the reference saves 22 rows (KeyError: "['y'] not in index")

### p1603

```text
LOAD "data.csv" AS t;
SELECT t [a, b, y, flag, x, id, cat];
EXPECT t.id BETWEEN -71 AND 227;
FILTER t WHERE b < -2;
NORMALIZE t.a USING ZSCORE;
DERIVE t.d1 = b * b + 2;
DROP t [cat];
EXPECT t.x BETWEEN -40 AND 143;
SAVE t AS "out.csv";
```

- O0: contract error; agrees (ContractError: line 8: t.x has values outside -40 to 143)
- optimized: crash; crash, but the reference fails at line 8 (KeyError: "['cat'] not found in axis")

### p1629

```text
LOAD "data.csv" AS t;
DROP t [x];
EXPECT t.b NOT_NULL;
DROP t [y];
FILTER t WHERE (b <= -52 AND cat == "green");
EXPECT t.b BETWEEN -19 AND 143;
EXPECT t.id BETWEEN -86 AND 79;
SELECT t [flag, b, cat, a, id];
DROP_NULL t [cat];
DROP_NULL t [id, b];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p1641

```text
LOAD "data.csv" AS t;
SELECT t [flag, id, y, b, a];
FILL_NULL t.a WITH MEAN;
NORMALIZE t.a USING ZSCORE;
SELECT t [b, a, id, flag];
FILTER t WHERE b != 11;
NORMALIZE t.a USING ZSCORE;
FILL_NULL t.id WITH 1;
DROP t [a];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['a'] not in index"), but the reference saves 36 rows (KeyError: "['a'] not in index")

### p1646

```text
LOAD "data.csv" AS t;
SELECT t [b, flag, a, id, y, cat];
DROP t [flag];
DROP t [a];
SELECT t [b, cat, id];
DERIVE t.d1 = id - id;
DROP t [cat];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 28 rows (KeyError: "['flag'] not found in axis")

### p1654

```text
LOAD "data.csv" AS t;
SELECT t [x, a, id, flag, y, cat, b];
FILL_NULL t.x WITH MEDIAN;
DROP t [flag];
FILTER t WHERE (cat == "blue" OR id < 33);
DERIVE t.d1 = id + b + 2;
NORMALIZE t.d1 USING MINMAX;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 9 rows (KeyError: "['flag'] not found in axis")

### p1770

```text
LOAD "data.csv" AS t;
FILTER t WHERE (x > 32.8 OR (id < 1 AND cat == "blue"));
EXPECT t.b BETWEEN -95 AND 18;
DROP t [cat];
DROP t [b];
SAVE t AS "out.csv";
```

- O0: crash; crash, but the reference fails at line 3 (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash, but the reference fails at line 3 (TypeError: boolean value of NA is ambiguous)

### p1792

```text
LOAD "data.csv" AS t;
FILL_NULL t.id WITH -1;
FILTER t WHERE (a <= 29 OR cat != "blue");
EXPECT t.y NOT_NULL;
SAVE t AS "out.csv";
```

- O0: crash; crash, but the reference fails at line 4 (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash, but the reference fails at line 4 (TypeError: boolean value of NA is ambiguous)

### p1821

```text
LOAD "data.csv" AS t;
FILTER t WHERE flag != TRUE;
FILTER t WHERE ((id > 39 AND cat == "red") AND (b >= -60 AND x > 38.2));
NORMALIZE t.y USING MINMAX;
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p1864

```text
LOAD "data.csv" AS t;
FILTER t WHERE a == -3;
SELECT t [b, a, flag, x, y, cat, id];
DROP t [flag];
DERIVE t.d1 = id * y;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 0 rows (KeyError: "['flag'] not found in axis")

### p1878

```text
LOAD "data.csv" AS t;
SELECT t [flag, b, y, id];
DROP t [flag];
FILTER t WHERE y == 55.5;
EXPECT t.b BETWEEN -110 AND 38;
FILL_NULL t.id WITH 2;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 0 rows (KeyError: "['flag'] not found in axis")

### p1879

```text
LOAD "data.csv" AS t;
NORMALIZE t.a USING ZSCORE;
FILTER t WHERE x != 38.8;
SELECT t [flag, a, id, y];
FILL_NULL t.id WITH MEAN;
SELECT t [y, flag, a, id];
DROP t [y];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['y'] not in index"), but the reference saves 31 rows (KeyError: "['y'] not in index")

### p1894

```text
LOAD "data.csv" AS t;
SELECT t [b, id, cat];
NORMALIZE t.b USING ZSCORE;
DERIVE t.d1 = id - id;
NORMALIZE t.d1 USING MINMAX;
NORMALIZE t.b USING ZSCORE;
DROP t [b];
FILTER t WHERE NOT d1 != 81.2;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['b'] not found in axis"), but the reference saves 0 rows (KeyError: "['b'] not found in axis")

### p1920

```text
LOAD "data.csv" AS t;
DROP_NULL t [a];
FILTER t WHERE flag == FALSE;
SELECT t [x, flag, a, id, y];
EXPECT t.id BETWEEN -5 AND 68;
NORMALIZE t.a USING ZSCORE;
SELECT t [x, a, id, y, flag];
DERIVE t.d1 = a - id;
FILL_NULL t.flag WITH FALSE;
DROP t [x];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['x'] not in index"), but the reference saves 13 rows (KeyError: "['x'] not in index")

### p1923

```text
LOAD "data.csv" AS t;
FILTER t WHERE y > 41.8;
FILTER t WHERE cat == "green";
SELECT t [cat, flag, id, x];
DERIVE t.d1 = x * id * 4;
DROP t [flag];
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 1 rows (KeyError: "['flag'] not found in axis")

### p1944

```text
LOAD "data.csv" AS t;
DERIVE t.d1 = id - b + 2;
SELECT t [a, d1, x, cat, flag, id];
DROP t [cat];
DROP t [a];
FILL_NULL t.x WITH 2.5;
DERIVE t.d2 = id * id;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['cat'] not found in axis"), but the reference saves 36 rows (KeyError: "['cat'] not found in axis")

### p1962

```text
LOAD "data.csv" AS t;
FILTER t WHERE ((b <= 3 AND y == 60.8) OR cat != "blue");
SELECT t [b, a, x, y, flag, cat, id];
DROP t [y];
FILL_NULL t.a WITH MEDIAN;
FILL_NULL t.x WITH 0.5;
DROP t [flag];
DERIVE t.d1 = x - a;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not found in axis"), but the reference saves 28 rows (KeyError: "['flag'] not found in axis")

### p1964

```text
LOAD "data.csv" AS t;
EXPECT t.flag NOT_NULL;
DERIVE t.d1 = id * b + 4;
NORMALIZE t.d1 USING MINMAX;
FILTER t WHERE (a > -32 AND (cat == "blue" AND y > 76.0));
EXPECT t.y BETWEEN -40 AND 151;
FILL_NULL t.y WITH MODE;
FILTER t WHERE flag == FALSE;
SELECT t [x, y, flag, id, d1, a];
SAVE t AS "out.csv";
```

- O0: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)
- optimized: crash; crash (TypeError: boolean value of NA is ambiguous), but the reference saves 0 rows (TypeError: boolean value of NA is ambiguous)

### p1996

```text
LOAD "data.csv" AS t;
DERIVE t.d1 = b - x * 2;
FILL_NULL t.cat WITH "none";
SELECT t [cat, y, id, d1, flag, a, b, x];
EXPECT t.cat NOT_NULL;
SELECT t [id, flag, y, cat, x, a];
DROP_NULL t [y, cat];
DROP t [flag];
EXPECT t.id BETWEEN -49 AND 48;
NORMALIZE t.x USING MINMAX;
SAVE t AS "out.csv";
```

- O0: saved; agrees
- optimized: crash; crash (KeyError: "['flag'] not in index"), but the reference saves 18 rows (KeyError: "['flag'] not in index")

