"""
schema.py
=========

The PrepLang type system and the CSV schema analyser.

This module is what makes PrepLang *schema-aware*. A conventional compiler
builds its type environment from declarations written in the program; PrepLang
builds it by **inspecting the dataset**. Once the schema exists, ordinary
semantic analysis applies: a column is a variable and its inferred type is its
type.

Inference algorithm (per column, over a sample of rows)
-------------------------------------------------------
1. Collect the non-empty values of the column.
2. If every value is ``TRUE``/``FALSE``/``YES``/``NO``/``0``/``1``-style
   boolean text -> ``BOOLEAN``.
3. Else if every value parses as an integer -> ``INTEGER``.
4. Else if every value parses as a real number -> ``FLOAT``.
5. Else -> ``STRING``.
6. ``nullable`` is true when at least one sampled cell was empty.

A single non-numeric value therefore demotes a column to ``STRING`` — which is
exactly the behaviour that lets the compiler detect a dirty dataset, as in
``students_dirty.csv`` where ``age`` contains ``abc``.

Only the standard-library :mod:`csv` module is used, so the entire compiler
front end has no third-party dependency; Pandas is needed only to *execute*
the generated code.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path

from .errors import SchemaError

#: How many data rows are inspected when inferring types.
DEFAULT_SAMPLE_SIZE = 2000

_TRUE_WORDS = {"true", "yes", "y", "t"}
_FALSE_WORDS = {"false", "no", "n", "f"}
#: Strings that denote a missing value in a CSV cell.
NULL_TOKENS = {"", "na", "n/a", "nan", "null", "none", "-"}


class DataType(Enum):
    """The PrepLang column type lattice."""

    INTEGER = "INTEGER"
    FLOAT = "FLOAT"
    STRING = "STRING"
    BOOLEAN = "BOOLEAN"
    UNKNOWN = "UNKNOWN"

    def is_numeric(self) -> bool:
        return self in (DataType.INTEGER, DataType.FLOAT)

    def __str__(self) -> str:  # pragma: no cover - trivial
        return self.value

    @staticmethod
    def of_python(value: object) -> "DataType":
        """The PrepLang type of a Python literal taken from the program."""
        if isinstance(value, bool):
            return DataType.BOOLEAN
        if isinstance(value, int):
            return DataType.INTEGER
        if isinstance(value, float):
            return DataType.FLOAT
        if isinstance(value, str):
            return DataType.STRING
        return DataType.UNKNOWN


def is_null(cell: str) -> bool:
    """True when a raw CSV cell denotes a missing value."""
    return cell.strip().lower() in NULL_TOKENS


def _looks_boolean(cell: str) -> bool:
    return cell.strip().lower() in (_TRUE_WORDS | _FALSE_WORDS)


def _looks_integer(cell: str) -> bool:
    text = cell.strip()
    if text.startswith(("+", "-")):
        text = text[1:]
    return text.isdigit() and text != ""


def _looks_float(cell: str) -> bool:
    try:
        float(cell.strip())
        return True
    except ValueError:
        return False


def infer_column_type(values: list[str]) -> DataType:
    """Infer the :class:`DataType` of one column from its non-null values."""
    if not values:
        return DataType.UNKNOWN
    if all(_looks_boolean(v) for v in values):
        return DataType.BOOLEAN
    if all(_looks_integer(v) for v in values):
        return DataType.INTEGER
    if all(_looks_float(v) for v in values):
        return DataType.FLOAT
    return DataType.STRING


# ---------------------------------------------------------------------------
# Schema records
# ---------------------------------------------------------------------------


@dataclass
class ColumnSchema:
    """The inferred description of one CSV column."""

    name: str
    type: DataType
    nullable: bool
    index: int
    null_count: int = 0
    sample_count: int = 0

    def __str__(self) -> str:
        null = "YES" if self.nullable else "NO"
        return f"{self.name:<14}{self.type!s:<10}nullable={null}"


@dataclass
class DatasetSchema:
    """The inferred description of one CSV file."""

    path: str
    columns: dict[str, ColumnSchema] = field(default_factory=dict)
    sampled_rows: int = 0
    total_rows: int = 0

    @property
    def names(self) -> list[str]:
        return list(self.columns)

    def numeric_columns(self) -> list[str]:
        return [n for n, c in self.columns.items() if c.type.is_numeric()]

    def __contains__(self, column: str) -> bool:
        return column in self.columns

    def format(self, dataset_name: str = "") -> str:
        title = f"Schema of {dataset_name or Path(self.path).name}"
        header = f"{'#':<4}{'COLUMN':<16}{'TYPE':<10}{'NULLABLE':<10}{'NULLS':<8}SAMPLED"
        lines = [title, "-" * len(header), header, "-" * len(header)]
        for column in self.columns.values():
            lines.append(
                f"{column.index:<4}{column.name:<16}{column.type!s:<10}"
                f"{('YES' if column.nullable else 'NO'):<10}{column.null_count:<8}{column.sample_count}"
            )
        lines.append("-" * len(header))
        lines.append(
            f"{len(self.columns)} columns, {self.total_rows} data rows "
            f"({self.sampled_rows} sampled for type inference)"
        )
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# The schema analyser
# ---------------------------------------------------------------------------


def infer_schema(
    path: str | Path,
    base_dir: str | Path | None = None,
    sample_size: int = DEFAULT_SAMPLE_SIZE,
    line: int | None = None,
) -> DatasetSchema:
    """Read *path* and infer a :class:`DatasetSchema`.

    Raises :class:`~preplang.errors.SchemaError` when the file cannot be read
    or has no header row.
    """
    resolved = Path(path)
    if base_dir is not None and not resolved.is_absolute():
        resolved = Path(base_dir) / resolved

    if not resolved.exists():
        raise SchemaError(
            problem=f"Cannot read dataset file {str(path)!r}.",
            line=line,
            found=f"{resolved} does not exist",
            hint="Check the path in the LOAD statement; it is resolved relative to the program file.",
        )

    try:
        with resolved.open("r", newline="", encoding="utf-8-sig") as handle:
            reader = csv.reader(handle)
            try:
                header = next(reader)
            except StopIteration:
                raise SchemaError(
                    problem=f"Dataset file {str(path)!r} is empty.",
                    line=line,
                    expected="a CSV file with a header row",
                )

            header = [h.strip() for h in header]
            if not header or all(h == "" for h in header):
                raise SchemaError(
                    problem=f"Dataset file {str(path)!r} has no usable header row.",
                    line=line,
                    expected="a first row containing column names",
                )

            samples: list[list[str]] = [[] for _ in header]
            nulls = [0] * len(header)
            sampled = 0
            total = 0

            for row in reader:
                total += 1
                if sampled < sample_size:
                    sampled += 1
                    for index in range(len(header)):
                        cell = row[index] if index < len(row) else ""
                        if is_null(cell):
                            nulls[index] += 1
                        else:
                            samples[index].append(cell)
    except OSError as exc:  # pragma: no cover - filesystem dependent
        raise SchemaError(
            problem=f"Cannot read dataset file {str(path)!r}.",
            line=line,
            found=str(exc),
        ) from exc

    columns: dict[str, ColumnSchema] = {}
    for index, name in enumerate(header):
        columns[name] = ColumnSchema(
            name=name,
            type=infer_column_type(samples[index]),
            nullable=nulls[index] > 0,
            index=index,
            null_count=nulls[index],
            sample_count=sampled,
        )

    return DatasetSchema(
        path=str(resolved), columns=columns, sampled_rows=sampled, total_rows=total
    )
