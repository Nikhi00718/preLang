// Unit tests for the parser (syntax analysis and AST construction).
// Run: build/Debug/test_parser.exe

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"

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
    check(second && second->method == NormalizeMethod::MinMax && second->target.parts.back() == "marks",
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

void testFillNullStatistic() {
    Diagnostics diagnostics;
    const Program program = parse("FILL_NULL s.age WITH MEDIAN;\nDROP_NULL s [age, city];", diagnostics);
    const auto* fill = statementAs<FillNullStmt>(program, 0);
    const auto* drop = statementAs<DropNullStmt>(program, 1);
    check(fill && fill->method == FillMethod::Median, "FILL_NULL ... WITH MEDIAN");
    check(drop && drop->columns == std::vector<std::string>{"age", "city"}, "DROP_NULL with a column list");
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

void testWildcardSelector() {
    Diagnostics diagnostics;
    const Program program =
        parse("NORMALIZE company.*.* WHERE TYPE NUMERIC EXCEPT [id, year] USING MINMAX;"
              "FILL_NULL company.*.age WITH 0 IF_EXISTS;", diagnostics);
    const auto* normalize = statementAs<NormalizeStmt>(program, 0);
    const auto* fill = statementAs<FillNullStmt>(program, 1);
    check(normalize && normalize->target.parts == std::vector<std::string>{"company", "*", "*"} &&
              normalize->target.filter == SelectorFilter::Numeric &&
              normalize->target.except == std::vector<std::string>{"id", "year"} &&
              normalize->method == NormalizeMethod::MinMax,
          "wildcard selector with WHERE TYPE and EXCEPT");
    check(fill && fill->target.ifExists && fill->target.text() == "company.*.age", "IF_EXISTS on a selector");
}

void testImageStatements() {
    Diagnostics diagnostics;
    const Program program = parse(R"(
        LOAD_IMAGES "data/" AS imgs RECURSIVE;
        RESIZE imgs TO 224 224 KEEP_ASPECT;
        NORMALIZE imgs RANGE 0 1;
        CONVERT imgs TO jpg;
        COMPRESS imgs QUALITY 80;
        SAVE_IMAGES imgs TO "out/";
    )", diagnostics);
    const auto* load = statementAs<LoadStmt>(program, 0);
    const auto* resize = statementAs<ResizeStmt>(program, 1);
    const auto* normalize = statementAs<NormalizeStmt>(program, 2);
    const auto* convert = statementAs<ConvertStmt>(program, 3);
    const auto* save = statementAs<SaveStmt>(program, 5);
    check(!diagnostics.hasErrors() && load && load->kind == DataKind::ImageSet && load->recursive,
          "LOAD_IMAGES ... RECURSIVE");
    check(resize && resize->width == 224 && resize->height == 224 && resize->keepAspect, "RESIZE ... KEEP_ASPECT");
    check(normalize && normalize->method == NormalizeMethod::Range && normalize->high == "1", "NORMALIZE ... RANGE");
    check(convert && convert->format == "JPG", "CONVERT format is case-insensitive");
    check(save && save->kind == DataKind::ImageSet && save->path == "out/", "SAVE_IMAGES ... TO");
}

void testDatasetContracts() {
    Diagnostics diagnostics;
    const Program program = parse(R"(
        EXPECT orders.customer_id REFERENCES customers.id;
        EXPECT train SCHEMA_SAME_AS test EXCEPT [label];
        LINK imgs WITH labels ON filename AS training;
        EXPECT training MATCH_RATE 99.5%;
    )", diagnostics);
    const auto* references = statementAs<ExpectStmt>(program, 0);
    const auto* schema = statementAs<ExpectStmt>(program, 1);
    const auto* link = statementAs<LinkStmt>(program, 2);
    const auto* rate = statementAs<ExpectStmt>(program, 3);
    check(references && references->constraint == ConstraintKind::References &&
              references->other.text() == "customers.id",
          "REFERENCES names the key column");
    check(schema && schema->constraint == ConstraintKind::SchemaSameAs && schema->except.size() == 1,
          "SCHEMA_SAME_AS ... EXCEPT");
    check(link && link->images == "imgs" && link->column == "filename" && link->dataset == "training", "LINK");
    check(rate && rate->constraint == ConstraintKind::MatchRate && rate->low == "99.5", "MATCH_RATE with %");
}

void testWholeNumbers() {
    Diagnostics diagnostics;
    parse("RESIZE imgs TO 22.5 224;", diagnostics);
    check(diagnostics.all().size() == 1 && diagnostics.all()[0].message.find("whole number") != std::string::npos,
          "sizes must be whole numbers");
}

}  // namespace

void testDerive() {
    Diagnostics diagnostics;
    const Program program = parse("DERIVE t.c = a + b * 2 - (c - d) / 4;\nDERIVE t.h = hour(stamp);\n"
                                  "DERIVE ts.x.n = -a - -3;\nPARSE_DATE t.stamp;\n",
                                  diagnostics);
    const auto* first = statementAs<DeriveStmt>(program, 0);
    const auto* second = statementAs<DeriveStmt>(program, 1);
    const auto* third = statementAs<DeriveStmt>(program, 2);
    check(!diagnostics.hasErrors() && first && second && third && statementAs<ParseDateStmt>(program, 3),
          "DERIVE and PARSE_DATE parse");
    check(first && valueText(*first->value) == "a + b * 2 - (c - d) / 4" && first->value->text == "-",
          "* and / bind tighter than + and -, both left to right");
    check(second && second->value->kind == ValueKind::DatePart && second->value->text == "HOUR" &&
              second->value->left->text == "stamp",
          "a date part is a name followed by '(' (any case), not a keyword");
    check(third && third->target.parts.size() == 3 && valueText(*third->value) == "0 - a - -3",
          "unary minus: -3 is a number, -a is 0 - a");
}

void testDeriveErrors() {
    Diagnostics unknown;
    parse("DERIVE t.c = sqrt(a);", unknown);
    Diagnostics noColumn;
    parse("DERIVE t = 1;", noColumn);
    Diagnostics compare;
    parse("FILTER t WHERE a = 1;", compare);
    check(unknown.hasErrors() && unknown.all()[0].message.find("unknown function 'sqrt'") != std::string::npos,
          "an unknown function is named");
    check(noColumn.hasErrors(), "DERIVE needs table.column");
    check(compare.hasErrors() && compare.all()[0].hint.find("==") != std::string::npos,
          "a FILTER comparing with '=' is told to use '=='");
}

int main() {
    std::cout << "Parser tests\n";
    testAllStatements();
    testLoadFields();
    testNormalizeMethod();
    testExpectBetween();
    testFillNullString();
    testFillNullStatistic();
    testPrecedence();
    testParentheses();
    testNot();
    testColumnList();
    testMissingSemicolon();
    testRecovery();
    testTypoHint();
    testKeywordAsName();
    testWildcardSelector();
    testImageStatements();
    testDatasetContracts();
    testWholeNumbers();
    testDerive();
    testDeriveErrors();

    std::cout << (failures == 0 ? "All parser tests passed.\n" : "Some parser tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
