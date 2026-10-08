# The terminal demo, typed by hand

You type every command yourself, in PowerShell, about 12 minutes. Each part says what to type, what
appears, and what to say. Every command here was run and checked.

## The day before (once)

```powershell
cd C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang
cmake --build build --config Release
py -3 demo\run_demo.py prepare
```

## Just before the review

```powershell
cd C:\Users\atnik\Documents\ChatGPT\comp_v1\PrepLang
py -3 demo\run_demo.py stage
cd demo\live
cls
```

`stage` makes a fresh `demo\live` folder: the program, the data files, the image folders, the
Titanic script, and two short commands, `.\preplang` (today's compiler) and `.\phase2` (the
compiler from Review 2). Run `stage` again whenever you want to start over.

Make the terminal font large (Ctrl + scroll) so the room can read it. Run `cls` between parts.

---

## Part 1. The problem (1.5 min)

```powershell
type students.prep
```

> This is a PrepLang program. It loads a student CSV, states contracts with EXPECT, fills missing
> values, normalizes, filters, and saves. Watch line 19: in `typo.prep` I misspelled marks as mark.

```powershell
py -3 ..\..\eval\baseline.py typo.prep script_out
```

You see a long traceback ending in `KeyError: "['mark'] not in index"`.

> A normal pandas script does all the work and then crashes at the last step.

```powershell
.\preplang typo.prep --explain
```

You see `error[semantic]: column 'mark' does not exist`, line 19, a caret, and
`hint: did you mean 'marks'?`

> PrepLang stops before running anything, with the line and the fix. On 16 million rows the script
> fails after 2.8 seconds; the compiler in 0.02.

The silent one, which is worse:

```powershell
cd images
py -3 ..\..\..\eval\baseline.py black.prep script_out 2>$null
explorer script_out
```

The folder opens: every image is black, and there was no error.

> One word is wrong: PNG instead of NPY. The pixels were scaled to between 0 and 1, and PNG stores
> 0 to 255, so every image is black.

```powershell
.\preplang black.prep --explain
cd ..
```

You see `error[type]: cannot save FLOAT32 pixels as PNG: NORMALIZE at line 10 scaled them to [0, 1]`.

> The compiler knows the pixel type because it read every image header at LOAD.

## Part 2. The compiler phases (2.5 min)

```powershell
.\preplang students.prep --tokens
```

> Phase 1, lexer: every token with its line and column, used in error messages.

```powershell
.\preplang students.prep --ast
```

> Phase 2, parser: hand-written recursive descent. Every statement starts with its own keyword, so
> one token of look-ahead decides: the grammar is LL(1).

```powershell
.\preplang students.prep --schema
```

> Phase 3: the compiler reads the CSV now, while compiling: type, missing values, and range of
> every column. Marks are 35 to 95.

```powershell
.\preplang students.prep --semantic
```

> Phase 4: the symbol table holds these facts, and every statement changes them. After FILTER
> marks >= 50, marks are 50 to 95. Look at the contracts: lines 4, 5, 6 are PROVEN; line 16 cannot
> be proven after the filter, so it is checked at run time.

```powershell
.\preplang students.prep --ir
```

> Phases 5 and 6: three-address code with temporaries t1 to t5, then the optimizer. The three
> proven checks are gone: "contract check elimination".

## Part 3. Error messages (45 s)

```powershell
cls
.\preplang semantic_error.prep --explain
```

> One run, eight errors: type errors, a misspelled column with a suggestion, contracts the data
> breaks with the row, and a column used after it was dropped. The compiler does not stop at the
> first error. (Also: `lexical_error.prep` and `invalid.prep` for the lexer and the parser.)

## Part 4. Phase 2 against Phase 3: speed (1 min)

```powershell
Measure-Command { .\phase2 big.prep } | Select-Object TotalSeconds
Measure-Command { .\preplang big.prep } | Select-Object TotalSeconds
```

