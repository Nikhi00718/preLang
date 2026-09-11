"""
contracts.py
============

Data contracts -- the signature feature of PrepLang.

An `EXPECT` statement declares a property that the data must satisfy. Unlike a
validation library, the property is part of the *language*, so it is processed
by the compiler in two stages:

**Stage 1 -- static checking** (in :mod:`preplang.semantic_analyzer`)
    Whatever can be decided from the inferred schema is decided at compile
    time and never reaches the data. ``EXPECT students.age NUMBER`` is
    rejected outright when the schema says ``age : STRING``; ``EXPECT ...
    BETWEEN`` on a text column is a type error; an empty range is a semantic
    error.

**Stage 2 -- data validation** (this module)
    What cannot be decided statically -- whether an actual value lies in a
    range, whether a nullable column actually contains a null -- is checked by
    reading the dataset once and evaluating every contract for that dataset
    simultaneously.

Contracts are evaluated against the dataset **as loaded**, i.e. the input
file, because that is what the programmer is making a claim about: `EXPECT`
documents an assumption about incoming data, and the pipeline that follows is
allowed to rely on it.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass, field
from pathlib import Path

from .ast_nodes import ExpectNotNullNode, ExpectRangeNode, ExpectTypeNode, StatementNode
from .errors import ContractError, ErrorReporter
from .schema import DataType, is_null
from .symbol_table import SymbolTable

#: Only the first few offending rows of each contract are recorded.
MAX_VIOLATION_SAMPLES = 5


@dataclass
class Violation:
    """One offending cell."""

    row: int
    value: str

    def __str__(self) -> str:
        shown = "(empty)" if self.value == "" else repr(self.value)
        return f"row {self.row}: {shown}"


@dataclass
class ContractResult:
    """The outcome of checking one contract against the data."""

    line: int
    dataset: str
    column: str
    kind: str
    description: str
    checked_rows: int = 0
    violations: list[Violation] = field(default_factory=list)
    violation_count: int = 0
    skipped: str | None = None

    @property
    def satisfied(self) -> bool:
        return self.violation_count == 0 and self.skipped is None

    def status(self) -> str:
        if self.skipped:
            return "SKIPPED"
        return "PASS" if self.satisfied else "FAIL"


@dataclass
class ContractReport:
    """The result of validating every contract in the program."""

    results: list[ContractResult] = field(default_factory=list)

    @property
    def passed(self) -> list[ContractResult]:
        return [r for r in self.results if r.satisfied]

    @property
    def failed(self) -> list[ContractResult]:
        return [r for r in self.results if not r.satisfied and not r.skipped]

    @property
    def ok(self) -> bool:
        return not self.failed

    def format(self) -> str:
        if not self.results:
            return "(no data contracts declared)"
        lines = ["DATA CONTRACT VALIDATION", "=" * 62]
        for result in self.results:
            mark = {"PASS": "[PASS]", "FAIL": "[FAIL]", "SKIPPED": "[SKIP]"}[result.status()]
            lines.append(
                f"{mark}  line {result.line:>3}  {result.dataset}.{result.column}  --  {result.description}"
            )
            if result.skipped:
                lines.append(f"          skipped: {result.skipped}")
                continue
            if result.satisfied:
                lines.append(f"          {result.checked_rows} rows checked, no violation")
            else:
                lines.append(
                    f"          {result.violation_count} violation(s) in {result.checked_rows} rows"
                )
                for violation in result.violations:
                    lines.append(f"            - {violation}")
                if result.violation_count > len(result.violations):
                    hidden = result.violation_count - len(result.violations)
                    lines.append(f"            ... and {hidden} more")
        lines.append("=" * 62)
        lines.append(
            f"{len(self.passed)} passed, {len(self.failed)} failed, "
            f"{len(self.results) - len(self.passed) - len(self.failed)} skipped"
        )
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# Predicates
# ---------------------------------------------------------------------------


def _describe(contract: StatementNode) -> tuple[str, str]:
    """Return ``(kind, human description)`` for a contract node."""
    if isinstance(contract, ExpectTypeNode):
        return "TYPE", f"every value must be {contract.expected}"
    if isinstance(contract, ExpectRangeNode):
        return "RANGE", f"{_num(contract.low)} <= value <= {_num(contract.high)}"
    if isinstance(contract, ExpectNotNullNode):
        return "NOT_NULL", "no value may be missing"
    return "UNKNOWN", "unknown contract"


def _num(value: float) -> str:
    return str(int(value)) if float(value).is_integer() else str(value)


def _violates(contract: StatementNode, cell: str) -> bool:
    """True when the raw CSV *cell* violates *contract*."""
    empty = is_null(cell)

    if isinstance(contract, ExpectNotNullNode):
        return empty

    # A missing value cannot violate a type or range contract; that is what
    # NOT_NULL is for. Checking one property at a time keeps each diagnostic
    # about exactly one thing.
    if empty:
        return False

    if isinstance(contract, ExpectTypeNode):
        text = cell.strip()
        if contract.expected == "NUMBER":
            try:
                float(text)
                return False
            except ValueError:
                return True
        if contract.expected == "BOOLEAN":
            return text.lower() not in {"true", "false", "yes", "no", "t", "f", "y", "n"}
        return False  # STRING: any non-null text is acceptable

    if isinstance(contract, ExpectRangeNode):
        try:
            value = float(cell.strip())
        except ValueError:
            return True  # not a number at all -> outside any numeric range
        return not (contract.low <= value <= contract.high)

    return False


# ---------------------------------------------------------------------------
# Validation driver
# ---------------------------------------------------------------------------


def validate_contracts(
    contracts: list[StatementNode],
    symbols: SymbolTable,
    base_dir: str | Path | None = None,
    reporter: ErrorReporter | None = None,
) -> ContractReport:
    """Check every contract against the data of its dataset.

    The dataset file is read **once per dataset**, no matter how many
    contracts refer to it.
    """
    report = ContractReport()
    if not contracts:
        return report

    # group by dataset so each file is streamed only once
    by_dataset: dict[str, list[StatementNode]] = {}
    for contract in contracts:
        by_dataset.setdefault(getattr(contract, "dataset", ""), []).append(contract)

    for dataset_name, group in by_dataset.items():
        dataset = symbols.lookup_dataset(dataset_name)
        results = [_result_for(c, dataset_name) for c in group]
        report.results.extend(results)

        if dataset is None or dataset.schema is None:
            for result in results:
                result.skipped = "dataset schema unavailable"
            continue

        path = Path(dataset.schema.path)
        if not path.exists():  # pragma: no cover - defensive
            for result in results:
                result.skipped = f"cannot read {path}"
            continue

        with path.open("r", newline="", encoding="utf-8-sig") as handle:
            reader = csv.reader(handle)
            header = [h.strip() for h in next(reader, [])]
            index_of = {name: i for i, name in enumerate(header)}

            pairs = []
            for contract, result in zip(group, results):
                column = getattr(contract, "column", "")
                if column not in index_of:
                    result.skipped = f"column {column!r} not present in the file"
                    continue
                pairs.append((contract, result, index_of[column]))

            # row 1 is the header, so data rows start at 2 -- matching what a
            # user sees when opening the CSV in a spreadsheet
            row_number = 1
            for row in reader:
                row_number += 1
                for contract, result, index in pairs:
                    cell = row[index] if index < len(row) else ""
                    result.checked_rows += 1
                    if _violates(contract, cell):
                        result.violation_count += 1
                        if len(result.violations) < MAX_VIOLATION_SAMPLES:
                            result.violations.append(Violation(row=row_number, value=cell))

    if reporter is not None:
        for result in report.failed:
            reporter.report(
                ContractError(
                    problem=(
                        f"Data contract violated: {result.dataset}.{result.column} -- "
                        f"{result.description}."
                    ),
                    line=result.line,
                    expected=result.description,
                    found=f"{result.violation_count} violating row(s), "
                    f"e.g. {result.violations[0]}" if result.violations else None,
                    hint="Add DROP_NULL / FILTER / FILL_NULL before this contract, "
                    "or correct the source data.",
                )
            )
    return report


def _result_for(contract: StatementNode, dataset: str) -> ContractResult:
    kind, description = _describe(contract)
    return ContractResult(
        line=contract.line,
        dataset=dataset,
        column=getattr(contract, "column", ""),
        kind=kind,
        description=description,
    )


def static_type_of_contract(contract: StatementNode) -> DataType:
    """The column type a contract implies (used by the tests and the report)."""
    if isinstance(contract, ExpectTypeNode):
        return {
            "NUMBER": DataType.FLOAT,
            "STRING": DataType.STRING,
            "BOOLEAN": DataType.BOOLEAN,
        }[contract.expected]
    if isinstance(contract, ExpectRangeNode):
        return DataType.FLOAT
    return DataType.UNKNOWN
