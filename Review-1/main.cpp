#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// PrepLang Review 1
// ------------------
// A small C++17 compiler front end:
//   1. lexer  - source text to tokens
//   2. parser - tokens to recognised statements
//
// Review 1 deliberately stops here.  It does not build an AST, read CSV data,
// perform semantic checks, or generate target code.

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

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string value;
    std::string lexeme;
    int line = 1;
    int column = 1;
};

struct Diagnostic {
    std::string phase;
    std::string problem;
    int line = 0;
    int column = 0;
    std::string expected;
    std::string found;
    std::string hint;
};

class ErrorReporter {
public:
    explicit ErrorReporter(const std::string& source) : source_(source) {}

    void report(Diagnostic diagnostic) {
        errors_.push_back(std::move(diagnostic));
    }

    bool ok() const { return errors_.empty(); }
    const std::vector<Diagnostic>& errors() const { return errors_; }

    std::string render() const {
        std::ostringstream out;
        for (std::size_t i = 0; i < errors_.size(); ++i) {
            if (i > 0) out << "\n\n" << std::string(60, '-') << "\n\n";
            const Diagnostic& error = errors_[i];
            out << error.phase;
            if (error.line > 0) out << " - Line " << error.line;
            out << "\n\nProblem:\n    " << error.problem;

            if (error.line > 0) {
                std::string line = sourceLine(error.line);
                out << "\n\nSource:\n    " << line;
                if (error.column > 0) {
                    out << "\n    " << std::string(static_cast<std::size_t>(error.column - 1), ' ') << '^';
                }
            }
            if (!error.expected.empty()) out << "\n\nExpected:\n    " << error.expected;
            if (!error.found.empty()) out << "\n\nFound:\n    " << error.found;
            if (!error.hint.empty()) out << "\n\nHint:\n    " << error.hint;
        }
        return out.str();
    }

    std::string summary() const {
        return std::to_string(errors_.size()) + " error(s)";
    }

private:
    std::string sourceLine(int wantedLine) const {
        if (wantedLine < 1) return "";

        std::size_t start = 0;
        for (int line = 1; line < wantedLine; ++line) {
            std::size_t newline = source_.find('\n', start);
            if (newline == std::string::npos) return "";
            start = newline + 1;
        }

        std::size_t end = source_.find('\n', start);
        if (end == std::string::npos) end = source_.size();
        if (end > start && source_[end - 1] == '\r') --end;
        return source_.substr(start, end - start);
    }

    std::string source_;
    std::vector<Diagnostic> errors_;
};

std::string upper(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string quoteText(const std::string& text) {
    return "'" + text + "'";
}

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

const std::vector<std::string> STATEMENT_KEYWORDS = {
    "LOAD", "DROP_NULL", "FILL_NULL", "NORMALIZE", "FILTER",
    "SELECT", "DROP", "SAVE", "EXPECT"
};

class Lexer {
public:
    Lexer(const std::string& source, ErrorReporter& reporter)
        : source_(source), reporter_(reporter) {}

    std::vector<Token> tokenize() {
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
                    "PrepLang identifiers may only use letters, digits and '_'."
                });
            }
        }

        tokens_.push_back({TokenType::EndOfFile, "", "<eof>", line_, column_});
        return tokens_;
    }

