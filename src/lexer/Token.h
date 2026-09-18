#pragma once

#include "common/Diagnostics.h"

#include <string>

// Every kind of token in PrepLang. The order matters: statement keywords come
// first, then the other keywords, so isStatementKeyword() and isKeyword() are
// simple range checks.
enum class TokenType {
    // Statement keywords: every statement starts with one of these
    LOAD, LOAD_TABLES, LOAD_IMAGE, LOAD_IMAGES,
    DROP_NULL, FILL_NULL, NORMALIZE, FILTER, SELECT, DROP,
    SAVE, SAVE_TABLES, SAVE_IMAGE, SAVE_IMAGES, EXPECT,
    RESIZE, GRAYSCALE, COMPRESS, CONVERT, REMOVE_CORRUPT, LINK,

    // Other keywords
    AS, WITH, TO, ON, USING, ZSCORE, MINMAX, RANGE, WHERE, TYPE, EXCEPT, IF_EXISTS, RECURSIVE,
    NUMBER, NUMERIC, STRING, BOOLEAN, NULLABLE, NOT_NULL, BETWEEN, UNIQUE, REFERENCES, SCHEMA_SAME_AS,
    CHANNELS, MIN_WIDTH, MIN_HEIGHT, MATCH_RATE, KEEP_ASPECT, QUALITY, JPG, PNG, NPY,
    AND, OR, NOT, TRUE, FALSE,

    // Names and values
    IDENTIFIER, NUMBER_LITERAL, STRING_LITERAL,

    // Comparison operators
    EQUAL_EQUAL, NOT_EQUAL, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,

    // Punctuation
    DOT, COMMA, SEMICOLON, LEFT_BRACKET, RIGHT_BRACKET, LEFT_PAREN, RIGHT_PAREN, MINUS, STAR, PERCENT,

    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string lexeme;       // token text; string literals are stored without their quotes
    SourceLocation location;  // where the token starts
    int length;               // characters it takes in the source, quotes included
};

inline bool isStatementKeyword(TokenType type) { return type <= TokenType::LINK; }
inline bool isKeyword(TokenType type) { return type < TokenType::IDENTIFIER; }
