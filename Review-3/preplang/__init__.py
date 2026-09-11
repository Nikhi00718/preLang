"""
PrepLang -- a schema-aware domain-specific language compiler for safe and
optimized data preprocessing.

Review 3 package (Phase 3: Final Implementation and Testing).  The complete
compiler, front end and back end:

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
    optimizer.py          PHASE 5  live-column analysis and IR optimisation
    code_generator.py     PHASE 6  Python/Pandas target code generation
    executor.py                    runs the generated program
    pipeline.py           drives all phases and collects every artefact

The visual IDE lives in ``ui.py`` at the top of the Review-3 folder.
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
from .code_generator import GeneratedCode, generate_code  # noqa: F401
from .executor import ExecutionResult, execute  # noqa: F401
from .optimizer import OptimizationReport, analyse_liveness, optimize  # noqa: F401
from .pipeline import (  # noqa: F401
    CompilationResult,
    compile_file,
    compile_source,
    generate_unoptimised,
)
from .schema import DataType, DatasetSchema, infer_schema  # noqa: F401
from .semantic_analyzer import SemanticAnalyzer, analyse  # noqa: F401
from .symbol_table import SymbolTable  # noqa: F401
from .tokens import KEYWORDS, Token, TokenType  # noqa: F401

__version__ = "1.0.0"