private:
    bool atEnd() const { return position_ >= source_.size(); }

    char peek(int offset = 0) const {
        std::size_t position = position_ + static_cast<std::size_t>(offset);
        return position < source_.size() ? source_[position] : '\0';
    }

    char advance() {
        char character = source_[position_++];
        if (character == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return character;
    }

    bool match(char expected) {
        if (peek() != expected) return false;
        advance();
        return true;
    }

    void add(TokenType type, const std::string& value, std::size_t start, int line, int column) {
        tokens_.push_back({type, value, source_.substr(start, position_ - start), line, column});
    }

    void skipComment() {
        if (peek() == '-') {
            advance();
            advance();
        }
        while (!atEnd() && peek() != '\n') advance();
    }

    void scanWord(int line, int column) {
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

    void scanNumber(int line, int column) {
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
                "A numeric literal cannot be followed directly by letters."
            });
            return;
        }

        add(TokenType::NumberLiteral, source_.substr(start, position_ - start), start, line, column);
    }

    void scanString(int line, int column) {
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
                    "Unterminated string literal (newline reached).",
                    line,
                    column,
                    std::string("a closing ") + quoteCharacter + " on the same line",
                    "",
                    "String literals in PrepLang cannot span multiple lines."
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
                    "Unterminated string literal (newline reached).",
                    line,
                    column,
                    std::string("a closing ") + quoteCharacter + " on the same line",
                    "",
                    "String literals in PrepLang cannot span multiple lines."
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
            "Unterminated string literal (end of file reached).",
            line,
            column,
            std::string("a closing ") + quoteCharacter,
            "",
            ""
        });
    }

    void scanOperator(int line, int column) {
        std::size_t start = position_;
        char first = advance();
        if (match('=')) {
            // The second character is part of the same operator.
        }

        std::string operatorText = source_.substr(start, position_ - start);
        if (operatorText == ">" || operatorText == "<" || operatorText == ">=" ||
            operatorText == "<=" || operatorText == "==" || operatorText == "!=") {
            add(TokenType::RelationalOperator, operatorText, start, line, column);
            return;
        }

        reporter_.report({
            "LEXICAL ERROR",
            "Invalid operator " + quoteText(operatorText) + ".",
            line,
            column,
            "one of >  <  >=  <=  ==  !=",
            quoteText(operatorText),
            first == '=' ? "Use '==' for equality comparison." : ""
        });
    }

    void scanPunctuation(int line, int column) {
        char character = advance();
        TokenType type = TokenType::Dot;
        if (character == ',') type = TokenType::Comma;
        if (character == ';') type = TokenType::Semicolon;
        if (character == '[') type = TokenType::LeftBracket;
        if (character == ']') type = TokenType::RightBracket;
        std::string value(1, character);
        add(type, value, position_ - 1, line, column);
    }

    const std::string& source_;
    ErrorReporter& reporter_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
    std::vector<Token> tokens_;
};

struct StatementInfo {
    std::string kind;
    int line = 0;
    std::vector<std::pair<std::string, std::string>> details;
};

class Parser {
public:
    Parser(const std::vector<Token>& tokens, ErrorReporter& reporter)
        : tokens_(tokens), reporter_(reporter) {}

    std::vector<StatementInfo> parse() {
        while (!check(TokenType::EndOfFile)) {
            std::size_t before = position_;
            StatementInfo statement;
            if (parseStatement(statement)) {
                statements_.push_back(std::move(statement));
            } else {
                synchronize();
            }
            if (position_ == before && !check(TokenType::EndOfFile)) advance();
        }
        return statements_;
    }

private:
    const Token& current() const {
        static const Token eof;
        if (tokens_.empty()) return eof;
        return tokens_[std::min(position_, tokens_.size() - 1)];
    }

    Token advance() {
        Token token = current();
        if (token.type != TokenType::EndOfFile && position_ < tokens_.size()) ++position_;
        return token;
    }

    bool check(TokenType type) const { return current().type == type; }

    bool checkKeyword(const std::string& keyword) const {
        return current().type == TokenType::Keyword && current().value == keyword;
    }

    bool isStatementKeyword() const {
        if (current().type != TokenType::Keyword) return false;
        return std::find(STATEMENT_KEYWORDS.begin(), STATEMENT_KEYWORDS.end(), current().value) !=
               STATEMENT_KEYWORDS.end();
    }

    std::string describe(const Token& token) const {
        if (token.type == TokenType::EndOfFile) return "end of file";
        return quoteText(token.lexeme) + " (" + tokenTypeName(token.type) + ")";
    }

    void syntaxError(const std::string& expected, const std::string& hint = "") {
        const Token& token = current();
        reporter_.report({
            "SYNTAX ERROR",
            "Unexpected " + describe(token) + ".",
            token.line,
            token.column,
            expected,
            describe(token),
            hint
        });
    }

    bool expect(TokenType type, const std::string& expected, Token& result) {
        if (check(type)) {
            result = advance();
            return true;
        }
        syntaxError(expected);
        return false;
    }

    bool expectKeyword(const std::string& keyword, Token& result) {
        if (checkKeyword(keyword)) {
            result = advance();
            return true;
        }
        syntaxError("keyword " + keyword);
        return false;
    }

    bool parseStatement(StatementInfo& statement) {
        if (!isStatementKeyword()) {
            syntaxError(
                "a statement keyword: LOAD, DROP_NULL, FILL_NULL, NORMALIZE, FILTER, SELECT, DROP, SAVE, EXPECT",
                "Every PrepLang statement begins with a keyword and ends with ';'."
            );
            return false;
        }

        const std::string keyword = current().value;
        if (keyword == "LOAD") return parseLoad(statement);
        if (keyword == "DROP_NULL") return parseDropNull(statement);
        if (keyword == "FILL_NULL") return parseFillNull(statement);
        if (keyword == "NORMALIZE") return parseNormalize(statement);
        if (keyword == "FILTER") return parseFilter(statement);
        if (keyword == "SELECT") return parseSelect(statement);
        if (keyword == "DROP") return parseDrop(statement);
        if (keyword == "SAVE") return parseSave(statement);
        return parseExpect(statement);
    }

