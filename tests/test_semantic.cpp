// Unit tests for semantic analysis: names, types, flow of facts, and contracts.
// Run: build/Debug/test_semantic.exe  (it writes a small CSV file in the current folder)

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <fstream>
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

// age: INTEGER with one missing value, marks: INTEGER in [42, 91],
// city: STRING with one missing value, code: STRING because of "x7".
const char* const csvFile = "test_semantic_students.csv";
const char* const csvText = "name,age,marks,city,code\n"
                            "Asha,20,85,Chennai,10\n"
                            "Ravi,,42,,x7\n"
                            "Mina,22,91,Pune,12\n"
                            "Joe,21,60,Delhi,15\n";

struct Analysis {
    Diagnostics diagnostics;
    SymbolTable symbols;
    Program program;
    AnalysisResult result;
};

// Analyzes `body`, placed after:  LOAD "<test csv>" AS s;
void analyze(const std::string& body, Analysis& analysis) {
    const std::string source = "LOAD \"" + std::string(csvFile) + "\" AS s;\n" + body;
    analysis.program = Parser(Lexer(source, analysis.diagnostics).tokenize(), analysis.diagnostics).parse();
    SchemaProvider schemas(analysis.diagnostics);
    analysis.result = SemanticAnalyzer(analysis.symbols, schemas, ".", analysis.diagnostics).analyze(analysis.program);
}

// Whether there is an error or warning from `phase` whose message or hint contains `text`.
bool reported(const Analysis& analysis, Severity severity, Phase phase, const std::string& text) {
    for (const Diagnostic& d : analysis.diagnostics.all()) {
        const bool matches = d.message.find(text) != std::string::npos || d.hint.find(text) != std::string::npos;
        if (d.severity == severity && d.phase == phase && matches) {
            return true;
        }
    }
    return false;
}

// Whether analysis proved (some target of) a statement to have no effect.
bool hasNoEffect(const Analysis& analysis, size_t statementIndex) {
    const Stmt* statement = analysis.program.statements.at(statementIndex).get();
    for (const auto& [key, reason] : analysis.result.noEffect) {
        if (key.first == statement) {
            return true;
        }
    }
    return false;
}

ColumnState* column(Analysis& analysis, const std::string& name) {
    return analysis.symbols.lookup("s")->tables.front().column(name);
}

void testSchemaFacts() {
    Analysis a;
    analyze("SAVE s AS \"out.csv\";", a);
    const ColumnState* age = column(a, "age");
    const ColumnState* code = column(a, "code");
    check(!a.diagnostics.hasErrors() && age && age->type == ColumnType::Integer && age->nullable && age->exact &&
              age->range.min == 20 && age->range.max == 22,
          "LOAD records type, missing values and range from the CSV");
    check(code && code->type == ColumnType::String && code->stringReason.find("x7") != std::string::npos,
          "a column with a non-number becomes STRING and remembers why");
}

void testUndefinedDataset() {
    Analysis a;
    analyze("DROP_NULL t;", a);
    check(reported(a, Severity::Error, Phase::Semantic, "'t' is not defined"), "undefined dataset is an error");
}

void testRedefinedDataset() {
    Analysis a;
    analyze("LOAD \"" + std::string(csvFile) + "\" AS s;", a);
    check(reported(a, Severity::Error, Phase::Semantic, "already defined"), "loading the same name twice is an error");
}

void testUnknownColumnSuggestion() {
    Analysis a;
    analyze("NORMALIZE s.mark;", a);
    check(reported(a, Severity::Error, Phase::Semantic, "did you mean 'marks'?"), "unknown column suggests a close name");
}

void testNormalizeNeedsNumber() {
    Analysis a;
    analyze("NORMALIZE s.code;", a);
    check(reported(a, Severity::Error, Phase::Type, "x7"), "NORMALIZE on STRING is a type error that explains why");
}

void testComparisonTypes() {
    Analysis a;
    analyze("FILTER s WHERE marks > \"high\"; FILTER s WHERE city > \"A\";", a);
    check(reported(a, Severity::Error, Phase::Type, "cannot compare INTEGER"), "number column compared with text");
    check(reported(a, Severity::Error, Phase::Type, "operator '>' needs a numeric column"), "'>' on a STRING column");
}

void testFillValueType() {
    Analysis a;
    analyze("FILL_NULL s.age WITH 2.5;", a);
    check(reported(a, Severity::Error, Phase::Type, "needs a matching column type, but s.age is INTEGER"),
          "decimal cannot fill an INTEGER column");
}

void testUseAfterDrop() {
    Analysis a;
    analyze("DROP s [city];\nFILL_NULL s.city WITH \"none\";", a);
    check(reported(a, Severity::Error, Phase::Semantic, "was removed from 's' at line 2"),
          "using a dropped column reports where it was dropped");
}

