"""
Unit tests for Compiler Phase 2 -- Syntax Analysis and AST construction.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from preplang.ast_nodes import (  # noqa: E402
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
    format_ast,
    walk,
)
from preplang.errors import ErrorReporter  # noqa: E402
from preplang.lexer import Lexer  # noqa: E402
from preplang.parser import Parser  # noqa: E402
from preplang.schema import DataType  # noqa: E402

EXAMPLES = Path(__file__).resolve().parents[1] / "examples"


def parse(source: str):
    reporter = ErrorReporter(source)
    tokens = Lexer(source, reporter).tokenize()
    return Parser(tokens, reporter).parse(), reporter


def first(source: str):
    program, reporter = parse(source)
    assert reporter.ok, reporter.summary()
    return program.statements[0]


# ---------------------------------------------------------------------------
# One test per grammar production: the right node, with the right fields
# ---------------------------------------------------------------------------


def test_load_builds_a_load_node():
    node = first('LOAD "students.csv" AS students;')
    assert isinstance(node, LoadNode)
    assert (node.path, node.dataset, node.line) == ("students.csv", "students", 1)


def test_drop_null_node():
    node = first("DROP_NULL students;")
    assert isinstance(node, DropNullNode) and node.dataset == "students"


def test_fill_null_node_carries_a_typed_literal():
    node = first("FILL_NULL students.salary WITH 0;")
    assert isinstance(node, FillNullNode)
    assert (node.dataset, node.column) == ("students", "salary")
    assert node.value.value == 0 and node.value.type is DataType.INTEGER


def test_fill_null_with_a_string_literal():
    node = first('FILL_NULL students.department WITH "UNKNOWN";')
    assert node.value.type is DataType.STRING


def test_normalize_node():
    node = first("NORMALIZE students.age;")
    assert isinstance(node, NormalizeNode)
    assert (node.dataset, node.column) == ("students", "age")


def test_filter_node_has_a_condition_subtree():
    node = first("FILTER students WHERE marks >= 50;")
    assert isinstance(node, FilterNode)
    assert node.condition.column == "marks"
    assert node.condition.operator == ">="
    assert node.condition.value.value == 50


def test_select_node_keeps_column_order():
    node = first("SELECT students [ name, age, marks ];")
    assert isinstance(node, SelectNode)
    assert node.columns == ["name", "age", "marks"]


def test_drop_node():
    node = first("DROP students [ phone, address ];")
    assert isinstance(node, DropNode) and node.columns == ["phone", "address"]


def test_save_node():
    node = first('SAVE students AS "cleaned.csv";')
    assert isinstance(node, SaveNode) and node.path == "cleaned.csv"


def test_expect_type_node():
    node = first("EXPECT students.age NUMBER;")
    assert isinstance(node, ExpectTypeNode) and node.expected == "NUMBER"


def test_expect_range_node():
    node = first("EXPECT students.marks BETWEEN 0 AND 100;")
    assert isinstance(node, ExpectRangeNode)
    assert (node.low, node.high) == (0.0, 100.0)


def test_expect_not_null_node():
    node = first("EXPECT students.name NOT_NULL;")
    assert isinstance(node, ExpectNotNullNode) and node.column == "name"


# ---------------------------------------------------------------------------
# Tree shape
# ---------------------------------------------------------------------------


def test_program_root_holds_statements_in_source_order():
    program, reporter = parse(
        'LOAD "s.csv" AS s;\nDROP_NULL s;\nNORMALIZE s.age;\nSAVE s AS "o.csv";'
    )
    assert reporter.ok
    assert isinstance(program, ProgramNode)
    assert [type(n).__name__ for n in program.statements] == [
        "LoadNode",
        "DropNullNode",
        "NormalizeNode",
        "SaveNode",
    ]


def test_every_node_records_its_source_line():
    program, _ = parse('LOAD "s.csv" AS s;\n\n\nNORMALIZE s.age;')
    assert [s.line for s in program.statements] == [1, 4]


def test_walk_visits_nested_nodes():
    program, _ = parse("FILTER students WHERE marks >= 50;")
    kinds = [type(n).__name__ for n in walk(program)]
    assert kinds == ["ProgramNode", "FilterNode", "ConditionNode", "LiteralNode"]


def test_format_ast_renders_a_tree():
    program, _ = parse("FILTER students WHERE marks >= 50;")
    text = format_ast(program)
    assert "Program(1 statements)" in text
    assert "Filter(students)" in text
    assert "Condition(marks >= ...)" in text
    assert "Literal(50 : INTEGER)" in text


# ---------------------------------------------------------------------------
# Error handling (behaviour inherited from the Review 1 recogniser)
# ---------------------------------------------------------------------------


def test_missing_semicolon_is_reported():
    _, reporter = parse('LOAD "s.csv" AS s')
    assert not reporter.ok and "';'" in reporter.errors[0].expected


def test_missing_as_keyword_is_reported():
    _, reporter = parse('LOAD "s.csv" s;')
    assert reporter.errors[0].expected == "keyword AS"


def test_empty_column_list_is_rejected():
    _, reporter = parse("SELECT students [];")
    assert "at least one column" in reporter.errors[0].expected


def test_unknown_statement_keyword_is_rejected():
    _, reporter = parse("SHUFFLE students;")
    assert "statement keyword" in reporter.errors[0].expected


def test_panic_mode_reports_every_broken_statement():
    program, reporter = parse(
        'LOAD "s.csv" s;\nNORMALIZE students age;\nSELECT s [];'
    )
    assert len(reporter.errors) == 3
    assert program.statements == []


def test_valid_statements_after_an_error_still_produce_nodes():
    program, reporter = parse('LOAD "s.csv" s;\nDROP_NULL students;\nNORMALIZE students.age;')
    assert len(reporter.errors) == 1
    assert [type(n).__name__ for n in program.statements] == ["DropNullNode", "NormalizeNode"]


# ---------------------------------------------------------------------------
# Bundled examples
# ---------------------------------------------------------------------------


def test_every_non_error_example_parses():
    for path in sorted(EXAMPLES.glob("*.prep")):
        if "lexical_error" in path.name or "syntax_error" in path.name:
            continue
        program, reporter = parse(path.read_text(encoding="utf-8"))
        assert reporter.ok, f"{path.name}: {reporter.summary()}"
        assert program.statements, path.name
