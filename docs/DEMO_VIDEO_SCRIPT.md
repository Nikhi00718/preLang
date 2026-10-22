# Demo video script (about 4 minutes)

A screencast for the tool paper's supplementary material. Every command below was run on the
staged demo folder and gives the output described. Record the terminal only, font large enough to
read at 720p; speak the quoted lines (or add them as captions).

## Before recording

```powershell
cd C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang
cmake --build build --config Release
py -3 demo\run_demo.py prepare
py -3 demo\run_demo.py stage
cd demo\live
cls
```

`.\preplang` in `demo\live` runs the Release compiler.

## 1. A mistake found before anything runs (40 s)

```powershell
type typo.prep
.\preplang typo.prep --explain
```

Output: `error[semantic]: column 'mark' does not exist in 'students'` at `typo.prep:19:1`, and
`hint: did you mean 'marks'?`.

> PrepLang is a small language for preparing datasets. Its compiler reads the data at compile time,
> so a misspelled column is a compile error with the line and the fix. The same pandas script runs
> to the last step and crashes with a KeyError.

## 2. What the compiler knows and proves (50 s)

```powershell
type students.prep
.\preplang students.prep --explain
```

Output: the inputs with their row counts, `contracts proven 3`, `contracts checked at run time 1`,
`contract check elimination 2` and `instructions 16 -> 14`.

> Every EXPECT is proven, refuted, or left for run time. Proven contracts and operations that change
> nothing are removed, and only the columns the program needs are read (the IR is printed with
> --ir). The output is one Python program using pandas.

```powershell
.\preplang students.prep --run
```

Output: `Compiled successfully`, `Saved 4 rows to ...\output\clean.csv`.

## 3. Reusing the compiled program on next week's data (70 s)

```powershell
.\preplang students.prep --certificate
```

Output: the certificate: `marks INTEGER >= 0 <= 100 <- line 5`, `name no missing values <- line 6`,
the other columns only their type.

> The compiler records which facts each proof needs. Another file that has them can use the same
> compiled program, without compiling again.

```powershell
py -3 output\generated.py students=next_week.csv
```

Output: `Admitted next_week.csv ... it satisfies the data certificate`, `Saved 3 rows`.

```powershell
py -3 output\generated.py students=typo_week.csv
```

Output: `Admitted typo_week.csv`, then
`Deoptimized line 5: EXPECT students.marks BETWEEN 0 AND 100: marks goes up to 730, above 100`, then
`ContractError: line 5: students.marks has values outside 0 to 100`.

> The check on line 5 was removed because the compile-time data proved it. This file breaks the
> fact that proof used, so exactly that check runs again, and it catches the bad value. Nothing else
> in the program is deoptimized.

## 4. Compiling without seeing the data (50 s)

```powershell
.\preplang students.prep --queries
py -3 ..\..\tools\release_facts.py output\preplang_queries.tsv --epsilon 1 --schema-declared --out facts.tsv
.\preplang students.prep --facts facts.tsv --python
py -3 output\generated.py
```

Output: the queries file is written; the compile prints two warnings of the form
`FILL_NULL probably has no effect: by the released facts ...; it stays in the program and runs if the
data needs it`; the run saves 4 rows.

> The data owner answers the compiler's questions with differential privacy, and the analyst compiles
> from those noisy facts alone. Released facts never decide what is removed for good: the program
> still checks everything it relies on, on the real data.

## 5. An existing pandas script (30 s)

```powershell
cd titanic
type titanic_clean.py
py -3 ..\..\..\frontend\pandas2prep.py titanic_clean.py -o lifted.prep
type lifted.prep
cd ..
```

> Cleaning code that already exists can be lifted: fillna with the median becomes FILL_NULL WITH
> MEDIAN, an assert becomes an EXPECT, and the lifter says where it stopped.

## 6. Close (10 s)

> The compiler, the tests, the evaluation and this demo are in the repository; `docker build` gives
> the same environment on Linux.

## After recording

Delete `facts.tsv` and run `py -3 demo\run_demo.py stage` again before the next take. Upload the
video (MP4, H.264) as supplementary material, or link it in the paper's code metadata.
