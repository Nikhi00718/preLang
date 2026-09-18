#pragma once

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "lexer/Token.h"

#include <memory>
#include <string>
#include <vector>

// Syntax analysis with a hand-written recursive-descent parser.
// Each grammar rule is one method; the rule is written above it in Parser.cpp.
class Parser {
public:
    Parser(std::vector<Token> tokens, Diagnostics& diagnostics);

    // Parses the whole program. A statement with a syntax error is reported
    // and left out, and parsing continues with the next statement.
    Program parse();

private:
    std::vector<Token> tokens_;
    Diagnostics& diagnostics_;
    size_t current_ = 0;  // index of the next token to read

    // Statements
    std::unique_ptr<Stmt> statement();
    std::unique_ptr<Stmt> loadStatement(DataKind kind);
    std::unique_ptr<Stmt> dropNullStatement();
    std::unique_ptr<Stmt> fillNullStatement();
    std::unique_ptr<Stmt> normalizeStatement();
    std::unique_ptr<Stmt> filterStatement();
    std::unique_ptr<Stmt> selectStatement();
    std::unique_ptr<Stmt> dropStatement();
    std::unique_ptr<Stmt> saveStatement(DataKind kind);
    std::unique_ptr<Stmt> expectStatement();
    std::unique_ptr<Stmt> resizeStatement();
    std::unique_ptr<Stmt> grayscaleStatement();
    std::unique_ptr<Stmt> compressStatement();
    std::unique_ptr<Stmt> convertStatement();
    std::unique_ptr<Stmt> removeCorruptStatement();
    std::unique_ptr<Stmt> linkStatement();

    // FILTER conditions, from lowest to highest precedence
    std::unique_ptr<Expr> orExpression();
    std::unique_ptr<Expr> andExpression();
    std::unique_ptr<Expr> notExpression();
    std::unique_ptr<Expr> primary();

    // Selectors
    Selector tableRef();
    Selector tableSelector();
    Selector columnSelector();
    Selector path();
    void optionalIfExists(Selector& selector);

    // Pieces shared by several statements
    std::vector<std::string> columnList();
    Literal literal();
    std::string number();
    int integer(const std::string& what);
    std::string identifier(const std::string& what);
    void endStatement();

    // Reading tokens
    const Token& peek() const;
    const Token& previous() const;
    bool isAtEnd() const;
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token& advance();
    const Token& consume(TokenType type, const std::string& what);

    // Errors and recovery
    [[noreturn]] void errorAt(SourceLocation location, const std::string& message, const std::string& hint = "");
    [[noreturn]] void errorExpected(const std::string& what, const std::string& hint = "");
    void synchronize();
};