About 1.9 s, then about 0.13 s.

> One million rows, 59 MB. Both compilers check every cell. Phase 2 took about two seconds. Phase 3
> reads the file once, cuts it into one piece per CPU core, and looks at cells in place without
> copying: about 15 times faster, faster than pandas just reading the file.

## Part 5. Data certificates, the main idea (2.5 min)

First the old problem. The `old` folder has the Review 2 compiler's own copy.

```powershell
cd old
.\phase2 students.prep --python | Select-String Compiled
copy next_week.csv students.csv
py -3 output\generated.py
cd ..
```

It ends with a long traceback and `ContractError: ... students.csv changed after compilation`.

> In Review 2, a compiled program was tied to the exact file by a checksum. Next week's file is
> refused even though it is perfectly fine. So you had to recompile for every new batch.

```powershell
type next_week.csv
.\preplang students.prep --certificate
```

You see the certificate: `marks INTEGER >= 0 <= 100 <- line 5`, `name no missing values <- line 6`,
every other column only its type.

> After compiling, the compiler asks which facts each proof really needs. It weakens one fact at a
> time and analyses the program again: 13 more times here. Marks were 35 to 95, but the only proof
> about marks is BETWEEN 0 AND 100, so the certificate needs only 0 to 100.

```powershell
py -3 output\generated.py students=next_week.csv
```

`Admitted next_week.csv ... it satisfies the data certificate`, `Saved 3 rows`.

> Next week has marks 12 and 100. Phase 2 refused it; the certificate admits it, without compiling
> again.

```powershell
py -3 output\generated.py students=typo_week.csv
py -3 output\generated.py students=missing_name.csv
```

Both refused, in one line: `marks is 730 in data row 3 ... line 5`, and `name is missing in data
row 2 ... line 6`.

> And a bad file is still refused, with the row and the contract that needs it.

## Part 6. Image folders (1 min)

```powershell
cd images
.\preplang imageset.prep --certificate
py -3 output\generated.py train=new_photos
py -3 output\generated.py train=small_photos
cd ..
```

`new_photos` admitted and 4 images saved; `small_photos` refused: `photo_1.jpg has width 150`.

> For images the certificate is a box: allowed channels and sizes. The program says MIN_WIDTH 200,
> so any photo at least 200 wide is fine, of any height or color. To decide, only the image
> headers are read, not the pixels.

## Part 7. An existing pandas script (1 min)

```powershell
cd titanic
type titanic_clean.py
py -3 ..\..\..\frontend\pandas2prep.py titanic_clean.py -o lifted.prep
type lifted.prep
```

> Most cleaning code already exists in pandas. This front end reads the script with Python's own
> parser and translates it into PrepLang: fillna with the median becomes FILL_NULL WITH MEDIAN, the
> assert becomes an EXPECT. It stops at line 15, the map, which PrepLang cannot express exactly.

```powershell
.\preplang lifted.prep --certificate
py -3 output\generated.py train=test.csv
cd ..
```

`Admitted test.csv`, `Saved 416 rows`.

> Kaggle's test file has no Survived column. The program only carries that column, so it may be
> missing, and the test file is admitted.

## Part 8. Tests (30 s)

```powershell
ctest --test-dir ..\..\build -C Release
```

`100% tests passed out of 9` (9 test programs, 207 tests).

> 207 unit tests. On 238 real monthly data batches the certificate admitted 86 percent, against 1
> percent for the old file's own facts, and every admitted run gave exactly the result of a fresh
> compilation.

---

## If something goes wrong

- `stage` says demo\live is in use: your terminal is still inside it. `cd ..\..` first.
- Anything looks odd: `cd ..\..` back to PrepLang, `py -3 demo\run_demo.py stage`, `cd demo\live`.
- The automatic version of the same demo: `py -3 demo\run_demo.py` (or `--only N` for one part).
- No terminal at all: open `demo\PrepLang_Demo.html`; it has the same outputs.
