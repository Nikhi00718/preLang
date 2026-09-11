"""
Unit tests for data-contract validation and for Phase 4 (IR generation).
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from conftest import EXAMPLES, compile_demo, load  # noqa: E402
from preplang.ir import IROp  # noqa: E402


def run(body: str, dataset: str = "students.csv"):
    return compile_demo(load(dataset) + body)


def result_for(report, column: str):
    matches = [r for r in report.results if r.column == column]
    assert matches, f"no contract result for {column}"
    return matches[0]


# ---------------------------------------------------------------------------
# Data contract validation
# ---------------------------------------------------------------------------


def test_a_satisfied_type_contract_passes():
    report = run('EXPECT students.age NUMBER; SAVE students AS "o.csv";').contract_report
    assert result_for(report, "age").satisfied
    assert result_for(report, "age").checked_rows == 12


def test_a_range_violation_is_detected_with_its_row_number():
    report = run('EXPECT students.marks BETWEEN 0 AND 100; SAVE students AS "o.csv";').contract_report
    marks = result_for(report, "marks")
    assert not marks.satisfied
    assert marks.violation_count == 1
    assert marks.violations[0].row == 6        # spreadsheet row, header = row 1
    assert marks.violations[0].value == "150"


def test_a_not_null_violation_is_detected():
    report = run('EXPECT students.name NOT_NULL; SAVE students AS "o.csv";').contract_report
    name = result_for(report, "name")
    assert not name.satisfied and name.violations[0].row == 9


def test_a_satisfied_range_contract_passes():
    report = run('EXPECT students.attendance BETWEEN 0 AND 100; SAVE students AS "o.csv";').contract_report
    assert result_for(report, "attendance").satisfied


def test_missing_values_do_not_violate_a_range_contract():
    # attendance has one empty cell; only NOT_NULL is concerned with that
    report = run('EXPECT students.attendance BETWEEN 0 AND 100; SAVE students AS "o.csv";').contract_report
    assert result_for(report, "attendance").violation_count == 0


def test_several_contracts_share_a_single_pass_over_the_file():
    report = run(
        "EXPECT students.age NUMBER;"
        "EXPECT students.marks BETWEEN 0 AND 100;"
        "EXPECT students.name NOT_NULL;"
        'SAVE students AS "o.csv";'
    ).contract_report
    assert len(report.results) == 3
    assert all(r.checked_rows == 12 for r in report.results)


def test_report_counts_passes_and_failures():
    report = run(
        "EXPECT students.age NUMBER;"
        "EXPECT students.marks BETWEEN 0 AND 100;"
        'SAVE students AS "o.csv";'
    ).contract_report
    assert len(report.passed) == 1 and len(report.failed) == 1
    assert not report.ok


def test_report_renders_violations():
    report = run('EXPECT students.marks BETWEEN 0 AND 100; SAVE students AS "o.csv";').contract_report
    text = report.format()
    assert "[FAIL]" in text and "row 6" in text and "0 <= value <= 100" in text


def test_compilation_is_not_ok_when_a_contract_fails():
    result = run('EXPECT students.name NOT_NULL; SAVE students AS "o.csv";')
    assert result.reporter.ok          # no compiler error ...
    assert not result.ok               # ... but the program is still rejected


# ---------------------------------------------------------------------------
# Phase 4 -- intermediate code generation
# ---------------------------------------------------------------------------


def test_each_statement_lowers_to_one_instruction():
    result = run(
        """
        DROP_NULL students;
        NORMALIZE students.age;
        FILTER students WHERE marks >= 50;
        SELECT students [ name, age, marks ];
        SAVE students AS "clean.csv";
        """
    )
    assert [i.op for i in result.ir] == [
        IROp.LOAD,
        IROp.DROP_NULL,
        IROp.NORMALIZE,
        IROp.FILTER,
        IROp.SELECT,
        IROp.SAVE,
    ]


def test_instructions_keep_their_source_line():
    result = run('DROP_NULL students;\nSAVE students AS "o.csv";')
    assert [i.line for i in result.ir] == [1, 2, 3]


def test_load_instruction_carries_the_file_name():
    result = run('SAVE students AS "o.csv";')
    assert result.ir[0].args == ("students.csv",)


def test_filter_instruction_carries_column_operator_and_value():
    result = run('FILTER students WHERE marks >= 50; SAVE students AS "o.csv";')
    instruction = [i for i in result.ir if i.op is IROp.FILTER][0]
    assert instruction.args == ("marks", ">=", 50)


def test_select_instruction_carries_the_column_list():
    result = run('SELECT students [ name, age ]; SAVE students AS "o.csv";')
    instruction = [i for i in result.ir if i.op is IROp.SELECT][0]
    assert instruction.columns == ["name", "age"]


def test_contracts_are_lowered_to_ir_too():
    result = run(
        "EXPECT students.age NUMBER;"
        "EXPECT students.attendance BETWEEN 0 AND 100;"
        'SAVE students AS "o.csv";'
    )
    ops = [i.op for i in result.ir]
    assert IROp.EXPECT_TYPE in ops and IROp.EXPECT_RANGE in ops


def test_identical_instructions_compare_equal_in_effect():
    result = run('DROP_NULL students;\nDROP_NULL students;\nSAVE students AS "o.csv";')
    first, second = result.ir[1], result.ir[2]
    assert first.same_effect_as(second)      # what the Review 3 optimiser exploits


def test_ir_is_rendered_as_a_table():
    result = run('NORMALIZE students.age; SAVE students AS "o.csv";')
    text = result.ir.format()
    assert "IR_NORMALIZE" in text and "students" in text and "age" in text


def test_no_ir_is_produced_when_compilation_fails():
    result = compile_demo(load() + "NORMALIZE students.name;")
    assert result.ir is None


# ---------------------------------------------------------------------------
# End-to-end behaviour of the bundled examples
# ---------------------------------------------------------------------------


def test_bundled_examples_behave_as_documented():
    expectations = {
        "demo_valid.prep": True,
        "demo_all_statements.prep": True,
        "demo_wide_dataset.prep": True,
        "demo_lexical_error.prep": False,
        "demo_syntax_error.prep": False,
        "demo_type_error.prep": False,
        "demo_dropped_column.prep": False,
        "demo_dirty_schema.prep": False,
        "demo_contract_violation.prep": False,
    }
    for name, should_compile in expectations.items():
        source = (EXAMPLES / name).read_text(encoding="utf-8")
        result = compile_demo(source)
        assert result.ok is should_compile, f"{name}: {result.summary()}"
