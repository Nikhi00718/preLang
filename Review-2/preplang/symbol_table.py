"""
symbol_table.py
===============

Symbol-table management.

PrepLang has a two-level name space:

    global scope          the dataset names bound by LOAD
      +-- dataset scope   the columns of that dataset

A `DatasetSymbol` therefore plays the role of a scope, and a `ColumnSymbol`
the role of a variable declaration. Each column symbol carries

    * its inferred datatype (from the schema analyser),
    * whether it may contain nulls,
    * whether it is still **live** -- a column removed by DROP or excluded by
      SELECT stays in the table but is marked inactive, so that a later use
      can be reported as "column no longer available" rather than the less
      helpful "column does not exist".

Keeping dropped columns rather than deleting them is a deliberate design
decision: it is what allows the analyser to distinguish a typo from a
lifetime error.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .schema import ColumnSchema, DataType, DatasetSchema


@dataclass
class ColumnSymbol:
    """One column of one dataset."""

    name: str
    type: DataType
    nullable: bool
    index: int
    dataset: str
    active: bool = True
    #: line at which the column was removed, when it is no longer active
    removed_at: int | None = None
    #: operations applied to this column, for the optimiser and reports
    history: list[str] = field(default_factory=list)

    def state(self) -> str:
        return "live" if self.active else f"dropped(line {self.removed_at})"

    def __str__(self) -> str:
        return f"{self.name:<14}{self.type!s:<10}{'null?' if self.nullable else '     '}  {self.state()}"


@dataclass
class DatasetSymbol:
    """One dataset -- the scope that holds column symbols."""

    name: str
    source: str
    line: int
    columns: dict[str, ColumnSymbol] = field(default_factory=dict)
    schema: DatasetSchema | None = None
    #: statements applied to the dataset, in order (for reporting)
    operations: list[str] = field(default_factory=list)

    # -- queries -----------------------------------------------------------
    def live_columns(self) -> list[str]:
        return [n for n, c in self.columns.items() if c.active]

    def dropped_columns(self) -> list[str]:
        return [n for n, c in self.columns.items() if not c.active]

    def numeric_columns(self, only_live: bool = True) -> list[str]:
        return [
            n
            for n, c in self.columns.items()
            if c.type.is_numeric() and (c.active or not only_live)
        ]

    def lookup(self, column: str) -> ColumnSymbol | None:
        return self.columns.get(column)

    # -- mutation ----------------------------------------------------------
    def drop(self, column: str, line: int) -> None:
        symbol = self.columns.get(column)
        if symbol is not None:
            symbol.active = False
            symbol.removed_at = line

    def keep_only(self, columns: list[str], line: int) -> None:
        """Apply SELECT: every column not listed becomes inactive."""
        keep = set(columns)
        for name, symbol in self.columns.items():
            if name not in keep and symbol.active:
                symbol.active = False
                symbol.removed_at = line


class SymbolTable:
    """The global scope: a mapping from dataset name to :class:`DatasetSymbol`."""

    def __init__(self) -> None:
        self.datasets: dict[str, DatasetSymbol] = {}

    # -- insertion ---------------------------------------------------------
    def define_dataset(
        self, name: str, source: str, line: int, schema: DatasetSchema | None = None
    ) -> DatasetSymbol:
        """Bind *name* to a dataset and populate its column scope from *schema*."""
        symbol = DatasetSymbol(name=name, source=source, line=line, schema=schema)
        if schema is not None:
            for column in schema.columns.values():
                symbol.columns[column.name] = self._column_symbol(column, name)
        self.datasets[name] = symbol
        return symbol

    @staticmethod
    def _column_symbol(column: ColumnSchema, dataset: str) -> ColumnSymbol:
        return ColumnSymbol(
            name=column.name,
            type=column.type,
            nullable=column.nullable,
            index=column.index,
            dataset=dataset,
        )

    # -- lookup ------------------------------------------------------------
    def lookup_dataset(self, name: str) -> DatasetSymbol | None:
        return self.datasets.get(name)

    def lookup_column(self, dataset: str, column: str) -> ColumnSymbol | None:
        symbol = self.datasets.get(dataset)
        return symbol.lookup(column) if symbol else None

    def __contains__(self, name: str) -> bool:
        return name in self.datasets

    def __len__(self) -> int:
        return len(self.datasets)

    @property
    def names(self) -> list[str]:
        return list(self.datasets)

    # -- rendering ---------------------------------------------------------
    def format(self) -> str:
        if not self.datasets:
            return "(symbol table is empty -- no dataset was loaded)"

        lines: list[str] = []
        for dataset in self.datasets.values():
            lines.append(f"DATASET  {dataset.name}")
            lines.append(f"  source     : {dataset.source}")
            lines.append(f"  defined at : line {dataset.line}")
            lines.append(f"  operations : {', '.join(dataset.operations) or '(none)'}")
            header = f"  {'#':<4}{'COLUMN':<16}{'TYPE':<10}{'NULLABLE':<10}{'STATE':<20}HISTORY"
            lines.append("  " + "-" * (len(header) - 2))
            lines.append(header)
            lines.append("  " + "-" * (len(header) - 2))
            for column in dataset.columns.values():
                lines.append(
                    f"  {column.index:<4}{column.name:<16}{column.type!s:<10}"
                    f"{('YES' if column.nullable else 'NO'):<10}{column.state():<20}"
                    f"{', '.join(column.history)}"
                )
            lines.append("  " + "-" * (len(header) - 2))
            lines.append(
                f"  {len(dataset.columns)} columns "
                f"({len(dataset.live_columns())} live, {len(dataset.dropped_columns())} dropped)"
            )
            lines.append("")
        return "\n".join(lines).rstrip()
