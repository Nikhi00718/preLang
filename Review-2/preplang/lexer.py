"""
lexer.py
========

Compiler Phase 1 -- Lexical Analysis.

A hand-written (table free) scanner for PrepLang.  It reads the source text one
character at a time and groups characters into tokens, tracking line and column
numbers so that later phases can produce located error messages.

Why hand-written?
-----------------
The laboratory project is about *understanding* the phases.  A hand-written
scanner makes the DFA explicit: every ``_scan_*`` method below corresponds to
one accepting path of the automaton, and the dispatch in :meth:`Lexer.tokenize`
is the automaton's start state.

Error handling
--------------
The lexer never stops at the first bad character.  It records a
:class:`~preplang.errors.LexicalError`, skips the offending character and keeps
scanning (panic-mode recovery), so one compilation can report every lexical
problem in the file.
"""

from __future__ import annotations

from .errors import ErrorReporter, LexicalError
from .tokens import (
    BOOLEAN_LITERALS,
    KEYWORDS,
    PUNCTUATION,
    Token,
    TokenType,
)

#: Characters that may start an identifier or keyword.
_ID_START = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_")
#: Characters that may continue an identifier or keyword.
_ID_CONTINUE = _ID_START | set("0123456789")
_DIGITS = set("0123456789")
_WHITESPACE = set(" \t\r\n")


