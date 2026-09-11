"""
ir.py
=====

The PrepLang Intermediate Representation.

The IR is a **flat, linear list of instructions** -- the preprocessing analogue
of three-address code. Each instruction names one operation, the dataset it
acts on, and its operands:

    IR_LOAD        students  "students.csv"
    IR_DROP_NULL   students
    IR_NORMALIZE   students  age
    IR_FILTER      students  marks >= 50
    IR_SELECT      students  [name, age, marks]
    IR_SAVE        students  "cleaned.csv"

Why introduce an IR at all, when the AST could be translated to Pandas
directly?

1. **Optimisation needs a simple shape.** Column pruning, redundancy
   elimination and dead-operation removal are list rewrites over the IR; the
   same passes over a tree would be far harder to express.
2. **It separates the front end from the back end.** The IR mentions no
   Pandas concept, so a Polars or DuckDB backend would need only a new code
   generator -- the lexer, parser and analyser stay untouched.
3. **It is a phase the project must demonstrate.** Intermediate code
   generation is an examinable compiler phase in its own right.

Each instruction keeps its source ``line`` so that optimiser reports and
generated-code comments can point back at the original program.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum

from .ast_nodes import fmt_number


class IROp(Enum):
    """The PrepLang IR instruction set (11 opcodes)."""

    LOAD = "IR_LOAD"
    DROP_NULL = "IR_DROP_NULL"
    FILL_NULL = "IR_FILL_NULL"
    NORMALIZE = "IR_NORMALIZE"
    FILTER = "IR_FILTER"
    SELECT = "IR_SELECT"
    DROP = "IR_DROP"
    SAVE = "IR_SAVE"
    EXPECT_TYPE = "IR_EXPECT_TYPE"
    EXPECT_RANGE = "IR_EXPECT_RANGE"
    EXPECT_NOT_NULL = "IR_EXPECT_NOT_NULL"

    def __str__(self) -> str:  # pragma: no cover - trivial
        return self.value


#: Opcodes that only validate data; they never modify a dataset.
CONTRACT_OPS = frozenset(
    {IROp.EXPECT_TYPE, IROp.EXPECT_RANGE, IROp.EXPECT_NOT_NULL}
)

#: Opcodes that are idempotent -- applying them twice in a row is the same as
#: applying them once. Used by the redundant-operation elimination pass.
IDEMPOTENT_OPS = frozenset({IROp.DROP_NULL, IROp.NORMALIZE, IROp.SELECT})


@dataclass
class IRInstruction:
    """One IR instruction."""

    op: IROp
    dataset: str
    args: tuple = ()
    line: int = 0
    #: analysis annotations (e.g. the `usecols` set attached by the optimiser)
    meta: dict = field(default_factory=dict)

    @property
    def column(self) -> str | None:
        """The single column operand, when the instruction has one."""
        if self.op in (IROp.NORMALIZE, IROp.FILL_NULL, IROp.EXPECT_TYPE,
                       IROp.EXPECT_RANGE, IROp.EXPECT_NOT_NULL):
            return str(self.args[0]) if self.args else None
        if self.op is IROp.FILTER:
            return str(self.args[0]) if self.args else None
        return None

    @property
    def columns(self) -> list[str]:
        """The column-list operand, when the instruction has one."""
        if self.op in (IROp.SELECT, IROp.DROP) and self.args:
            return list(self.args[0])
        return []

    def operand_text(self) -> str:
        if self.op is IROp.LOAD:
            return f'"{self.args[0]}"'
        if self.op is IROp.SAVE:
            return f'"{self.args[0]}"'
        if self.op is IROp.FILTER:
            column, operator, value = self.args
            return f"{column} {operator} {value!r}"
        if self.op is IROp.FILL_NULL:
            return f"{self.args[0]} <- {self.args[1]!r}"
        if self.op in (IROp.SELECT, IROp.DROP):
            return "[" + ", ".join(self.args[0]) + "]"
        if self.op is IROp.EXPECT_RANGE:
            return (
                f"{self.args[0]} in "
                f"[{fmt_number(self.args[1])}, {fmt_number(self.args[2])}]"
            )
        if self.op is IROp.EXPECT_TYPE:
            return f"{self.args[0]} : {self.args[1]}"
        if self.args:
            return " ".join(str(a) for a in self.args)
        return ""

    def __str__(self) -> str:
        return f"{self.op!s:<20}{self.dataset:<12}{self.operand_text()}"

    def same_effect_as(self, other: "IRInstruction") -> bool:
        """True when two instructions perform exactly the same operation."""
        return (
            self.op is other.op
            and self.dataset == other.dataset
            and self.args == other.args
        )


@dataclass
class IRProgram:
    """An ordered list of IR instructions."""

    instructions: list[IRInstruction] = field(default_factory=list)

    def append(self, instruction: IRInstruction) -> None:
        self.instructions.append(instruction)

    def __len__(self) -> int:
        return len(self.instructions)

    def __iter__(self):
        return iter(self.instructions)

    def __getitem__(self, index):
        return self.instructions[index]

    def datasets(self) -> list[str]:
        seen: list[str] = []
        for instruction in self.instructions:
            if instruction.dataset not in seen:
                seen.append(instruction.dataset)
        return seen

    def of_dataset(self, name: str) -> list[IRInstruction]:
        return [i for i in self.instructions if i.dataset == name]

    def copy(self) -> "IRProgram":
        return IRProgram(
            instructions=[
                IRInstruction(i.op, i.dataset, i.args, i.line, dict(i.meta))
                for i in self.instructions
            ]
        )

    def format(self, title: str = "INTERMEDIATE REPRESENTATION") -> str:
        header = f"{'#':<5}{'LINE':<7}{'OPCODE':<20}{'DATASET':<12}OPERANDS"
        lines = [title, "-" * len(header), header, "-" * len(header)]
        for index, instruction in enumerate(self.instructions, start=1):
            lines.append(
                f"{index:<5}{instruction.line:<7}{instruction.op!s:<20}"
                f"{instruction.dataset:<12}{instruction.operand_text()}"
            )
            if instruction.meta:
                for key, value in instruction.meta.items():
                    lines.append(f"{'':<12}. {key} = {value}")
        lines.append("-" * len(header))
        lines.append(f"{len(self.instructions)} IR instructions")
        return "\n".join(lines)