    bool parseLoad(StatementInfo& statement) {
        Token start, path, asToken, name;
        if (!expectKeyword("LOAD", start)) return false;
        if (!expect(TokenType::StringLiteral, "a quoted file path, e.g. \"students.csv\"", path)) return false;
        if (!expectKeyword("AS", asToken)) return false;
        if (!expect(TokenType::Identifier, "a dataset name", name)) return false;
        if (!terminator()) return false;
        statement = {"LOAD", start.line, {{"file", path.value}, {"dataset", name.value}}};
        return true;
    }

    bool parseDropNull(StatementInfo& statement) {
        Token start, name;
        if (!expectKeyword("DROP_NULL", start)) return false;
        if (!expect(TokenType::Identifier, "a dataset name", name)) return false;
        if (!terminator()) return false;
        statement = {"DROP_NULL", start.line, {{"dataset", name.value}}};
        return true;
    }

    bool parseFillNull(StatementInfo& statement) {
        Token start;
        if (!expectKeyword("FILL_NULL", start)) return false;
        std::string dataset, column, value;
        if (!columnReference(dataset, column)) return false;
        Token ignored;
        if (!expectKeyword("WITH", ignored)) return false;
        if (!literal(value)) return false;
        if (!terminator()) return false;
        statement = {"FILL_NULL", start.line, {{"dataset", dataset}, {"column", column}, {"value", value}}};
        return true;
    }

    bool parseNormalize(StatementInfo& statement) {
        Token start;
        if (!expectKeyword("NORMALIZE", start)) return false;
        std::string dataset, column;
        if (!columnReference(dataset, column)) return false;
        if (!terminator()) return false;
        statement = {"NORMALIZE", start.line, {{"dataset", dataset}, {"column", column}}};
        return true;
    }

    bool parseFilter(StatementInfo& statement) {
        Token start, dataset, column, operation;
        if (!expectKeyword("FILTER", start)) return false;
        if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
        Token ignored;
        if (!expectKeyword("WHERE", ignored)) return false;
        if (!expect(TokenType::Identifier, "a column name", column)) return false;
        if (!expect(TokenType::RelationalOperator,
                    "a relational operator (>, <, >=, <=, ==, !=)", operation)) return false;
        std::string value;
        if (!literal(value)) return false;
        if (!terminator()) return false;
        statement = {"FILTER", start.line,
                     {{"dataset", dataset.value}, {"column", column.value},
                      {"op", operation.value}, {"value", value}}};
        return true;
    }

    bool parseSelect(StatementInfo& statement) {
        Token start, dataset;
        if (!expectKeyword("SELECT", start)) return false;
        if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
        std::vector<std::string> columns;
        if (!identifierList(columns)) return false;
        if (!terminator()) return false;
        statement = {"SELECT", start.line, {{"dataset", dataset.value}, {"columns", join(columns)}}};
        return true;
    }

    bool parseDrop(StatementInfo& statement) {
        Token start, dataset;
        if (!expectKeyword("DROP", start)) return false;
        if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
        std::vector<std::string> columns;
        if (!identifierList(columns)) return false;
        if (!terminator()) return false;
        statement = {"DROP", start.line, {{"dataset", dataset.value}, {"columns", join(columns)}}};
        return true;
    }

    bool parseSave(StatementInfo& statement) {
        Token start, dataset, asToken, path;
        if (!expectKeyword("SAVE", start)) return false;
        if (!expect(TokenType::Identifier, "a dataset name", dataset)) return false;
        if (!expectKeyword("AS", asToken)) return false;
        if (!expect(TokenType::StringLiteral, "a quoted output path, e.g. \"clean.csv\"", path)) return false;
        if (!terminator()) return false;
        statement = {"SAVE", start.line, {{"dataset", dataset.value}, {"file", path.value}}};
        return true;
    }

