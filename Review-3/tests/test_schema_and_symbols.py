"""
Unit tests for the schema analyser (type inference) and the symbol table.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pytest  # noqa: E402

from preplang.errors import SchemaError  # noqa: E402
from preplang.schema import (  # noqa: E402
    DataType,
    infer_column_type,
    infer_schema,
    is_null,
)
from preplang.symbol_table import SymbolTable  # noqa: E402

EXAMPLES = Path(__file__).resolve().parents[1] / "examples"


# ---------------------------------------------------------------------------
# Type inference
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "values, expected",
    [
        (["1", "2", "3"], DataType.INTEGER),
        (["-1", "+2", "0"], DataType.INTEGER),
        (["1.5", "2.0"], DataType.FLOAT),
        (["1", "2.5"], DataType.FLOAT),          # mixed int/float widens to FLOAT
        (["TRUE", "false"], DataType.BOOLEAN),
        (["yes", "no"], DataType.BOOLEAN),
        (["CSE", "ECE"], DataType.STRING),
        (["1", "2", "abc"], DataType.STRING),    # one bad value demotes the column
        ([], DataType.UNKNOWN),
    ],
)
def test_column_type_inference(values, expected):
    assert infer_column_type(values) is expected


def test_numeric_predicate():
    assert DataType.INTEGER.is_numeric() and DataType.FLOAT.is_numeric()
    assert not DataType.STRING.is_numeric() and not DataType.BOOLEAN.is_numeric()


@pytest.mark.parametrize("cell", ["", "  ", "NA", "n/a", "null", "NaN", "-"])
def test_null_tokens(cell):
    assert is_null(cell)


def test_non_null_values():
    assert not is_null("0") and not is_null("CSE")


# ---------------------------------------------------------------------------
# Schema inference from a real file
# ---------------------------------------------------------------------------


def test_students_schema_is_inferred_correctly():
    schema = infer_schema("students.csv", base_dir=EXAMPLES)
    assert schema.names == ["name", "age", "marks", "attendance", "department", "passed"]
    assert schema.columns["name"].type is DataType.STRING
    assert schema.columns["age"].type is DataType.INTEGER
    assert schema.columns["marks"].type is DataType.INTEGER
    assert schema.columns["attendance"].type is DataType.FLOAT
    assert schema.columns["passed"].type is DataType.BOOLEAN


def test_nullability_is_detected():
    schema = infer_schema("students.csv", base_dir=EXAMPLES)
    assert schema.columns["age"].nullable          # one row has no age
    assert schema.columns["name"].nullable         # one row has no name
    assert not schema.columns["marks"].nullable    # marks is complete


def test_row_count_and_numeric_columns():
    schema = infer_schema("students.csv", base_dir=EXAMPLES)
    assert schema.total_rows == 12
    assert schema.numeric_columns() == ["age", "marks", "attendance"]


def test_a_dirty_column_is_inferred_as_string():
    schema = infer_schema("students_dirty.csv", base_dir=EXAMPLES)
    # age contains "abc", so the whole column is STRING -- this is what makes
    # `NORMALIZE students.age` a compile-time error on this dataset
    assert schema.columns["age"].type is DataType.STRING


def test_wide_dataset_schema():
    schema = infer_schema("employees.csv", base_dir=EXAMPLES)
    assert len(schema.columns) == 30
    assert schema.total_rows == 200


def test_missing_file_raises_a_schema_error():
    with pytest.raises(SchemaError) as info:
        infer_schema("does_not_exist.csv", base_dir=EXAMPLES)
    assert "Cannot read dataset file" in info.value.problem


# ---------------------------------------------------------------------------
# Symbol table
# ---------------------------------------------------------------------------


def build_table() -> SymbolTable:
    table = SymbolTable()
    schema = infer_schema("students.csv", base_dir=EXAMPLES)
    table.define_dataset("students", "students.csv", 1, schema)
    return table


def test_dataset_definition_populates_the_column_scope():
    table = build_table()
    assert "students" in table
    dataset = table.lookup_dataset("students")
    assert len(dataset.columns) == 6
    assert dataset.lookup("age").type is DataType.INTEGER


def test_unknown_names_return_none():
    table = build_table()
    assert table.lookup_dataset("missing") is None
    assert table.lookup_column("students", "salary") is None


def test_drop_marks_a_column_inactive_but_keeps_it():
    table = build_table()
    dataset = table.lookup_dataset("students")
    dataset.drop("department", line=5)
    symbol = dataset.lookup("department")
    assert symbol is not None          # kept, so "removed at line 5" can be reported
    assert not symbol.active and symbol.removed_at == 5
    assert "department" not in dataset.live_columns()


def test_select_keeps_only_the_listed_columns():
    table = build_table()
    dataset = table.lookup_dataset("students")
    dataset.keep_only(["name", "age"], line=7)
    assert dataset.live_columns() == ["name", "age"]
    assert set(dataset.dropped_columns()) == {"marks", "attendance", "department", "passed"}


def test_numeric_columns_respect_liveness():
    table = build_table()
    dataset = table.lookup_dataset("students")
    assert dataset.numeric_columns() == ["age", "marks", "attendance"]
    dataset.drop("marks", line=3)
    assert dataset.numeric_columns() == ["age", "attendance"]


def test_symbol_table_renders_a_readable_report():
    text = build_table().format()
    assert "DATASET  students" in text
    assert "age" in text and "INTEGER" in text
