"""
tokens.py
=========

Token definitions for the PrepLang compiler.

This module is the single source of truth for:
    * the set of token categories produced by the lexer,
    * the reserved keywords of the language,
    * the Token record itself.

Compiler phase: Lexical Analysis (support module).
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class TokenType(Enum):
    """Categories of lexemes recognised by the PrepLang lexer."""

    KEYWORD = "KEYWORD"
    IDENTIFIER = "IDENTIFIER"
    NUMBER_LITERAL = "NUMBER_LITERAL"
    STRING_LITERAL = "STRING_LITERAL"
    BOOLEAN_LITERAL = "BOOLEAN_LITERAL"

    DOT = "DOT"
    COMMA = "COMMA"
    SEMICOLON = "SEMICOLON"
    LEFT_BRACKET = "LEFT_BRACKET"
    RIGHT_BRACKET = "RIGHT_BRACKET"

    RELATIONAL_OPERATOR = "RELATIONAL_OPERATOR"

    EOF = "EOF"

    def __str__(self) -> str:  # pragma: no cover - trivial
        return self.value


# ---------------------------------------------------------------------------
# Reserved words
# ---------------------------------------------------------------------------
# PrepLang keywords are written in UPPERCASE by convention.  The lexer accepts
# any casing and canonicalises the token value to uppercase, so `load` and
# `LOAD` produce the same KEYWORD token.  Because they are *reserved*, they may
# not be used as dataset or column identifiers.

KEYWORDS: frozenset[str] = frozenset(
    {
        # statement keywords
        "LOAD",
        "AS",
        "DROP_NULL",
        "FILL_NULL",
        "WITH",
        "NORMALIZE",
        "FILTER",
        "WHERE",
        "SELECT",
        "DROP",
        "SAVE",
        "EXPECT",
        # contract / type keywords
        "NUMBER",
        "STRING",
        "BOOLEAN",
        "BETWEEN",
        "AND",
        "NOT_NULL",
    }
)

#: Literals that look like identifiers but denote boolean values.
BOOLEAN_LITERALS: frozenset[str] = frozenset({"TRUE", "FALSE"})

#: Relational operators usable inside a FILTER condition.
RELATIONAL_OPERATORS: frozenset[str] = frozenset({">", "<", ">=", "<=", "==", "!="})

#: Single characters that map directly onto a punctuation token.
PUNCTUATION: dict[str, TokenType] = {
    ".": TokenType.DOT,
    ",": TokenType.COMMA,
    ";": TokenType.SEMICOLON,
    "[": TokenType.LEFT_BRACKET,
    "]": TokenType.RIGHT_BRACKET,
}


@dataclass(frozen=True)
class Token:
    """A single lexical unit produced by the lexer.

    Attributes
    ----------
    type:
        The token category (see :class:`TokenType`).
    value:
        The *semantic* value of the token.  For keywords this is the
        canonical uppercase spelling, for numbers a python ``int``/``float``,
        for strings the unescaped text, for booleans a ``bool``.
    lexeme:
        The exact text as it appeared in the source program.  Kept so that
        error messages can quote the program verbatim.
    line, column:
        1-based position of the first character of the lexeme.  Used by every
        later phase to produce located diagnostics.
    """

    type: TokenType
    value: object
    lexeme: str
    line: int
    column: int

    def __str__(self) -> str:
        return f"{self.lexeme!r:<18} {self.type}"

    @property
    def position(self) -> str:
        """Human readable ``line:column`` string."""
        return f"{self.line}:{self.column}"


def is_keyword(word: str) -> bool:
    """Return ``True`` when *word* (any casing) is a reserved PrepLang keyword."""
    return word.upper() in KEYWORDS


def is_boolean_literal(word: str) -> bool:
    """Return ``True`` when *word* (any casing) is ``TRUE`` or ``FALSE``."""
    return word.upper() in BOOLEAN_LITERALS
