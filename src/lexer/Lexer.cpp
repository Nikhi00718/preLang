#include "lexer/Lexer.h"

#include <cctype>
#include <iomanip>
#include <unordered_map>
#include <utility>

namespace {

bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isWordStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isWordPart(char c) { return isWordStart(c) || isDigit(c); }

// Keywords ignore case: LOAD, load and Load are the same keyword.
TokenType keywordOrIdentifier(const std::string& word) {
    static const std::unordered_map<std::string, TokenType> keywords = {
        {"LOAD", TokenType::LOAD},           {"DROP_NULL", TokenType::DROP_NULL},
        {"FILL_NULL", TokenType::FILL_NULL}, {"NORMALIZE", TokenType::NORMALIZE},
        {"FILTER", TokenType::FILTER},       {"SELECT", TokenType::SELECT},
        {"DROP", TokenType::DROP},           {"SAVE", TokenType::SAVE},
        {"EXPECT", TokenType::EXPECT},       {"AS", TokenType::AS},
        {"WITH", TokenType::WITH},           {"USING", TokenType::USING},
        {"ZSCORE", TokenType::ZSCORE},       {"MINMAX", TokenType::MINMAX},
        {"WHERE", TokenType::WHERE},         {"NUMBER", TokenType::NUMBER},
        {"STRING", TokenType::STRING},       {"BOOLEAN", TokenType::BOOLEAN},
        {"NOT_NULL", TokenType::NOT_NULL},   {"BETWEEN", TokenType::BETWEEN},
        {"AND", TokenType::AND},             {"OR", TokenType::OR},
        {"NOT", TokenType::NOT},             {"TRUE", TokenType::TRUE},
        {"FALSE", TokenType::FALSE},
    };
    std::string upper;
    for (char c : word) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const auto found = keywords.find(upper);
    return found == keywords.end() ? TokenType::IDENTIFIER : found->second;
}

const char* typeName(TokenType type) {
    switch (type) {
        case TokenType::LOAD:           return "LOAD";
        case TokenType::DROP_NULL:      return "DROP_NULL";
        case TokenType::FILL_NULL:      return "FILL_NULL";
        case TokenType::NORMALIZE:      return "NORMALIZE";
        case TokenType::FILTER:         return "FILTER";
        case TokenType::SELECT:         return "SELECT";
        case TokenType::DROP:           return "DROP";
        case TokenType::SAVE:           return "SAVE";
        case TokenType::EXPECT:         return "EXPECT";
        case TokenType::AS:             return "AS";
        case TokenType::WITH:           return "WITH";
        case TokenType::USING:          return "USING";
        case TokenType::ZSCORE:         return "ZSCORE";
        case TokenType::MINMAX:         return "MINMAX";
        case TokenType::WHERE:          return "WHERE";
        case TokenType::NUMBER:         return "NUMBER";
        case TokenType::STRING:         return "STRING";
        case TokenType::BOOLEAN:        return "BOOLEAN";
        case TokenType::NOT_NULL:       return "NOT_NULL";
        case TokenType::BETWEEN:        return "BETWEEN";
        case TokenType::AND:            return "AND";
        case TokenType::OR:             return "OR";
        case TokenType::NOT:            return "NOT";
        case TokenType::TRUE:           return "TRUE";
        case TokenType::FALSE:          return "FALSE";
        case TokenType::IDENTIFIER:     return "IDENTIFIER";
        case TokenType::NUMBER_LITERAL: return "NUMBER_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::EQUAL_EQUAL:    return "EQUAL_EQUAL";
        case TokenType::NOT_EQUAL:      return "NOT_EQUAL";
        case TokenType::LESS:           return "LESS";
        case TokenType::LESS_EQUAL:     return "LESS_EQUAL";
        case TokenType::GREATER:        return "GREATER";
        case TokenType::GREATER_EQUAL:  return "GREATER_EQUAL";
        case TokenType::DOT:            return "DOT";
        case TokenType::COMMA:          return "COMMA";
        case TokenType::SEMICOLON:      return "SEMICOLON";
        case TokenType::LEFT_BRACKET:   return "LEFT_BRACKET";
        case TokenType::RIGHT_BRACKET:  return "RIGHT_BRACKET";
        case TokenType::LEFT_PAREN:     return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN:    return "RIGHT_PAREN";
        case TokenType::MINUS:          return "MINUS";
        case TokenType::END_OF_FILE:    return "END_OF_FILE";
    }
    return "";
}

const char* categoryName(TokenType type) {
    if (isKeyword(type)) {
        return "KEYWORD";
    }
    switch (type) {
        case TokenType::IDENTIFIER:
            return "IDENTIFIER";
        case TokenType::NUMBER_LITERAL:
        case TokenType::STRING_LITERAL:
            return "LITERAL";
        case TokenType::EQUAL_EQUAL:
        case TokenType::NOT_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
        case TokenType::MINUS:
            return "OPERATOR";
        case TokenType::END_OF_FILE:
            return "END";
        default:
            return "PUNCTUATION";
    }
}

}  // namespace

