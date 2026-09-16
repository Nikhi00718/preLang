#pragma once

#include "Diagnostics.h"
#include "Token.h"

#include <string>
#include <vector>

namespace prep {

class Lexer {
public:
    Lexer(const std::string& source, ErrorReporter& reporter);
    std::vector<Token> tokenize();

private:
    bool atEnd() const;
    char peek(int offset = 0) const;
    char advance();
    bool match(char expected);
    void add(TokenType type, const std::string& value, std::size_t start, int line, int column);
    void skipComment();
    void scanWord(int line, int column);
    void scanNumber(int line, int column);
    void scanString(int line, int column);
    void scanOperator(int line, int column);
    void scanPunctuation(int line, int column);

    const std::string& source_;
    ErrorReporter& reporter_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
    std::vector<Token> tokens_;
};

std::string formatTokenTable(const std::vector<Token>& tokens);

}  // namespace prep