    bool parseExpect(StatementInfo& statement) {
        Token start;
        if (!expectKeyword("EXPECT", start)) return false;
        std::string dataset, column;
        if (!columnReference(dataset, column)) return false;

        std::vector<std::pair<std::string, std::string>> details = {
            {"dataset", dataset}, {"column", column}
        };
        if (checkKeyword("NUMBER") || checkKeyword("STRING") || checkKeyword("BOOLEAN")) {
            Token type = advance();
            details.push_back({"contract", "TYPE"});
            details.push_back({"type", type.value});
        } else if (checkKeyword("NOT_NULL")) {
            advance();
            details.push_back({"contract", "NOT_NULL"});
        } else if (checkKeyword("BETWEEN")) {
            advance();
            Token low, high, ignored;
            if (!expect(TokenType::NumberLiteral, "a numeric lower bound", low)) return false;
            if (!expectKeyword("AND", ignored)) return false;
            if (!expect(TokenType::NumberLiteral, "a numeric upper bound", high)) return false;
            details.push_back({"contract", "RANGE"});
            details.push_back({"low", low.value});
            details.push_back({"high", high.value});
        } else {
            syntaxError("an expectation: NUMBER | STRING | BOOLEAN | NOT_NULL | BETWEEN n AND m");
            return false;
        }

        if (!terminator()) return false;
        statement = {"EXPECT", start.line, std::move(details)};
        return true;
    }

    bool columnReference(std::string& dataset, std::string& column) {
        Token datasetToken, dot, columnToken;
        if (!expect(TokenType::Identifier, "a dataset name", datasetToken)) return false;
        if (!expect(TokenType::Dot, "'.' between the dataset and the column name", dot)) return false;
        if (!expect(TokenType::Identifier, "a column name", columnToken)) return false;
        dataset = datasetToken.value;
        column = columnToken.value;
        return true;
    }

    bool identifierList(std::vector<std::string>& columns) {
        Token ignored;
        if (!expect(TokenType::LeftBracket, "'[' before the column list", ignored)) return false;
        if (check(TokenType::RightBracket)) {
            syntaxError("at least one column name", "An empty column list is not allowed.");
            return false;
        }

        while (true) {
            Token column;
            if (!expect(TokenType::Identifier, "a column name", column)) return false;
            columns.push_back(column.value);
            if (!check(TokenType::Comma)) break;
            advance();
        }
        return expect(TokenType::RightBracket, "']' after the column list", ignored);
    }

    bool literal(std::string& value) {
        if (check(TokenType::NumberLiteral) || check(TokenType::StringLiteral) ||
            check(TokenType::BooleanLiteral)) {
            value = advance().value;
            return true;
        }
        syntaxError("a literal value (number, quoted string, TRUE or FALSE)");
        return false;
    }

    bool terminator() {
        if (check(TokenType::Semicolon)) {
            advance();
            return true;
        }
        syntaxError("';' at the end of the statement");
        return false;
    }

    void synchronize() {
        while (!check(TokenType::EndOfFile)) {
            if (check(TokenType::Semicolon)) {
                advance();
                return;
            }
            if (isStatementKeyword()) return;
            advance();
        }
    }

    static std::string join(const std::vector<std::string>& values) {
        std::string result = "[";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i > 0) result += ", ";
            result += values[i];
        }
        return result + "]";
    }

    const std::vector<Token>& tokens_;
    ErrorReporter& reporter_;
    std::size_t position_ = 0;
    std::vector<StatementInfo> statements_;
};

std::string formatTokenTable(const std::vector<Token>& tokens) {
    std::ostringstream out;
    out << std::left << std::setw(10) << "LINE:COL"
        << std::setw(20) << "LEXEME"
        << std::setw(22) << "TOKEN TYPE" << "VALUE\n";
    out << std::string(70, '-') << '\n';

    for (const Token& token : tokens) {
        std::string position = std::to_string(token.line) + ":" + std::to_string(token.column);
        std::string value = token.value;
        if (token.type == TokenType::StringLiteral) value = quoteText(value);
        out << std::left << std::setw(10) << position
            << std::setw(20) << token.lexeme
            << std::setw(22) << tokenTypeName(token.type) << value << '\n';
    }
    out << std::string(70, '-') << '\n' << tokens.size() << " tokens";
    return out.str();
}