class Lexer:
    """Convert PrepLang source text into a list of :class:`Token` objects."""

    def __init__(self, source: str, reporter: ErrorReporter | None = None) -> None:
        self.source = source
        self.reporter = reporter if reporter is not None else ErrorReporter(source)
        self.reporter.set_source(source)

        self._pos = 0  # index of the next character to read
        self._line = 1
        self._column = 1
        self.tokens: list[Token] = []

    # ------------------------------------------------------------------
    # Character level helpers
    # ------------------------------------------------------------------
    @property
    def _at_end(self) -> bool:
        return self._pos >= len(self.source)

    def _peek(self, offset: int = 0) -> str:
        index = self._pos + offset
        if index >= len(self.source):
            return ""
        return self.source[index]

    def _advance(self) -> str:
        """Consume and return one character, maintaining line/column counters."""
        char = self.source[self._pos]
        self._pos += 1
        if char == "\n":
            self._line += 1
            self._column = 1
        else:
            self._column += 1
        return char

    def _match(self, expected: str) -> bool:
        """Consume the next character only if it equals *expected*."""
        if self._peek() == expected:
            self._advance()
            return True
        return False

    def _add(self, type_: TokenType, value: object, lexeme: str, line: int, column: int) -> None:
        self.tokens.append(Token(type=type_, value=value, lexeme=lexeme, line=line, column=column))

    # ------------------------------------------------------------------
    # Main loop -- the start state of the automaton
    # ------------------------------------------------------------------
    def tokenize(self) -> list[Token]:
        """Scan the whole source and return the token stream (ending in EOF)."""
        while not self._at_end:
            char = self._peek()

            # 1. whitespace is a separator and carries no token
            if char in _WHITESPACE:
                self._advance()
                continue

            # 2. comments: '#' or '--' run to the end of the line
            if char == "#" or (char == "-" and self._peek(1) == "-"):
                self._skip_comment()
                continue

            line, column = self._line, self._column

            # 3. identifiers, keywords and boolean literals
            if char in _ID_START:
                self._scan_word(line, column)
                continue

            # 4. numeric literals
            if char in _DIGITS:
                self._scan_number(line, column)
                continue

            # 5. string literals
            if char in ('"', "'"):
                self._scan_string(line, column)
                continue

            # 6. relational operators
            if char in "<>=!":
                self._scan_operator(line, column)
                continue

            # 7. single character punctuation
            if char in PUNCTUATION:
                lexeme = self._advance()
                self._add(PUNCTUATION[lexeme], lexeme, lexeme, line, column)
                continue

            # 8. anything else is a lexical error -- recover and continue
            bad = self._advance()
            self.reporter.report(
                LexicalError(
                    problem=f"Unknown character {bad!r} in source program.",
                    line=line,
                    column=column,
                    found=repr(bad),
                    hint="PrepLang identifiers may only use letters, digits and '_'.",
                )
            )

        self._add(TokenType.EOF, None, "<eof>", self._line, self._column)
        return self.tokens

    # ------------------------------------------------------------------
    # Scanning routines (accepting states)
    # ------------------------------------------------------------------
    def _skip_comment(self) -> None:
        while not self._at_end and self._peek() != "\n":
            self._advance()

    def _scan_word(self, line: int, column: int) -> None:
        """Scan an identifier, a reserved keyword, or a boolean literal."""
        start = self._pos
        while not self._at_end and self._peek() in _ID_CONTINUE:
            self._advance()
        lexeme = self.source[start:self._pos]
        upper = lexeme.upper()

        if upper in BOOLEAN_LITERALS:
            self._add(TokenType.BOOLEAN_LITERAL, upper == "TRUE", lexeme, line, column)
        elif upper in KEYWORDS:
            # canonicalise keyword spelling so the parser can compare uppercase
            self._add(TokenType.KEYWORD, upper, lexeme, line, column)
        else:
            self._add(TokenType.IDENTIFIER, lexeme, lexeme, line, column)

    def _scan_number(self, line: int, column: int) -> None:
        """Scan an integer or floating point literal.

        A '.' is only treated as a decimal point when a digit follows it; this
        keeps ``students.age`` (identifier DOT identifier) unambiguous.
        """
        start = self._pos
        while not self._at_end and self._peek() in _DIGITS:
            self._advance()

        is_float = False
        if self._peek() == "." and self._peek(1) in _DIGITS:
            is_float = True
            self._advance()  # consume '.'
            while not self._at_end and self._peek() in _DIGITS:
                self._advance()

        lexeme = self.source[start:self._pos]

        # A letter immediately after a number (e.g. 12abc) is a malformed literal.
        if not self._at_end and self._peek() in _ID_START:
            bad_start = self._pos
            while not self._at_end and self._peek() in _ID_CONTINUE:
                self._advance()
            bad_lexeme = self.source[start:self._pos]
            self.reporter.report(
                LexicalError(
                    problem=f"Malformed numeric literal {bad_lexeme!r}.",
                    line=line,
                    column=column,
                    expected="a number such as 42 or 3.14",
                    found=repr(bad_lexeme),
                    hint="A numeric literal cannot be followed directly by letters.",
                )
            )
            del bad_start
            return

        value: object = float(lexeme) if is_float else int(lexeme)
        self._add(TokenType.NUMBER_LITERAL, value, lexeme, line, column)

    def _scan_string(self, line: int, column: int) -> None:
        """Scan a quoted string literal, supporting ``\\`` escapes."""
        quote = self._advance()  # opening quote
        chars: list[str] = []
        while not self._at_end and self._peek() != quote:
            char = self._advance()
            if char == "\n":
                self.reporter.report(
                    LexicalError(
                        problem="Unterminated string literal (newline reached).",
                        line=line,
                        column=column,
                        expected=f"a closing {quote} on the same line",
                        hint="String literals in PrepLang cannot span multiple lines.",
                    )
                )
                return
            if char == "\\" and not self._at_end:
                escape = self._advance()
                chars.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'"}.get(escape, escape))
            else:
                chars.append(char)

        if self._at_end:
            self.reporter.report(
                LexicalError(
                    problem="Unterminated string literal (end of file reached).",
                    line=line,
                    column=column,
                    expected=f"a closing {quote}",
                )
            )
            return

        self._advance()  # closing quote
        text = "".join(chars)
        self._add(TokenType.STRING_LITERAL, text, f"{quote}{text}{quote}", line, column)

    def _scan_operator(self, line: int, column: int) -> None:
        """Scan a relational operator: > < >= <= == !=."""
        first = self._advance()
        lexeme = first
        if self._match("="):
            lexeme = first + "="

        if lexeme in (">", "<", ">=", "<=", "==", "!="):
            self._add(TokenType.RELATIONAL_OPERATOR, lexeme, lexeme, line, column)
            return

        # '=' alone and '!' alone are not operators in PrepLang
        expected = "one of >  <  >=  <=  ==  !="
        hint = "Use '==' for equality comparison." if lexeme == "=" else None
        self.reporter.report(
            LexicalError(
                problem=f"Invalid operator {lexeme!r}.",
                line=line,
                column=column,
                expected=expected,
                found=repr(lexeme),
                hint=hint,
            )
        )


# ---------------------------------------------------------------------------
# Convenience helpers
# ---------------------------------------------------------------------------


def tokenize(source: str, reporter: ErrorReporter | None = None) -> tuple[list[Token], ErrorReporter]:
    """Tokenize *source* and return ``(tokens, reporter)``."""
    lexer = Lexer(source, reporter)
    return lexer.tokenize(), lexer.reporter


def format_token_table(tokens: list[Token], show_eof: bool = True) -> str:
    """Render a token stream as an aligned table (used by the CLI and IDE)."""
    rows = [t for t in tokens if show_eof or t.type is not TokenType.EOF]
    header = f"{'LINE:COL':<10} {'LEXEME':<20} {'TOKEN TYPE':<20} VALUE"
    lines = [header, "-" * len(header)]
    for token in rows:
        value = "" if token.value is None else repr(token.value)
        lines.append(f"{token.position:<10} {token.lexeme:<20} {str(token.type):<20} {value}")
    lines.append("-" * len(header))
    lines.append(f"{len(rows)} tokens")
    return "\n".join(lines)
