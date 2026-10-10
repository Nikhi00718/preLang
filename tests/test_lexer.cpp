// Unit tests for the lexer (lexical analysis).
// Run: build/Debug/test_lexer.exe

#include "common/Diagnostics.h"
#include "lexer/Lexer.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool passed, const std::string& name) {
    std::cout << (passed ? "  PASS  " : "  FAIL  ") << name << '\n';
    if (!passed) {
        ++failures;
    }
}

std::vector<TokenType> typesOf(const std::vector<Token>& tokens) {
    std::vector<TokenType> types;
    for (const Token& token : tokens) {
        types.push_back(token.type);
    }
    return types;
}

using T = TokenType;

void testStatement() {
    Diagnostics diagnostics;
    const auto tokens = Lexer("NORMALIZE students.age;", diagnostics).tokenize();
    check(typesOf(tokens) == std::vector<T>{T::NORMALIZE, T::IDENTIFIER, T::DOT, T::IDENTIFIER, T::SEMICOLON,
                                            T::END_OF_FILE},
          "keyword, identifiers and punctuation");
    check(!diagnostics.hasErrors(), "valid input gives no errors");
}

void testKeywordsIgnoreCase() {
    Diagnostics diagnostics;
    const auto tokens = Lexer("load Load LOAD", diagnostics).tokenize();
    check(typesOf(tokens) == std::vector<T>{T::LOAD, T::LOAD, T::LOAD, T::END_OF_FILE},
          "keywords are case-insensitive");
}

void testLongestMatch() {
    Diagnostics diagnostics;
    const auto tokens = Lexer("DROP_NULL DROP >= > == != <= <", diagnostics).tokenize();
    check(typesOf(tokens) == std::vector<T>{T::DROP_NULL, T::DROP, T::GREATER_EQUAL, T::GREATER, T::EQUAL_EQUAL,
                                            T::NOT_EQUAL, T::LESS_EQUAL, T::LESS, T::END_OF_FILE},
          "longest match: DROP_NULL and >= are single tokens");
}

void testNumbers() {
    Diagnostics diagnostics;
    const auto tokens = Lexer("42 92.5", diagnostics).tokenize();
    check(tokens[0].type == T::NUMBER_LITERAL && tokens[0].lexeme == "42", "integer literal");
    check(tokens[1].type == T::NUMBER_LITERAL && tokens[1].lexeme == "92.5", "decimal literal");
}

void testStringKeepsBackslashes() {
    Diagnostics diagnostics;
    const auto tokens = Lexer(R"("C:\data\students.csv")", diagnostics).tokenize();
    check(tokens[0].type == T::STRING_LITERAL && tokens[0].lexeme == R"(C:\data\students.csv)",
          "string literal is stored without quotes and keeps backslashes");
    check(tokens[0].length == 22, "string length includes both quotes");
}

void testQuotedIdentifier() {
    Diagnostics diagnostics;
    const auto tokens = Lexer("`First Name`", diagnostics).tokenize();
    check(tokens[0].type == T::IDENTIFIER && tokens[0].lexeme == "First Name", "backticks allow spaces in names");
}

void testCommentsAndLocations() {
    Diagnostics diagnostics;
    const auto tokens = Lexer("# a comment\n  SAVE", diagnostics).tokenize();
    check(tokens.size() == 2 && tokens[0].type == T::SAVE, "comments are skipped");
    check(tokens[0].location.line == 2 && tokens[0].location.column == 3, "line and column are tracked");
}

void testUnknownCharacter() {
    Diagnostics diagnostics;
    const auto tokens = Lexer("LOAD $ AS", diagnostics).tokenize();
    check(diagnostics.all().size() == 1 && diagnostics.all()[0].location.column == 6,
          "unknown character is reported at its column");
    check(typesOf(tokens) == std::vector<T>{T::LOAD, T::AS, T::END_OF_FILE}, "lexer continues after an error");
}

void testUnterminatedString() {
    Diagnostics diagnostics;
    Lexer("LOAD \"students.csv", diagnostics).tokenize();
    check(diagnostics.all().size() == 1 && diagnostics.all()[0].phase == Phase::Lexical,
          "unterminated string is a lexical error");
}

void testSingleEquals() {
    // '=' assigns in DERIVE; a FILTER that compares with it is a syntax error (see test_parser).
    Diagnostics diagnostics;
    const std::vector<Token> tokens = Lexer("DERIVE t.c = a + b / 2", diagnostics).tokenize();
    check(diagnostics.all().empty() && tokens[4].type == TokenType::EQUAL && tokens[6].type == TokenType::PLUS &&
              tokens[8].type == TokenType::SLASH,
          "'=', '+' and '/' are tokens for DERIVE");
}

}  // namespace

int main() {
    std::cout << "Lexer tests\n";
    testStatement();
    testKeywordsIgnoreCase();
    testLongestMatch();
    testNumbers();
    testStringKeepsBackslashes();
    testQuotedIdentifier();
    testCommentsAndLocations();
    testUnknownCharacter();
    testUnterminatedString();
    testSingleEquals();

    std::cout << (failures == 0 ? "All lexer tests passed.\n" : "Some lexer tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
