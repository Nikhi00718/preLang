// Unit tests for the parser (syntax analysis and AST construction).
// Run: build/Debug/test_parser.exe

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool passed, const std::string& name) {
    std::cout << (passed ? "  PASS  " : "  FAIL  ") << name << '\n';
    if (!passed) {
        ++failures;
    }
}

Program parse(const std::string& source, Diagnostics& diagnostics) {
    return Parser(Lexer(source, diagnostics).tokenize(), diagnostics).parse();
}

// The statement at `index` as type T, or nullptr if it is missing or another type.
template <typename T>
const T* statementAs(const Program& program, size_t index) {
    if (index >= program.statements.size()) {
        return nullptr;
    }
    return dynamic_cast<const T*>(program.statements[index].get());
}

// The condition of the first statement, if it is a FILTER.
const Expr* filterCondition(const Program& program) {
    const auto* filter = statementAs<FilterStmt>(program, 0);
    return filter ? filter->condition.get() : nullptr;
}

void testAllStatements() {
    Diagnostics diagnostics;
    const Program program = parse(R"(
        LOAD "students.csv" AS s;
        EXPECT s.marks BETWEEN 0 AND 100;
        FILL_NULL s.age WITH 0;
        DROP_NULL s;
        NORMALIZE s.age;
        FILTER s WHERE marks >= 50;
        DROP s [phone];
        SELECT s [name, age];
        SAVE s AS "clean.csv";
    )", diagnostics);

    check(!diagnostics.hasErrors() && program.statements.size() == 9, "all 9 statement types parse");
    check(statementAs<LoadStmt>(program, 0) && statementAs<ExpectStmt>(program, 1) &&
              statementAs<FillNullStmt>(program, 2) && statementAs<DropNullStmt>(program, 3) &&
              statementAs<NormalizeStmt>(program, 4) && statementAs<FilterStmt>(program, 5) &&
              statementAs<DropStmt>(program, 6) && statementAs<SelectStmt>(program, 7) &&
              statementAs<SaveStmt>(program, 8),
          "each statement becomes the right AST node");
}

void testLoadFields() {
    Diagnostics diagnostics;
    const Program program = parse(R"(LOAD "students.csv" AS students;)", diagnostics);
    const auto* load = statementAs<LoadStmt>(program, 0);
    check(load && load->path == "students.csv" && load->dataset == "students", "LOAD stores file and dataset");
}

void testNormalizeMethod() {
    Diagnostics diagnostics;
    const Program program = parse("NORMALIZE s.age; NORMALIZE s.marks USING MINMAX;", diagnostics);
    const auto* first = statementAs<NormalizeStmt>(program, 0);
    const auto* second = statementAs<NormalizeStmt>(program, 1);
    check(first && first->method == NormalizeMethod::ZScore, "NORMALIZE uses ZSCORE by default");
    check(second && second->method == NormalizeMethod::MinMax && second->target.column == "marks",
          "NORMALIZE ... USING MINMAX");
}

void testExpectBetween() {
    Diagnostics diagnostics;
    const Program program = parse("EXPECT s.temp BETWEEN -5 AND 40.5;", diagnostics);
    const auto* expect = statementAs<ExpectStmt>(program, 0);
    check(expect && expect->constraint == ConstraintKind::Between && expect->low == "-5" && expect->high == "40.5",
          "EXPECT BETWEEN with negative and decimal bounds");
}

void testFillNullString() {
    Diagnostics diagnostics;
    const Program program = parse(R"(FILL_NULL s.city WITH "UNKNOWN";)", diagnostics);
    const auto* fill = statementAs<FillNullStmt>(program, 0);
    check(fill && fill->value.kind == LiteralKind::String && fill->value.value == "UNKNOWN",
          "FILL_NULL with a string value");
}

void testPrecedence() {
    Diagnostics diagnostics;
    const Program program = parse("FILTER s WHERE a > 1 OR b > 2 AND c > 3;", diagnostics);
    const auto* root = dynamic_cast<const LogicalExpr*>(filterCondition(program));
    const auto* right = root ? dynamic_cast<const LogicalExpr*>(root->right.get()) : nullptr;
    check(root && root->op == LogicalOp::Or && right && right->op == LogicalOp::And,
          "AND binds tighter than OR");
}

void testParentheses() {
    Diagnostics diagnostics;
    const Program program = parse("FILTER s WHERE (a > 1 OR b > 2) AND c > 3;", diagnostics);
    const auto* root = dynamic_cast<const LogicalExpr*>(filterCondition(program));
    const auto* left = root ? dynamic_cast<const LogicalExpr*>(root->left.get()) : nullptr;
    check(root && root->op == LogicalOp::And && left && left->op == LogicalOp::Or,
          "parentheses override precedence");
}

void testNot() {
    Diagnostics diagnostics;
    const Program program = parse("FILTER s WHERE NOT passed == TRUE;", diagnostics);
    const auto* notExpr = dynamic_cast<const NotExpr*>(filterCondition(program));
    const auto* comparison = notExpr ? dynamic_cast<const ComparisonExpr*>(notExpr->operand.get()) : nullptr;
    check(comparison && comparison->column == "passed" && comparison->value.kind == LiteralKind::Boolean,
          "NOT wraps a comparison");
}

void testColumnList() {
    Diagnostics diagnostics;
    const Program program = parse("SELECT s [name, `First Name`, age];", diagnostics);
    const auto* select = statementAs<SelectStmt>(program, 0);
    check(select && select->columns.size() == 3 && select->columns[1] == "First Name",
          "SELECT column list with a quoted name");
}

void testMissingSemicolon() {
    Diagnostics diagnostics;
    const Program program = parse("LOAD \"a.csv\" AS s\nSAVE s AS \"b.csv\";", diagnostics);
    const bool oneError = diagnostics.all().size() == 1;
    check(oneError && diagnostics.all()[0].location.line == 1 && diagnostics.all()[0].location.column == 18,
          "missing ';' is reported right after the last token of the line");
    check(statementAs<SaveStmt>(program, 0) != nullptr, "the next statement is still parsed");
}

void testRecovery() {
    Diagnostics diagnostics;
    const Program program = parse(R"(
        LOAD "a.csv" AS s;
        NORMALIZE s.;
        SELECT s [name,];
        SAVE s AS "b.csv";
    )", diagnostics);
    check(diagnostics.all().size() == 2, "two broken statements give two errors");
    check(program.statements.size() == 2, "the two valid statements are kept");
}

void testTypoHint() {
    Diagnostics diagnostics;
    parse("NORMALISE s.age;", diagnostics);
    check(diagnostics.all().size() == 1 && diagnostics.all()[0].hint == "did you mean 'NORMALIZE'?",
          "misspelled keyword gets a suggestion");
}

void testKeywordAsName() {
    Diagnostics diagnostics;
    parse("SELECT s [name, number];", diagnostics);
    check(diagnostics.all().size() == 1 && diagnostics.all()[0].hint.find("`number`") != std::string::npos,
          "keyword used as a column name suggests backticks");
}

}  // namespace

int main() {
    std::cout << "Parser tests\n";
    testAllStatements();
    testLoadFields();
    testNormalizeMethod();
    testExpectBetween();
    testFillNullString();
    testPrecedence();
    testParentheses();
    testNot();
    testColumnList();
    testMissingSemicolon();
    testRecovery();
    testTypoHint();
    testKeywordAsName();

    std::cout << (failures == 0 ? "All parser tests passed.\n" : "Some parser tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
