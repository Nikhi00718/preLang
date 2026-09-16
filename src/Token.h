#pragma once

#include <string>

namespace prep {

enum class TokenType {
    Keyword,
    Identifier,
    NumberLiteral,
    StringLiteral,
    BooleanLiteral,
    Dot,
    Comma,
    Semicolon,
    LeftBracket,
    RightBracket,
    RelationalOperator,
    EndOfFile
};

std::string tokenTypeName(TokenType type);

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string value;
    std::string lexeme;
    int line = 1;
    int column = 1;
};

}  // namespace prep
