"""
ir_generator.py
===============

Compiler Phase 4 -- Intermediate Code Generation.

Lowers the validated AST into the linear IR of :mod:`preplang.ir`. The
translation is one-to-one and deliberately trivial: every semantic decision has
already been made by the analyser, so this phase only has to flatten the tree.

    AST node            ->  IR instruction
    ------------------------------------------------------------
    LoadNode            ->  IR_LOAD        d  "file.csv"
    DropNullNode        ->  IR_DROP_NULL   d
    FillNullNode        ->  IR_FILL_NULL   d  col <- value
    NormalizeNode       ->  IR_NORMALIZE   d  col
    FilterNode          ->  IR_FILTER      d  col op value
    SelectNode          ->  IR_SELECT      d  [cols]
    DropNode            ->  IR_DROP        d  [cols]
    SaveNode            ->  IR_SAVE        d  "out.csv"
    ExpectTypeNode      ->  IR_EXPECT_TYPE d  col : TYPE
    ExpectRangeNode     ->  IR_EXPECT_RANGE d col in [lo, hi]
    ExpectNotNullNode   ->  IR_EXPECT_NOT_NULL d col

Generating IR for contracts as well as for transformations keeps validation
inside the compiled pipeline instead of bolting it on afterwards -- which is
the point of making contracts part of the language.
"""

from __future__ import annotations

from .ast_nodes import (
    DropNode,
    DropNullNode,
    ExpectNotNullNode,
    ExpectRangeNode,
    ExpectTypeNode,
    FillNullNode,
    FilterNode,
    LoadNode,
    NormalizeNode,
    ProgramNode,
    SaveNode,
    SelectNode,
)
from .ir import IRInstruction, IROp, IRProgram


class IRGenerator:
    """Translates a validated :class:`ProgramNode` into an :class:`IRProgram`."""

    def generate(self, program: ProgramNode) -> IRProgram:
        ir = IRProgram()
        for statement in program.statements:
            instruction = self._lower(statement)
            if instruction is not None:
                ir.append(instruction)
        return ir

    # ------------------------------------------------------------------
    def _lower(self, node) -> IRInstruction | None:
        if isinstance(node, LoadNode):
            return IRInstruction(IROp.LOAD, node.dataset, (node.path,), node.line)

        if isinstance(node, DropNullNode):
            return IRInstruction(IROp.DROP_NULL, node.dataset, (), node.line)

        if isinstance(node, FillNullNode):
            value = node.value.value if node.value else None
            return IRInstruction(
                IROp.FILL_NULL, node.dataset, (node.column, value), node.line
            )

        if isinstance(node, NormalizeNode):
            return IRInstruction(IROp.NORMALIZE, node.dataset, (node.column,), node.line)

        if isinstance(node, FilterNode):
            condition = node.condition
            if condition is None or condition.value is None:
                return None
            return IRInstruction(
                IROp.FILTER,
                node.dataset,
                (condition.column, condition.operator, condition.value.value),
                node.line,
            )

        if isinstance(node, SelectNode):
            return IRInstruction(
                IROp.SELECT, node.dataset, (tuple(node.columns),), node.line
            )

        if isinstance(node, DropNode):
            return IRInstruction(
                IROp.DROP, node.dataset, (tuple(node.columns),), node.line
            )

        if isinstance(node, SaveNode):
            return IRInstruction(IROp.SAVE, node.dataset, (node.path,), node.line)

        if isinstance(node, ExpectTypeNode):
            return IRInstruction(
                IROp.EXPECT_TYPE, node.dataset, (node.column, node.expected), node.line
            )

        if isinstance(node, ExpectRangeNode):
            return IRInstruction(
                IROp.EXPECT_RANGE,
                node.dataset,
                (node.column, node.low, node.high),
                node.line,
            )

        if isinstance(node, ExpectNotNullNode):
            return IRInstruction(
                IROp.EXPECT_NOT_NULL, node.dataset, (node.column,), node.line
            )

        return None


def generate_ir(program: ProgramNode) -> IRProgram:
    """Convenience wrapper around :class:`IRGenerator`."""
    return IRGenerator().generate(program)
