// Unit tests for Parquet input: reading the footer (schema, row count and
// statistics), which contracts the statistics decide, and the generated reader.
// Run: build/Debug/test_parquet.exe  (it reads the files in tests/data/)

#include "backend/ExecutionPlanner.h"
#include "common/Diagnostics.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/PassManager.h"
#include "parser/Parser.h"
#include "schema/ParquetFooter.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

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

std::string dataFile(const std::string& name) {
    return std::string(PREPLANG_TEST_DATA) + "/" + name;
}

const ColumnInfo* column(const TableSchema& table, const std::string& name) {
    for (const ColumnInfo& info : table.columns) {
        if (info.name == name) {
            return &info;
        }
    }
    return nullptr;
}

struct Compiled {
    Diagnostics diagnostics;
    SymbolTable symbols;
    Program program;
    AnalysisResult analysis;
    IRProgram ir;
};

// Compiles `body`, placed after  LOAD "<tests/data/file>" AS s;
void compile(const std::string& file, const std::string& body, Compiled& c) {
    const std::string source = "LOAD \"" + dataFile(file) + "\" AS s;\n" + body;
    c.program = Parser(Lexer(source, c.diagnostics).tokenize(), c.diagnostics).parse();
    SchemaProvider schemas(c.diagnostics);
    c.analysis = SemanticAnalyzer(c.symbols, schemas, ".", c.diagnostics).analyze(c.program);
    if (!c.diagnostics.hasErrors()) {
        c.ir = IRBuilder(c.symbols, c.analysis).build(c.program);
        PassManager({true, false}).run(c.ir);
    }
}

// The verdict of the only contract, with its reason in `reason`.
bool verdictIs(const Compiled& c, Verdict verdict, const std::string& reasonPart = "") {
    return c.analysis.contracts.size() == 1 && c.analysis.contracts.front().verdict == verdict &&
           c.analysis.contracts.front().reason.find(reasonPart) != std::string::npos;
}

