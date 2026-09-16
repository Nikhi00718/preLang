#include "Parser.h"

#include <algorithm>
#include <utility>

namespace prep {
namespace {

const std::vector<std::string> STATEMENT_KEYWORDS = {
    "LOAD", "DROP_NULL", "FILL_NULL", "NORMALIZE", "FILTER",
    "SELECT", "DROP", "SAVE", "EXPECT"
};

ParseTreeNode node(const std::string& name, const std::string& value = "") {
    return {name, value, {}};
}

}  // namespace

Parser::Parser(const std::vector<Token>& tokens, ErrorReporter& reporter)
    : tokens_(tokens), reporter_(reporter) {}

const Token& Parser::current() const {
    static const Token eof;
    if (tokens_.empty()) return eof;
    return tokens_[std::min(position_, tokens_.size() - 1)];
}

Token Parser::advance() {
    Token token = current();
    if (token.type != TokenType::EndOfFile && position_ < tokens_.size()) ++position_;
    return token;
}

bool Parser::check(TokenType type) const {
    return current().type == type;
}

bool Parser::checkKeyword(const std::string& keyword) const {
    return current().type == TokenType::Keyword && current().value == keyword;
}

bool Parser::isStatementKeyword() const {
    return current().type == TokenType::Keyword &&
           std::find(STATEMENT_KEYWORDS.begin(), STATEMENT_KEYWORDS.end(), current().value) !=
               STATEMENT_KEYWORDS.end();
}

std::string Parser::describe(const Token& token) const {
    if (token.type == TokenType::EndOfFile) return "end of file";
    return quoteText(token.lexeme) + " (" + tokenTypeName(token.type) + ")";
}

void Parser::syntaxError(const std::string& expected, const std::string& hint) {
    const Token& token = current();
    reporter_.report({
        "SYNTAX ERROR",
        "Unexpected " + describe(token) + ".",
        token.line,
        token.column,
        expected,
        describe(token),
        hint
    });
}

bool Parser::expect(TokenType type, const std::string& expected, Token& result) {
    if (check(type)) {
        result = advance();
        return true;
    }
    syntaxError(expected);
    return false;
}

bool Parser::expectKeyword(const std::string& keyword, Token& result) {
    if (checkKeyword(keyword)) {
        result = advance();
        return true;
    }
    syntaxError("keyword " + keyword);
    return false;
}

ParseResult Parser::parse() {
    ParseResult result;
    while (!check(TokenType::EndOfFile)) {
        std::size_t before = position_;
        Statement statement;
        ParseTreeNode tree;
        if (parseStatement(statement, tree)) {
            result.program.statements.push_back(std::move(statement));
            result.tree.children.push_back(std::move(tree));
        } else {
            synchronize();
        }
        if (position_ == before && !check(TokenType::EndOfFile)) advance();
    }
    return result;
}

bool Parser::parseStatement(Statement& statement, ParseTreeNode& tree) {
    if (!isStatementKeyword()) {
        syntaxError(
            "a statement keyword: LOAD, DROP_NULL, FILL_NULL, NORMALIZE, FILTER, SELECT, DROP, SAVE, EXPECT",
            "Every statement begins with a keyword and ends with ';'."
        );
        return false;
    }

    const std::string keyword = current().value;
    if (keyword == "LOAD") return parseLoad(statement, tree);
    if (keyword == "EXPECT") return parseExpect(statement, tree);
    if (keyword == "DROP_NULL") return parseDropNull(statement, tree);
    if (keyword == "FILL_NULL") return parseFillNull(statement, tree);
    if (keyword == "NORMALIZE") return parseNormalize(statement, tree);
    if (keyword == "FILTER") return parseFilter(statement, tree);
    if (keyword == "SELECT") return parseSelect(statement, tree);
    if (keyword == "DROP") return parseDrop(statement, tree);
    return parseSave(statement, tree);
}

bool Parser::parseLoad(Statement& statement, ParseTreeNode& tree) {
    Token start, path, ignored, dataset;
    if (!expectKeyword("LOAD", start)) return false;
    if (!expect(TokenType::StringLiteral, "a quoted file path", path)) return false;
    if (!expectKeyword("AS", ignored)) return false;
    if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
    if (!terminator()) return false;

    statement.kind = StatementKind::Load;
    statement.line = start.line;
    statement.file = path.value;
    statement.dataset = dataset.value;
    tree = node("load_stmt");
    tree.children = {node("file", path.value), node("dataset", dataset.value)};
    return true;
}

bool Parser::parseExpect(Statement& statement, ParseTreeNode& tree) {
    Token start;
    if (!expectKeyword("EXPECT", start)) return false;
    std::string dataset, column;
    ParseTreeNode reference;
    if (!columnReference(dataset, column, reference)) return false;

    statement.kind = StatementKind::Expect;
    statement.line = start.line;
    statement.dataset = dataset;
    statement.column = column;
    tree = node("expect_stmt");
    tree.children.push_back(std::move(reference));

    if (checkKeyword("NUMBER") || checkKeyword("STRING") || checkKeyword("BOOLEAN")) {
        Token type = advance();
        statement.contract = "TYPE";
        statement.expectedType = type.value;
        tree.children.push_back(node("type", type.value));
    } else if (checkKeyword("NOT_NULL")) {
        advance();
        statement.contract = "NOT_NULL";
        tree.children.push_back(node("contract", "NOT_NULL"));
    } else if (checkKeyword("BETWEEN")) {
        advance();
        Token low, high, ignored;
        if (!expect(TokenType::NumberLiteral, "a numeric lower bound", low)) return false;
        if (!expectKeyword("AND", ignored)) return false;
        if (!expect(TokenType::NumberLiteral, "a numeric upper bound", high)) return false;
        statement.contract = "RANGE";
        statement.low = low.value;
        statement.high = high.value;
        tree.children.push_back(node("range", low.value + ".." + high.value));
    } else {
        syntaxError("NUMBER, STRING, BOOLEAN, NOT_NULL or BETWEEN n AND m");
        return false;
    }

    if (!terminator()) return false;
    return true;
}

bool Parser::parseDropNull(Statement& statement, ParseTreeNode& tree) {
    Token start, dataset;
    if (!expectKeyword("DROP_NULL", start)) return false;
    if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
    if (!terminator()) return false;
    statement.kind = StatementKind::DropNull;
    statement.line = start.line;
    statement.dataset = dataset.value;
    tree = node("drop_null_stmt", dataset.value);
    return true;
}

bool Parser::parseFillNull(Statement& statement, ParseTreeNode& tree) {
    Token start, ignored;
    if (!expectKeyword("FILL_NULL", start)) return false;
    std::string dataset, column;
    ParseTreeNode reference;
    if (!columnReference(dataset, column, reference)) return false;
    if (!expectKeyword("WITH", ignored)) return false;
    Literal value;
    ParseTreeNode literalNode;
    if (!literal(value, literalNode)) return false;
    if (!terminator()) return false;
    statement.kind = StatementKind::FillNull;
    statement.line = start.line;
    statement.dataset = dataset;
    statement.column = column;
    statement.value = value;
    statement.hasValue = true;
    tree = node("fill_null_stmt");
    tree.children = {std::move(reference), std::move(literalNode)};
    return true;
}

bool Parser::parseNormalize(Statement& statement, ParseTreeNode& tree) {
    Token start;
    if (!expectKeyword("NORMALIZE", start)) return false;
    std::string dataset, column;
    ParseTreeNode reference;
    if (!columnReference(dataset, column, reference)) return false;
    if (!terminator()) return false;
    statement.kind = StatementKind::Normalize;
    statement.line = start.line;
    statement.dataset = dataset;
    statement.column = column;
    tree = node("normalize_stmt");
    tree.children.push_back(std::move(reference));
    return true;
}

bool Parser::parseFilter(Statement& statement, ParseTreeNode& tree) {
    Token start, dataset, column, operation, ignored;
    if (!expectKeyword("FILTER", start)) return false;
    if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
    if (!expectKeyword("WHERE", ignored)) return false;
    if (!expect(TokenType::Identifier, "a column name", column)) return false;
    if (!expect(TokenType::RelationalOperator, "a relational operator", operation)) return false;
    Literal value;
    ParseTreeNode literalNode;
    if (!literal(value, literalNode)) return false;
    if (!terminator()) return false;
    statement.kind = StatementKind::Filter;
    statement.line = start.line;
    statement.dataset = dataset.value;
    statement.column = column.value;
    statement.operation = operation.value;
    statement.value = value;
    statement.hasValue = true;
    tree = node("filter_stmt");
    tree.children = {node("dataset", dataset.value), node("column", column.value),
                     node("operator", operation.value), std::move(literalNode)};
    return true;
}

bool Parser::parseSelect(Statement& statement, ParseTreeNode& tree) {
    Token start, dataset;
    if (!expectKeyword("SELECT", start)) return false;
    if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
    std::vector<std::string> columns;
    ParseTreeNode list;
    if (!identifierList(columns, list)) return false;
    if (!terminator()) return false;
    statement.kind = StatementKind::Select;
    statement.line = start.line;
    statement.dataset = dataset.value;
    statement.columns = columns;
    tree = node("select_stmt");
    tree.children = {node("dataset", dataset.value), std::move(list)};
    return true;
}

bool Parser::parseDrop(Statement& statement, ParseTreeNode& tree) {
    Token start, dataset;
    if (!expectKeyword("DROP", start)) return false;
    if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
    std::vector<std::string> columns;
    ParseTreeNode list;
    if (!identifierList(columns, list)) return false;
    if (!terminator()) return false;
    statement.kind = StatementKind::Drop;
    statement.line = start.line;
    statement.dataset = dataset.value;
    statement.columns = columns;
    tree = node("drop_stmt");
    tree.children = {node("dataset", dataset.value), std::move(list)};
    return true;
}

bool Parser::parseSave(Statement& statement, ParseTreeNode& tree) {
    Token start, dataset, ignored, path;
    if (!expectKeyword("SAVE", start)) return false;
    if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
    if (!expectKeyword("AS", ignored)) return false;
    if (!expect(TokenType::StringLiteral, "a quoted output path", path)) return false;
    if (!terminator()) return false;
    statement.kind = StatementKind::Save;
    statement.line = start.line;
    statement.dataset = dataset.value;
    statement.file = path.value;
    tree = node("save_stmt");
    tree.children = {node("dataset", dataset.value), node("file", path.value)};
    return true;
}

bool Parser::columnReference(std::string& dataset, std::string& column, ParseTreeNode& tree) {
    Token datasetToken, dot, columnToken;
    if (!expect(TokenType::Identifier, "a dataset name", datasetToken)) return false;
    if (!expect(TokenType::Dot, "'.' between dataset and column", dot)) return false;
    if (!expect(TokenType::Identifier, "a column name", columnToken)) return false;
    dataset = datasetToken.value;
    column = columnToken.value;
    tree = node("column_ref");
    tree.children = {node("dataset", dataset), node("column", column)};
    return true;
}

bool Parser::identifierList(std::vector<std::string>& columns, ParseTreeNode& tree) {
    Token ignored;
    if (!expect(TokenType::LeftBracket, "'[' before the column list", ignored)) return false;
    if (check(TokenType::RightBracket)) {
        syntaxError("at least one column name", "An empty column list is not allowed.");
        return false;
    }

    tree = node("identifier_list");
    while (true) {
        Token column;
        if (!expect(TokenType::Identifier, "a column name", column)) return false;
        columns.push_back(column.value);
        tree.children.push_back(node("column", column.value));
        if (!check(TokenType::Comma)) break;
        advance();
    }
    return expect(TokenType::RightBracket, "']' after the column list", ignored);
}

bool Parser::literal(Literal& value, ParseTreeNode& tree) {
    if (check(TokenType::NumberLiteral)) {
        value = {LiteralKind::Number, advance().value};
        tree = node("number", value.value);
        return true;
    }
    if (check(TokenType::StringLiteral)) {
        value = {LiteralKind::String, advance().value};
        tree = node("string", value.value);
        return true;
    }
    if (check(TokenType::BooleanLiteral)) {
        value = {LiteralKind::Boolean, advance().value};
        tree = node("boolean", value.value);
        return true;
    }
    syntaxError("a number, string or boolean literal");
    return false;
}

bool Parser::terminator() {
    Token ignored;
    return expect(TokenType::Semicolon, "';' at the end of the statement", ignored);
}

void Parser::synchronize() {
    while (!check(TokenType::EndOfFile)) {
        if (check(TokenType::Semicolon)) {
            advance();
            return;
        }
        if (isStatementKeyword()) return;
        advance();
    }
}

}  // namespace prep
