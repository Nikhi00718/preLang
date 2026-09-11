# 08 — Optimisation: Live-Column Analysis and the Four Passes

## 8.1 One analysis, four passes

Every optimisation in PrepLang is driven by a single dataflow analysis: a
**backwards live-column analysis**, the preprocessing analogue of classical
live-variable analysis.

> A column is *live* at a program point if some later instruction reads it.

The analysis walks the IR backwards, maintaining a live set per dataset:

| Instruction | Effect on the live set (read backwards) |
|---|---|
| `IR_SAVE` | `live ∪= ` every column still present (all of them are written out) |
| `IR_SELECT [c…]` | `live = live ∩ {c…}` — nothing outside the projection can be live before it |
| `IR_DROP [c…]` | `live = live − {c…}` — the dropped columns are not needed earlier either |
| `IR_DROP_NULL` | record `subset = live` (the null test ranges over the live columns) |
| `IR_NORMALIZE c` / `IR_FILL_NULL c` | if `c ∉ live` mark **dead**; else `live ∪= {c}` |
| `IR_FILTER c …` | `live ∪= {c}` — the filter reads `c` even if it is not output |
| `IR_EXPECT_* c` | `live ∪= {c}` — a contract must be able to read its column |
| `IR_LOAD` | record `usecols = live` — **exactly the columns the program needs** |

Everything else follows from this table.

## 8.2 Pass 1 — Redundant-operation elimination

Two *idempotent* operations (`DROP_NULL`, `NORMALIZE`, `SELECT`) on the same
dataset, with the same operands, and with no operation that modifies the
dataset in between, collapse into one.

```text
DROP_NULL emp;          IR_DROP_NULL  emp          IR_DROP_NULL  emp
DROP_NULL emp;    ⇒     IR_DROP_NULL  emp     ⇒    (removed)
```

`EXPECT` instructions never separate two operations, because they validate
without modifying. A `FILTER` between two `DROP_NULL`s *does* separate them, so
the second is kept — there is a test for both cases.

## 8.3 Pass 2 — Dead-operation elimination

An operation whose written column is not live afterwards cannot affect the
output.

```text
NORMALIZE data.salary;      IR_NORMALIZE data salary   ← salary ∉ live
DROP data [ salary ];  ⇒    IR_DROP      data [salary]
```

The instruction is removed and a warning is reported:

```text
OPTIMISER WARNING - Line 7

    IR_NORMALIZE data.salary has no effect on the output: the column is
    removed before it is used.
```

Passes 1 and 2 are iterated to a fixed point (at most five rounds), because
removing one dead operation can make another dead.

## 8.4 Pass 3 — Column pruning

The live set at `IR_LOAD` *is* the set of required columns. It is attached to
the instruction, and the code generator turns it into `usecols=`:

```python
emp = pd.read_csv(
    'employees.csv',
    usecols=['name', 'age', 'salary', 'department', 'rating'],
)
```

For a 30-column file where the pipeline needs five, 25 columns are never parsed
and never allocated. This is exactly *projection push-down* in a relational
optimiser, applied at the file-reading boundary.

## 8.5 Pass 4 — Projection repair

Pruning changes the shape of the loaded dataframe, so any later `SELECT` or
`DROP` must be brought back into agreement with it — otherwise the generated
code would fail at run time with a `KeyError`.

| Situation | Repair |
|---|---|
| A `DROP` whose columns were all pruned away | Removed — the pruning already did its job |
| A `DROP`/`SELECT` mentioning some pruned columns | The list is narrowed to the columns that are actually loaded |
| A `SELECT` that selects exactly the present columns, *in the same order* | Removed — it is a no-op |

The order condition matters. `SELECT` also fixes the **output column order**,
so removing one that reorders columns would change the result. The pass
therefore only deletes a `SELECT` when the order already matches:

```text
SELECT students [ name, age ];    usecols = [name, age]   → SELECT removed
SELECT students [ marks, name ];  usecols = [name, marks] → SELECT kept
```

After pass 4 the liveness information is deliberately **not** recomputed: a
fresh backwards pass over an IR whose `SELECT` has just been deleted would
conclude that every column is live again and undo the pruning it was asked to
respect.

## 8.6 Soundness

An optimisation that changes the answer is a bug. The one operator that could
be affected by pruning is `DROP_NULL`, since removing a column changes which
rows contain a missing value. Two decisions make pruning sound:

1. **The language defines `DROP_NULL` over the live columns** (language
   specification §2.6), not over every column in the file. This is the same
   move a relational optimiser makes when it pushes a projection below a
   filter: the operator semantics are defined on the projected schema.

2. **The live set is emitted in both pipelines.** The code generator writes
   `dropna(subset=[...])` whether or not the optimiser ran, so turning
   optimisation on changes *how much data is read*, never *what is computed*.

This is verified, not asserted: `benchmarks/performance.py` compiles the same
program twice — once with the optimiser, once without — executes both, and
compares the two output files byte for byte.

```text
4. CORRECTNESS OF THE OPTIMISATION
======================================================================
  unoptimised output : out_baseline.csv  (1,120,839 bytes)
  optimised   output : out_optimised.csv  (1,120,839 bytes)

  byte-for-byte identical : YES
```

## 8.7 Measured effect

On a generated 30,000 × 30 dataset (5.0 MiB), with the pipeline
`2 contracts · DROP_NULL ×2 · NORMALIZE · FILTER · SELECT · SAVE`:

| Metric | Unoptimised | Optimised | Change |
|---|---|---|---|
| IR instructions | 9 | 8 | −11.1 % |
| Columns read from disk | 30 | 5 | −83.3 % |
| Execution time | 1227.8 ms | 1070.8 ms | −12.8 % |
| Peak memory | 51.0 MiB | 7.2 MiB | −85.9 % |
| Output | 28 914 × 4 | 28 914 × 4 | identical bytes |

The memory reduction is the headline number: the optimised pipeline never
materialises the 25 columns it does not need. Execution-time savings vary with
the machine and the dataset; the memory saving is structural.

## 8.8 What the optimiser deliberately does not do

| Not implemented | Why |
|---|---|
| Reordering `FILTER` before `NORMALIZE` | It would change the normalisation statistics, so it is *not* semantics-preserving — a good viva answer, not an oversight |
| Constant folding in filter conditions | The grammar allows only a literal on the right-hand side, so there is nothing to fold |
| Common sub-expression elimination | PrepLang has no expressions beyond one filter condition |
| Cost-based optimisation | Listed as future work; it needs a cost model and row-count statistics |
