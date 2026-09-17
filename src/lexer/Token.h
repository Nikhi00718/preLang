#pragma once

#include "common/Diagnostics.h"

#include <string>

// Every kind of token in PrepLang.
// Keywords are listed first so isKeyword() can be a simple range check.
enum class TokenType {
    // Statement keywords: every statement starts with one of these
    LOAD, DROP_NULL, FILL_NULL, NORMALIZE, FILTER, SELECT, DROP, SAVE, EXPECT,

    // Other keywords
    AS, WITH, USING, ZSCORE, MINMAX, WHERE,
    NUMBER, STRING, BOOLEAN, NOT_NULL, BETWEEN,
    AND, OR, NOT, TRUE, FALSE,

    // Names and values
    IDENTIFIER, NUMBER_LITERAL, STRING_LITERAL,

    // Comparison operators
    EQUAL_EQUAL, NOT_EQUAL, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,

    // Punctuation
    DOT, COMMA, SEMICOLON, LEFT_BRACKET, RIGHT_BRACKET, LEFT_PAREN, RIGHT_PAREN, MINUS,

    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string lexeme;       // token text; string literals are stored without their quotes
    SourceLocation location;  // where the token starts
    int length;               // characters it takes in the source, quotes included
};

inline bool isStatementKeyword(TokenType type) { return type <= TokenType::EXPECT; }
inline bool isKeyword(TokenType type) { return type <= TokenType::FALSE; }
