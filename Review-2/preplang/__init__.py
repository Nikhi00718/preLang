"""
PrepLang -- a schema-aware domain-specific language compiler for safe and
optimized data preprocessing.

Review 2 package (Phase 2: Core Implementation).  The complete compiler
front end:

    tokens.py             token categories and reserved words
    errors.py             structured, phase-tagged diagnostics
    lexer.py              PHASE 1  lexical analysis
    ast_nodes.py          Abstract Syntax Tree definitions
    parser.py             PHASE 2  syntax analysis, builds the AST
    schema.py             the type system and the CSV schema analyser
    symbol_table.py       datasets, columns, types and liveness
    semantic_analyzer.py  PHASE 3  semantic analysis and static type checking
    contracts.py          data-contract compilation and validation
    ir.py                 the intermediate representation
    ir_generator.py       PHASE 4  intermediate code generation
    pipeline.py           drives all phases and collects every artefact

The back end (optimiser, Python/Pandas code generator, execution engine and
the visual IDE) is delivered in Review 3.
"""

from .ast_nodes import ProgramNode, format_ast  # noqa: F401
from .contracts import ContractReport, validate_contracts  # noqa: F401
from .errors import (  # noqa: F401
    CompilerError,
    CompilerWarning,
    ContractError,
    ErrorReporter,
    LexicalError,
    SchemaError,
    SemanticError,
    SyntaxError_,
    TypeError_,
)
from .ir import IRInstruction, IROp, IRProgram  # noqa: F401
from .ir_generator import generate_ir  # noqa: F401
from .lexer import Lexer, format_token_table, tokenize  # noqa: F401
from .parser import Parser, parse  # noqa: F401
from .pipeline import CompilationResult, compile_file, compile_source  # noqa: F401
from .schema import DataType, DatasetSchema, infer_schema  # noqa: F401
from .semantic_analyzer import SemanticAnalyzer, analyse  # noqa: F401
from .symbol_table import SymbolTable  # noqa: F401
from .tokens import KEYWORDS, Token, TokenType  # noqa: F401

__version__ = "0.2.0"