Lexer::Lexer(std::string source, Diagnostics& diagnostics)
    : source_(std::move(source)), diagnostics_(diagnostics) {}

std::vector<Token> Lexer::tokenize() {
    while (true) {
        skipWhitespaceAndComments();
        if (isAtEnd()) {
            break;
        }
        tokenStart_ = position_;
        tokenLocation_ = {line_, column_};
        scanToken();
    }
    tokens_.push_back({TokenType::END_OF_FILE, "", {line_, column_}, 0});
    return tokens_;
}

// Spaces, tabs, newlines and '#' comments separate tokens but are not tokens.
void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        const char c = peek();
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance();
        } else if (c == '#') {
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
        } else {
            return;
        }
    }
}

// Reads one token. The first character decides which kind it can be.
void Lexer::scanToken() {
    const char c = advance();

    if (isWordStart(c)) {
        scanWord();
        return;
    }
    if (isDigit(c)) {
        scanNumber();
        return;
    }

    switch (c) {
        case '"': scanQuoted('"', TokenType::STRING_LITERAL); return;
        case '`': scanQuoted('`', TokenType::IDENTIFIER); return;
        case '.': addToken(TokenType::DOT); return;
        case ',': addToken(TokenType::COMMA); return;
        case ';': addToken(TokenType::SEMICOLON); return;
        case '[': addToken(TokenType::LEFT_BRACKET); return;
        case ']': addToken(TokenType::RIGHT_BRACKET); return;
        case '(': addToken(TokenType::LEFT_PAREN); return;
        case ')': addToken(TokenType::RIGHT_PAREN); return;
        case '-': addToken(TokenType::MINUS); return;

        // Longest match: "<=" is one token, not "<" followed by "=".
        case '<': addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS); return;
        case '>': addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER); return;
        case '=':
            if (match('=')) {
                addToken(TokenType::EQUAL_EQUAL);
            } else {
                report("unexpected character '='", "use '==' to compare values");
            }
            return;
        case '!':
            if (match('=')) {
                addToken(TokenType::NOT_EQUAL);
            } else {
                report("unexpected character '!'", "use '!=' for 'not equal', or NOT to negate a condition");
            }
            return;

        default:
            report(std::string("unexpected character '") + c + "'", "");
            return;
    }
}

// word: [A-Za-z_][A-Za-z0-9_]*   (a keyword if it is in the keyword table)
void Lexer::scanWord() {
    while (isWordPart(peek())) {
        advance();
    }
    const std::string word = source_.substr(tokenStart_, position_ - tokenStart_);
    addToken(keywordOrIdentifier(word), word);
}

// number: [0-9]+ ( '.' [0-9]+ )?
void Lexer::scanNumber() {
    while (isDigit(peek())) {
        advance();
    }
    if (peek() == '.' && isDigit(peek(1))) {
        advance();
        while (isDigit(peek())) {
            advance();
        }
    }
    addToken(TokenType::NUMBER_LITERAL);
}

// "text" is a string literal and `First Name` is an identifier with spaces.
// Both must close on the same line. Backslashes are ordinary characters, so
// Windows paths such as "C:\data\students.csv" work as typed.
void Lexer::scanQuoted(char quote, TokenType type) {
    while (!isAtEnd() && peek() != quote && peek() != '\n') {
        advance();
    }
    if (peek() != quote) {
        report(std::string("missing closing ") + quote, std::string("add ") + quote + " before the end of the line");
        return;
    }
    advance();  // the closing quote
    addToken(type, source_.substr(tokenStart_ + 1, position_ - tokenStart_ - 2));
}

void Lexer::addToken(TokenType type) {
    addToken(type, source_.substr(tokenStart_, position_ - tokenStart_));
}

void Lexer::addToken(TokenType type, std::string lexeme) {
    const int length = static_cast<int>(position_ - tokenStart_);
    tokens_.push_back({type, std::move(lexeme), tokenLocation_, length});
}

void Lexer::report(const std::string& message, const std::string& hint) {
    diagnostics_.error(Phase::Lexical, tokenLocation_, message, hint);
}

bool Lexer::isAtEnd() const {
    return position_ >= source_.size();
}

char Lexer::peek(size_t offset) const {
    const size_t index = position_ + offset;
    return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() {
    const char c = source_[position_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() != expected) {
        return false;
    }
    advance();
    return true;
}

void printTokens(std::ostream& out, const std::vector<Token>& tokens) {
    out << std::left << std::setw(10) << "LINE:COL" << std::setw(17) << "TYPE"
        << std::setw(13) << "CATEGORY" << "LEXEME\n";
    for (const Token& token : tokens) {
        const std::string position = std::to_string(token.location.line) + ":" + std::to_string(token.location.column);
        const std::string lexeme = token.type == TokenType::STRING_LITERAL ? "\"" + token.lexeme + "\"" : token.lexeme;
        out << std::setw(10) << position << std::setw(17) << typeName(token.type)
            << std::setw(13) << categoryName(token.type) << lexeme << '\n';
    }
}
