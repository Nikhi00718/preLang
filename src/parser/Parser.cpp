#include "parser/Parser.h"

#include <utility>

namespace {

// Thrown after a syntax error has been reported. parse() catches it and
// skips to the next statement (panic-mode recovery).
struct ParseError {};

const std::vector<std::string> statementKeywords = {
    "LOAD", "DROP_NULL", "FILL_NULL", "NORMALIZE", "FILTER", "SELECT", "DROP", "SAVE", "EXPECT"};

// How a token is shown in "found ..." messages.
std::string describe(const Token& token) {
    switch (token.type) {
        case TokenType::END_OF_FILE:    return "end of file";
        case TokenType::STRING_LITERAL: return "\"" + token.lexeme + "\"";
        default:                        return "'" + token.lexeme + "'";
    }
}

}  // namespace

Parser::Parser(std::vector<Token> tokens, Diagnostics& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

// program -> { statement } END_OF_FILE
Program Parser::parse() {
    Program program;
    while (!isAtEnd()) {
        try {
            program.statements.push_back(statement());
        } catch (const ParseError&) {
            synchronize();
        }
    }
    return program;
}

// ---------- Statements ----------

// statement -> load | drop_null | fill_null | normalize | filter
//            | select | drop | save | expect
// Every statement starts with a different keyword, so one token of lookahead
// is enough to choose the rule. That makes the grammar LL(1).
std::unique_ptr<Stmt> Parser::statement() {
    switch (peek().type) {
        case TokenType::LOAD:      return loadStatement();
        case TokenType::DROP_NULL: return dropNullStatement();
        case TokenType::FILL_NULL: return fillNullStatement();
        case TokenType::NORMALIZE: return normalizeStatement();
        case TokenType::FILTER:    return filterStatement();
        case TokenType::SELECT:    return selectStatement();
        case TokenType::DROP:      return dropStatement();
        case TokenType::SAVE:      return saveStatement();
        case TokenType::EXPECT:    return expectStatement();
        default:                   break;
    }
    const std::string hint = check(TokenType::IDENTIFIER) ? suggestClosest(peek().lexeme, statementKeywords) : "";
    errorAt(peek().location, "expected a statement (LOAD, FILTER, SAVE, ...), found " + describe(peek()), hint);
}

// load -> LOAD STRING_LITERAL AS IDENTIFIER ;
std::unique_ptr<Stmt> Parser::loadStatement() {
    auto node = std::make_unique<LoadStmt>();
    node->location = advance().location;
    node->path = consume(TokenType::STRING_LITERAL, "a file name in double quotes").lexeme;
    consume(TokenType::AS, "'AS'");
    node->dataset = identifier("a dataset name");
    endStatement();
    return node;
}

// drop_null -> DROP_NULL IDENTIFIER ;
std::unique_ptr<Stmt> Parser::dropNullStatement() {
    auto node = std::make_unique<DropNullStmt>();
    node->location = advance().location;
    node->dataset = identifier("a dataset name");
    endStatement();
    return node;
}

// fill_null -> FILL_NULL column_ref WITH literal ;
std::unique_ptr<Stmt> Parser::fillNullStatement() {
    auto node = std::make_unique<FillNullStmt>();
    node->location = advance().location;
    node->target = columnRef();
    consume(TokenType::WITH, "'WITH'");
    node->value = literal();
    endStatement();
    return node;
}

// normalize -> NORMALIZE column_ref [ USING ( ZSCORE | MINMAX ) ] ;
std::unique_ptr<Stmt> Parser::normalizeStatement() {
    auto node = std::make_unique<NormalizeStmt>();
    node->location = advance().location;
    node->target = columnRef();
    if (match(TokenType::USING)) {
        if (match(TokenType::ZSCORE)) {
            node->method = NormalizeMethod::ZScore;
        } else if (match(TokenType::MINMAX)) {
            node->method = NormalizeMethod::MinMax;
        } else {
            errorExpected("ZSCORE or MINMAX after USING");
        }
    }
    endStatement();
    return node;
}

// filter -> FILTER IDENTIFIER WHERE or_expr ;
std::unique_ptr<Stmt> Parser::filterStatement() {
    auto node = std::make_unique<FilterStmt>();
    node->location = advance().location;
    node->dataset = identifier("a dataset name");
    consume(TokenType::WHERE, "'WHERE'");
    node->condition = orExpression();
    endStatement();
    return node;
}

// select -> SELECT IDENTIFIER column_list ;
std::unique_ptr<Stmt> Parser::selectStatement() {
    auto node = std::make_unique<SelectStmt>();
    node->location = advance().location;
    node->dataset = identifier("a dataset name");
    node->columns = columnList();
    endStatement();
    return node;
}

// drop -> DROP IDENTIFIER column_list ;
std::unique_ptr<Stmt> Parser::dropStatement() {
    auto node = std::make_unique<DropStmt>();
    node->location = advance().location;
    node->dataset = identifier("a dataset name");
    node->columns = columnList();
    endStatement();
    return node;
}

// save -> SAVE IDENTIFIER AS STRING_LITERAL ;
std::unique_ptr<Stmt> Parser::saveStatement() {
    auto node = std::make_unique<SaveStmt>();
    node->location = advance().location;
    node->dataset = identifier("a dataset name");
    consume(TokenType::AS, "'AS'");
    node->path = consume(TokenType::STRING_LITERAL, "a file name in double quotes").lexeme;
    endStatement();
    return node;
}

// expect     -> EXPECT column_ref constraint ;
// constraint -> NUMBER | STRING | BOOLEAN | NOT_NULL | BETWEEN number AND number
std::unique_ptr<Stmt> Parser::expectStatement() {
    auto node = std::make_unique<ExpectStmt>();
    node->location = advance().location;
    node->target = columnRef();
    if (match(TokenType::NUMBER)) {
        node->constraint = ConstraintKind::IsNumber;
    } else if (match(TokenType::STRING)) {
        node->constraint = ConstraintKind::IsString;
    } else if (match(TokenType::BOOLEAN)) {
        node->constraint = ConstraintKind::IsBoolean;
    } else if (match(TokenType::NOT_NULL)) {
        node->constraint = ConstraintKind::NotNull;
    } else if (match(TokenType::BETWEEN)) {
        node->constraint = ConstraintKind::Between;
        node->low = number();
        consume(TokenType::AND, "'AND'");
        node->high = number();
    } else {
        errorExpected("NUMBER, STRING, BOOLEAN, NOT_NULL or BETWEEN");
    }
    endStatement();
    return node;
}

// ---------- FILTER conditions ----------
// Precedence from lowest to highest: OR, AND, NOT, comparison.
// Each level calls the next one, so higher levels group first.

// or_expr -> and_expr { OR and_expr }
std::unique_ptr<Expr> Parser::orExpression() {
    std::unique_ptr<Expr> left = andExpression();
    while (match(TokenType::OR)) {
        auto node = std::make_unique<LogicalExpr>();
        node->op = LogicalOp::Or;
        node->left = std::move(left);
        node->right = andExpression();
        left = std::move(node);
    }
    return left;
}

// and_expr -> not_expr { AND not_expr }
std::unique_ptr<Expr> Parser::andExpression() {
    std::unique_ptr<Expr> left = notExpression();
    while (match(TokenType::AND)) {
        auto node = std::make_unique<LogicalExpr>();
        node->op = LogicalOp::And;
        node->left = std::move(left);
        node->right = notExpression();
        left = std::move(node);
    }
    return left;
}

// not_expr -> NOT not_expr | primary
std::unique_ptr<Expr> Parser::notExpression() {
    if (match(TokenType::NOT)) {
        auto node = std::make_unique<NotExpr>();
        node->operand = notExpression();
        return node;
    }
    return primary();
}

// primary -> ( or_expr ) | IDENTIFIER comparison_op literal
std::unique_ptr<Expr> Parser::primary() {
    if (match(TokenType::LEFT_PAREN)) {
        std::unique_ptr<Expr> inner = orExpression();
        consume(TokenType::RIGHT_PAREN, "')'");
        return inner;
    }

    auto node = std::make_unique<ComparisonExpr>();
    node->location = peek().location;
    node->column = identifier("a column name or '('");
    switch (peek().type) {
        case TokenType::EQUAL_EQUAL:
        case TokenType::NOT_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
            node->op = advance().lexeme;
            break;
        default:
            errorExpected("a comparison operator (==, !=, <, <=, >, >=)");
    }
    node->value = literal();
    return node;
}

// ---------- Shared pieces ----------

// column_ref -> IDENTIFIER . IDENTIFIER
ColumnRef Parser::columnRef() {
    ColumnRef ref;
    ref.location = peek().location;
    ref.dataset = identifier("a dataset name");
    consume(TokenType::DOT, "'.' between dataset and column (dataset.column)");
    ref.column = identifier("a column name");
    return ref;
}

// column_list -> [ IDENTIFIER { , IDENTIFIER } ]
std::vector<std::string> Parser::columnList() {
    consume(TokenType::LEFT_BRACKET, "'[' before the column names");
    std::vector<std::string> columns;
    columns.push_back(identifier("a column name"));
    while (match(TokenType::COMMA)) {
        columns.push_back(identifier("a column name"));
    }
    consume(TokenType::RIGHT_BRACKET, "']' after the column names");
    return columns;
}

// literal -> number | STRING_LITERAL | TRUE | FALSE
Literal Parser::literal() {
    switch (peek().type) {
        case TokenType::NUMBER_LITERAL:
        case TokenType::MINUS:
            return {LiteralKind::Number, number()};
        case TokenType::STRING_LITERAL:
            return {LiteralKind::String, advance().lexeme};
        case TokenType::TRUE:
        case TokenType::FALSE:
            return {LiteralKind::Boolean, advance().type == TokenType::TRUE ? "TRUE" : "FALSE"};
        default:
            errorExpected("a value (a number, \"text\", TRUE or FALSE)");
    }
}

// number -> [ - ] NUMBER_LITERAL
std::string Parser::number() {
    const std::string sign = match(TokenType::MINUS) ? "-" : "";
    return sign + consume(TokenType::NUMBER_LITERAL, "a number").lexeme;
}

// A dataset or column name. A column called e.g. "number" clashes with the
// keyword NUMBER, so the hint shows how to quote it.
std::string Parser::identifier(const std::string& what) {
    if (check(TokenType::IDENTIFIER)) {
        return advance().lexeme;
    }
    std::string hint;
    if (isKeyword(peek().type)) {
        hint = "'" + peek().lexeme + "' is a keyword; write `" + peek().lexeme + "` to use it as a name";
    }
    errorExpected(what, hint);
}

void Parser::endStatement() {
    consume(TokenType::SEMICOLON, "';' at the end of the statement");
}

// ---------- Reading tokens ----------

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::isAtEnd() const {
    return peek().type == TokenType::END_OF_FILE;
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

const Token& Parser::advance() {
    if (!isAtEnd()) {
        ++current_;
    }
    return previous();
}

const Token& Parser::consume(TokenType type, const std::string& what) {
    if (!check(type)) {
        errorExpected(what);
    }
    return advance();
}

// ---------- Errors and recovery ----------

void Parser::errorAt(SourceLocation location, const std::string& message, const std::string& hint) {
    diagnostics_.error(Phase::Syntax, location, message, hint);
    throw ParseError{};
}

// Reports that `what` was expected but the next token is something else.
// If that token is already on the next line (for example a missing ';'),
// the error points just after the previous token, where the fix belongs.
void Parser::errorExpected(const std::string& what, const std::string& hint) {
    SourceLocation location = peek().location;
    if (current_ > 0 && previous().location.line != peek().location.line) {
        location = previous().location;
        location.column += previous().length;
    }
    errorAt(location, "expected " + what + ", found " + describe(peek()), hint);
}

// Panic mode: throw away tokens until the broken statement ends (';') or the
// next statement starts, so errors later in the file are still reported.
void Parser::synchronize() {
    while (!isAtEnd()) {
        if (isStatementKeyword(peek().type)) {
            return;
        }
        if (advance().type == TokenType::SEMICOLON) {
            return;
        }
    }
}
