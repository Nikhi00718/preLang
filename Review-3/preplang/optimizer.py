"""
optimizer.py
============

Compiler Phase 5 -- Machine-independent optimisation.

Three classical optimisations are applied to the IR, all driven by one
analysis: a backwards **live-column analysis**, which is the data-preprocessing
analogue of live-variable analysis.

    Pass 1  Redundant-operation elimination
            Two consecutive idempotent operations on the same dataset with the
            same operands collapse into one.
                IR_DROP_NULL emp ; IR_DROP_NULL emp   ->   IR_DROP_NULL emp

    Pass 2  Dead-operation elimination
            An operation that writes a column which is never read afterwards
            has no effect on the output and is removed (with a warning).
                NORMALIZE d.salary ; DROP d [salary]  ->  NORMALIZE removed

    Pass 3  Column pruning
            The live set at the LOAD instruction is exactly the set of columns
            the program needs. It is attached to IR_LOAD, and the code
            generator turns it into `usecols=` so unused columns are never read
            from disk at all.
                30 columns in the file, 5 required  ->  25 columns never loaded

    Pass 4  Projection repair
            SELECT and DROP are rewritten to mention only columns that are
            still loaded; one that becomes empty (or a no-op) is removed.

Passes 1 and 2 are iterated to a fixed point (at most :data:`MAX_ROUNDS` rounds),
because eliminating a dead operation can make another operation dead.

Soundness of column pruning
---------------------------
Pruning is only sound if no operation depends on a column that has been pruned
away. The one operator that could is ``DROP_NULL``, which removes rows
containing a missing value. PrepLang therefore defines ``DROP_NULL`` over the
columns that are **live at that point in the program** (see the language
specification, section 2.6), and the analysis records that set in
``instruction.meta["subset"]``. The code generator emits it as
``dropna(subset=[...])`` in *both* the optimised and the unoptimised pipeline,
so switching the optimiser on changes how much data is read -- never the
result. This mirrors projection push-down in a relational optimiser, where the
operator semantics are likewise defined over the projected schema.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .errors import ErrorReporter
from .ir import IDEMPOTENT_OPS, IRInstruction, IROp, IRProgram
from .symbol_table import SymbolTable

#: Safety bound on the fixed-point iteration.
MAX_ROUNDS = 5


@dataclass
class PassResult:
    """What one optimisation pass did."""

    name: str
    removed: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    @property
    def changed(self) -> bool:
        return bool(self.removed)


@dataclass
class OptimizationReport:
    """Everything the optimiser did, for the report and the visualiser."""

    instructions_before: int = 0
    instructions_after: int = 0
    columns_available: int = 0
    columns_required: int = 0
    required_columns: list[str] = field(default_factory=list)
    pruned_columns: list[str] = field(default_factory=list)
    passes: list[PassResult] = field(default_factory=list)
    rounds: int = 0

    @property
    def instructions_removed(self) -> int:
        return self.instructions_before - self.instructions_after

    @property
    def instruction_reduction(self) -> float:
        if not self.instructions_before:
            return 0.0
        return 100.0 * self.instructions_removed / self.instructions_before

    @property
    def column_reduction(self) -> float:
        if not self.columns_available:
            return 0.0
        return 100.0 * len(self.pruned_columns) / self.columns_available

    def format(self) -> str:
        lines = ["OPTIMIZATION REPORT", "=" * 62]
        lines.append(f"Fixed-point rounds              : {self.rounds}")
        lines.append("")
        lines.append(f"IR instructions before          : {self.instructions_before}")
        lines.append(f"IR instructions after           : {self.instructions_after}")
        lines.append(
            f"Instructions eliminated         : {self.instructions_removed} "
            f"({self.instruction_reduction:.1f}% reduction)"
        )
        lines.append("")
        lines.append(f"Columns available in the file(s): {self.columns_available}")
        lines.append(f"Columns actually required       : {self.columns_required}")
        lines.append(
            f"Columns pruned (never loaded)   : {len(self.pruned_columns)} "
            f"({self.column_reduction:.1f}% of the file)"
        )
        if self.required_columns:
            lines.append(f"  required : {', '.join(self.required_columns)}")
        if self.pruned_columns:
            shown = self.pruned_columns[:12]
            suffix = "" if len(self.pruned_columns) <= 12 else f", ... (+{len(self.pruned_columns) - 12} more)"
            lines.append(f"  pruned   : {', '.join(shown)}{suffix}")
        lines.append("")
        lines.append("PASSES APPLIED")
        lines.append("-" * 62)
        for result in self.passes:
            status = f"{len(result.removed)} change(s)" if result.changed else "no change"
            lines.append(f"{result.name:<38}{status}")
            for item in result.removed:
                lines.append(f"    - {item}")
            for note in result.notes:
                lines.append(f"    . {note}")
        lines.append("=" * 62)
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# The analysis: which columns are live where?
# ---------------------------------------------------------------------------


def _present_columns(ir: IRProgram, symbols: SymbolTable) -> dict[int, list[str]]:
    """Forward pass: the columns still present *after* each instruction.

    SELECT and DROP are the only instructions that change the set.
    """
    present: dict[str, list[str]] = {}
    for name in ir.datasets():
        dataset = symbols.lookup_dataset(name)
        present[name] = list(dataset.schema.names) if dataset and dataset.schema else []

    result: dict[int, list[str]] = {}
    for index, instruction in enumerate(ir.instructions):
        columns = present.get(instruction.dataset, [])
        if instruction.op is IROp.SELECT:
            kept = set(instruction.columns)
            columns = [c for c in columns if c in kept]
        elif instruction.op is IROp.DROP:
            removed = set(instruction.columns)
            columns = [c for c in columns if c not in removed]
        present[instruction.dataset] = columns
        result[index] = list(columns)
    return result


def analyse_liveness(ir: IRProgram, symbols: SymbolTable) -> dict[str, set[str]]:
    """Backwards live-column analysis.

    Annotates the IR in place:

    * ``IR_LOAD.meta["usecols"]``  -- the columns the program needs from the file
    * ``IR_DROP_NULL.meta["subset"]`` -- the columns its null test ranges over
    * ``instruction.meta["dead"]`` -- True for a write no one reads afterwards

    Returns the live set at each dataset's LOAD, i.e. the required columns.
    """
    present_after = _present_columns(ir, symbols)
    live: dict[str, set[str]] = {name: set() for name in ir.datasets()}
    required: dict[str, set[str]] = {}

    for index in range(len(ir.instructions) - 1, -1, -1):
        instruction = ir.instructions[index]
        dataset = instruction.dataset
        current = live.setdefault(dataset, set())
        instruction.meta.pop("dead", None)

        if instruction.op is IROp.SAVE:
            # everything still present at this point is written to the file
            current |= set(present_after[index])

        elif instruction.op is IROp.SELECT:
            # only the selected columns can be live afterwards
            live[dataset] = current & set(instruction.columns)

        elif instruction.op is IROp.DROP:
            # the dropped columns are not needed before this point either
            live[dataset] = current - set(instruction.columns)

        elif instruction.op is IROp.DROP_NULL:
            # the null test ranges over every column live at this point
            order = present_after[index]
            instruction.meta["subset"] = [c for c in order if c in current]

        elif instruction.op in (IROp.NORMALIZE, IROp.FILL_NULL):
            column = instruction.column
            if column is not None and column not in current:
                # writes a column nobody reads afterwards -> dead
                instruction.meta["dead"] = True
            elif column is not None:
                current.add(column)

        elif instruction.op is IROp.FILTER:
            column = instruction.column
            if column is not None:
                current.add(column)

        elif instruction.op in (IROp.EXPECT_TYPE, IROp.EXPECT_RANGE, IROp.EXPECT_NOT_NULL):
            column = instruction.column
            if column is not None:
                current.add(column)

        elif instruction.op is IROp.LOAD:
            symbol = symbols.lookup_dataset(dataset)
            order = list(symbol.schema.names) if symbol and symbol.schema else sorted(current)
            usecols = [c for c in order if c in current]
            instruction.meta["usecols"] = usecols
            required[dataset] = set(usecols)

    return required


# ---------------------------------------------------------------------------
# The passes
# ---------------------------------------------------------------------------


def eliminate_redundant(ir: IRProgram) -> PassResult:
    """Pass 1 -- drop an idempotent operation repeated with no effect between."""
    result = PassResult("1. Redundant-operation elimination")
    last_mutation: dict[str, IRInstruction] = {}
    keep: list[IRInstruction] = []

    for instruction in ir.instructions:
        if instruction.op in (IROp.EXPECT_TYPE, IROp.EXPECT_RANGE, IROp.EXPECT_NOT_NULL):
            # contracts validate but never modify, so they do not separate two
            # otherwise-adjacent operations
            keep.append(instruction)
            continue

        previous = last_mutation.get(instruction.dataset)
        if (
            instruction.op in IDEMPOTENT_OPS
            and previous is not None
            and previous.same_effect_as(instruction)
        ):
            result.removed.append(
                f"line {instruction.line}: {instruction.op} {instruction.dataset} "
                f"{instruction.operand_text()} (already applied at line {previous.line})"
            )
            continue

        keep.append(instruction)
        last_mutation[instruction.dataset] = instruction

    ir.instructions = keep
    return result


def eliminate_dead(ir: IRProgram, reporter: ErrorReporter | None = None) -> PassResult:
    """Pass 2 -- drop operations whose result is never read."""
    result = PassResult("2. Dead-operation elimination")
    keep: list[IRInstruction] = []

    for instruction in ir.instructions:
        if instruction.meta.get("dead"):
            message = (
                f"{instruction.op} {instruction.dataset}.{instruction.column} has no effect "
                f"on the output: the column is removed before it is used."
            )
            result.removed.append(f"line {instruction.line}: {message}")
            if reporter is not None:
                reporter.warn(message, line=instruction.line, kind="OPTIMISER WARNING")
            continue
        keep.append(instruction)

    ir.instructions = keep
    return result


def prune_columns(ir: IRProgram, symbols: SymbolTable) -> PassResult:
    """Pass 3 -- record the required column set on every IR_LOAD."""
    result = PassResult("3. Column pruning")
    for instruction in ir.instructions:
        if instruction.op is not IROp.LOAD:
            continue
        dataset = symbols.lookup_dataset(instruction.dataset)
        if dataset is None or dataset.schema is None:
            continue
        available = list(dataset.schema.names)
        usecols = instruction.meta.get("usecols", available)
        pruned = [c for c in available if c not in set(usecols)]
        if pruned:
            result.removed.append(
                f"line {instruction.line}: {instruction.dataset} loads "
                f"{len(usecols)}/{len(available)} columns; "
                f"{len(pruned)} never read from disk"
            )
        else:
            result.notes.append(
                f"{instruction.dataset}: all {len(available)} columns are required"
            )
    return result


def rewrite_projections(ir: IRProgram, symbols: SymbolTable) -> PassResult:
    """Pass 4 -- make SELECT and DROP agree with the pruned schema.

    Column pruning removes columns from the LOAD, so a later ``DROP`` of a
    column that is no longer read would fail at run time, and a ``SELECT``
    listing one would too. Both are repaired here:

    * a DROP whose columns were all pruned away is *subsumed by the pruning*
      and is removed;
    * a SELECT or DROP that mentions a pruned column has that column removed
      from its list;
    * a SELECT that selects exactly the columns already present, in the same
      order, is a no-op and is removed.

    The last rule is deliberately strict about order: SELECT also fixes the
    output column order, so a SELECT may only be dropped when the order is
    already what it asks for -- otherwise the optimised output would differ
    from the unoptimised one.
    """
    result = PassResult("4. Projection repair after pruning")
    present: dict[str, list[str]] = {}
    keep: list[IRInstruction] = []

    for instruction in ir.instructions:
        dataset = instruction.dataset

        if instruction.op is IROp.LOAD:
            symbol = symbols.lookup_dataset(dataset)
            available = list(symbol.schema.names) if symbol and symbol.schema else []
            present[dataset] = list(instruction.meta.get("usecols", available))
            keep.append(instruction)
            continue

        if instruction.op is IROp.SELECT:
            current = present.get(dataset, [])
            wanted = [c for c in instruction.columns if c in current]
            if wanted == current:
                result.removed.append(
                    f"line {instruction.line}: IR_SELECT {dataset} is a no-op after pruning"
                )
                continue
            if wanted != list(instruction.columns):
                result.removed.append(
                    f"line {instruction.line}: IR_SELECT {dataset} narrowed to "
                    f"[{', '.join(wanted)}] (the rest were never loaded)"
                )
                instruction.args = (tuple(wanted),)
            present[dataset] = wanted
            keep.append(instruction)
            continue

        if instruction.op is IROp.DROP:
            current = present.get(dataset, [])
            wanted = [c for c in instruction.columns if c in current]
            if not wanted:
                result.removed.append(
                    f"line {instruction.line}: IR_DROP {dataset} removed; its columns "
                    f"were already pruned at load time"
                )
                continue
            if wanted != list(instruction.columns):
                result.removed.append(
                    f"line {instruction.line}: IR_DROP {dataset} narrowed to "
                    f"[{', '.join(wanted)}]"
                )
                instruction.args = (tuple(wanted),)
            present[dataset] = [c for c in current if c not in set(wanted)]
            keep.append(instruction)
            continue

        keep.append(instruction)

    ir.instructions = keep
    return result


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def optimize(
    ir: IRProgram,
    symbols: SymbolTable,
    reporter: ErrorReporter | None = None,
) -> tuple[IRProgram, OptimizationReport]:
    """Optimise a copy of *ir*; return ``(optimised ir, report)``.

    The input program is not modified, so the visualiser can show the IR before
    and after side by side.
    """
    optimised = ir.copy()
    report = OptimizationReport(instructions_before=len(ir))

    # the liveness annotation the code generator needs is valid for the
    # unoptimised program too, so apply it there as well
    analyse_liveness(ir, symbols)

    for round_number in range(1, MAX_ROUNDS + 1):
        analyse_liveness(optimised, symbols)
        passes = [
            eliminate_redundant(optimised),
            eliminate_dead(optimised, reporter if round_number == 1 else None),
        ]
        report.rounds = round_number
        if round_number == 1:
            report.passes.extend(passes)
        else:
            for existing, extra in zip(report.passes, passes):
                existing.removed.extend(extra.removed)
        if not any(p.changed for p in passes):
            break

    # pruning is computed last, from the final liveness information, and the
    # projections are then repaired to match the pruned schema
    analyse_liveness(optimised, symbols)
    report.passes.append(prune_columns(optimised, symbols))
    report.passes.append(rewrite_projections(optimised, symbols))
    # NB: liveness is deliberately *not* recomputed here. Pass 4 may delete a
    # SELECT that has become a no-op, and a fresh backwards pass would then
    # conclude that every column is live again -- undoing the pruning it was
    # just asked to respect.

    report.instructions_after = len(optimised)

    available: list[str] = []
    requiredcols: list[str] = []
    for instruction in optimised.instructions:
        if instruction.op is not IROp.LOAD:
            continue
        dataset = symbols.lookup_dataset(instruction.dataset)
        if dataset is None or dataset.schema is None:
            continue
        available.extend(dataset.schema.names)
        requiredcols.extend(instruction.meta.get("usecols", dataset.schema.names))

    report.columns_available = len(available)
    report.columns_required = len(requiredcols)
    report.required_columns = requiredcols
    report.pruned_columns = [c for c in available if c not in set(requiredcols)]
    return optimised, report
