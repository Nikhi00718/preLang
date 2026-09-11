# 07 — The PrepLang Intermediate Representation

## 7.1 Shape

The IR is a **flat, linear list of instructions** — the preprocessing analogue
of three-address code. Each instruction is

```
<opcode>  <dataset>  <operands...>        [source line]
```

Example. For

```text
LOAD "students.csv" AS students;
EXPECT students.age NUMBER;
FILL_NULL students.attendance WITH 0;
DROP_NULL students;
NORMALIZE students.age;
FILTER students WHERE marks >= 50;
SELECT students [ name, age, marks ];
SAVE students AS "cleaned_students.csv";
```

the generator emits

```
#    LINE   OPCODE              DATASET     OPERANDS
1    7      IR_LOAD             students    "students.csv"
2    9      IR_EXPECT_TYPE      students    age : NUMBER
3    13     IR_FILL_NULL        students    attendance <- 0
4    15     IR_DROP_NULL        students
5    17     IR_NORMALIZE        students    age
6    19     IR_FILTER           students    marks >= 50
7    21     IR_SELECT           students    [name, age, marks]
8    28     IR_SAVE             students    "cleaned_students.csv"
```

## 7.2 Instruction set (11 opcodes)

| Opcode | Operands | Meaning |
|---|---|---|
| `IR_LOAD` | file | Read the dataset |
| `IR_DROP_NULL` | — | Remove rows containing any missing value |
| `IR_FILL_NULL` | column, value | Replace missing values in one column |
| `IR_NORMALIZE` | column | Z-score normalise a numeric column |
| `IR_FILTER` | column, operator, value | Keep matching rows |
| `IR_SELECT` | column list | Keep only these columns |
| `IR_DROP` | column list | Remove these columns |
| `IR_SAVE` | file | Write the dataset |
| `IR_EXPECT_TYPE` | column, type | Validate a type contract |
| `IR_EXPECT_RANGE` | column, low, high | Validate a range contract |
| `IR_EXPECT_NOT_NULL` | column | Validate a not-null contract |

Instruction properties used by later phases:

* `CONTRACT_OPS` — the three `EXPECT` opcodes; they validate but never modify a
  dataset, so the optimiser may reorder around them.
* `IDEMPOTENT_OPS` — `DROP_NULL`, `NORMALIZE`, `SELECT`; applying one twice in
  a row has the same effect as applying it once. This is what the
  redundant-operation elimination pass exploits.
* `meta` — a free-form annotation dictionary. The Review 3 optimiser attaches
  the pruned `usecols` set to `IR_LOAD` here.

## 7.3 Why an IR at all?

The AST could be translated to Pandas directly. Three reasons not to:

1. **Optimisation needs a simple shape.** Column pruning, redundancy
   elimination and dead-operation removal are *list rewrites* over the IR.
   Expressing them as tree rewrites over the AST would be considerably harder
   and much less readable.

2. **It separates front end from back end.** No IR instruction mentions a
   Pandas concept. Adding a Polars or DuckDB backend therefore needs only a
   new code generator; the lexer, parser, symbol table and analyser are
   untouched. This is the standard argument for an IR, and here it can be
   demonstrated rather than merely asserted.

   ```
                        ┌── Pandas code generator   (implemented)
   AST ──► IR ──► opt ──┼── Polars code generator   (future)
                        └── DuckDB code generator   (future)
   ```

3. **It is an examinable compiler phase.** Intermediate code generation is a
   phase in its own right, and the IR is the artefact that proves it exists.

## 7.4 Lowering rules

The translation is one-to-one because every semantic decision has already been
made by the analyser; this phase only flattens the tree.

| AST node | IR instruction |
|---|---|
| `LoadNode` | `IR_LOAD d "file.csv"` |
| `DropNullNode` | `IR_DROP_NULL d` |
| `FillNullNode` | `IR_FILL_NULL d col <- value` |
| `NormalizeNode` | `IR_NORMALIZE d col` |
| `FilterNode` | `IR_FILTER d col op value` |
| `SelectNode` | `IR_SELECT d [cols]` |
| `DropNode` | `IR_DROP d [cols]` |
| `SaveNode` | `IR_SAVE d "out.csv"` |
| `ExpectTypeNode` | `IR_EXPECT_TYPE d col : TYPE` |
| `ExpectRangeNode` | `IR_EXPECT_RANGE d col in [lo, hi]` |
| `ExpectNotNullNode` | `IR_EXPECT_NOT_NULL d col` |

Contracts are lowered too, rather than being handled outside the pipeline.
That keeps validation *inside* the compiled program — which is the point of
making contracts part of the language instead of a separate library.

## 7.5 What Review 3 does with it

| Pass | Uses |
|---|---|
| Redundant-operation elimination | `same_effect_as` + `IDEMPOTENT_OPS` |
| Column pruning | A backwards live-column analysis over the instruction list; the result is attached to `IR_LOAD.meta["usecols"]` |
| Dead-operation elimination | The same liveness information: an operation writing a column that is never read afterwards is removed |
| Code generation | One emitter function per opcode |
