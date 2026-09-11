"""
Unit tests for Compiler Phase 6 (target code generation) and for the
execution engine -- including the end-to-end behaviour of the whole compiler.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pytest  # noqa: E402

from conftest import EXAMPLES, compile_demo, load  # noqa: E402
from preplang.pipeline import generate_unoptimised  # noqa: E402

pd = pytest.importorskip("pandas", reason="Pandas is needed to execute generated code")


def build(body: str, dataset: str = "students.csv", **kwargs):
    return compile_demo(load(dataset) + body, **kwargs)


def code_of(body: str, **kwargs) -> str:
    result = build(body, **kwargs)
    assert result.code is not None, result.reporter.summary()
    return result.code.source


# ---------------------------------------------------------------------------
# Emitted code, statement by statement
# ---------------------------------------------------------------------------


def test_prelude_imports_pandas():
    assert "import pandas as pd" in code_of('SAVE students AS "o.csv";')


def test_load_emits_read_csv_with_pruned_columns():
    source = code_of('SELECT students [ name, age ];\nSAVE students AS "o.csv";')
    assert "pd.read_csv(" in source
    assert "usecols=['name', 'age']" in source


def test_load_without_pruning_reads_every_column():
    source = code_of('SAVE students AS "o.csv";')
    assert "usecols" in source  # all six columns are required, but still listed
    assert "'department'" in source


def test_drop_null_emits_a_subset():
    source = code_of('DROP_NULL students;\nSELECT students [ name, age ];\nSAVE students AS "o.csv";')
    assert "dropna(subset=['name', 'age'])" in source


def test_fill_null_emits_fillna():
    source = code_of('FILL_NULL students.attendance WITH 0;\nSAVE students AS "o.csv";')
    assert "students['attendance'].fillna(0)" in source


def test_normalize_emits_the_z_score_formula():
    source = code_of('NORMALIZE students.age;\nSAVE students AS "o.csv";')
    assert "students['age'].mean()" in source
    assert "students['age'].std()" in source


def test_filter_emits_a_boolean_mask():
    source = code_of('FILTER students WHERE marks >= 50;\nSAVE students AS "o.csv";')
    assert "students[students['marks'] >= 50]" in source


def test_select_emits_a_column_projection():
    # with the optimiser off, the SELECT is emitted verbatim
    source = code_of('SELECT students [ name, age ];\nSAVE students AS "o.csv";', optimise=False)
    assert "students[['name', 'age']]" in source


def test_select_that_reorders_columns_survives_optimisation():
    # pruning loads name and marks in file order, so the SELECT is still
    # needed -- it fixes the *output* order
    source = code_of('SELECT students [ marks, name ];\nSAVE students AS "o.csv";')
    assert "students[['marks', 'name']]" in source


def test_a_select_made_redundant_by_pruning_is_removed():
    # after pruning only name and age are loaded, so the SELECT is a no-op
    source = code_of('SELECT students [ name, age ];\nSAVE students AS "o.csv";')
    assert "students[['name', 'age']]" not in source
    assert "usecols=['name', 'age']" in source


def test_drop_emits_drop_columns():
    source = code_of('DROP students [ passed ];\nSAVE students AS "o.csv";', optimise=False)
    assert "drop(columns=['passed'])" in source


def test_a_drop_subsumed_by_pruning_is_removed():
    # 'passed' is never loaded, so dropping it would fail at run time
    source = code_of('DROP students [ passed ];\nSAVE students AS "o.csv";')
    assert "drop(columns=" not in source


def test_save_emits_to_csv():
    source = code_of('SAVE students AS "clean.csv";')
    assert "to_csv('clean.csv', index=False)" in source


def test_contracts_are_compiled_into_validation_calls():
    source = code_of(
        "EXPECT students.age NUMBER;"
        "EXPECT students.attendance BETWEEN 0 AND 100;"
        "EXPECT students.department NOT_NULL;"
        'SAVE students AS "o.csv";'
    )
    assert "_expect_type(students, 'students', 'age', 'NUMBER'" in source
    assert "_expect_range(students, 'students', 'attendance', 0, 100" in source
    assert "_expect_not_null(students, 'students', 'department'" in source


def test_every_emitted_block_cites_its_source_line():
    source = code_of('NORMALIZE students.age;\nSAVE students AS "o.csv";')
    assert "# line 2: IR_NORMALIZE age" in source


def test_generated_code_is_valid_python():
    compile(code_of('NORMALIZE students.age;\nSAVE students AS "o.csv";'), "<gen>", "exec")


def test_no_code_is_generated_for_a_rejected_program():
    assert build("NORMALIZE students.name;").code is None


# ---------------------------------------------------------------------------
# Execution
# ---------------------------------------------------------------------------


def test_the_full_pipeline_runs_and_writes_a_file(tmp_path):
    result = build(
        """
        FILL_NULL students.attendance WITH 0;
        DROP_NULL students;
        NORMALIZE students.age;
        FILTER students WHERE marks >= 50;
        SELECT students [ name, age, marks ];
        SAVE students AS "test_output.csv";
        """,
        run=True,
    )
    assert result.execution.ok, result.execution.format()
    output = EXAMPLES / "test_output.csv"
    try:
        assert output.exists()
        frame = pd.read_csv(output)
        assert list(frame.columns) == ["name", "age", "marks"]
        assert (frame["marks"] >= 50).all()
        assert abs(frame["age"].mean()) < 1e-9      # z-scores are centred on 0
    finally:
        output.unlink(missing_ok=True)


def test_execution_reports_the_resulting_shape():
    result = build('SELECT students [ name, age ];\nSAVE students AS "t2.csv";', run=True)
    try:
        assert result.execution.frames["students"] == (12, 2)
    finally:
        (EXAMPLES / "t2.csv").unlink(missing_ok=True)


def test_contract_messages_reach_standard_output():
    result = build('EXPECT students.age NUMBER;\nSAVE students AS "t3.csv";', run=True)
    try:
        assert "[PASS]" in result.execution.stdout
    finally:
        (EXAMPLES / "t3.csv").unlink(missing_ok=True)


def test_a_missing_file_is_reported_as_a_runtime_error():
    from preplang.executor import execute

    result = execute("import pandas as pd\npd.read_csv('definitely_missing.csv')\n", EXAMPLES)
    assert not result.ok
    assert result.error is not None
    assert result.error.phase == "RUNTIME ERROR"
    assert "could not open a file" in result.error.problem


def test_any_other_runtime_failure_is_reported_as_a_runtime_error():
    from preplang.executor import execute

    result = execute("raise ValueError('boom')\n", EXAMPLES)
    assert not result.ok
    assert "raised ValueError" in result.error.problem
    assert "runtime failure" in (result.error.hint or "")


# ---------------------------------------------------------------------------
# The optimisation must not change the answer
# ---------------------------------------------------------------------------


PIPELINE = """
DROP_NULL students;
DROP_NULL students;
NORMALIZE students.age;
FILTER students WHERE marks >= 50;
SELECT students [ name, age, marks ];
SAVE students AS "{output}";
"""


def test_optimised_and_unoptimised_pipelines_produce_identical_output():
    from preplang.executor import execute

    optimised = build(PIPELINE.format(output="opt_out.csv"))
    baseline = generate_unoptimised(build(PIPELINE.format(output="base_out.csv")))

    execute(optimised.code.source, EXAMPLES)
    execute(baseline.source, EXAMPLES)

    opt_file = EXAMPLES / "opt_out.csv"
    base_file = EXAMPLES / "base_out.csv"
    try:
        assert opt_file.read_bytes() == base_file.read_bytes()
    finally:
        opt_file.unlink(missing_ok=True)
        base_file.unlink(missing_ok=True)


def test_the_optimised_pipeline_really_reads_fewer_columns():
    optimised = build(PIPELINE.format(output="x.csv"))
    baseline = generate_unoptimised(build(PIPELINE.format(output="x.csv")))
    assert "usecols" in optimised.code.source
    assert "usecols" not in baseline.source


# ---------------------------------------------------------------------------
# End-to-end: every bundled example
# ---------------------------------------------------------------------------


def test_every_valid_example_compiles_and_runs():
    outputs = ["cleaned_students.csv", "clean_employees.csv", "clean.csv"]
    for name in ("demo_valid.prep", "demo_all_statements.prep", "demo_wide_dataset.prep"):
        result = compile_demo((EXAMPLES / name).read_text(encoding="utf-8"), run=True)
        assert result.ok, f"{name}: {result.summary()}"
        assert result.execution.ok, f"{name}: {result.execution.format()}"
    for name in outputs:
        (EXAMPLES / name).unlink(missing_ok=True)


def test_every_broken_example_is_still_rejected():
    for name in (
        "demo_lexical_error.prep",
        "demo_syntax_error.prep",
        "demo_type_error.prep",
        "demo_dropped_column.prep",
        "demo_dirty_schema.prep",
        "demo_contract_violation.prep",
    ):
        result = compile_demo((EXAMPLES / name).read_text(encoding="utf-8"))
        assert not result.ok, name
