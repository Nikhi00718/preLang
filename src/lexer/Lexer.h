#pragma once

#include "common/Diagnostics.h"
#include "lexer/Token.h"

#include <ostream>
#include <string>
#include <vector>

// Lexical analysis: turns PrepLang source text into a list of tokens.
// Bad characters are reported and skipped, so scanning always finishes.
class Lexer {
public:
    Lexer(std::string source, Diagnostics& diagnostics);

    // Scans the whole source. The list always ends with END_OF_FILE.
    std::vector<Token> tokenize();

private:
    std::string source_;
    Diagnostics& diagnostics_;
    std::vector<Token> tokens_;

    size_t position_ = 0;  // index of the next character to read
    int line_ = 1;
    int column_ = 1;

    size_t tokenStart_ = 0;         // index where the current token begins
    SourceLocation tokenLocation_;  // line and column where it begins

    void skipWhitespaceAndComments();
    void scanToken();
    void scanWord();
    void scanNumber();
    void scanQuoted(char quote, TokenType type);

    void addToken(TokenType type);
    void addToken(TokenType type, std::string lexeme);
    void report(const std::string& message, const std::string& hint);

    bool isAtEnd() const;
    char peek(size_t offset = 0) const;
    char advance();
    bool match(char expected);
};

// Prints the tokens as a table: position, type, category and lexeme.
void printTokens(std::ostream& out, const std::vector<Token>& tokens);
