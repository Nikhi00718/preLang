"""
Unit tests for Compiler Phase 1 -- Lexical Analysis.

Run with:   pytest -q        (from the Review-1 directory)
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from preplang.lexer import Lexer, tokenize  # noqa: E402
from preplang.tokens import TokenType  # noqa: E402


def types_of(source: str) -> list[TokenType]:
    tokens, _ = tokenize(source)
    return [t.type for t in tokens if t.type is not TokenType.EOF]


def values_of(source: str) -> list[object]:
    tokens, _ = tokenize(source)
    return [t.value for t in tokens if t.type is not TokenType.EOF]


# ---------------------------------------------------------------------------
# Valid token recognition
# ---------------------------------------------------------------------------


def test_column_reference_is_three_tokens():
    assert types_of("students.age") == [
        TokenType.IDENTIFIER,
        TokenType.DOT,
        TokenType.IDENTIFIER,
    ]


def test_normalize_statement_token_stream():
    assert types_of("NORMALIZE students.age;") == [
        TokenType.KEYWORD,
        TokenType.IDENTIFIER,
        TokenType.DOT,
        TokenType.IDENTIFIER,
        TokenType.SEMICOLON,
    ]


def test_filter_statement_token_stream():
    assert types_of("FILTER students WHERE marks >= 50;") == [
        TokenType.KEYWORD,
        TokenType.IDENTIFIER,
        TokenType.KEYWORD,
        TokenType.IDENTIFIER,
        TokenType.RELATIONAL_OPERATOR,
        TokenType.NUMBER_LITERAL,
        TokenType.SEMICOLON,
    ]


def test_keywords_are_case_insensitive_and_canonicalised():
    tokens, reporter = tokenize("load 'a.csv' as d;")
    assert reporter.ok
    assert tokens[0].type is TokenType.KEYWORD
    assert tokens[0].value == "LOAD"
    assert tokens[0].lexeme == "load"  # original spelling preserved


def test_identifier_is_not_a_keyword():
    tokens, _ = tokenize("students")
    assert tokens[0].type is TokenType.IDENTIFIER


def test_string_literal_value_excludes_quotes():
    assert values_of('"students.csv"') == ["students.csv"]


def test_single_quoted_string_is_accepted():
    assert values_of("'clean.csv'") == ["clean.csv"]


def test_integer_and_float_literals():
    assert values_of("42 3.14") == [42, 3.14]
    tokens, _ = tokenize("42 3.14")
    assert isinstance(tokens[0].value, int)
    assert isinstance(tokens[1].value, float)


def test_boolean_literals():
    tokens, _ = tokenize("TRUE false")
    assert [t.type for t in tokens[:2]] == [TokenType.BOOLEAN_LITERAL] * 2
    assert [t.value for t in tokens[:2]] == [True, False]


def test_all_relational_operators():
    tokens, reporter = tokenize("> < >= <= == !=")
    assert reporter.ok
    assert [t.value for t in tokens if t.type is TokenType.RELATIONAL_OPERATOR] == [
        ">",
        "<",
        ">=",
        "<=",
        "==",
        "!=",
    ]


def test_comments_are_skipped():
    tokens, reporter = tokenize("# a comment\nDROP_NULL d;  -- trailing comment")
    assert reporter.ok
    assert [t.type for t in tokens if t.type is not TokenType.EOF] == [
        TokenType.KEYWORD,
        TokenType.IDENTIFIER,
        TokenType.SEMICOLON,
    ]


def test_line_and_column_tracking():
    tokens, _ = tokenize("LOAD\n  'a.csv'")
    assert (tokens[0].line, tokens[0].column) == (1, 1)
    assert (tokens[1].line, tokens[1].column) == (2, 3)


def test_token_stream_always_ends_with_eof():
    tokens, _ = tokenize("DROP_NULL d;")
    assert tokens[-1].type is TokenType.EOF


def test_empty_source_produces_only_eof():
    tokens, reporter = tokenize("")
    assert reporter.ok
    assert len(tokens) == 1 and tokens[0].type is TokenType.EOF


# ---------------------------------------------------------------------------
# Lexical error detection and recovery
# ---------------------------------------------------------------------------


def test_unknown_character_is_a_lexical_error():
    _, reporter = tokenize("NORMALIZE data$age;")
    assert not reporter.ok
    assert "Unknown character" in reporter.errors[0].problem
    assert reporter.errors[0].line == 1


def test_lexer_recovers_and_reports_every_bad_character():
    _, reporter = tokenize("a $ b @ c;")
    assert len(reporter.errors) == 2  # both '$' and '@' reported


def test_single_equals_is_rejected_with_a_hint():
    _, reporter = tokenize("FILTER d WHERE x = 1;")
    assert not reporter.ok
    assert "==" in (reporter.errors[0].hint or "")


def test_unterminated_string_is_reported():
    _, reporter = tokenize('SAVE d AS "clean.csv;\n')
    assert not reporter.ok
    assert "Unterminated string" in reporter.errors[0].problem


def test_malformed_number_is_reported():
    _, reporter = tokenize("FILTER d WHERE x > 12abc;")
    assert not reporter.ok
    assert "Malformed numeric literal" in reporter.errors[0].problem


def test_error_carries_the_offending_source_line():
    _, reporter = tokenize("LOAD 'a.csv' AS d;\nNORMALIZE d$x;")
    assert reporter.errors[0].source_line.strip() == "NORMALIZE d$x;"


# ---------------------------------------------------------------------------
# Lexer object API
# ---------------------------------------------------------------------------


def test_lexer_class_and_helper_agree():
    source = "SELECT d [ a, b ];"
    assert [t.type for t in Lexer(source).tokenize()] == [
        t.type for t in tokenize(source)[0]
    ]