bool errorContains(const Compiled& c, const std::string& text) {
    for (const Diagnostic& d : c.diagnostics.all()) {
        if (d.severity == Severity::Error && d.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void testFooter() {
    std::string problem;
    const auto table = readParquetFooter(dataFile("students.parquet"), "s", problem);
    check(table && table->rowCount == 6 && table->columns.size() == 6 && table->format == TableFormat::Parquet &&
              table->fingerprint != 0,
          "the footer gives the row count and the columns");
    if (!table) {
        return;
    }
    const ColumnInfo* marks = column(*table, "marks");
    const ColumnInfo* age = column(*table, "age");
    const ColumnInfo* name = column(*table, "name");
    const ColumnInfo* passed = column(*table, "passed");
    check(marks && marks->type == ColumnType::Integer && marks->hasRange && marks->min == 35 && marks->max == 91 &&
              marks->boundsExact,
          "min and max are combined over three row groups, and pyarrow marks them exact");
    check(age && age->missingKnown && age->missingCount == 1, "null counts are added up over the row groups");
    check(name && name->type == ColumnType::String && name->missingKnown && name->missingCount == 0,
          "a string column with no nulls");
    check(passed && passed->type == ColumnType::Boolean, "a boolean column");
}

void testFloatNaN() {
    std::string problem;
    const auto table = readParquetFooter(dataFile("students.parquet"), "s", problem);
    const ColumnInfo* score = table ? column(*table, "score") : nullptr;
    check(score && score->type == ColumnType::Float && !score->missingKnown && score->hasRange && score->min == 0.1,
          "a float column may hold NaN, which pandas reads as missing, so its missing values are not known");
}

void testNoStatistics() {
    std::string problem;
    const auto table = readParquetFooter(dataFile("nostats.parquet"), "s", problem);
    const ColumnInfo* marks = table ? column(*table, "marks") : nullptr;
    check(marks && !marks->hasRange && !marks->missingKnown, "without statistics nothing is known about the values");
}

void testRequiredUnsigned() {
    std::string problem;
    const auto table = readParquetFooter(dataFile("required.parquet"), "s", problem);
    const ColumnInfo* n = table ? column(*table, "n") : nullptr;
    check(n && n->type == ColumnType::Integer && n->missingKnown && n->missingCount == 0,
          "a REQUIRED column has no missing values even without statistics");
    check(n && n->hasRange && n->max == 4000000000.0, "unsigned statistics are decoded as unsigned");
}

// A date or timestamp column is DATETIME: it can be kept, saved and checked for
// missing values, but there are no date values to compare it with or fill it with.
void testDates() {
    std::string problem;
    const auto table = readParquetFooter(dataFile("dates.parquet"), "s", problem);
    const ColumnInfo* day = table ? column(*table, "day") : nullptr;
    check(day && day->type == ColumnType::DateTime && !day->hasRange, "a date column is DATETIME, without a range");
    Compiled ok;
    compile("dates.parquet", "EXPECT s.day NOT_NULL;\nSAVE s AS \"o.parquet\";\n", ok);
    check(!ok.diagnostics.hasErrors() && verdictIs(ok, Verdict::Proven), "NOT_NULL on a DATETIME column is proven");
    Compiled compare;
    compile("dates.parquet", "FILTER s WHERE day > 3;\nSAVE s AS \"o.parquet\";\n", compare);
    check(errorContains(compare, "cannot compare DATETIME column"), "comparing a DATETIME column is a type error");
}

void testUnsupported() {
    std::string problem;
    check(!readParquetFooter(dataFile("nested.parquet"), "s", problem) && problem.find("nested") != std::string::npos,
          "a list column is reported as nested");
    problem.clear();
    check(!readParquetFooter(dataFile("broken.parquet"), "s", problem) && problem.find("damaged") != std::string::npos,
          "a damaged footer is an error, not a crash");
    problem.clear();
    check(!readParquetFooter(dataFile("make_parquet.py"), "s", problem) && problem.find("not a Parquet") != std::string::npos,
          "a file without the Parquet footer is rejected");
}

void testContracts() {
    Compiled proven;
    compile("students.parquet", "EXPECT s.marks BETWEEN 0 AND 100;\nSAVE s AS \"o.csv\";", proven);
    check(!proven.diagnostics.hasErrors() && verdictIs(proven, Verdict::Proven),
          "BETWEEN is proven from the footer statistics");

    Compiled violated;
    compile("students.parquet", "EXPECT s.marks BETWEEN 0 AND 90;\nSAVE s AS \"o.csv\";", violated);
    check(errorContains(violated, "according to the statistics in the footer"),
          "BETWEEN is violated when the exact statistics fall outside");

    Compiled bounds;
    compile("nostats.parquet", "EXPECT s.marks BETWEEN 0 AND 100;\nSAVE s AS \"o.csv\";", bounds);
    check(!bounds.diagnostics.hasErrors() && verdictIs(bounds, Verdict::Deferred), "without statistics BETWEEN is deferred");

    Compiled unique;
    compile("students.parquet", "EXPECT s.id UNIQUE;\nSAVE s AS \"o.csv\";", unique);
    check(!unique.diagnostics.hasErrors() && verdictIs(unique, Verdict::Deferred, "footer"),
          "UNIQUE needs the values, which are not read: deferred");

    Compiled notNull;
    compile("students.parquet", "EXPECT s.age NOT_NULL;\nSAVE s AS \"o.csv\";", notNull);
    check(errorContains(notNull, "1 missing value"), "NOT_NULL is violated by the footer's null count");

    Compiled nan;
    compile("students.parquet", "EXPECT s.score NOT_NULL;\nSAVE s AS \"o.csv\";", nan);
    check(!nan.diagnostics.hasErrors() && verdictIs(nan, Verdict::Deferred, "statistics"),
          "NOT_NULL on a float column without a NaN count is deferred");

    // score may have no missing values at all, so 5 need not appear after FILL_NULL:
    // reporting the contract as violated would be wrong.
    Compiled fill;
    compile("students.parquet", "FILL_NULL s.score WITH 5;\nEXPECT s.score BETWEEN 0 AND 1;\nSAVE s AS \"o.csv\";", fill);
    check(!fill.diagnostics.hasErrors() && verdictIs(fill, Verdict::Deferred),
          "a fill value that may not appear does not make a contract violated");
}

void testGeneratedReader() {
    Compiled c;
    compile("students.parquet", "SELECT s [name, marks];\nSAVE s AS \"o.parquet\";", c);
    const std::string python = ExecutionPlanner("output", "test.prep", Certificate{}).generate(c.ir, "");
    check(python.find("read_parquet_checked(") != std::string::npos &&
              python.find("def read_csv_checked") == std::string::npos,
          "the generated program reads Parquet, and has no unused CSV reader");
    check(python.find(".to_parquet(path, index=False)") != std::string::npos, "SAVE to a .parquet path writes Parquet");
    check(python.find("[\"name\", \"marks\"]") != std::string::npos, "only the live columns are read");
}

}  // namespace

int main() {
    std::cout << "Parquet tests\n";
    testFooter();
    testFloatNaN();
    testNoStatistics();
    testRequiredUnsigned();
    testDates();
    testUnsupported();
    testContracts();
    testGeneratedReader();
    std::cout << (failures == 0 ? "All Parquet tests passed.\n" : "Some Parquet tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
