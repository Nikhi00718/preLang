#include "lexer/Lexer.h"

#include <cctype>
#include <iomanip>
#include <unordered_map>
#include <utility>

namespace {

bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isWordStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isWordPart(char c) { return isWordStart(c) || isDigit(c); }

const std::unordered_map<std::string, TokenType>& keywordTable() {
    static const std::unordered_map<std::string, TokenType> keywords = {
        {"LOAD", TokenType::LOAD}, {"LOAD_TABLES", TokenType::LOAD_TABLES},
        {"LOAD_IMAGE", TokenType::LOAD_IMAGE}, {"LOAD_IMAGES", TokenType::LOAD_IMAGES},
        {"DROP_NULL", TokenType::DROP_NULL}, {"FILL_NULL", TokenType::FILL_NULL},
        {"NORMALIZE", TokenType::NORMALIZE}, {"FILTER", TokenType::FILTER}, {"SELECT", TokenType::SELECT},
        {"DROP", TokenType::DROP}, {"PARSE_DATE", TokenType::PARSE_DATE}, {"DERIVE", TokenType::DERIVE},
        {"JOIN", TokenType::JOIN}, {"AGGREGATE", TokenType::AGGREGATE},
        {"SAVE", TokenType::SAVE}, {"SAVE_TABLES", TokenType::SAVE_TABLES},
        {"SAVE_IMAGE", TokenType::SAVE_IMAGE}, {"SAVE_IMAGES", TokenType::SAVE_IMAGES},
        {"EXPECT", TokenType::EXPECT}, {"RESIZE", TokenType::RESIZE}, {"GRAYSCALE", TokenType::GRAYSCALE},
        {"COMPRESS", TokenType::COMPRESS}, {"CONVERT", TokenType::CONVERT},
        {"REMOVE_CORRUPT", TokenType::REMOVE_CORRUPT}, {"LINK", TokenType::LINK},
        {"AS", TokenType::AS}, {"WITH", TokenType::WITH}, {"TO", TokenType::TO}, {"ON", TokenType::ON},
        {"USING", TokenType::USING}, {"ZSCORE", TokenType::ZSCORE}, {"MINMAX", TokenType::MINMAX},
        {"MEAN", TokenType::MEAN}, {"MEDIAN", TokenType::MEDIAN}, {"MODE", TokenType::MODE},
        {"RANGE", TokenType::RANGE}, {"WHERE", TokenType::WHERE}, {"TYPE", TokenType::TYPE},
        {"EXCEPT", TokenType::EXCEPT}, {"IF_EXISTS", TokenType::IF_EXISTS}, {"RECURSIVE", TokenType::RECURSIVE},
        {"NUMBER", TokenType::NUMBER}, {"NUMERIC", TokenType::NUMERIC}, {"STRING", TokenType::STRING},
        {"BOOLEAN", TokenType::BOOLEAN}, {"NULLABLE", TokenType::NULLABLE}, {"NOT_NULL", TokenType::NOT_NULL},
        {"BETWEEN", TokenType::BETWEEN}, {"UNIQUE", TokenType::UNIQUE}, {"REFERENCES", TokenType::REFERENCES},
        {"SCHEMA_SAME_AS", TokenType::SCHEMA_SAME_AS}, {"CHANNELS", TokenType::CHANNELS},
        {"MIN_WIDTH", TokenType::MIN_WIDTH}, {"MIN_HEIGHT", TokenType::MIN_HEIGHT},
        {"MATCH_RATE", TokenType::MATCH_RATE}, {"KEEP_ASPECT", TokenType::KEEP_ASPECT},
        {"QUALITY", TokenType::QUALITY}, {"JPG", TokenType::JPG}, {"PNG", TokenType::PNG}, {"NPY", TokenType::NPY},
        {"AND", TokenType::AND}, {"OR", TokenType::OR}, {"NOT", TokenType::NOT},
        {"TRUE", TokenType::TRUE}, {"FALSE", TokenType::FALSE},
    };
    return keywords;
}

// Keywords ignore case: LOAD, load and Load are the same keyword.
TokenType keywordOrIdentifier(const std::string& word) {
    std::string upper;
    for (char c : word) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const auto found = keywordTable().find(upper);
    return found == keywordTable().end() ? TokenType::IDENTIFIER : found->second;
}

std::string typeName(TokenType type) {
    if (isKeyword(type)) {  // a keyword's type name is the keyword itself
        for (const auto& [word, keyword] : keywordTable()) {
            if (keyword == type) {
                return word;
            }
        }
    }
    switch (type) {
        case TokenType::IDENTIFIER:     return "IDENTIFIER";
        case TokenType::NUMBER_LITERAL: return "NUMBER_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::EQUAL_EQUAL:    return "EQUAL_EQUAL";
        case TokenType::NOT_EQUAL:      return "NOT_EQUAL";
        case TokenType::LESS:           return "LESS";
        case TokenType::LESS_EQUAL:     return "LESS_EQUAL";
        case TokenType::GREATER:        return "GREATER";
        case TokenType::GREATER_EQUAL:  return "GREATER_EQUAL";
        case TokenType::PLUS:           return "PLUS";
        case TokenType::SLASH:          return "SLASH";
        case TokenType::EQUAL:          return "EQUAL";
        case TokenType::DOT:            return "DOT";
        case TokenType::COMMA:          return "COMMA";
        case TokenType::SEMICOLON:      return "SEMICOLON";
        case TokenType::LEFT_BRACKET:   return "LEFT_BRACKET";
        case TokenType::RIGHT_BRACKET:  return "RIGHT_BRACKET";
        case TokenType::LEFT_PAREN:     return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN:    return "RIGHT_PAREN";
        case TokenType::MINUS:          return "MINUS";
        case TokenType::STAR:           return "STAR";
        case TokenType::PERCENT:        return "PERCENT";
        case TokenType::END_OF_FILE:    return "END_OF_FILE";
        default:                        return "";
    }
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
        case TokenType::PLUS:
        case TokenType::SLASH:
        case TokenType::EQUAL:
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
        case '+': addToken(TokenType::PLUS); return;
        case '/': addToken(TokenType::SLASH); return;
        case '*': addToken(TokenType::STAR); return;
        case '%': addToken(TokenType::PERCENT); return;

        // Longest match: "<=" is one token, not "<" followed by "=".
        case '<': addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS); return;
        case '>': addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER); return;
        // '=' assigns in DERIVE; a FILTER that uses it to compare is reported by the parser.
        case '=': addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL); return;
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
