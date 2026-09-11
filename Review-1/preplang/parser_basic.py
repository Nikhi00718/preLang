"""
parser_basic.py
===============

Compiler Phase 2 (prototype) -- Syntax Analysis / recognition.

This is the **Review 1** parser.  It is a predictive, recursive-descent
*recogniser*: it checks that the token stream produced by the lexer conforms to
the PrepLang grammar and reports precise syntax errors, but it does not yet
build the full Abstract Syntax Tree (that is Review 2 work).  Instead it
records a compact :class:`StatementInfo` summary per statement, which is enough
to prove the grammar is being enforced and to drive the demo.

Grammar implemented (see docs/03_grammar.md for the complete BNF):

    program        -> statement* EOF
    statement      -> load_stmt | drop_null_stmt | fill_null_stmt
                    | normalize_stmt | filter_stmt | select_stmt
                    | drop_stmt | save_stmt | expect_stmt

Because PrepLang statements all begin with a distinct keyword, the grammar is
LL(1): a single token of lookahead selects the production, so no backtracking
is ever required.

Error recovery
--------------
On a syntax error the parser enters panic mode: it records the diagnostic and
skips tokens until it passes a ``;`` (statement terminator) or reaches a token
that can legally start a new statement.  Parsing then resumes, so a single run
can report several independent syntax errors.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .errors import ErrorReporter, SyntaxError_
from .tokens import Token, TokenType

#: Keywords that may begin a statement -- the synchronising set for recovery.
STATEMENT_KEYWORDS = (
    "LOAD",
    "DROP_NULL",
    "FILL_NULL",
    "NORMALIZE",
    "FILTER",
    "SELECT",
    "DROP",
    "SAVE",
    "EXPECT",
)


@dataclass
class StatementInfo:
    """A validated statement, summarised for the Review 1 prototype."""

    kind: str
    line: int
    details: dict[str, object] = field(default_factory=dict)

    def __str__(self) -> str:
        parts = ", ".join(f"{k}={v!r}" for k, v in self.details.items())
        return f"line {self.line:>3}  {self.kind:<12} {parts}"


class BasicParser:
    """Recursive-descent recogniser for the PrepLang grammar."""

    def __init__(self, tokens: list[Token], reporter: ErrorReporter | None = None) -> None:
        self.tokens = tokens
        self.reporter = reporter if reporter is not None else ErrorReporter()
        self._pos = 0
        self.statements: list[StatementInfo] = []

    # ------------------------------------------------------------------
    # Token stream helpers
    # ------------------------------------------------------------------
    @property
    def current(self) -> Token:
        return self.tokens[self._pos]

    def _advance(self) -> Token:
        token = self.tokens[self._pos]
        if token.type is not TokenType.EOF:
            self._pos += 1
        return token

    def _check(self, type_: TokenType, value: object | None = None) -> bool:
        token = self.current
        if token.type is not type_:
            return False
        return value is None or token.value == value

    def _check_keyword(self, *keywords: str) -> bool:
        return self.current.type is TokenType.KEYWORD and self.current.value in keywords

    # ------------------------------------------------------------------
    # Error production
    # ------------------------------------------------------------------
    def _describe(self, token: Token) -> str:
        if token.type is TokenType.EOF:
            return "end of file"
        return f"{token.lexeme!r} ({token.type})"

    def _error(self, expected: str, hint: str | None = None) -> SyntaxError_:
        token = self.current
        return self.reporter.report(
            SyntaxError_(
                problem=f"Unexpected {self._describe(token)}.",
                line=token.line,
                column=token.column,
                expected=expected,
                found=self._describe(token),
                hint=hint,
            )
        )

    def _expect(self, type_: TokenType, description: str, value: object | None = None) -> Token:
        """Consume a token of the required kind or raise a located syntax error."""
        if self._check(type_, value):
            return self._advance()
        raise self._error(description)

    def _expect_keyword(self, keyword: str) -> Token:
        if self._check_keyword(keyword):
            return self._advance()
        raise self._error(f"keyword {keyword}")

    def _synchronise(self) -> None:
        """Panic-mode recovery: skip to the end of the offending statement."""
        while self.current.type is not TokenType.EOF:
            if self.current.type is TokenType.SEMICOLON:
                self._advance()
                return
            if self._check_keyword(*STATEMENT_KEYWORDS):
                return
            self._advance()

    # ------------------------------------------------------------------
    # program -> statement* EOF
    # ------------------------------------------------------------------
    def parse(self) -> list[StatementInfo]:
        while self.current.type is not TokenType.EOF:
            try:
                self.statements.append(self._statement())
            except SyntaxError_:
                self._synchronise()
        return self.statements

    def _statement(self) -> StatementInfo:
        if not self._check_keyword(*STATEMENT_KEYWORDS):
            raise self._error(
                "a statement keyword: " + ", ".join(STATEMENT_KEYWORDS),
                hint="Every PrepLang statement begins with a keyword and ends with ';'.",
            )

        keyword = self.current.value
        dispatch = {
            "LOAD": self._load_stmt,
            "DROP_NULL": self._drop_null_stmt,
            "FILL_NULL": self._fill_null_stmt,
            "NORMALIZE": self._normalize_stmt,
            "FILTER": self._filter_stmt,
            "SELECT": self._select_stmt,
            "DROP": self._drop_stmt,
            "SAVE": self._save_stmt,
            "EXPECT": self._expect_stmt,
        }
        return dispatch[str(keyword)]()

    # ------------------------------------------------------------------
    # Individual productions
    # ------------------------------------------------------------------
    def _load_stmt(self) -> StatementInfo:
        """load_stmt -> LOAD STRING AS IDENTIFIER ';'"""
        line = self._expect_keyword("LOAD").line
        path = self._expect(TokenType.STRING_LITERAL, 'a quoted file path, e.g. "students.csv"')
        self._expect_keyword("AS")
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._terminator()
        return StatementInfo("LOAD", line, {"file": path.value, "dataset": name.value})

    def _drop_null_stmt(self) -> StatementInfo:
        """drop_null_stmt -> DROP_NULL IDENTIFIER ';'"""
        line = self._expect_keyword("DROP_NULL").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._terminator()
        return StatementInfo("DROP_NULL", line, {"dataset": name.value})

    def _fill_null_stmt(self) -> StatementInfo:
        """fill_null_stmt -> FILL_NULL column_ref WITH literal ';'"""
        line = self._expect_keyword("FILL_NULL").line
        dataset, column = self._column_ref()
        self._expect_keyword("WITH")
        value = self._literal()
        self._terminator()
        return StatementInfo(
            "FILL_NULL", line, {"dataset": dataset, "column": column, "value": value}
        )

    def _normalize_stmt(self) -> StatementInfo:
        """normalize_stmt -> NORMALIZE column_ref ';'"""
        line = self._expect_keyword("NORMALIZE").line
        dataset, column = self._column_ref()
        self._terminator()
        return StatementInfo("NORMALIZE", line, {"dataset": dataset, "column": column})

    def _filter_stmt(self) -> StatementInfo:
        """filter_stmt -> FILTER IDENTIFIER WHERE condition ';'"""
        line = self._expect_keyword("FILTER").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._expect_keyword("WHERE")
        column = self._expect(TokenType.IDENTIFIER, "a column name")
        operator = self._expect(TokenType.RELATIONAL_OPERATOR, "a relational operator (>, <, >=, <=, ==, !=)")
        value = self._literal()
        self._terminator()
        return StatementInfo(
            "FILTER",
            line,
            {"dataset": name.value, "column": column.value, "op": operator.value, "value": value},
        )

    def _select_stmt(self) -> StatementInfo:
        """select_stmt -> SELECT IDENTIFIER '[' identifier_list ']' ';'"""
        line = self._expect_keyword("SELECT").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        columns = self._bracketed_identifier_list()
        self._terminator()
        return StatementInfo("SELECT", line, {"dataset": name.value, "columns": columns})

    def _drop_stmt(self) -> StatementInfo:
        """drop_stmt -> DROP IDENTIFIER '[' identifier_list ']' ';'"""
        line = self._expect_keyword("DROP").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        columns = self._bracketed_identifier_list()
        self._terminator()
        return StatementInfo("DROP", line, {"dataset": name.value, "columns": columns})

    def _save_stmt(self) -> StatementInfo:
        """save_stmt -> SAVE IDENTIFIER AS STRING ';'"""
        line = self._expect_keyword("SAVE").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._expect_keyword("AS")
        path = self._expect(TokenType.STRING_LITERAL, 'a quoted output path, e.g. "clean.csv"')
        self._terminator()
        return StatementInfo("SAVE", line, {"dataset": name.value, "file": path.value})

    def _expect_stmt(self) -> StatementInfo:
        """expect_stmt -> EXPECT column_ref expectation ';'"""
        line = self._expect_keyword("EXPECT").line
        dataset, column = self._column_ref()
        details: dict[str, object] = {"dataset": dataset, "column": column}

        if self._check_keyword("NUMBER", "STRING", "BOOLEAN"):
            details["contract"] = "TYPE"
            details["type"] = self._advance().value
        elif self._check_keyword("NOT_NULL"):
            self._advance()
            details["contract"] = "NOT_NULL"
        elif self._check_keyword("BETWEEN"):
            self._advance()
            low = self._expect(TokenType.NUMBER_LITERAL, "a numeric lower bound")
            self._expect_keyword("AND")
            high = self._expect(TokenType.NUMBER_LITERAL, "a numeric upper bound")
            details["contract"] = "RANGE"
            details["low"] = low.value
            details["high"] = high.value
        else:
            raise self._error(
                "an expectation: NUMBER | STRING | BOOLEAN | NOT_NULL | BETWEEN n AND m"
            )

        self._terminator()
        return StatementInfo("EXPECT", line, details)

    # ------------------------------------------------------------------
    # Shared sub-productions
    # ------------------------------------------------------------------
    def _column_ref(self) -> tuple[str, str]:
        """column_ref -> IDENTIFIER '.' IDENTIFIER"""
        dataset = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._expect(TokenType.DOT, "'.' between the dataset and the column name")
        column = self._expect(TokenType.IDENTIFIER, "a column name")
        return str(dataset.value), str(column.value)

    def _bracketed_identifier_list(self) -> list[str]:
        """'[' IDENTIFIER (',' IDENTIFIER)* ']'"""
        self._expect(TokenType.LEFT_BRACKET, "'[' before the column list")
        columns: list[str] = []
        if self._check(TokenType.RIGHT_BRACKET):
            raise self._error("at least one column name", hint="An empty column list is not allowed.")
        while True:
            column = self._expect(TokenType.IDENTIFIER, "a column name")
            columns.append(str(column.value))
            if self._check(TokenType.COMMA):
                self._advance()
                continue
            break
        self._expect(TokenType.RIGHT_BRACKET, "']' after the column list")
        return columns

    def _literal(self) -> object:
        """literal -> NUMBER | STRING | BOOLEAN"""
        if self.current.type in (
            TokenType.NUMBER_LITERAL,
            TokenType.STRING_LITERAL,
            TokenType.BOOLEAN_LITERAL,
        ):
            return self._advance().value
        raise self._error("a literal value (number, quoted string, TRUE or FALSE)")

    def _terminator(self) -> None:
        if self._check(TokenType.SEMICOLON):
            self._advance()
            return
        raise self._error("';' at the end of the statement")


# ---------------------------------------------------------------------------
# Convenience helper
# ---------------------------------------------------------------------------


def parse(tokens: list[Token], reporter: ErrorReporter | None = None) -> tuple[list[StatementInfo], ErrorReporter]:
    """Validate *tokens* and return ``(statements, reporter)``."""
    parser = BasicParser(tokens, reporter)
    return parser.parse(), parser.reporter


def format_statement_table(statements: list[StatementInfo]) -> str:
    """Render the recognised statements as a readable table."""
    if not statements:
        return "(no statements recognised)"
    header = f"{'#':<4}{'LINE':<7}{'STATEMENT':<14}DETAILS"
    lines = [header, "-" * max(len(header), 60)]
    for index, statement in enumerate(statements, start=1):
        details = ", ".join(f"{k}={v!r}" for k, v in statement.details.items())
        lines.append(f"{index:<4}{statement.line:<7}{statement.kind:<14}{details}")
    lines.append("-" * max(len(header), 60))
    lines.append(f"{len(statements)} statement(s) recognised")
    return "\n".join(lines)
