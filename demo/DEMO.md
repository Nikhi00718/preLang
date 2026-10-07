# PrepLang demo: what to show and what to say

About 12 minutes, plus questions. Two things are open side by side:

- **the demo page** `demo/PrepLang_Demo.html` (works offline; arrow keys move between chapters)
- **a terminal** in the project folder for the live run: `py -3 demo/run_demo.py`

Before the review, once: build the compiler, then

```bash
py -3 demo/run_demo.py prepare
```

```bash
py -3 demo/run_demo.py --no-pause
```

The first builds the Phase 2 compiler from git (commit f088d66) and makes the sample data; the
second is a full rehearsal. If anything changed, `py -3 demo/capture.py` re-runs every scenario and
rebuilds the page from the real outputs.

## 0. Opening (30 s)

> PrepLang is a small language for cleaning tables and images before machine learning. Its
> compiler is different from a normal one: it opens the real data files while it compiles. So it
> knows the type of every column, which values are missing, the smallest and largest values, and
> the size of every image, before anything runs.

## 1. The problem (1.5 min) - page chapter 01, terminal step 1

Click **Decimal pixels saved as PNG**.

> One word is wrong: PNG instead of NPY. A normal script runs every image and finishes with no
> error; every image it wrote is black. PrepLang refuses to compile, in 0.02 seconds, and the hint
> says exactly what to change.

Click **A misspelled column in the last step**: the script crashes with a KeyError after processing
everything; PrepLang names the column and the line. Point at the line under the panels: on 16 million
rows the script fails after 2.8 s, PrepLang in 0.02 s.

## 2. How the compiler works (2.5 min) - chapter 02, terminal step 2

Click the eight phases in order. One sentence each:

1. **Lexer**: characters to tokens, with line and column for error messages.
2. **Parser**: hand-written recursive descent; every statement starts with its own keyword, so the
   grammar is LL(1). Panic-mode recovery reports several errors in one run.
3. **Schema**: the data is read here, at compile time. A one-million-row CSV takes 0.12 s.
4. **Semantic analysis**: the symbol table holds the facts about the data, and every statement
   changes them. `FILTER marks >= 50` makes marks start at 50. Contracts get their verdicts here.
5. **IR**: three-address code; conditions become temporaries t1, t2, t3.
6. **Optimizer**: removes checks that are already proven, operations that do nothing, and columns
   that are never used.
7. **Certificate** (new in Phase 3): explained in chapter 05.
8. **Code generation**: one Python program, pandas for tables, OpenCV for images.

## 3. Contracts (45 s) - chapter 03

> Proven: no check at run time. Violated: compile error naming the row. Undecidable, like after a
> filter: a run-time check, and after it passes, the compiler uses the fact.

## 4. What Phase 3 changed (1.5 min) - chapter 04, terminal step 4

> Phase 2 had two limits. Big CSV files were slow to analyse: 1.8 s for a million rows. Phase 3 reads
> the file once, splits it into one piece per CPU core and checks every cell in 0.12 s, faster than
> pandas just reading it. And a compiled program refused any new data file, because its proofs were
> tied to the exact file by a checksum.

Run terminal step 4 to show both timings live.

## 5. Data certificates (2.5 min) - chapter 05, terminal step 5. The main idea.

> After compiling, the compiler asks: which facts does each proof really need? It weakens one fact
> at a time, analyses the program again, and keeps only the facts without which a proof breaks.
> The data had marks from 35 to 95; the only proof about marks is BETWEEN 0 AND 100, so the
> certificate needs only 0 to 100.

On the page, click **Next week**: the Phase 2 fingerprint refuses, the old file's facts refuse
(marks 12 and 100), the certificate admits. Click **A typo**: refused, with the row and the line of
the contract. Click **Missing name**: refused, naming the NOT_NULL contract. Optionally edit a
value in the box live. Then run terminal step 5: the real generated program says the same.

## 6. Image folders (1 min) - chapter 06, terminal step 6

> For a folder of images the certificate is a box: the channel counts and size ranges the images may
> have. The program has MIN_WIDTH 200, so new photos of any height are fine, but one photo 150 pixels
> wide is refused. Only the image headers are read to decide.

## 7. Existing pandas scripts (1 min) - chapter 07, terminal step 7

> Most cleaning code already exists as pandas scripts. A second front end parses the script with
> Python's own parser and translates its cleaning into PrepLang, until the first line it cannot
> express, here the map. The compiled program then accepts Kaggle's test file, which has no
> Survived column, because the program only carries that column to the output.

## 8. Evidence and honesty (1 min) - chapters 08 and 09, terminal step 8

> 207 unit tests. On 238 real monthly batches of public data, the certificate accepted 86 percent,
> against 1 percent for the old file's own facts, and every accepted run gave exactly the result of
> compiling that month fresh. On 1,364 runs of held-out GitHub scripts, every accepted run matched the
> script's own result. Comparing every run with the ground truth found five bugs in my own compiler;
> all are fixed and tested.

## Likely viva questions

- **Why is the grammar LL(1)?** Every statement begins with a unique keyword; one token of
  look-ahead chooses the rule. Conditions use precedence levels: OR, AND, NOT, comparison.
- **What is in the symbol table?** One global scope of datasets; for each column its type, whether it
  may be missing, its value interval, and whether those facts are exact; for images every file's
  size and channels, the pixel type and output format.
- **Why is the certificate sound?** Every transfer function is monotone once exactness is cleared,
  and every proof is "the state lies inside a fixed condition". A file inside the certificate has a
  smaller state, so it keeps every proof. Randomized tests check this on 1,200 random tables and 600
  random image folders.
- **Why not just keep the checks?** Proven checks cost time on every run; the certificate keeps the
  speed of the optimized program and still protects it, and it explains every refusal.
- **What can it not do?** Joins, loops, user functions; certificates are per-column facts, so a fact
  needed only after a filter is still required of every input row; scripts that read data inside
  functions are not lifted.
- **What did you use AI tools for?** Answer truthfully, as the course rules ask.
