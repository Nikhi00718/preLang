"""
Unit tests for Compiler Phase 5 -- live-column analysis and IR optimisation.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from conftest import compile_demo, load  # noqa: E402
from preplang.ir import IROp  # noqa: E402


def run(body: str, dataset: str = "students.csv", optimise: bool = True):
    return compile_demo(load(dataset) + body, optimise=optimise)


def ops(ir) -> list[IROp]:
    return [i.op for i in ir]


def load_instruction(ir):
    return [i for i in ir if i.op is IROp.LOAD][0]


# ---------------------------------------------------------------------------
# Pass 1 -- redundant-operation elimination
# ---------------------------------------------------------------------------


def test_two_consecutive_drop_nulls_collapse_into_one():
    result = run('DROP_NULL students;\nDROP_NULL students;\nSAVE students AS "o.csv";')
    assert ops(result.ir).count(IROp.DROP_NULL) == 2
    assert ops(result.optimised_ir).count(IROp.DROP_NULL) == 1


def test_three_consecutive_drop_nulls_collapse_into_one():
    body = "DROP_NULL students;\n" * 3 + 'SAVE students AS "o.csv";'
    result = run(body)
    assert ops(result.optimised_ir).count(IROp.DROP_NULL) == 1


def test_a_contract_between_two_operations_does_not_block_the_pass():
    result = run(
        "DROP_NULL students;\n"
        "EXPECT students.age NUMBER;\n"
        "DROP_NULL students;\n"
        'SAVE students AS "o.csv";'
    )
    assert ops(result.optimised_ir).count(IROp.DROP_NULL) == 1


def test_a_real_operation_between_two_drop_nulls_blocks_the_pass():
    result = run(
        "DROP_NULL students;\n"
        "FILTER students WHERE marks >= 50;\n"
        "DROP_NULL students;\n"
        'SAVE students AS "o.csv";'
    )
    # the FILTER changes the dataset, so the second DROP_NULL is not redundant
    assert ops(result.optimised_ir).count(IROp.DROP_NULL) == 2


def test_normalising_the_same_column_twice_collapses():
    result = run('NORMALIZE students.age;\nNORMALIZE students.age;\nSAVE students AS "o.csv";')
    assert ops(result.optimised_ir).count(IROp.NORMALIZE) == 1


def test_normalising_two_different_columns_is_not_redundant():
    result = run('NORMALIZE students.age;\nNORMALIZE students.marks;\nSAVE students AS "o.csv";')
    assert ops(result.optimised_ir).count(IROp.NORMALIZE) == 2


def test_the_report_names_what_was_removed():
    result = run('DROP_NULL students;\nDROP_NULL students;\nSAVE students AS "o.csv";')
    pass_one = result.optimisation.passes[0]
    assert "IR_DROP_NULL" in pass_one.removed[0]
    assert "already applied at line" in pass_one.removed[0]


# ---------------------------------------------------------------------------
# Pass 2 -- dead-operation elimination
# ---------------------------------------------------------------------------


def test_normalising_a_column_that_is_later_dropped_is_dead():
    result = run(
        "NORMALIZE students.attendance;\n"
        "DROP students [ attendance ];\n"
        'SAVE students AS "o.csv";'
    )
    assert IROp.NORMALIZE in ops(result.ir)
    assert IROp.NORMALIZE not in ops(result.optimised_ir)


def test_dead_operation_elimination_warns():
    result = run(
        "NORMALIZE students.attendance;\n"
        "DROP students [ attendance ];\n"
        'SAVE students AS "o.csv";'
    )
    assert any("has no effect" in w.message for w in result.reporter.warnings)


def test_a_column_excluded_by_select_makes_an_earlier_write_dead():
    result = run(
        "FILL_NULL students.attendance WITH 0;\n"
        "SELECT students [ name, age ];\n"
        'SAVE students AS "o.csv";'
    )
    assert IROp.FILL_NULL not in ops(result.optimised_ir)


def test_a_normalise_whose_column_is_saved_is_not_dead():
    result = run('NORMALIZE students.age;\nSAVE students AS "o.csv";')
    assert IROp.NORMALIZE in ops(result.optimised_ir)


def test_a_column_used_only_by_a_filter_keeps_the_filter_alive():
    result = run(
        "FILTER students WHERE marks >= 50;\n"
        "SELECT students [ name, age ];\n"
        'SAVE students AS "o.csv";'
    )
    assert IROp.FILTER in ops(result.optimised_ir)


# ---------------------------------------------------------------------------
# Pass 3 -- column pruning
# ---------------------------------------------------------------------------


def test_only_the_required_columns_are_loaded():
    result = run('SELECT students [ name, age ];\nSAVE students AS "o.csv";')
    assert load_instruction(result.optimised_ir).meta["usecols"] == ["name", "age"]


def test_a_column_used_only_by_a_filter_is_still_loaded():
    result = run(
        "FILTER students WHERE marks >= 50;\n"
        "SELECT students [ name, age ];\n"
        'SAVE students AS "o.csv";'
    )
    assert load_instruction(result.optimised_ir).meta["usecols"] == ["name", "age", "marks"]


def test_a_column_used_only_by_a_contract_is_still_loaded():
    result = run(
        "EXPECT students.attendance BETWEEN 0 AND 100;\n"
        "SELECT students [ name ];\n"
        'SAVE students AS "o.csv";'
    )
    assert "attendance" in load_instruction(result.optimised_ir).meta["usecols"]


def test_usecols_follows_the_file_column_order():
    result = run('SELECT students [ marks, name ];\nSAVE students AS "o.csv";')
    # the file order is name, age, marks, ... -- read_csv requires file order
    assert load_instruction(result.optimised_ir).meta["usecols"] == ["name", "marks"]


def test_pruning_on_the_wide_dataset():
    result = compile_demo(
        'LOAD "employees.csv" AS emp;\n'
        "SELECT emp [ name, age, salary ];\n"
        'SAVE emp AS "o.csv";'
    )
    report = result.optimisation
    assert report.columns_available == 30
    assert report.columns_required == 3
    assert len(report.pruned_columns) == 27
    assert report.column_reduction == 90.0


def test_a_program_that_needs_everything_prunes_nothing():
    result = run('SAVE students AS "o.csv";')
    assert result.optimisation.pruned_columns == []


# ---------------------------------------------------------------------------
# The DROP_NULL subset -- what makes pruning sound
# ---------------------------------------------------------------------------


def test_drop_null_records_the_live_column_set():
    result = run(
        "DROP_NULL students;\n"
        "SELECT students [ name, age ];\n"
        'SAVE students AS "o.csv";'
    )
    instruction = [i for i in result.optimised_ir if i.op is IROp.DROP_NULL][0]
    assert instruction.meta["subset"] == ["name", "age"]


def test_the_same_subset_is_recorded_without_optimisation():
    body = 'DROP_NULL students;\nSELECT students [ name, age ];\nSAVE students AS "o.csv";'
    optimised = run(body, optimise=True)
    plain = run(body, optimise=False)
    subset_of = lambda ir: [i for i in ir if i.op is IROp.DROP_NULL][0].meta["subset"]  # noqa: E731
    assert subset_of(optimised.optimised_ir) == subset_of(plain.optimised_ir)


# ---------------------------------------------------------------------------
# The report and the driver
# ---------------------------------------------------------------------------


def test_the_original_ir_is_not_modified_by_the_optimiser():
    result = run('DROP_NULL students;\nDROP_NULL students;\nSAVE students AS "o.csv";')
    assert len(result.ir) == 4          # LOAD + 2 DROP_NULL + SAVE
    assert len(result.optimised_ir) == 3


def test_optimisation_can_be_disabled():
    result = run('DROP_NULL students;\nDROP_NULL students;\nSAVE students AS "o.csv";', optimise=False)
    assert len(result.optimised_ir) == 4
    assert result.optimisation is None


def test_the_report_renders_the_headline_numbers():
    result = compile_demo(
        'LOAD "employees.csv" AS emp;\nSELECT emp [ name ];\nSAVE emp AS "o.csv";'
    )
    text = result.optimisation.format()
    assert "OPTIMIZATION REPORT" in text
    assert "Columns pruned" in text
    assert "29" in text                  # 30 available, 1 required


def test_the_optimiser_reaches_a_fixed_point():
    result = run('DROP_NULL students;\nDROP_NULL students;\nSAVE students AS "o.csv";')
    assert 1 <= result.optimisation.rounds <= 5
