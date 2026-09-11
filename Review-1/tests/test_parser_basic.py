"""
Unit tests for Compiler Phase 2 (prototype) -- Syntax Analysis.

Run with:   pytest -q        (from the Review-1 directory)
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from preplang.errors import ErrorReporter  # noqa: E402
from preplang.lexer import Lexer  # noqa: E402
from preplang.parser_basic import BasicParser  # noqa: E402

EXAMPLES = Path(__file__).resolve().parents[1] / "examples"


def compile_source(source: str):
    """Lex + parse *source*; return ``(statements, reporter)``."""
    reporter = ErrorReporter(source)
    tokens = Lexer(source, reporter).tokenize()
    statements = BasicParser(tokens, reporter).parse()
    return statements, reporter


def kinds(source: str) -> list[str]:
    statements, _ = compile_source(source)
    return [s.kind for s in statements]


# ---------------------------------------------------------------------------
# Valid programs -- one test per grammar production
# ---------------------------------------------------------------------------


def test_load_statement():
    statements, reporter = compile_source('LOAD "students.csv" AS students;')
    assert reporter.ok
    assert statements[0].kind == "LOAD"
    assert statements[0].details == {"file": "students.csv", "dataset": "students"}


def test_drop_null_statement():
    statements, reporter = compile_source("DROP_NULL students;")
    assert reporter.ok and statements[0].details["dataset"] == "students"


def test_fill_null_statement():
    statements, reporter = compile_source("FILL_NULL students.salary WITH 0;")
    assert reporter.ok
    assert statements[0].details == {
        "dataset": "students",
        "column": "salary",
        "value": 0,
    }


def test_normalize_statement():
    statements, reporter = compile_source("NORMALIZE students.age;")
    assert reporter.ok
    assert statements[0].details == {"dataset": "students", "column": "age"}


def test_filter_statement():
    statements, reporter = compile_source("FILTER students WHERE marks >= 50;")
    assert reporter.ok
    assert statements[0].details["op"] == ">="
    assert statements[0].details["value"] == 50


def test_select_statement_with_multiline_list():
    statements, reporter = compile_source("SELECT students [\n name,\n age,\n marks\n];")
    assert reporter.ok
    assert statements[0].details["columns"] == ["name", "age", "marks"]


def test_drop_statement():
    statements, reporter = compile_source("DROP students [ phone, address ];")
    assert reporter.ok
    assert statements[0].details["columns"] == ["phone", "address"]


def test_save_statement():
    statements, reporter = compile_source('SAVE students AS "cleaned.csv";')
    assert reporter.ok
    assert statements[0].details["file"] == "cleaned.csv"


def test_expect_type_contract():
    statements, reporter = compile_source("EXPECT students.age NUMBER;")
    assert reporter.ok
    assert statements[0].details["contract"] == "TYPE"
    assert statements[0].details["type"] == "NUMBER"


def test_expect_range_contract():
    statements, reporter = compile_source("EXPECT students.marks BETWEEN 0 AND 100;")
    assert reporter.ok
    assert (statements[0].details["low"], statements[0].details["high"]) == (0, 100)


def test_expect_not_null_contract():
    statements, reporter = compile_source("EXPECT students.name NOT_NULL;")
    assert reporter.ok and statements[0].details["contract"] == "NOT_NULL"


def test_multiple_statements_keep_source_order():
    source = """
    LOAD "s.csv" AS s;
    DROP_NULL s;
    NORMALIZE s.age;
    SAVE s AS "o.csv";
    """
    assert kinds(source) == ["LOAD", "DROP_NULL", "NORMALIZE", "SAVE"]


def test_statement_line_numbers_are_recorded():
    statements, _ = compile_source('LOAD "s.csv" AS s;\n\nDROP_NULL s;')
    assert [s.line for s in statements] == [1, 3]


# ---------------------------------------------------------------------------
# Syntax error detection
# ---------------------------------------------------------------------------


def test_missing_semicolon_is_reported():
    _, reporter = compile_source('LOAD "s.csv" AS s')
    assert not reporter.ok
    assert "';'" in reporter.errors[0].expected


def test_missing_as_keyword_is_reported():
    _, reporter = compile_source('LOAD "s.csv" s;')
    assert not reporter.ok
    assert reporter.errors[0].expected == "keyword AS"


def test_column_reference_requires_a_dot():
    _, reporter = compile_source("NORMALIZE students age;")
    assert not reporter.ok
    assert "'.'" in reporter.errors[0].expected


def test_operands_in_wrong_order_are_rejected():
    _, reporter = compile_source("NORMALIZE . age students;")
    assert not reporter.ok


def test_empty_column_list_is_rejected():
    _, reporter = compile_source("SELECT students [];")
    assert not reporter.ok
    assert "at least one column" in reporter.errors[0].expected


def test_unknown_statement_keyword_is_rejected():
    _, reporter = compile_source("SHUFFLE students;")
    assert not reporter.ok
    assert "statement keyword" in reporter.errors[0].expected


def test_invalid_expectation_is_rejected():
    _, reporter = compile_source("EXPECT students.age POSITIVE;")
    assert not reporter.ok


def test_errors_record_line_and_column():
    _, reporter = compile_source('LOAD "s.csv" AS s;\nNORMALIZE students age;')
    assert reporter.errors[0].line == 2
    assert reporter.errors[0].column is not None


# ---------------------------------------------------------------------------
# Panic-mode error recovery
# ---------------------------------------------------------------------------


def test_parser_recovers_and_finds_later_errors():
    source = 'LOAD "s.csv" s;\nNORMALIZE students age;\nSELECT s [];'
    _, reporter = compile_source(source)
    assert len(reporter.errors) == 3  # every statement reported independently


def test_valid_statements_after_an_error_are_still_recognised():
    source = 'LOAD "s.csv" s;\nDROP_NULL students;\nNORMALIZE students.age;'
    statements, reporter = compile_source(source)
    assert [s.kind for s in statements] == ["DROP_NULL", "NORMALIZE"]
    assert len(reporter.errors) == 1


# ---------------------------------------------------------------------------
# Bundled example programs
# ---------------------------------------------------------------------------


def test_bundled_valid_examples_compile_cleanly():
    for name in ("demo_valid.prep", "demo_all_statements.prep"):
        source = (EXAMPLES / name).read_text(encoding="utf-8")
        statements, reporter = compile_source(source)
        assert reporter.ok, f"{name}: {reporter.summary()}"
        assert statements


def test_bundled_error_examples_are_rejected():
    for name in ("demo_lexical_error.prep", "demo_syntax_error.prep"):
        source = (EXAMPLES / name).read_text(encoding="utf-8")
        _, reporter = compile_source(source)
        assert not reporter.ok, f"{name} should not compile"
