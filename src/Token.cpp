#include "Token.h"

namespace prep {

std::string tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::Keyword: return "KEYWORD";
        case TokenType::Identifier: return "IDENTIFIER";
        case TokenType::NumberLiteral: return "NUMBER_LITERAL";
        case TokenType::StringLiteral: return "STRING_LITERAL";
        case TokenType::BooleanLiteral: return "BOOLEAN_LITERAL";
        case TokenType::Dot: return "DOT";
        case TokenType::Comma: return "COMMA";
        case TokenType::Semicolon: return "SEMICOLON";
        case TokenType::LeftBracket: return "LEFT_BRACKET";
        case TokenType::RightBracket: return "RIGHT_BRACKET";
        case TokenType::RelationalOperator: return "RELATIONAL_OPERATOR";
        case TokenType::EndOfFile: return "EOF";
    }
    return "UNKNOWN";
}

}  // namespace prep
