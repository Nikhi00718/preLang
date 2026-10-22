// Unit tests for semantic analysis: names, types, flow of facts, and contracts.
// Run: build/Debug/test_semantic.exe  (it writes a small CSV file in the current folder)

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <cmath>
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

void testFillStatistics() {
    Analysis a;
    analyze("FILL_NULL s.age WITH MEDIAN;\nFILL_NULL s.city WITH MODE;\nSAVE s AS \"o.csv\";", a);
    const ColumnState* age = column(a, "age");
    const ColumnState* city = column(a, "city");
    check(!a.diagnostics.hasErrors() && age && age->type == ColumnType::Float && !age->nullable &&
              age->range.min == 20 && age->range.max == 22,
          "MEDIAN fills an INTEGER column: FLOAT, no missing values, same range");
    check(city && city->type == ColumnType::String && !city->nullable, "MODE fills a STRING column");
    Analysis text;
    analyze("FILL_NULL s.city WITH MEAN;", text);
    check(reported(text, Severity::Error, Phase::Type, "FILL_NULL with MEAN needs a numeric column"),
          "MEAN of a STRING column is a type error");
}

void testDropNullColumns() {
    Analysis a;
    analyze("DROP_NULL s [age];\nSAVE s AS \"o.csv\";", a);
    const ColumnState* age = column(a, "age");
    const ColumnState* city = column(a, "city");
    check(!a.diagnostics.hasErrors() && age && !age->nullable && city && city->nullable && !city->exact,
          "DROP_NULL s [age]: age has no missing values, city still may, and rows changed");
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

// Excel saved the file as Windows-1252 (github.com/Faisalhakimi22/github-issue-classifier/issues/1).
void testNotUtf8() {
    std::ofstream("test_semantic_cp1252.csv", std::ios::binary) << "name,city\nAsha,Chennai\nZo\xE9,Paris\n";
    std::ofstream("test_semantic_utf8.csv", std::ios::binary) << "name,city\nZo\xC3\xA9,Paris\n";
    Diagnostics diagnostics;
    SchemaProvider schemas(diagnostics);
    const bool rejected = !schemas.readTable("test_semantic_cp1252.csv", "t", {});
    const bool accepted = schemas.readTable("test_semantic_utf8.csv", "u", {}).has_value();
    bool named = false;
    for (const Diagnostic& d : diagnostics.all()) {
        named = named || d.message.find("line 3 of 'test_semantic_cp1252.csv' is not UTF-8 text (byte 0xE9)") !=
                             std::string::npos;
    }
    check(rejected && named && accepted, "a CSV file that is not UTF-8 is reported with its line and byte");
}

// Dates: from 2024-03-30 22:00 to 2024-04-02 06:30, one missing.
void testDates() {
    std::ofstream("test_semantic_dates.csv") << "stamp,day,amount\n2024-03-30 22:00:00,2024-03-30,5\n"
                                                ",2024-03-31,7\n2024-04-02 06:30:00,2024-04-02,9\n";
    Analysis a;
    analyze("SAVE s AS \"o.csv\";\nLOAD \"test_semantic_dates.csv\" AS d;\nPARSE_DATE d.stamp;\nPARSE_DATE d.day;\n"
            "DERIVE d.year = YEAR(day);\nDERIVE d.month = MONTH(stamp);\nDERIVE d.dom = DAY(day);\n"
            "DERIVE d.hour = HOUR(stamp);\nDERIVE d.weekday = WEEKDAY(day);\nDERIVE d.cents = amount * 100 + 1;\n"
            "DERIVE d.half = amount / 2;\nFILTER d WHERE year == 2024;\nSAVE d AS \"d.csv\";",
            a);
    TableState& d = a.symbols.lookup("d")->tables.front();
    const auto range = [&](const std::string& name) { return d.column(name)->range; };
    check(!a.diagnostics.hasErrors() && d.column("stamp")->type == ColumnType::DateTime && d.column("stamp")->nullable,
          "PARSE_DATE makes a column of dates DATETIME; missing values stay missing");
    check(range("year").min == 2024 && range("year").max == 2024 && range("month").min == 3 &&
              range("month").max == 4 && range("dom").min == 1 && range("dom").max == 31,
          "YEAR and MONTH follow the dates; DAY spans 1-31 across a month boundary");
    check(range("hour").min == 0 && range("hour").max == 23 && range("weekday").min == 0 && range("weekday").max == 6,
          "HOUR across days is 0-23; WEEKDAY from Saturday to Tuesday wraps around the week, so 0-6");
    check(d.column("cents")->type == ColumnType::Integer && range("cents").min == 501 && range("cents").max == 901 &&
              d.column("half")->type == ColumnType::Float && range("half").min == 2.5,
          "interval arithmetic: INTEGER for + - *, FLOAT for /");
    check(hasNoEffect(a, 12), "a FILTER on a part of the dates can be proven to have no effect");
}

void testDateAndDivisionErrors() {
    std::ofstream("test_semantic_dates2.csv") << "when,size,zero\n2024-01-05,3,0\nsoon,4,2\n";
    Analysis a;
    analyze("SAVE s AS \"o.csv\";\nLOAD \"test_semantic_dates2.csv\" AS d;\nPARSE_DATE d.when;\n"
            "DERIVE d.r = size / zero;\nDERIVE d.q = size / (zero + 1);\nDERIVE d.h = HOUR(size);\n"
            "PARSE_DATE d.size;\nSAVE d AS \"d.csv\";",
            a);
    check(reported(a, Severity::Error, Phase::Contract, "row 2 has \"soon\""),
          "PARSE_DATE of a column with text that is not a date names the row");
    check(reported(a, Severity::Error, Phase::Contract, "divides by zero, which can be 0 here (it is in [0, 2])") &&
              !reported(a, Severity::Error, Phase::Contract, "divides by zero + 1"),
          "a division by a column that can be 0 is an error; by zero + 1 it is not");
    check(reported(a, Severity::Error, Phase::Type, "HOUR needs a DATETIME column") &&
              reported(a, Severity::Error, Phase::Type, "PARSE_DATE needs a STRING column"),
          "date parts need DATETIME, and PARSE_DATE needs text");
}

void testDeriveReplacesAndDefines() {
    Analysis a;
    analyze("DERIVE s.marks = marks - 40;\nDERIVE s.total = marks + age;\nEXPECT s.total NOT_NULL;\nSAVE s AS \"o.csv\";", a);
    const ColumnState* marks = column(a, "marks");
    const ColumnState* total = column(a, "total");
    check(!a.diagnostics.hasErrors() && marks && marks->range.min == 2 && marks->range.max == 51 && !marks->exact,
          "DERIVE can replace a column; the new values are not exact");
    bool deferred = false;
    for (const ContractRecord& contract : a.result.contracts) {
        deferred = deferred || (contract.contract == "s.total NOT_NULL" && contract.verdict == Verdict::Deferred);
    }
    check(total && deferred, "a derived value may be missing where an operand is (age has a missing value)");
}

// Lookup tables for JOIN: zones by city; one with a repeated city, one whose city is a
// number, one that also has a marks column.
const char* const lookupFiles[][2] = {
    {"test_semantic_zones.csv", "city,zone,pin\nChennai,South,600001\nPune,West,411001\nDelhi,North,110001\n"},
    {"test_semantic_zones_repeated.csv", "city,zone\nChennai,South\nPune,West\nChennai,East\n"},
    {"test_semantic_zones_numbers.csv", "city,zone\n1,South\n2,West\n"},
    {"test_semantic_zones_marks.csv", "city,marks\nChennai,1\n"},
};

void testJoin() {
    Analysis a;
    analyze("LOAD \"test_semantic_zones.csv\" AS z;\nJOIN s WITH z ON city;\nSAVE s AS \"out.csv\";", a);
    const ColumnState* zone = column(a, "zone");
    const ColumnState* pin = column(a, "pin");
    check(!a.diagnostics.hasErrors() && zone && zone->type == ColumnType::String && zone->nullable && pin &&
              pin->type == ColumnType::Integer && pin->range.min == 110001 && pin->range.max == 600001,
          "JOIN adds the lookup columns with their types and ranges, missing where no key matches");
    check(!reported(a, Severity::Warning, Phase::Semantic, "never saved"),
          "a lookup table read by JOIN need not be saved");
}

void testJoinErrors() {
    Analysis repeated;
    analyze("LOAD \"test_semantic_zones_repeated.csv\" AS z;\nJOIN s WITH z ON city;\nSAVE s AS \"o.csv\";", repeated);
    check(reported(repeated, Severity::Error, Phase::Contract, "\"Chennai\" is in rows 1 and 3"),
          "a key that repeats in the lookup file is an error, with its rows");
    Analysis types;
    analyze("LOAD \"test_semantic_zones_numbers.csv\" AS z;\nJOIN s WITH z ON city;\nSAVE s AS \"o.csv\";", types);
    check(reported(types, Severity::Error, Phase::Type, "STRING column s.city with INTEGER column z.city"),
          "keys of different types are a type error");
    Analysis both;
    analyze("LOAD \"test_semantic_zones_marks.csv\" AS z;\nJOIN s WITH z ON city;\nSAVE s AS \"o.csv\";", both);
    check(reported(both, Severity::Error, Phase::Semantic, "column 'marks' is in both s and z"),
          "a column in both tables is an error, not renamed");
}

void testAggregate() {
    Analysis a;
    analyze("AGGREGATE s BY [city] COUNT AS n, SUM marks AS total, MEAN age AS average, MAX marks AS top;\n"
            "SAVE s AS \"out.csv\";",
            a);
    const ColumnState* city = column(a, "city");
    const ColumnState* n = column(a, "n");
    const ColumnState* total = column(a, "total");
    const ColumnState* average = column(a, "average");
    const ColumnState* top = column(a, "top");
    check(!a.diagnostics.hasErrors() && city && !city->nullable && n && n->type == ColumnType::Integer &&
              !n->nullable && n->range.min == 1 && std::isinf(n->range.max),
          "AGGREGATE: BY columns are never missing, COUNT is an INTEGER of at least 1");
    check(total && total->type == ColumnType::Integer && !total->nullable && total->range.min == 0 &&
              std::isinf(total->range.max),
          "SUM keeps the type and is at least 0 when every value is");
    check(average && average->type == ColumnType::Float && average->nullable && average->range.min == 20 &&
              average->range.max == 22 && top && top->range.min == 42 && top->range.max == 91 && !column(a, "marks"),
          "MEAN is FLOAT within the range, missing like its column; MAX keeps the range; other columns go");
    Analysis text;
    analyze("AGGREGATE s BY [city] MEAN name AS x;\nSAVE s AS \"out.csv\";", text);
    check(reported(text, Severity::Error, Phase::Type, "MEAN needs a numeric column, but 'name' is STRING"),
          "MEAN of a text column is a type error");
}

}  // namespace

int main() {
    std::ofstream(csvFile) << csvText;
    for (const auto& [file, text] : lookupFiles) {
        std::ofstream(file) << text;
    }

    std::cout << "Semantic analysis tests\n";
    testSchemaFacts();
    testUndefinedDataset();
    testRedefinedDataset();
    testUnknownColumnSuggestion();
    testNormalizeNeedsNumber();
    testComparisonTypes();
    testFillValueType();
    testFillStatistics();
    testDropNullColumns();
    testUseAfterDrop();
    testContractProven();
    testContractViolated();
    testContractDeferred();
    testNullabilityFlow();
    testFilterRefines();
    testFilterAlwaysAndNever();
    testNeverSaved();
    testScientificNotation();
    testNotUtf8();
    testDates();
    testDateAndDivisionErrors();
    testDeriveReplacesAndDefines();
    testJoin();
    testJoinErrors();
    testAggregate();

    std::cout << (failures == 0 ? "All semantic tests passed.\n" : "Some semantic tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
