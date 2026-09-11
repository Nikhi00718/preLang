"""
semantic_analyzer.py
====================

Compiler Phase 3 -- Semantic Analysis.

This is the phase that gives the project its name. It walks the AST in source
order while maintaining the symbol table, and enforces the ten static rules of
the language specification:

    S1   a dataset must be loaded before it is used
    S2   a dataset name may be bound only once
    S3   every referenced column must exist in the schema
    S4   a column removed by DROP/SELECT may not be used afterwards
    S5   NORMALIZE requires a numeric column
    S6   FILTER operands must be type-compatible
    S7   ordering operators require numeric operands
    S8   the FILL_NULL value must be assignable to the column type
    S9   EXPECT ... BETWEEN requires a numeric column and low <= high
    S10  SAVE must name a loaded dataset with at least one live column

The analogy with a conventional compiler is exact:

    int x = "hello";          ->  TYPE ERROR: cannot assign STRING to int
    NORMALIZE students.name;  ->  TYPE ERROR: NORMALIZE requires a numeric column

The difference is only where the type environment comes from: a conventional
compiler reads declarations from the program, PrepLang infers them from the
dataset (see :mod:`preplang.schema`).

Besides diagnostics, the analyser records a :class:`SemanticFact` per statement
-- the questions it asked and the answers it got. These facts are what the
compiler visualiser displays, and they make the phase explainable rather than
merely correct.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

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
    StatementNode,
)
from .errors import (
    ContractError,
    ErrorReporter,
    SchemaError,
    SemanticError,
    TypeError_,
    closest_names,
)
from .schema import DataType, infer_schema
from .symbol_table import ColumnSymbol, DatasetSymbol, SymbolTable


def _article(word: str) -> str:
    """'a' or 'an', so diagnostics read as English rather than as templates."""
    return "an" if word[:1].upper() in "AEIOU" else "a"


#: Operators that impose an ordering and therefore require numeric operands.
ORDERING_OPERATORS = {">", "<", ">=", "<="}

#: Mapping from a contract type keyword to the acceptable column types.
CONTRACT_TYPES: dict[str, tuple[DataType, ...]] = {
    "NUMBER": (DataType.INTEGER, DataType.FLOAT),
    "STRING": (DataType.STRING,),
    "BOOLEAN": (DataType.BOOLEAN,),
}


@dataclass
class SemanticFact:
    """The checks performed for one statement, recorded for visualisation."""

    line: int
    statement: str
    checks: list[tuple[str, str]] = field(default_factory=list)
    ok: bool = True

    def add(self, question: str, answer: str) -> None:
        self.checks.append((question, answer))


@dataclass
class AnalysisResult:
    """Everything the semantic phase produces."""

    symbols: SymbolTable
    facts: list[SemanticFact] = field(default_factory=list)
    #: contract statements kept for the data-validation phase
    contracts: list[StatementNode] = field(default_factory=list)
    #: dataset -> the output file it is saved to (used by the code generator)
    outputs: dict[str, str] = field(default_factory=dict)

    def format_facts(self) -> str:
        if not self.facts:
            return "(no statements analysed)"
        blocks: list[str] = []
        for fact in self.facts:
            status = "OK" if fact.ok else "REJECTED"
            blocks.append(f"line {fact.line:>3}  {fact.statement}   [{status}]")
            width = max((len(q) for q, _ in fact.checks), default=0)
            for question, answer in fact.checks:
                blocks.append(f"          {question:<{width}} : {answer}")
        return "\n".join(blocks)


class SemanticAnalyzer:
    """Walks the AST, maintains the symbol table and enforces rules S1-S10."""

    def __init__(
        self,
        reporter: ErrorReporter | None = None,
        base_dir: str | Path | None = None,
    ) -> None:
        self.reporter = reporter if reporter is not None else ErrorReporter()
        self.base_dir = Path(base_dir) if base_dir else Path.cwd()
        self.symbols = SymbolTable()
        self.result = AnalysisResult(symbols=self.symbols)
        self._fact: SemanticFact | None = None

    # ------------------------------------------------------------------
    # Entry point
    # ------------------------------------------------------------------
    def analyse(self, program: ProgramNode) -> AnalysisResult:
        dispatch = {
            LoadNode: self._load,
            DropNullNode: self._drop_null,
            FillNullNode: self._fill_null,
            NormalizeNode: self._normalize,
            FilterNode: self._filter,
            SelectNode: self._select,
            DropNode: self._drop,
            SaveNode: self._save,
            ExpectTypeNode: self._expect_type,
            ExpectRangeNode: self._expect_range,
            ExpectNotNullNode: self._expect_not_null,
        }
        for statement in program.statements:
            self._fact = SemanticFact(line=statement.line, statement=statement.describe())
            self.result.facts.append(self._fact)
            handler = dispatch.get(type(statement))
            if handler is not None:
                handler(statement)  # type: ignore[operator]

        self._final_checks()
        return self.result

    # ------------------------------------------------------------------
    # Reporting helpers
    # ------------------------------------------------------------------
    def _fail(self, error) -> None:
        if self._fact is not None:
            self._fact.ok = False
        self.reporter.report(error)

    def _note(self, question: str, answer: str) -> None:
        if self._fact is not None:
            self._fact.add(question, answer)

    # ------------------------------------------------------------------
    # Name resolution (rules S1, S3, S4)
    # ------------------------------------------------------------------
    def _resolve_dataset(self, name: str, line: int) -> DatasetSymbol | None:
        """S1 -- the dataset must have been loaded."""
        dataset = self.symbols.lookup_dataset(name)
        if dataset is None:
            self._fail(
                SemanticError(
                    problem=f"Undefined dataset {name!r}.",
                    line=line,
                    expected="a dataset bound by a previous LOAD statement",
                    found=name,
                    suggestions=closest_names(name, self.symbols.names),
                    hint="Every dataset must be introduced with LOAD \"file.csv\" AS name; first.",
                )
            )
            self._note("Dataset exists", "NO")
            return None
        self._note("Dataset exists", f"YES ({dataset.source})")
        return dataset

    def _resolve_column(
        self, dataset: DatasetSymbol, column: str, line: int
    ) -> ColumnSymbol | None:
        """S3 and S4 -- the column must exist and still be live."""
        if dataset.schema is None:
            # The LOAD of this dataset already failed, so its column scope is
            # empty. Reporting "column does not exist" here would bury the real
            # diagnostic under one cascading error per later statement.
            self._note("Column exists", "UNKNOWN (dataset could not be loaded)")
            return None

        symbol = dataset.lookup(column)
        if symbol is None:
            self._fail(
                SchemaError(
                    problem=f"Column {column!r} does not exist in dataset {dataset.name!r}.",
                    line=line,
                    expected="one of: " + ", ".join(dataset.live_columns()),
                    found=f"{dataset.name}.{column}",
                    suggestions=closest_names(column, dataset.columns),
                    hint=f"The schema of {dataset.source} was inferred when the dataset was loaded.",
                )
            )
            self._note("Column exists", "NO")
            return None

        if not symbol.active:
            self._fail(
                SemanticError(
                    problem=(
                        f"Column {column!r} of dataset {dataset.name!r} is no longer available; "
                        f"it was removed at line {symbol.removed_at}."
                    ),
                    line=line,
                    expected="a column that is still present: " + ", ".join(dataset.live_columns()),
                    found=f"{dataset.name}.{column} (removed at line {symbol.removed_at})",
                    hint="Move this statement before the DROP/SELECT that removes the column.",
                )
            )
            self._note("Column exists", f"REMOVED at line {symbol.removed_at}")
            return None

        self._note("Column exists", "YES")
        self._note("Column datatype", str(symbol.type))
        return symbol

    # ------------------------------------------------------------------
    # Statement handlers
    # ------------------------------------------------------------------
    def _load(self, node: LoadNode) -> None:
        # S2 -- no redefinition
        existing = self.symbols.lookup_dataset(node.dataset)
        if existing is not None:
            self._fail(
                SemanticError(
                    problem=f"Dataset {node.dataset!r} is already defined at line {existing.line}.",
                    line=node.line,
                    expected="a dataset name that is not yet in use",
                    found=node.dataset,
                    hint="Choose a different name; PrepLang datasets are bound once.",
                )
            )
            return

        try:
            schema = infer_schema(node.path, base_dir=self.base_dir, line=node.line)
        except SchemaError as error:
            self._fail(error)
            # Bind the name anyway, with an empty scope, so that later
            # statements report *their own* problems instead of cascading
            # "undefined dataset" errors. This is semantic error recovery.
            self.symbols.define_dataset(node.dataset, node.path, node.line, None)
            return

        dataset = self.symbols.define_dataset(node.dataset, node.path, node.line, schema)
        dataset.operations.append("LOAD")
        self._note("File readable", "YES")
        self._note("Columns detected", str(len(schema.columns)))
        self._note("Rows in file", str(schema.total_rows))
        self._note(
            "Inferred schema",
            ", ".join(f"{c.name}:{c.type}" for c in schema.columns.values()),
        )

    def _drop_null(self, node: DropNullNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        dataset.operations.append("DROP_NULL")
        nullable = [n for n, c in dataset.columns.items() if c.active and c.nullable]
        self._note("Nullable columns", ", ".join(nullable) if nullable else "none")
        if not nullable:
            self.reporter.warn(
                f"DROP_NULL on {node.dataset!r} has no effect: no live column of the "
                f"dataset contains missing values.",
                line=node.line,
                kind="OPTIMISER WARNING",
            )
        else:
            # after DROP_NULL no live column can contain nulls any more
            for symbol in dataset.columns.values():
                if symbol.active:
                    symbol.nullable = False
                    symbol.history.append("drop_null")

    def _fill_null(self, node: FillNullNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        column = self._resolve_column(dataset, node.column, node.line)
        if column is None or node.value is None:
            return

        value_type = node.value.type
        self._note("Value datatype", str(value_type))

        # S8 -- assignability
        if not self._assignable(value_type, column.type):
            self._fail(
                TypeError_(
                    problem=(
                        f"Cannot fill a {column.type} column with a {value_type} value."
                    ),
                    line=node.line,
                    expected=f"{_article(str(column.type))} {column.type} literal",
                    found=f"{node.value.value!r} : {value_type}",
                    hint=f"Column {dataset.name}.{column.name} was inferred as {column.type} "
                    f"from {dataset.source}.",
                )
            )
            return

        self._note("Operation allowed", "YES")
        if not column.nullable:
            self.reporter.warn(
                f"FILL_NULL on {node.dataset}.{node.column} has no effect: the column "
                f"contains no missing values.",
                line=node.line,
                kind="OPTIMISER WARNING",
            )
        column.nullable = False
        column.history.append("fill_null")
        dataset.operations.append(f"FILL_NULL({node.column})")

    def _normalize(self, node: NormalizeNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        column = self._resolve_column(dataset, node.column, node.line)
        if column is None:
            return

        # S5 -- NORMALIZE requires a numeric column
        if not column.type.is_numeric():
            numeric = dataset.numeric_columns()
            self._fail(
                TypeError_(
                    problem="NORMALIZE requires a numerical column.",
                    line=node.line,
                    expected="a column of type INTEGER or FLOAT",
                    found=f"{dataset.name}.{column.name} : {column.type}",
                    suggestions=numeric,
                    hint=(
                        "Numerical columns available in this dataset: "
                        + (", ".join(numeric) if numeric else "none")
                    ),
                )
            )
            self._note("Operation allowed", "NO")
            return

        self._note("Operation allowed", "YES")
        self._note("Result datatype", "FLOAT (z-score)")
        column.type = DataType.FLOAT  # normalisation always yields a real number
        column.history.append("normalize")
        dataset.operations.append(f"NORMALIZE({node.column})")

    def _filter(self, node: FilterNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None or node.condition is None:
            return
        condition = node.condition
        column = self._resolve_column(dataset, condition.column, node.line)
        if column is None or condition.value is None:
            return

        literal_type = condition.value.type
        self._note("Comparison", f"{column.type} {condition.operator} {literal_type}")

        # S6 -- operands must be comparable at all
        if not self._comparable(column.type, literal_type):
            self._fail(
                TypeError_(
                    problem=f"Cannot compare {column.type} with {literal_type}.",
                    line=node.line,
                    expected=f"{_article(str(column.type))} {column.type} literal on the right-hand side",
                    found=f"{condition.value.value!r} : {literal_type}",
                    hint=f"Column {dataset.name}.{column.name} was inferred as {column.type}.",
                )
            )
            self._note("Operation allowed", "NO")
            return

        # S7 -- ordering requires numeric operands
        if condition.operator in ORDERING_OPERATORS and not column.type.is_numeric():
            self._fail(
                TypeError_(
                    problem=(
                        f"Operator {condition.operator!r} requires numerical operands, "
                        f"but {dataset.name}.{column.name} is {column.type}."
                    ),
                    line=node.line,
                    expected="a numerical column, or the operators == and != ",
                    found=f"{column.type} {condition.operator} {literal_type}",
                    hint="Only == and != are defined for STRING and BOOLEAN columns.",
                )
            )
            self._note("Operation allowed", "NO")
            return

        self._note("Operation allowed", "YES")
        column.history.append("filter")
        dataset.operations.append(f"FILTER({condition.column})")

    def _select(self, node: SelectNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        resolved = [c for c in node.columns if self._resolve_column(dataset, c, node.line)]
        if len(resolved) != len(node.columns):
            return
        duplicates = {c for c in node.columns if node.columns.count(c) > 1}
        if duplicates:
            self.reporter.warn(
                f"SELECT lists duplicated column(s): {', '.join(sorted(duplicates))}.",
                line=node.line,
            )
        removed = [c for c in dataset.live_columns() if c not in set(node.columns)]
        dataset.keep_only(node.columns, node.line)
        dataset.operations.append("SELECT")
        self._note("Columns kept", ", ".join(node.columns))
        self._note("Columns removed", ", ".join(removed) if removed else "none")

    def _drop(self, node: DropNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        resolved = [c for c in node.columns if self._resolve_column(dataset, c, node.line)]
        if len(resolved) != len(node.columns):
            return
        for column in node.columns:
            dataset.drop(column, node.line)
        dataset.operations.append("DROP")
        self._note("Columns removed", ", ".join(node.columns))
        remaining = dataset.live_columns()
        self._note("Columns remaining", ", ".join(remaining) if remaining else "none")
        if not remaining:
            self._fail(
                SemanticError(
                    problem=f"DROP removes every column of dataset {dataset.name!r}.",
                    line=node.line,
                    expected="at least one column to remain",
                    hint="A dataset with no columns cannot be saved.",
                )
            )

    def _save(self, node: SaveNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        # S10 -- something must remain to save
        live = dataset.live_columns()
        if dataset.schema is not None and not live:
            self._fail(
                SemanticError(
                    problem=f"Dataset {dataset.name!r} has no columns left to save.",
                    line=node.line,
                    expected="at least one live column",
                    hint="A previous DROP or SELECT removed every column.",
                )
            )
            return
        dataset.operations.append("SAVE")
        self.result.outputs[dataset.name] = node.path
        self._note("Columns written", ", ".join(live) if live else "(unknown)")
        self._note("Output file", node.path)

    # ------------------------------------------------------------------
    # Contract statements
    # ------------------------------------------------------------------
    def _expect_type(self, node: ExpectTypeNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        column = self._resolve_column(dataset, node.column, node.line)
        if column is None:
            return

        accepted = CONTRACT_TYPES[node.expected]
        self._note("Contract", f"{node.column} must be {node.expected}")
        if column.type not in accepted:
            # A type contract can be decided statically from the schema: this
            # is a contract violation detected at COMPILE time.
            self._fail(
                ContractError(
                    problem=(
                        f"Contract violated: {dataset.name}.{column.name} is declared "
                        f"{node.expected} but the dataset schema says {column.type}."
                    ),
                    line=node.line,
                    expected=f"{node.expected} ({' or '.join(t.value for t in accepted)})",
                    found=f"{column.type} (inferred from {dataset.source})",
                    hint=(
                        "A single non-numeric value in the column is enough to make the "
                        "whole column STRING -- inspect the data."
                    ),
                )
            )
            self._note("Satisfied statically", "NO")
            return

        self._note("Satisfied statically", "YES")
        self.result.contracts.append(node)

    def _expect_range(self, node: ExpectRangeNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        column = self._resolve_column(dataset, node.column, node.line)
        if column is None:
            return

        # S9 -- a range contract only makes sense on a numeric column
        if not column.type.is_numeric():
            self._fail(
                TypeError_(
                    problem="A BETWEEN contract requires a numerical column.",
                    line=node.line,
                    expected="a column of type INTEGER or FLOAT",
                    found=f"{dataset.name}.{column.name} : {column.type}",
                    suggestions=dataset.numeric_columns(),
                )
            )
            return
        if node.low > node.high:
            self._fail(
                SemanticError(
                    problem=f"Empty range: {node.low} > {node.high}.",
                    line=node.line,
                    expected="low <= high",
                    found=f"BETWEEN {node.low} AND {node.high}",
                )
            )
            return

        self._note("Contract", f"{node.low} <= {node.column} <= {node.high}")
        self._note("Checked", "against the data (see the contract report)")
        self.result.contracts.append(node)

    def _expect_not_null(self, node: ExpectNotNullNode) -> None:
        dataset = self._resolve_dataset(node.dataset, node.line)
        if dataset is None:
            return
        column = self._resolve_column(dataset, node.column, node.line)
        if column is None:
            return
        self._note("Contract", f"{node.column} must not be null")
        if not column.nullable:
            self._note("Satisfied statically", "YES (schema reports no missing values)")
        else:
            self._note("Satisfied statically", "NO -- checked against the data")
        self.result.contracts.append(node)

    # ------------------------------------------------------------------
    # Whole-program checks
    # ------------------------------------------------------------------
    def _final_checks(self) -> None:
        for dataset in self.symbols.datasets.values():
            if "SAVE" not in dataset.operations and dataset.schema is not None:
                self.reporter.warn(
                    f"Dataset {dataset.name!r} is never saved; the pipeline produces no output "
                    f"for it.",
                    line=dataset.line,
                )

    # ------------------------------------------------------------------
    # Type rules
    # ------------------------------------------------------------------
    @staticmethod
    def _assignable(value: DataType, target: DataType) -> bool:
        """Can a literal of type *value* be stored in a column of *target*?"""
        if value is target:
            return True
        # INTEGER widens to FLOAT (the classical numeric coercion)
        if target is DataType.FLOAT and value is DataType.INTEGER:
            return True
        if target is DataType.UNKNOWN or value is DataType.UNKNOWN:
            return True  # error recovery: do not cascade
        return False

    @staticmethod
    def _comparable(column: DataType, literal: DataType) -> bool:
        """Are the two operand types comparable at all?"""
        if column is DataType.UNKNOWN or literal is DataType.UNKNOWN:
            return True  # error recovery
        if column.is_numeric() and literal.is_numeric():
            return True
        return column is literal


def analyse(
    program: ProgramNode,
    reporter: ErrorReporter | None = None,
    base_dir: str | Path | None = None,
) -> tuple[AnalysisResult, ErrorReporter]:
    """Analyse *program* and return ``(result, reporter)``."""
    analyser = SemanticAnalyzer(reporter, base_dir)
    return analyser.analyse(program), analyser.reporter
