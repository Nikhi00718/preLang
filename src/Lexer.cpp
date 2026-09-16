#include "Lexer.h"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace prep {
namespace {

bool isIdentifierStart(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) || character == '_';
}

bool isIdentifierPart(char character) {
    return isIdentifierStart(character) || std::isdigit(static_cast<unsigned char>(character));
}

bool isDigit(char character) {
    return std::isdigit(static_cast<unsigned char>(character));
}

const std::unordered_set<std::string> KEYWORDS = {
    "LOAD", "AS", "DROP_NULL", "FILL_NULL", "WITH", "NORMALIZE",
    "FILTER", "WHERE", "SELECT", "DROP", "SAVE", "EXPECT",
    "NUMBER", "STRING", "BOOLEAN", "BETWEEN", "AND", "NOT_NULL"
};

}  // namespace

Lexer::Lexer(const std::string& source, ErrorReporter& reporter)
    : source_(source), reporter_(reporter) {}

bool Lexer::atEnd() const {
    return position_ >= source_.size();
}

char Lexer::peek(int offset) const {
    std::size_t position = position_ + static_cast<std::size_t>(offset);
    return position < source_.size() ? source_[position] : '\0';
}

char Lexer::advance() {
    char character = source_[position_++];
    if (character == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return character;
}

bool Lexer::match(char expected) {
    if (peek() != expected) return false;
    advance();
    return true;
}

void Lexer::add(TokenType type, const std::string& value, std::size_t start,
                int line, int column) {
    tokens_.push_back({type, value, source_.substr(start, position_ - start), line, column});
}

std::vector<Token> Lexer::tokenize() {
    while (!atEnd()) {
        char character = peek();

        if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
            advance();
            continue;
        }
        if (character == '#' || (character == '-' && peek(1) == '-')) {
            skipComment();
            continue;
        }

        int line = line_;
        int column = column_;
        if (isIdentifierStart(character)) {
            scanWord(line, column);
        } else if (isDigit(character)) {
            scanNumber(line, column);
        } else if (character == '"' || character == '\'') {
            scanString(line, column);
        } else if (character == '<' || character == '>' || character == '=' || character == '!') {
            scanOperator(line, column);
        } else if (character == '.' || character == ',' || character == ';' ||
                   character == '[' || character == ']') {
            scanPunctuation(line, column);
        } else {
            std::string bad(1, advance());
            reporter_.report({
                "LEXICAL ERROR",
                "Unknown character " + quoteText(bad) + " in source program.",
                line,
                column,
                "",
                quoteText(bad),
                "Use letters, digits and '_' in identifiers."
            });
        }
    }

    tokens_.push_back({TokenType::EndOfFile, "", "<eof>", line_, column_});
    return tokens_;
}

void Lexer::skipComment() {
    if (peek() == '-') {
        advance();
        advance();
    }
    while (!atEnd() && peek() != '\n') advance();
}

void Lexer::scanWord(int line, int column) {
    std::size_t start = position_;
    while (!atEnd() && isIdentifierPart(peek())) advance();

    std::string lexeme = source_.substr(start, position_ - start);
    std::string canonical = upper(lexeme);
    if (canonical == "TRUE" || canonical == "FALSE") {
        add(TokenType::BooleanLiteral, canonical == "TRUE" ? "true" : "false", start, line, column);
    } else if (KEYWORDS.count(canonical) > 0) {
        add(TokenType::Keyword, canonical, start, line, column);
    } else {
        add(TokenType::Identifier, lexeme, start, line, column);
    }
}

void Lexer::scanNumber(int line, int column) {
    std::size_t start = position_;
    while (!atEnd() && isDigit(peek())) advance();

    if (peek() == '.' && isDigit(peek(1))) {
        advance();
        while (!atEnd() && isDigit(peek())) advance();
    }

    if (!atEnd() && isIdentifierStart(peek())) {
        while (!atEnd() && isIdentifierPart(peek())) advance();
        std::string bad = source_.substr(start, position_ - start);
        reporter_.report({
            "LEXICAL ERROR",
            "Malformed numeric literal " + quoteText(bad) + ".",
            line,
            column,
            "a number such as 42 or 3.14",
            quoteText(bad),
            "A number cannot be followed directly by letters."
        });
        return;
    }

    add(TokenType::NumberLiteral, source_.substr(start, position_ - start), start, line, column);
}

void Lexer::scanString(int line, int column) {
    std::size_t start = position_;
    char quoteCharacter = advance();
    std::string value;

    while (!atEnd()) {
        char character = advance();
        if (character == quoteCharacter) {
            add(TokenType::StringLiteral, value, start, line, column);
            return;
        }
        if (character == '\n') {
            reporter_.report({
                "LEXICAL ERROR",
                "Unterminated string literal.",
                line,
                column,
                "a closing quote on the same line",
                "",
                "String literals cannot span lines."
            });
            return;
        }
        if (character != '\\') {
            value += character;
            continue;
        }

        if (atEnd()) break;
        char escaped = advance();
        if (escaped == '\n') {
            reporter_.report({
                "LEXICAL ERROR",
                "Unterminated string literal.",
                line,
                column,
                "a closing quote on the same line",
                "",
                "String literals cannot span lines."
            });
            return;
        }
        switch (escaped) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case '\\': value += '\\'; break;
            case '"': value += '"'; break;
            case '\'': value += '\''; break;
            default: value += escaped; break;
        }
    }

    reporter_.report({
        "LEXICAL ERROR",
        "Unterminated string literal.",
        line,
        column,
        "a closing quote",
        "",
        "Add the missing closing quote."
    });
}

void Lexer::scanOperator(int line, int column) {
    std::size_t start = position_;
    char first = advance();
    match('=');
    std::string operation = source_.substr(start, position_ - start);

    if (operation == ">" || operation == "<" || operation == ">=" || operation == "<=" ||
        operation == "==" || operation == "!=") {
        add(TokenType::RelationalOperator, operation, start, line, column);
        return;
    }

    reporter_.report({
        "LEXICAL ERROR",
        "Invalid operator " + quoteText(operation) + ".",
        line,
        column,
        "one of >  <  >=  <=  ==  !=",
        quoteText(operation),
        first == '=' ? "Use '==' for equality comparison." : ""
    });
}

void Lexer::scanPunctuation(int line, int column) {
    std::size_t start = position_;
    char character = advance();
    TokenType type = TokenType::Dot;
    if (character == ',') type = TokenType::Comma;
    if (character == ';') type = TokenType::Semicolon;
    if (character == '[') type = TokenType::LeftBracket;
    if (character == ']') type = TokenType::RightBracket;
    add(type, std::string(1, character), start, line, column);
}

std::string formatTokenTable(const std::vector<Token>& tokens) {
    std::ostringstream out;
    out << std::left << std::setw(10) << "LINE:COL"
        << std::setw(20) << "LEXEME"
        << std::setw(22) << "TOKEN TYPE" << "VALUE\n";
    out << std::string(70, '-') << '\n';
    for (const Token& token : tokens) {
        std::string position = std::to_string(token.line) + ":" + std::to_string(token.column);
        std::string value = token.type == TokenType::StringLiteral ? quoteText(token.value) : token.value;
        out << std::left << std::setw(10) << position
            << std::setw(20) << token.lexeme
            << std::setw(22) << tokenTypeName(token.type) << value << '\n';
    }
    out << std::string(70, '-') << '\n' << tokens.size() << " tokens";
    return out.str();
}

}  // namespace prep