void testContractProven() {
    Analysis a;
    analyze("EXPECT s.marks BETWEEN 0 AND 100;\nEXPECT s.name NOT_NULL;", a);
    check(!a.diagnostics.hasErrors() && hasNoEffect(a, 1) && hasNoEffect(a, 2),
          "contracts that the data satisfies are proven at compile time");
}

void testContractViolated() {
    Analysis a;
    analyze("EXPECT s.age NOT_NULL;\nEXPECT s.marks BETWEEN 50 AND 100;\nEXPECT s.code NUMBER;", a);
    check(reported(a, Severity::Error, Phase::Contract, "s.age has 1 missing value"), "NOT_NULL violated by the data");
    check(reported(a, Severity::Error, Phase::Contract, "from 42 to 91"), "BETWEEN violated by the data");
    check(reported(a, Severity::Error, Phase::Contract, "x7"), "type contract violated, with the reason");
}

void testContractDeferred() {
    Analysis a;
    analyze("FILTER s WHERE age > 20;\nEXPECT s.marks BETWEEN 50 AND 100;", a);
    check(!a.diagnostics.hasErrors() && !hasNoEffect(a, 2),
          "after a FILTER the data is not exact, so the contract is checked at run time");
    check(column(a, "marks")->range.min == 50, "a run-time contract narrows the known range");
}

void testNullabilityFlow() {
    Analysis a;
    analyze("DROP_NULL s;\nDROP_NULL s;\nFILL_NULL s.city WITH \"none\";", a);
    check(!hasNoEffect(a, 1), "first DROP_NULL removes rows");
    check(hasNoEffect(a, 2) && reported(a, Severity::Warning, Phase::Semantic, "DROP_NULL has no effect"),
          "second DROP_NULL has no effect");
    check(hasNoEffect(a, 3), "FILL_NULL after DROP_NULL has no effect");
}

void testFilterRefines() {
    Analysis a;
    analyze("FILTER s WHERE marks >= 60 AND age < 22;", a);
    const ColumnState* marks = column(a, "marks");
    const ColumnState* age = column(a, "age");
    check(marks->range.min == 60 && marks->range.max == 91 && !marks->exact, "FILTER narrows the range");
    check(!age->nullable && age->range.max == 22, "a compared column has no missing values after FILTER");
}

void testFilterAlwaysAndNever() {
    Analysis a;
    analyze("FILTER s WHERE marks > 0;\nFILTER s WHERE age > 0;\nFILTER s WHERE marks > 100;", a);
    check(hasNoEffect(a, 1), "a condition true for every value is removed");
    check(!hasNoEffect(a, 2), "not removed when the column has missing values, which never pass");
    check(reported(a, Severity::Warning, Phase::Semantic, "removes every row"), "a condition that is never true warns");
}

void testNeverSaved() {
    Analysis a;
    analyze("DROP_NULL s;", a);
    check(reported(a, Severity::Warning, Phase::Semantic, "never saved"), "a dataset that is never saved warns");
}

void testScientificNotation() {
    std::ofstream("test_semantic_floats.csv") << "x,y,z\n0.5,1e3,7\n6.4e-05,2E+2,1e\n,-3e0,8\n";
    Diagnostics diagnostics;
    const auto table = SchemaProvider(diagnostics).readTable("test_semantic_floats.csv", "f", {});
    const ColumnInfo& x = table->columns[0];
    const ColumnInfo& y = table->columns[1];
    check(x.type == ColumnType::Float && x.min == 6.4e-05 && x.max == 0.5 && x.missingCount == 1,
          "numbers written with an exponent (6.4e-05) are FLOAT");
    check(y.type == ColumnType::Float && y.min == -3 && y.max == 1000, "1e3, 2E+2 and -3e0 are FLOAT with the right range");
    check(table->columns[2].type == ColumnType::String, "an exponent without digits (1e) is text");
}

}  // namespace

int main() {
    std::ofstream(csvFile) << csvText;

    std::cout << "Semantic analysis tests\n";
    testSchemaFacts();
    testUndefinedDataset();
    testRedefinedDataset();
    testUnknownColumnSuggestion();
    testNormalizeNeedsNumber();
    testComparisonTypes();
    testFillValueType();
    testUseAfterDrop();
    testContractProven();
    testContractViolated();
    testContractDeferred();
    testNullabilityFlow();
    testFilterRefines();
    testFilterAlwaysAndNever();
    testNeverSaved();
    testScientificNotation();

    std::cout << (failures == 0 ? "All semantic tests passed.\n" : "Some semantic tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
