// Unit tests for the parallel CSV scanner: the facts must not depend on how the
// file is cut into pieces, numbers must parse exactly like std::from_chars, and
// the first problem in the file must be reported with its line number.
// Run: build/Debug/test_scanner.exe

#include "schema/Checksum.h"
#include "schema/CsvScanner.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>

namespace {

int failures = 0;

void check(bool passed, const std::string& name) {
    std::cout << (passed ? "  PASS  " : "  FAIL  ") << name << '\n';
    if (!passed) {
        ++failures;
    }
}

bool sameColumns(const CsvScan& a, const CsvScan& b) {
    if (a.columns.size() != b.columns.size() || a.rowCount != b.rowCount || a.problem.line != b.problem.line ||
        a.problem.kind != b.problem.kind) {
        return false;
    }
    for (size_t i = 0; i < a.columns.size(); ++i) {
        const ColumnInfo& x = a.columns[i];
        const ColumnInfo& y = b.columns[i];
        if (x.name != y.name || x.type != y.type || x.missingCount != y.missingCount || x.hasRange != y.hasRange ||
            x.min != y.min || x.max != y.max || x.stringReason != y.stringReason) {
            return false;
        }
    }
    return true;
}

// A file of more than 1 MiB per thread, so it really is cut into pieces.
std::string bigFile(std::mt19937& random, size_t rows) {
    std::string text = "id,score,flag,mixed\n";
    std::uniform_real_distribution<double> score(-100, 100);
    for (size_t i = 0; i < rows; ++i) {
        char line[96];
        std::snprintf(line, sizeof line, "%zu,%.6f,%s,%zu\n", i, score(random), i % 3 ? "true" : "FALSE", i % 97);
        text += line;
        if (i % 50000 == 7) {
            text += "   \r\n";  // a blank line
        }
    }
    return text;
}

void testPiecesAgree() {
    std::cout << "Pieces give the same facts as one pass\n";
    std::mt19937 random(7);
    std::string text = bigFile(random, 300000);
    const CsvScan one = scanCsv(text, 1);
    for (unsigned threads : {2u, 3u, 8u, 16u}) {
        check(sameColumns(one, scanCsv(text, threads)), std::to_string(threads) + " threads = 1 thread");
    }
    check(one.rowCount == 300000, "300000 rows (blank lines skipped)");
    check(one.columns[0].type == ColumnType::Integer && one.columns[1].type == ColumnType::Float &&
              one.columns[2].type == ColumnType::Boolean,
          "INTEGER, FLOAT and BOOLEAN columns");
    check(one.fingerprint == crc32(text), "fingerprint is the CRC-32 of the bytes");

    // A late text cell makes 'mixed' STRING; the reason must name its real line,
    // even though it is found by a thread that started in the middle of the file.
    const size_t at = text.rfind("\n", text.size() - 2) + 1;  // start of the last line
    std::string late = text.substr(0, at) + "299999,1.5,true,oops\n";
    const CsvScan a = scanCsv(late, 1);
    const CsvScan b = scanCsv(late, 12);
    check(sameColumns(a, b), "late text cell: same facts with 12 threads");
    check(b.columns[3].type == ColumnType::String, "late text cell makes the column STRING");
    const std::int64_t lines = 1 + 300000 + 6;  // header, rows and 6 blank lines
    check(b.columns[3].stringReason == "line " + std::to_string(lines) + " has \"oops\"",
          "the reason names the last line: " + b.columns[3].stringReason);
}

void testProblems() {
    std::cout << "The first problem in the file is reported\n";
    std::mt19937 random(11);
    std::string text = bigFile(random, 200000);
    const size_t middle = text.find('\n', text.size() / 2) + 1;
    const size_t late = text.find('\n', text.size() * 9 / 10) + 1;
    std::string twoProblems = text;
    twoProblems.insert(late, "1,2,true,4,5\n");     // too many values, late in the file
    twoProblems.insert(middle, "1,2,\xFF,4\n");     // not UTF-8, earlier
    const CsvScan one = scanCsv(twoProblems, 1);
    const CsvScan many = scanCsv(twoProblems, 12);
    check(many.problem.kind == CsvProblem::Kind::NotUtf8 && many.problem.byte == 0xFF, "the earlier problem wins");
    check(one.problem.line == many.problem.line && one.problem.line > 2, "same line with 1 and 12 threads");

    check(scanCsv("").problem.kind == CsvProblem::Kind::Empty, "empty file");
    const CsvScan count = scanCsv("a,b\n1,2\n3\n");
    check(count.problem.kind == CsvProblem::Kind::FieldCount && count.problem.line == 3 && count.problem.fields == 1,
          "line 3 has 1 value");
}

void testCells() {
    std::cout << "Cells\n";
    const CsvScan scan = scanCsv("\xEF\xBB\xBF" "name, city ,n\r\n\"Doe, J\",\"say \"\"hi\"\"\",1\r\nAl,,-2e3\r\n");
    check(scan.columns[0].name == "name" && scan.columns[1].name == "city", "BOM removed, names trimmed");
    check(scan.columns[1].missingCount == 1 && scan.columns[1].type == ColumnType::String, "quoted cell, one missing");
    check(scan.columns[2].type == ColumnType::Float && scan.columns[2].min == -2000 && scan.columns[2].max == 1,
          "-2e3 is FLOAT, range [-2000, 1]");
    const CsvScan mix = scanCsv("v\n1\ntrue\n");
    check(mix.columns[0].type == ColumnType::String && mix.columns[0].stringReason == "line 3 has \"true\"",
          "number then boolean: STRING because of line 3");
    check(scanCsv("v\n.5\n").columns[0].type == ColumnType::String, ".5 is STRING: a number needs a digit before the point");
}

// Every number the fast path accepts must equal std::from_chars exactly.
void testNumbersExact() {
    std::cout << "Numbers parse exactly like std::from_chars\n";
    std::mt19937_64 random(3);
    const char* forms[] = {"%.0f", "%.1f", "%.3f", "%.7f", "%.12f", "%.15g", "%.17g", "%.4e", "%.9e"};
    int bad = 0;
    for (int i = 0; i < 20000; ++i) {
        const double magnitude = std::ldexp(static_cast<double>(random() % 1000000) / 1000.0, static_cast<int>(random() % 60) - 30);
        char cell[64];
        std::snprintf(cell, sizeof cell, forms[i % 9], (i % 2 ? -1 : 1) * magnitude);
        const std::string text = std::string("v\n") + cell + "\n";
        const CsvScan scan = scanCsv(text);
        double expected = 0;
        std::from_chars(cell, cell + std::char_traits<char>::length(cell), expected);
        if (scan.columns[0].hasRange && scan.columns[0].min != expected) {
            ++bad;
            std::cout << "    " << cell << '\n';
        }
    }
    check(bad == 0, "20000 random numbers");
    check(scanCsv("v\n12345678901234567890123\n").columns[0].min == 1.2345678901234568e22, "23 digits (slow path)");
    check(scanCsv("v\n1e-300\n").columns[0].min == 1e-300, "1e-300 (slow path)");
}

}  // namespace

int main() {
    testPiecesAgree();
    testProblems();
    testCells();
    testNumbersExact();
    std::cout << (failures == 0 ? "All scanner tests passed\n" : std::to_string(failures) + " scanner test(s) FAILED\n");
    return failures == 0 ? 0 : 1;
}
