"""
parser.py
=========

Compiler Phase 2 -- Syntax Analysis.

A predictive LL(1) recursive-descent parser that turns the token stream into
an Abstract Syntax Tree. It is the Review 2 successor of the Review 1
recogniser: the control flow is identical (one method per non-terminal, one
token of lookahead, panic-mode recovery) but each method now *returns an AST
node* instead of a summary record. This is syntax-directed translation: the
tree is built as a side effect of recognising the input.

Grammar: see docs/grammar.md. The parser methods appear in the same order as
the productions, and each carries its production in its docstring so that the
grammar and the code can be checked against each other line by line.
"""

from __future__ import annotations

from .ast_nodes import (
    ConditionNode,
    DropNode,
    DropNullNode,
    ExpectNotNullNode,
    ExpectRangeNode,
    ExpectTypeNode,
    FillNullNode,
    FilterNode,
    LiteralNode,
    LoadNode,
    NormalizeNode,
    ProgramNode,
    SaveNode,
    SelectNode,
    StatementNode,
)
from .errors import ErrorReporter, SyntaxError_
from .schema import DataType
from .tokens import Token, TokenType

#: Keywords that may begin a statement -- also the synchronising set.
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


class Parser:
    """Recursive-descent parser producing a :class:`ProgramNode`."""

    def __init__(self, tokens: list[Token], reporter: ErrorReporter | None = None) -> None:
        self.tokens = tokens
        self.reporter = reporter if reporter is not None else ErrorReporter()
        self._pos = 0

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

    def _check(self, type_: TokenType) -> bool:
        return self.current.type is type_

    def _check_keyword(self, *keywords: str) -> bool:
        return self.current.type is TokenType.KEYWORD and self.current.value in keywords

    # ------------------------------------------------------------------
    # Diagnostics
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

    def _expect(self, type_: TokenType, description: str) -> Token:
        if self._check(type_):
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
    def parse(self) -> ProgramNode:
        statements: list[StatementNode] = []
        first_line = self.current.line if self.tokens else 1
        while self.current.type is not TokenType.EOF:
            try:
                statements.append(self._statement())
            except SyntaxError_:
                self._synchronise()
        return ProgramNode(line=first_line, statements=statements)

    def _statement(self) -> StatementNode:
        if not self._check_keyword(*STATEMENT_KEYWORDS):
            raise self._error(
                "a statement keyword: " + ", ".join(STATEMENT_KEYWORDS),
                hint="Every PrepLang statement begins with a keyword and ends with ';'.",
            )
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
        return dispatch[str(self.current.value)]()

    # ------------------------------------------------------------------
    # Statement productions
    # ------------------------------------------------------------------
    def _load_stmt(self) -> LoadNode:
        """load_stmt -> LOAD STRING AS IDENTIFIER ';'"""
        line = self._expect_keyword("LOAD").line
        path = self._expect(TokenType.STRING_LITERAL, 'a quoted file path, e.g. "students.csv"')
        self._expect_keyword("AS")
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._terminator()
        return LoadNode(line=line, path=str(path.value), dataset=str(name.value))

    def _drop_null_stmt(self) -> DropNullNode:
        """drop_null_stmt -> DROP_NULL IDENTIFIER ';'"""
        line = self._expect_keyword("DROP_NULL").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._terminator()
        return DropNullNode(line=line, dataset=str(name.value))

    def _fill_null_stmt(self) -> FillNullNode:
        """fill_null_stmt -> FILL_NULL column_ref WITH literal ';'"""
        line = self._expect_keyword("FILL_NULL").line
        dataset, column = self._column_ref()
        self._expect_keyword("WITH")
        value = self._literal()
        self._terminator()
        return FillNullNode(line=line, dataset=dataset, column=column, value=value)

    def _normalize_stmt(self) -> NormalizeNode:
        """normalize_stmt -> NORMALIZE column_ref ';'"""
        line = self._expect_keyword("NORMALIZE").line
        dataset, column = self._column_ref()
        self._terminator()
        return NormalizeNode(line=line, dataset=dataset, column=column)

    def _filter_stmt(self) -> FilterNode:
        """filter_stmt -> FILTER IDENTIFIER WHERE condition ';'"""
        line = self._expect_keyword("FILTER").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._expect_keyword("WHERE")
        condition = self._condition()
        self._terminator()
        return FilterNode(line=line, dataset=str(name.value), condition=condition)

    def _select_stmt(self) -> SelectNode:
        """select_stmt -> SELECT IDENTIFIER '[' identifier_list ']' ';'"""
        line = self._expect_keyword("SELECT").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        columns = self._bracketed_identifier_list()
        self._terminator()
        return SelectNode(line=line, dataset=str(name.value), columns=columns)

    def _drop_stmt(self) -> DropNode:
        """drop_stmt -> DROP IDENTIFIER '[' identifier_list ']' ';'"""
        line = self._expect_keyword("DROP").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        columns = self._bracketed_identifier_list()
        self._terminator()
        return DropNode(line=line, dataset=str(name.value), columns=columns)

    def _save_stmt(self) -> SaveNode:
        """save_stmt -> SAVE IDENTIFIER AS STRING ';'"""
        line = self._expect_keyword("SAVE").line
        name = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._expect_keyword("AS")
        path = self._expect(TokenType.STRING_LITERAL, 'a quoted output path, e.g. "clean.csv"')
        self._terminator()
        return SaveNode(line=line, dataset=str(name.value), path=str(path.value))

    def _expect_stmt(self) -> StatementNode:
        """expect_stmt -> EXPECT column_ref expectation ';'"""
        line = self._expect_keyword("EXPECT").line
        dataset, column = self._column_ref()

        if self._check_keyword("NUMBER", "STRING", "BOOLEAN"):
            expected = str(self._advance().value)
            self._terminator()
            return ExpectTypeNode(line=line, dataset=dataset, column=column, expected=expected)

        if self._check_keyword("NOT_NULL"):
            self._advance()
            self._terminator()
            return ExpectNotNullNode(line=line, dataset=dataset, column=column)

        if self._check_keyword("BETWEEN"):
            self._advance()
            low = self._expect(TokenType.NUMBER_LITERAL, "a numeric lower bound")
            self._expect_keyword("AND")
            high = self._expect(TokenType.NUMBER_LITERAL, "a numeric upper bound")
            self._terminator()
            return ExpectRangeNode(
                line=line,
                dataset=dataset,
                column=column,
                low=float(low.value),  # type: ignore[arg-type]
                high=float(high.value),  # type: ignore[arg-type]
            )

        raise self._error(
            "an expectation: NUMBER | STRING | BOOLEAN | NOT_NULL | BETWEEN n AND m"
        )

    # ------------------------------------------------------------------
    # Shared sub-productions
    # ------------------------------------------------------------------
    def _column_ref(self) -> tuple[str, str]:
        """column_ref -> IDENTIFIER '.' IDENTIFIER"""
        dataset = self._expect(TokenType.IDENTIFIER, "a dataset name")
        self._expect(TokenType.DOT, "'.' between the dataset and the column name")
        column = self._expect(TokenType.IDENTIFIER, "a column name")
        return str(dataset.value), str(column.value)

    def _condition(self) -> ConditionNode:
        """condition -> IDENTIFIER RELOP literal"""
        column = self._expect(TokenType.IDENTIFIER, "a column name")
        operator = self._expect(
            TokenType.RELATIONAL_OPERATOR, "a relational operator (>, <, >=, <=, ==, !=)"
        )
        value = self._literal()
        return ConditionNode(
            line=column.line,
            column=str(column.value),
            operator=str(operator.value),
            value=value,
        )

    def _bracketed_identifier_list(self) -> list[str]:
        """'[' IDENTIFIER (',' IDENTIFIER)* ']'"""
        self._expect(TokenType.LEFT_BRACKET, "'[' before the column list")
        if self._check(TokenType.RIGHT_BRACKET):
            raise self._error("at least one column name", hint="An empty column list is not allowed.")
        columns: list[str] = []
        while True:
            columns.append(str(self._expect(TokenType.IDENTIFIER, "a column name").value))
            if self._check(TokenType.COMMA):
                self._advance()
                continue
            break
        self._expect(TokenType.RIGHT_BRACKET, "']' after the column list")
        return columns

    def _literal(self) -> LiteralNode:
        """literal -> NUMBER | STRING | BOOLEAN"""
        token = self.current
        if token.type in (
            TokenType.NUMBER_LITERAL,
            TokenType.STRING_LITERAL,
            TokenType.BOOLEAN_LITERAL,
        ):
            self._advance()
            return LiteralNode(
                line=token.line, value=token.value, type=DataType.of_python(token.value)
            )
        raise self._error("a literal value (number, quoted string, TRUE or FALSE)")

    def _terminator(self) -> None:
        if self._check(TokenType.SEMICOLON):
            self._advance()
            return
        raise self._error("';' at the end of the statement")


def parse(tokens: list[Token], reporter: ErrorReporter | None = None) -> tuple[ProgramNode, ErrorReporter]:
    """Parse *tokens* and return ``(program, reporter)``."""
    parser = Parser(tokens, reporter)
    return parser.parse(), parser.reporter
