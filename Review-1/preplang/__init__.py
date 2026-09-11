"""
PrepLang -- a schema-aware domain-specific language compiler for safe and
optimized data preprocessing.

Review 1 package (Phase 1: Problem Definition and Design).
Implemented in this phase:

    * ``tokens``        -- token categories and reserved words
    * ``errors``        -- structured, phase-tagged diagnostics
    * ``lexer``         -- Phase 1 of the compiler: lexical analysis
    * ``parser_basic``  -- Phase 2 prototype: recursive-descent syntax validation

Later phases (AST, symbol table, schema analysis, semantic analysis, IR,
optimisation and Pandas code generation) are delivered in Review 2 and Review 3.
"""

from .errors import (  # noqa: F401
    CompilerError,
    CompilerWarning,
    ErrorReporter,
    LexicalError,
    SyntaxError_,
)
from .lexer import Lexer, format_token_table, tokenize  # noqa: F401
from .parser_basic import (  # noqa: F401
    BasicParser,
    StatementInfo,
    format_statement_table,
    parse,
)
from .tokens import KEYWORDS, Token, TokenType  # noqa: F401

__version__ = "0.1.0"
__all__ = [
    "CompilerError",
    "CompilerWarning",
    "ErrorReporter",
    "LexicalError",
    "SyntaxError_",
    "Lexer",
    "tokenize",
    "format_token_table",
    "BasicParser",
    "StatementInfo",
    "parse",
    "format_statement_table",
    "Token",
    "TokenType",
    "KEYWORDS",
    "__version__",
]
