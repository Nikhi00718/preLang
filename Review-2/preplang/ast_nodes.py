"""
ast_nodes.py
============

Abstract Syntax Tree node definitions.

The AST is the condensed form of the parse tree: the punctuation that guided
parsing (``;``, ``[``, ``]``, ``.``, ``AS``, ``WITH``) has served its purpose
and disappears, leaving only the information later phases need.

Compare, for

    FILTER students WHERE marks >= 50;

the parse tree (9 nodes, including 4 terminals that carry no meaning) with the
AST (3 nodes)::

    FilterNode
    ├── dataset   : students
    └── condition : ConditionNode
                    ├── column   : marks
                    ├── operator : >=
                    └── value    : LiteralNode(50 : INTEGER)

Every node stores the source ``line`` so that the semantic analyser, the
contract checker and the IR generator can all produce located diagnostics
without re-consulting the token stream.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Sequence

from .schema import DataType


def fmt_number(value: float | int) -> str:
    """Render 100.0 as '100' so diagnostics read the way the user wrote them."""
    return str(int(value)) if float(value).is_integer() else str(value)


# ---------------------------------------------------------------------------
# Base
# ---------------------------------------------------------------------------


@dataclass
class Node:
    """Base class of every AST node."""

    line: int = 0

    @property
    def kind(self) -> str:
        return type(self).__name__

    def children(self) -> Sequence["Node"]:
        """Child nodes, for generic traversal."""
        return ()

    def describe(self) -> str:
        """One-line description used by the tree printer."""
        return self.kind


# ---------------------------------------------------------------------------
# Expressions
# ---------------------------------------------------------------------------


@dataclass
class LiteralNode(Node):
    """A constant appearing in the program (`50`, `"clean.csv"`, `TRUE`)."""

    value: object = None
    type: DataType = DataType.UNKNOWN

    def describe(self) -> str:
        return f"Literal({self.value!r} : {self.type.name})"


@dataclass
class ColumnRefNode(Node):
    """A qualified column reference `dataset.column`."""

    dataset: str = ""
    column: str = ""

    def describe(self) -> str:
        return f"ColumnRef({self.dataset}.{self.column})"


@dataclass
class ConditionNode(Node):
    """The condition of a FILTER: `column <relop> literal`."""

    column: str = ""
    operator: str = ""
    value: LiteralNode | None = None

    def children(self) -> Sequence[Node]:
        return (self.value,) if self.value else ()

    def describe(self) -> str:
        return f"Condition({self.column} {self.operator} ...)"


# ---------------------------------------------------------------------------
# Statements
# ---------------------------------------------------------------------------


@dataclass
class StatementNode(Node):
    """Base class of all statements."""


@dataclass
class LoadNode(StatementNode):
    path: str = ""
    dataset: str = ""

    def describe(self) -> str:
        return f"Load(file={self.path!r} -> {self.dataset})"


@dataclass
class DropNullNode(StatementNode):
    dataset: str = ""

    def describe(self) -> str:
        return f"DropNull({self.dataset})"


@dataclass
class FillNullNode(StatementNode):
    dataset: str = ""
    column: str = ""
    value: LiteralNode | None = None

    def children(self) -> Sequence[Node]:
        return (self.value,) if self.value else ()

    def describe(self) -> str:
        return f"FillNull({self.dataset}.{self.column})"


@dataclass
class NormalizeNode(StatementNode):
    dataset: str = ""
    column: str = ""

    def describe(self) -> str:
        return f"Normalize({self.dataset}.{self.column})"


@dataclass
class FilterNode(StatementNode):
    dataset: str = ""
    condition: ConditionNode | None = None

    def children(self) -> Sequence[Node]:
        return (self.condition,) if self.condition else ()

    def describe(self) -> str:
        return f"Filter({self.dataset})"


@dataclass
class SelectNode(StatementNode):
    dataset: str = ""
    columns: list[str] = field(default_factory=list)

    def describe(self) -> str:
        return f"Select({self.dataset} [{', '.join(self.columns)}])"


@dataclass
class DropNode(StatementNode):
    dataset: str = ""
    columns: list[str] = field(default_factory=list)

    def describe(self) -> str:
        return f"Drop({self.dataset} [{', '.join(self.columns)}])"


@dataclass
class SaveNode(StatementNode):
    dataset: str = ""
    path: str = ""

    def describe(self) -> str:
        return f"Save({self.dataset} -> {self.path!r})"


# ---------------------------------------------------------------------------
# Data contracts
# ---------------------------------------------------------------------------


@dataclass
class ExpectNode(StatementNode):
    """Base class of the three contract statements."""

    dataset: str = ""
    column: str = ""


@dataclass
class ExpectTypeNode(ExpectNode):
    """`EXPECT d.c NUMBER | STRING | BOOLEAN;`"""

    expected: str = "NUMBER"

    def describe(self) -> str:
        return f"ExpectType({self.dataset}.{self.column} : {self.expected})"


@dataclass
class ExpectRangeNode(ExpectNode):
    """`EXPECT d.c BETWEEN low AND high;`"""

    low: float = 0.0
    high: float = 0.0

    def describe(self) -> str:
        return (
            f"ExpectRange({self.dataset}.{self.column} in "
            f"[{fmt_number(self.low)}, {fmt_number(self.high)}])"
        )


@dataclass
class ExpectNotNullNode(ExpectNode):
    """`EXPECT d.c NOT_NULL;`"""

    def describe(self) -> str:
        return f"ExpectNotNull({self.dataset}.{self.column})"


# ---------------------------------------------------------------------------
# Program root
# ---------------------------------------------------------------------------


@dataclass
class ProgramNode(Node):
    """The root of the AST: an ordered list of statements."""

    statements: list[StatementNode] = field(default_factory=list)

    def children(self) -> Sequence[Node]:
        return tuple(self.statements)

    def describe(self) -> str:
        return f"Program({len(self.statements)} statements)"


# ---------------------------------------------------------------------------
# Pretty printing
# ---------------------------------------------------------------------------


def format_ast(node: Node, prefix: str = "", is_last: bool = True, is_root: bool = True) -> str:
    """Render an AST as an indented tree (used by the CLI and the IDE)."""
    lines: list[str] = []
    if is_root:
        lines.append(node.describe())
        child_prefix = ""
    else:
        connector = "`-- " if is_last else "|-- "
        line_tag = f"   [line {node.line}]" if node.line else ""
        lines.append(f"{prefix}{connector}{node.describe()}{line_tag}")
        child_prefix = prefix + ("    " if is_last else "|   ")

    children = [c for c in node.children() if c is not None]
    for index, child in enumerate(children):
        lines.append(
            format_ast(child, child_prefix, index == len(children) - 1, is_root=False)
        )
    return "\n".join(lines)


def walk(node: Node):
    """Yield every node of the tree in pre-order (generic traversal)."""
    yield node
    for child in node.children():
        if child is not None:
            yield from walk(child)
