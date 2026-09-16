#pragma once

#include "Ast.h"
#include "Diagnostics.h"
#include "ParseTree.h"
#include "Token.h"

#include <string>
#include <vector>

namespace prep {

struct ParseResult {
    Program program;
    ParseTreeNode tree{"program", "", {}};
};

class Parser {
public:
    Parser(const std::vector<Token>& tokens, ErrorReporter& reporter);
    ParseResult parse();

private:
    const Token& current() const;
    Token advance();
    bool check(TokenType type) const;
    bool checkKeyword(const std::string& keyword) const;
    bool isStatementKeyword() const;
    std::string describe(const Token& token) const;
    void syntaxError(const std::string& expected, const std::string& hint = "");
    bool expect(TokenType type, const std::string& expected, Token& result);
    bool expectKeyword(const std::string& keyword, Token& result);
    void synchronize();

    bool parseStatement(Statement& statement, ParseTreeNode& tree);
    bool parseLoad(Statement& statement, ParseTreeNode& tree);
    bool parseExpect(Statement& statement, ParseTreeNode& tree);
    bool parseDropNull(Statement& statement, ParseTreeNode& tree);
    bool parseFillNull(Statement& statement, ParseTreeNode& tree);
    bool parseNormalize(Statement& statement, ParseTreeNode& tree);
    bool parseFilter(Statement& statement, ParseTreeNode& tree);
    bool parseSelect(Statement& statement, ParseTreeNode& tree);
    bool parseDrop(Statement& statement, ParseTreeNode& tree);
    bool parseSave(Statement& statement, ParseTreeNode& tree);

    bool columnReference(std::string& dataset, std::string& column, ParseTreeNode& tree);
    bool identifierList(std::vector<std::string>& columns, ParseTreeNode& tree);
    bool literal(Literal& value, ParseTreeNode& tree);
    bool terminator();

    const std::vector<Token>& tokens_;
    ErrorReporter& reporter_;
    std::size_t position_ = 0;
};

}  // namespace prep
