"""
Unit tests for Compiler Phase 3 -- Semantic Analysis.

There is one test per static rule (S1-S10) plus tests for the positive cases,
for the quality of the diagnostics, and for semantic error recovery.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from conftest import compile_demo, load  # noqa: E402
from preplang.errors import ErrorPhase  # noqa: E402
from preplang.schema import DataType  # noqa: E402


def analyse(body: str, dataset: str = "students.csv"):
    return compile_demo(load(dataset) + body, validate_data=False)


def only_error(body: str, dataset: str = "students.csv"):
    result = analyse(body, dataset)
    assert len(result.reporter.errors) == 1, [e.problem for e in result.reporter.errors]
    return result.reporter.errors[0]


# ---------------------------------------------------------------------------
# Positive cases
# ---------------------------------------------------------------------------


def test_a_well_formed_pipeline_is_accepted():
    result = analyse(
        """
        DROP_NULL students;
        NORMALIZE students.age;
        FILTER students WHERE marks >= 50;
        SELECT students [ name, age, marks ];
        SAVE students AS "out.csv";
        """
    )
    assert result.reporter.ok, [e.problem for e in result.reporter.errors]


def test_schema_is_loaded_into_the_symbol_table_by_load():
    result = analyse('SAVE students AS "out.csv";')
    dataset = result.symbols.lookup_dataset("students")
    assert dataset.schema is not None
    assert dataset.lookup("age").type is DataType.INTEGER


def test_normalize_changes_the_column_type_to_float():
    result = analyse('NORMALIZE students.marks; SAVE students AS "o.csv";')
    assert result.symbols.lookup_column("students", "marks").type is DataType.FLOAT


def test_drop_null_clears_nullability():
    result = analyse('DROP_NULL students; SAVE students AS "o.csv";')
    assert not result.symbols.lookup_column("students", "age").nullable


def test_integer_literal_may_fill_a_float_column():
    # INTEGER widens to FLOAT -- the classical numeric coercion
    result = analyse('FILL_NULL students.attendance WITH 0; SAVE students AS "o.csv";')
    assert result.reporter.ok


# ---------------------------------------------------------------------------
# S1 -- undefined dataset
# ---------------------------------------------------------------------------


def test_s1_undefined_dataset_is_rejected():
    error = only_error("NORMALIZE results.age;")
    assert error.phase == ErrorPhase.SEMANTIC
    assert "Undefined dataset" in error.problem


def test_s1_suggests_a_similar_dataset_name():
    result = compile_demo(load() + "NORMALIZE studets.age;", validate_data=False)
    assert "students" in result.reporter.errors[0].suggestions


# ---------------------------------------------------------------------------
# S2 -- redefinition
# ---------------------------------------------------------------------------


def test_s2_dataset_may_not_be_redefined():
    error = only_error('LOAD "students.csv" AS students;')
    assert "already defined" in error.problem


# ---------------------------------------------------------------------------
# S3 -- unknown column
# ---------------------------------------------------------------------------


def test_s3_unknown_column_is_rejected():
    error = only_error("NORMALIZE students.salary;")
    assert error.phase == ErrorPhase.SCHEMA
    assert "does not exist" in error.problem


def test_s3_suggests_a_similar_column_name():
    error = only_error("NORMALIZE students.mark;")
    assert "marks" in error.suggestions


def test_s3_lists_the_available_columns():
    error = only_error("NORMALIZE students.salary;")
    assert "name" in error.expected and "age" in error.expected


# ---------------------------------------------------------------------------
# S4 -- use after removal
# ---------------------------------------------------------------------------


def test_s4_column_removed_by_drop_cannot_be_used():
    error = only_error("DROP students [ age ];\nNORMALIZE students.age;")
    assert "no longer available" in error.problem


def test_s4_column_excluded_by_select_cannot_be_used():
    error = only_error("SELECT students [ name ];\nNORMALIZE students.age;")
    assert "no longer available" in error.problem
    assert "removed at line" in error.found


# ---------------------------------------------------------------------------
# S5 -- NORMALIZE requires a numeric column
# ---------------------------------------------------------------------------


def test_s5_normalize_on_a_string_column_is_a_type_error():
    error = only_error("NORMALIZE students.name;")
    assert error.phase == ErrorPhase.TYPE
    assert error.problem == "NORMALIZE requires a numerical column."
    assert "STRING" in error.found


def test_s5_suggests_the_numeric_columns():
    error = only_error("NORMALIZE students.name;")
    assert list(error.suggestions) == ["age", "marks", "attendance"]


def test_s5_normalize_on_a_boolean_column_is_rejected():
    assert "numerical" in only_error("NORMALIZE students.passed;").problem


def test_s5_dirty_data_makes_a_numeric_looking_column_fail():
    # age contains "abc" in students_dirty.csv, so it is STRING
    error = only_error("NORMALIZE students.age;", dataset="students_dirty.csv")
    assert "numerical" in error.problem


# ---------------------------------------------------------------------------
# S6 / S7 -- FILTER type rules
# ---------------------------------------------------------------------------


def test_s6_comparing_a_number_with_a_string_is_rejected():
    error = only_error('FILTER students WHERE marks > "good";')
    assert error.problem == "Cannot compare INTEGER with STRING."


def test_s6_comparing_a_string_column_with_a_number_is_rejected():
    assert "Cannot compare" in only_error("FILTER students WHERE department == 5;").problem


def test_s7_ordering_a_string_column_is_rejected():
    error = only_error('FILTER students WHERE department > "CSE";')
    assert "requires numerical operands" in error.problem


def test_s7_equality_on_a_string_column_is_allowed():
    result = analyse('FILTER students WHERE department == "CSE"; SAVE students AS "o.csv";')
    assert result.reporter.ok


def test_float_column_compared_with_an_integer_is_allowed():
    result = analyse('FILTER students WHERE attendance > 80; SAVE students AS "o.csv";')
    assert result.reporter.ok


# ---------------------------------------------------------------------------
# S8 -- FILL_NULL assignability
# ---------------------------------------------------------------------------


def test_s8_filling_a_float_column_with_text_is_rejected():
    error = only_error('FILL_NULL students.attendance WITH "unknown";')
    assert error.problem == "Cannot fill a FLOAT column with a STRING value."


def test_s8_filling_a_string_column_with_a_number_is_rejected():
    assert "Cannot fill" in only_error("FILL_NULL students.department WITH 0;").problem


def test_s8_filling_a_string_column_with_text_is_allowed():
    result = analyse('FILL_NULL students.department WITH "UNKNOWN"; SAVE students AS "o.csv";')
    assert result.reporter.ok


# ---------------------------------------------------------------------------
# S9 -- range contracts
# ---------------------------------------------------------------------------


def test_s9_range_contract_on_a_text_column_is_rejected():
    error = only_error("EXPECT students.name BETWEEN 0 AND 100;")
    assert "BETWEEN contract requires a numerical column" in error.problem


def test_s9_empty_range_is_rejected():
    assert "Empty range" in only_error("EXPECT students.marks BETWEEN 100 AND 0;").problem


def test_type_contract_is_decided_statically_from_the_schema():
    error = only_error("EXPECT students.age NUMBER;", dataset="students_dirty.csv")
    assert "Contract violated" in error.problem
    assert "STRING" in error.found


# ---------------------------------------------------------------------------
# S10 -- SAVE
# ---------------------------------------------------------------------------


def test_s10_saving_a_dataset_with_no_columns_left_is_rejected():
    result = analyse(
        "DROP students [ name, age, marks, attendance, department, passed ];\n"
        'SAVE students AS "o.csv";'
    )
    assert any("every column" in e.problem or "no columns left" in e.problem
               for e in result.reporter.errors)


# ---------------------------------------------------------------------------
# Warnings and error recovery
# ---------------------------------------------------------------------------


def test_a_dataset_that_is_never_saved_produces_a_warning():
    result = analyse("NORMALIZE students.age;")
    assert any("never saved" in w.message for w in result.reporter.warnings)


def test_a_pointless_fill_null_produces_a_warning():
    result = analyse('DROP_NULL students;\nFILL_NULL students.age WITH 0;\nSAVE students AS "o.csv";')
    assert any("no effect" in w.message for w in result.reporter.warnings)


def test_several_semantic_errors_are_reported_from_one_run():
    result = analyse(
        "NORMALIZE students.name;\n"
        "NORMALIZE students.salary;\n"
        'FILTER students WHERE marks > "x";'
    )
    assert len(result.reporter.errors) == 3


def test_a_missing_csv_file_is_reported_once_without_cascading():
    result = compile_demo(
        'LOAD "no_such_file.csv" AS d;\nNORMALIZE d.age;\nSAVE d AS "o.csv";',
        validate_data=False,
    )
    # only the SCHEMA error: binding the name anyway prevents a cascade of
    # "undefined dataset" errors from every later statement
    assert len(result.reporter.errors) == 1
    assert result.reporter.errors[0].phase == ErrorPhase.SCHEMA


def test_semantic_facts_are_recorded_for_every_statement():
    result = analyse('NORMALIZE students.age;\nSAVE students AS "o.csv";')
    assert len(result.analysis.facts) == 3        # LOAD + NORMALIZE + SAVE
    text = result.analysis.format_facts()
    assert "Column datatype" in text and "Operation allowed" in text