std::string formatStatementTable(const std::vector<StatementInfo>& statements) {
    if (statements.empty()) return "(no statements recognised)";

    std::ostringstream out;
    out << std::left << std::setw(4) << "#" << std::setw(7) << "LINE"
        << std::setw(14) << "STATEMENT" << "DETAILS\n";
    out << std::string(70, '-') << '\n';
    for (std::size_t i = 0; i < statements.size(); ++i) {
        const StatementInfo& statement = statements[i];
        out << std::left << std::setw(4) << i + 1 << std::setw(7) << statement.line
            << std::setw(14) << statement.kind;
        for (std::size_t j = 0; j < statement.details.size(); ++j) {
            if (j > 0) out << ", ";
            out << statement.details[j].first << "=" << statement.details[j].second;
        }
        out << '\n';
    }
    out << std::string(70, '-') << '\n'
        << statements.size() << " statement(s) recognised";
    return out.str();
}

bool compileSource(const std::string& source, const std::string& name, bool showTokens) {
    std::cout << "\n============================================================\n"
              << "  PrepLang Review 1 Compiler -- " << name
              << "\n============================================================\n";

    std::cout << "\n--- SOURCE PROGRAM -----------------------------------------\n";
    std::istringstream input(source);
    std::string line;
    int lineNumber = 1;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::cout << std::right << std::setw(3) << lineNumber++ << " | " << line << '\n';
    }

    ErrorReporter reporter(source);
    std::vector<Token> tokens = Lexer(source, reporter).tokenize();
    if (showTokens) {
        std::cout << "\n--- PHASE 1: LEXICAL ANALYSIS (TOKENS) ---------------------\n"
                  << formatTokenTable(tokens) << '\n';
    }

    std::vector<StatementInfo> statements = Parser(tokens, reporter).parse();
    std::cout << "\n--- PHASE 2: SYNTAX ANALYSIS (RECOGNISED STATEMENTS) -------\n"
              << formatStatementTable(statements) << '\n';

    std::cout << "\n--- DIAGNOSTICS --------------------------------------------\n";
    if (reporter.ok()) {
        std::cout << "OK  No lexical or syntax errors detected.\n"
                  << "    " << tokens.size() - 1 << " tokens, " << statements.size()
                  << " statements.\n";
        return true;
    }

    std::cout << reporter.render() << "\n\n"
              << "Compilation failed: " << reporter.summary() << '\n';
    return false;
}

bool runFile(const std::filesystem::path& path, bool showTokens) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Unable to read " << path.string() << '\n';
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return compileSource(buffer.str(), path.filename().string(), showTokens);
}

int runDemo() {
    std::vector<std::filesystem::path> examples;
    for (const auto& entry : std::filesystem::directory_iterator("examples")) {
        if (entry.path().extension() == ".prep") examples.push_back(entry.path());
    }
    std::sort(examples.begin(), examples.end());

    if (examples.empty()) {
        std::cerr << "No examples found. Run this command from Review-1/.\n";
        return 1;
    }

    int failures = 0;
    for (const auto& example : examples) {
        std::string name = example.filename().string();
        bool expectedError = name.find("error") != std::string::npos;
        bool valid = runFile(example, name == "demo_valid.prep");
        if (valid == expectedError) ++failures;
    }

    std::cout << "\n============================================================\n"
              << "  DEMO SUMMARY\n"
              << "============================================================\n"
              << examples.size() << " example programs executed, "
              << failures << " unexpected result(s).\n";
    return failures == 0 ? 0 : 1;
}

void printUsage() {
    std::cout << "PrepLang Review 1\n\n"
              << "Usage:\n"
              << "  preplang <file.prep>\n"
              << "  preplang -c \"NORMALIZE students.age;\"\n"
              << "  preplang --demo\n\n"
              << "Options:\n"
              << "  --no-tokens   hide the token table\n"
              << "  --help        show this help\n";
}

}  // namespace prep

int main(int argc, char* argv[]) {
    bool demo = false;
    bool noTokens = false;
    std::string command;
    std::string file;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--demo") {
            demo = true;
        } else if (argument == "--no-tokens") {
            noTokens = true;
        } else if (argument == "--help" || argument == "-h") {
            prep::printUsage();
            return 0;
        } else if (argument == "-c" || argument == "--command") {
            if (i + 1 >= argc) {
                std::cerr << "Missing source after " << argument << '\n';
                return 1;
            }
            command = argv[++i];
        } else if (file.empty()) {
            file = argument;
        } else {
            std::cerr << "Unexpected argument: " << argument << '\n';
            return 1;
        }
    }

    if (demo) return prep::runDemo();
    if (!command.empty()) return prep::compileSource(command, "<command line>", !noTokens) ? 0 : 1;
    if (!file.empty()) return prep::runFile(file, !noTokens) ? 0 : 1;

    prep::printUsage();
    return 0;
}
