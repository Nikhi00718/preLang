// Unit tests for private compilation (schema/ReleasedFacts.h): the compiler builds its
// state from the data owner's released facts and never opens an input file; nothing
// it derives from them is exact (no contract is reported violated, contracts that need
// the values are checked at run time); and the property that makes noisy facts safe:
// whatever facts it is given, true or false, every removal an input does not deoptimize
// is a no-effect fact of that input's own analysis, so the generated program computes
// what the source program computes.
// Run: build/Debug/test_private.exe  (it writes small CSV and facts files in the current folder)

#include "backend/PandasCodegen.h"
#include "certify/Certifier.h"
#include "common/Diagnostics.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/PassManager.h"
#include "parser/Parser.h"
#include "schema/ReleasedFacts.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool passed, const std::string& name) {
    std::cout << (passed ? "  PASS  " : "  FAIL  ") << name << '\n';
    if (!passed) {
        ++failures;
    }
}

void writeFile(const std::string& name, const std::string& text) {
    std::ofstream(name, std::ios::binary) << text;
}

struct Compiled {
    Diagnostics diagnostics;
    SymbolTable symbols;
    Program program;
    AnalysisResult analysis;
    IRProgram ir;
    Certificate certificate;
};

// Compiles `source` from the data (no facts file) or privately, from `factsFile`.
void compile(const std::string& source, Compiled& c, const std::string& factsFile = "") {
    c.program = Parser(Lexer(source, c.diagnostics).tokenize(), c.diagnostics).parse();
    SchemaProvider schemas(c.diagnostics);
    if (!factsFile.empty()) {
        std::string problem;
        std::optional<ReleasedFacts> facts = readReleasedFacts(factsFile, ".", problem);
        if (!facts) {
            c.diagnostics.error(Phase::Schema, {}, problem);
            return;
        }
        schemas.setReleased(std::move(facts->tables));
    }
    c.analysis = SemanticAnalyzer(c.symbols, schemas, ".", c.diagnostics).analyze(c.program);
    if (c.diagnostics.hasErrors()) {
        return;
    }
    c.ir = IRBuilder(c.symbols, c.analysis).build(c.program);
    const std::vector<double> constants = programConstants(c.ir);
    PassManager({true, false, true}).run(c.ir);
    c.certificate = Certifier(c.program, schemas, ".").certify(c.ir, c.analysis, constants);
}

bool hasMessage(const Compiled& c, Severity severity, const std::string& text) {
    return std::any_of(c.diagnostics.all().begin(), c.diagnostics.all().end(), [&](const Diagnostic& d) {
        return d.severity == severity && d.message.find(text) != std::string::npos;
    });
}

const ContractRecord* contract(const Compiled& c, int line) {
    for (const ContractRecord& record : c.analysis.contracts) {
        if (record.line == line) {
            return &record;
        }
    }
    return nullptr;
}

const char* const header = "preplang-facts\t1\nmechanism\ttest\ntable\ttest_private_t.csv\ts\tcsv\n";

void testReadFacts() {
    std::cout << "Reading a facts file\n";
    writeFile("test_private_facts.tsv", std::string(header) +
                                            "column\tname\tSTRING\t0\t-inf\t0\tinf\t0\t-\t-\t-\t-\n"
                                            "column\tage\tINTEGER\t?\t-inf\t0\tinf\t0\t-\t-\t-\t-\n"
                                            "column\tmarks\tINTEGER\t1\t0\t1\t100\t0\t0\t<=\t100\t>\n");
    std::string problem;
    const std::optional<ReleasedFacts> facts = readReleasedFacts("test_private_facts.tsv", ".", problem);
    check(facts && facts->tables.size() == 1, "one table is read" + (problem.empty() ? "" : ": " + problem));
    if (!facts) {
        return;
    }
    const TableSchema& table = facts->tables.begin()->second;
    check(facts->tables.begin()->first == SchemaProvider::overrideKey("test_private_t.csv", "s"),
          "it is keyed like the LOAD that reads it");
    check(table.released && !table.valuesKnown && table.fingerprint == 0, "it is released, inexact, without a checksum");
    const ColumnInfo& name = table.columns[0];
    const ColumnInfo& age = table.columns[1];
    const ColumnInfo& marks = table.columns[2];
    check(name.missingKnown && name.missingCount == 0 && !name.seenMissing, "0: no missing values");
    check(!age.missingKnown && !age.seenMissing, "?: missing values unknown");
    check(!marks.missingKnown && marks.seenMissing, "1: missing values seen");
    check(marks.min > 0 && marks.min == std::nextafter(0.0, 1.0) && marks.max == 100 && !marks.boundsExact,
          "a strict lower bound (> 0) becomes the next double; bounds are not exact");
    check(marks.seenBelow == 0 && marks.seenBelowOrAt && marks.seenAbove == 100 && !marks.seenAboveOrAt,
          "the evidence: values <= 0 and values > 100 were seen");
    check(!age.hasRange, "a numeric column without bounds has no range");

    writeFile("test_private_bad.tsv", "preplang-facts\t1\ncolumn\tx\tINTEGER\t0\t0\t0\t1\t0\t-\t-\t-\t-\n");
    check(!readReleasedFacts("test_private_bad.tsv", ".", problem), "a column before any table is an error");
    writeFile("test_private_bad.tsv", "not a facts file\n");
    check(!readReleasedFacts("test_private_bad.tsv", ".", problem), "a file without the header is an error");
}

void testCompilesWithoutTheFile() {
    std::cout << "A private compilation opens no input file\n";
    std::remove("test_private_t.csv");  // the data is not here at all
    writeFile("test_private_facts.tsv", std::string(header) +
                                            "column\tname\tSTRING\t0\t-inf\t0\tinf\t0\t-\t-\t-\t-\n"
                                            "column\tage\tINTEGER\t?\t-inf\t0\tinf\t0\t-\t-\t-\t-\n"
                                            "column\tmarks\tINTEGER\t0\t0\t0\t100\t0\t-\t-\t-\t-\n");
    Compiled c;
    compile("LOAD \"test_private_t.csv\" AS s;\n"         // line 1
            "EXPECT s.marks BETWEEN 0 AND 100;\n"         // line 2: proven from the facts
            "EXPECT s.marks UNIQUE;\n"                    // line 3: needs the values: checked at run time
            "FILL_NULL s.marks WITH 0;\n"                 // line 4: no missing values: removed
            "FILL_NULL s.age WITH 0;\n"                   // line 5: may have missing values: kept
            "SAVE s AS \"test_private_out.csv\";\n",
            c, "test_private_facts.tsv");
    check(!c.diagnostics.hasErrors(), "it compiles although the file does not exist");
    check(contract(c, 2) && contract(c, 2)->verdict == Verdict::Proven, "a range contract is proven from the facts");
    check(contract(c, 3) && contract(c, 3)->verdict == Verdict::Deferred &&
              contract(c, 3)->reason.find("private compilation") != std::string::npos,
          "UNIQUE is checked at run time: the values are not known");
    const auto speculative = [&](int line, Opcode op) {
        return std::any_of(c.ir.code.begin(), c.ir.code.end(), [&](const Instruction& in) {
            return in.line == line && in.op == op && in.speculative;
        });
    };
    check(speculative(2, Opcode::Check) && speculative(4, Opcode::FillNull),
          "the proven check and the useless fill are removed speculatively, not for good");
    check(c.certificate.item("2:1:s..marks") && !c.certificate.item("2:1:s..marks")->empty(),
          "the removed check has an item certificate that the generated program checks");
    check(std::none_of(c.ir.code.begin(), c.ir.code.end(),
                       [](const Instruction& in) { return in.line == 5 && in.speculative; }),
          "the fill of a column that may have missing values is kept");
    check(hasMessage(c, Severity::Warning, "FILL_NULL probably has no effect: by the released facts"),
          "the analyst is told the fill is probably useless, not that it is");
    const std::string reader = PandasCodegen::helpers(true, true);
    check(reader.find("fingerprint is not None and (zlib.crc32(data) == fingerprint or TRUST_INPUTS)") !=
                  std::string::npos &&
              reader.find("fingerprint is not None and (zlib.crc32(footer) == fingerprint or TRUST_INPUTS)") !=
                  std::string::npos,
          "without a checksum no input is trusted, not even with PREPLANG_TRUST_INPUTS");

    Compiled missing;
    compile("LOAD \"test_private_other.csv\" AS s;\nSAVE s AS \"test_private_out.csv\";\n", missing,
            "test_private_facts.tsv");
    check(hasMessage(missing, Severity::Error, "the facts file has no table"),
          "a table without released facts is an error, not read from disk");
}

void testNothingExact() {
    std::cout << "Facts that say a contract is broken give a warning, never an error\n";
    // marks: values above 100 and missing names were seen.
    writeFile("test_private_facts.tsv", std::string(header) +
                                            "column\tname\tSTRING\t1\t-inf\t0\tinf\t0\t-\t-\t-\t-\n"
                                            "column\tage\tINTEGER\t0\t-inf\t0\tinf\t0\t-\t-\t-\t-\n"
                                            "column\tmarks\tINTEGER\t0\t0\t0\tinf\t0\t-\t-\t100\t>\n");
    Compiled c;
    compile("LOAD \"test_private_t.csv\" AS s;\n"   // line 1
            "EXPECT s.marks BETWEEN 0 AND 100;\n"   // line 2
            "EXPECT s.name NOT_NULL;\n"             // line 3
            "FILTER s WHERE age > 18;\n"            // line 4
            "EXPECT s.marks BETWEEN 0 AND 100;\n"   // line 5: rows were removed: no warning
            "SAVE s AS \"test_private_out.csv\";\n",
            c, "test_private_facts.tsv");
    check(!c.diagnostics.hasErrors(), "no contract is reported violated");
    check(contract(c, 2)->verdict == Verdict::Deferred && contract(c, 3)->verdict == Verdict::Deferred,
          "both contracts are checked at run time");
    check(hasMessage(c, Severity::Warning, "EXPECT s.marks BETWEEN 0 AND 100 is likely violated") &&
              hasMessage(c, Severity::Warning, "EXPECT s.name NOT_NULL is likely violated"),
          "the analyst is warned that both are likely violated");
    const auto warnings = std::count_if(c.diagnostics.all().begin(), c.diagnostics.all().end(), [](const Diagnostic& d) {
        return d.message.find("likely violated") != std::string::npos;
    });
    check(warnings == 2, "no warning after a FILTER: the released facts describe the file, not the rows left");
}

// A refusal is something the analyst sees. If a proof from released facts could make the
// generated program refuse a batch, an analyst who chooses the program's numbers could learn a
// bit about a row from every submission. So no released fact may enter the admission
// certificate: a removed check keeps its column loaded and can always run again.
void testNoReleasedFactDecidesAdmission() {
    std::cout << "No released fact decides whether a batch is refused\n";
    writeFile("test_private_facts.tsv", std::string(header) +
                                            "column\tname\tSTRING\t0\t-inf\t0\tinf\t0\t-\t-\t-\t-\n"
                                            "column\tage\tINTEGER\t?\t-inf\t0\tinf\t0\t-\t-\t-\t-\n"
                                            "column\tmarks\tINTEGER\t0\t0\t0\t100\t0\t-\t-\t-\t-\n");
    Compiled c;
    compile("LOAD \"test_private_t.csv\" AS s;\n"   // line 1
            "EXPECT s.name NOT_NULL;\n"             // line 2: proven from the facts
            "SELECT s [age];\n"                     // name is not used again
            "SAVE s AS \"test_private_out.csv\";\n",
            c, "test_private_facts.tsv");
    const bool speculative = std::any_of(c.ir.code.begin(), c.ir.code.end(), [](const Instruction& in) {
        return in.op == Opcode::Check && in.line == 2 && in.speculative;
    });
    const bool loaded = std::any_of(c.ir.code.begin(), c.ir.code.end(), [](const Instruction& in) {
        return in.op == Opcode::Load && std::find(in.columns.begin(), in.columns.end(), "name") != in.columns.end();
    });
    check(speculative && loaded, "the check of a column the program drops stays, able to run again");
    const TableCertificate* admission = c.certificate.table("s");
    const bool needsName = admission && std::any_of(admission->columns.begin(), admission->columns.end(),
                                                    [](const ColumnFacts& f) { return f.name == "name" && f.notNull; });
    check(admission && !needsName, "admission does not require the released fact (no missing names)");
}

void testQueries() {
    std::cout << "What the data owner is asked\n";
    TableSchema schema;
    schema.file = "data/s.csv";
    for (const auto& [name, type] : std::vector<std::pair<std::string, ColumnType>>{
             {"name", ColumnType::String}, {"marks", ColumnType::Integer}, {"city", ColumnType::String}}) {
        ColumnInfo column;
        column.name = name;
        column.type = type;
        schema.columns.push_back(column);
    }
    std::ostringstream out;
    writeQueries(out, {{"s.csv", "s", schema, {"marks"}, {"name"}}}, {-5, 0, 100});
    const std::string text = out.str();
    check(text.rfind("preplang-queries\t1\nconstants\t-5\t0\t100\n", 0) == 0, "the program's numbers");
    check(text.find("table\ts.csv\ts\tcsv\t") != std::string::npos, "each LOAD with its path as written");
    check(text.find("column\tmarks\tINTEGER\trange\n") != std::string::npos &&
              text.find("column\tname\tSTRING\tmissing\n") != std::string::npos &&
              text.find("column\tcity\tSTRING\tnone\n") != std::string::npos,
          "the schema, and per column: bounds, only missing values, or nothing asked");
}

// ---------- The compiler depends on the data only through occupied cells ----------

// What a compilation decided, without its messages (which quote values): contract verdicts,
// the no-effect proofs behind removals, which instructions are speculative, and the certificate.
std::string decisions(const Compiled& c) {
    std::ostringstream out;
    for (const ContractRecord& contract : c.analysis.contracts) {
        out << contract.line << ' ' << contract.contract << ' ' << static_cast<int>(contract.verdict) << '\n';
    }
    std::set<std::string> removed;  // keyed by statement pointers, so in no fixed order
    for (const auto& [key, reason] : c.analysis.noEffect) {
        removed.insert(proofKey(*key.first, key.second));
    }
    for (const std::string& key : removed) {
        out << "removed " << key << '\n';
    }
    for (const Instruction& in : c.ir.code) {
        out << in.line << ' ' << static_cast<int>(in.op) << (in.speculative ? " speculative" : "") << '\n';
    }
    printCertificate(out, c.certificate);
    for (const Diagnostic& d : c.diagnostics.all()) {
        out << "diagnostic line " << d.location.line << ' ' << static_cast<int>(d.severity) << '\n';
    }
    return out.str();
}

// The numbers of a program cut a column into cells: below the first, each number, between two,
// above the last. Two tables whose columns occupy the same cells (and both have missing values
// or neither) get the same decisions: the analysis compares bounds only with those numbers.
// Proved for the core in proofs/Cells.lean (cells_decide); tested here for the compiler.
void testCellsDecide() {
    std::cout << "Tables with the same occupied cells get the same decisions\n";
    const std::string program = "LOAD \"test_private_cells.csv\" AS s;\n"
                                "EXPECT s.marks BETWEEN 0 AND 100;\n"
                                "FILL_NULL s.age WITH 0;\n"
                                "FILTER s WHERE marks >= 40 AND age < 30;\n"
                                "EXPECT s.name NOT_NULL;\n"
                                "DROP_NULL s;\n"
                                "SAVE s AS \"test_private_out.csv\";\n";
    const std::vector<int> anchors = {0, 30, 40, 100};
    std::mt19937 random(7);
    const auto pick = [&](int n) { return static_cast<int>(random() % static_cast<unsigned>(n)); };
    // An integer value inside cell i: cells are (-inf,0), {0}, (0,30), {30}, (30,40), {40}, (40,100), {100}, (100,inf).
    const auto valueIn = [&](int cell) {
        if (cell % 2 == 1) {
            return anchors[static_cast<size_t>(cell / 2)];
        }
        const int low = cell == 0 ? anchors.front() - 50 : anchors[static_cast<size_t>(cell / 2 - 1)] + 1;
        const int high = cell / 2 == static_cast<int>(anchors.size()) ? anchors.back() + 50
                                                                      : anchors[static_cast<size_t>(cell / 2)] - 1;
        return low + pick(high - low + 1);
    };
    const int cells = 2 * static_cast<int>(anchors.size()) + 1;
    int same = 0, different = 0;
    for (int round = 0; round < 200; ++round) {
        // Which cells each numeric column occupies (at least one), and which columns have missing values.
        std::vector<std::vector<int>> occupied(2);
        for (auto& column : occupied) {
            for (int cell = 0; cell < cells; ++cell) {
                if (pick(3) == 0) {
                    column.push_back(cell);
                }
            }
            if (column.empty()) {
                column.push_back(pick(cells));
            }
        }
        const bool missingAge = pick(2), missingMarks = pick(2), missingName = pick(3) == 0;
        // Two different tables with these cells: other values, other row counts, other orders.
        const auto table = [&]() {
            std::vector<std::vector<std::string>> columns(3);
            for (size_t col = 0; col < 2; ++col) {
                for (int cell : occupied[col]) {
                    for (int n = 1 + pick(3); n > 0; --n) {
                        columns[col].push_back(std::to_string(valueIn(cell)));
                    }
                }
                if (col == 0 ? missingAge : missingMarks) {
                    columns[col].push_back("");
                }
            }
            const size_t rows = std::max(columns[0].size(), columns[1].size()) + static_cast<size_t>(pick(3));
            for (size_t col = 0; col < 2; ++col) {
                const size_t own = columns[col].size();
                while (columns[col].size() < rows) {
                    columns[col].push_back(columns[col][static_cast<size_t>(pick(static_cast<int>(own)))]);
                }
                std::shuffle(columns[col].begin(), columns[col].end(), random);
            }
            for (size_t i = 0; i < rows; ++i) {
                columns[2].push_back(missingName && i == 0 ? "" : "N" + std::to_string(pick(1000)));
            }
            std::string text = "name,age,marks\n";
            for (size_t i = 0; i < rows; ++i) {
                text += columns[2][i] + "," + columns[0][i] + "," + columns[1][i] + "\n";
            }
            return text;
        };
        writeFile("test_private_cells.csv", table());
        Compiled first;
        compile(program, first);
        writeFile("test_private_cells.csv", table());
        Compiled second;
        compile(program, second);
        (decisions(first) == decisions(second) ? same : different)++;
    }
    check(different == 0, std::to_string(same) + " pairs of different tables with the same occupied cells, " +
                              std::to_string(different) + " with different decisions");
}

// ---------- Any facts are safe ----------

std::set<std::string> removable(const Compiled& c) {
    std::set<std::string> keys;
    for (const auto& [key, reason] : c.analysis.noEffect) {
        keys.insert(proofKey(*key.first, key.second));
    }
    return keys;
}

// Is the private program's run on an input as the source program's run on it? `again` is the
// input compiled from its own data. Every compile error of `again` must be a contract the
// private program checks (kept, or deoptimized); before the first one, every removal the
// input does not deoptimize must be a no-effect fact of the input itself.
bool sameRun(const Compiled& base, const Compiled& again, const std::set<std::string>& deopts) {
    int stop = 1 << 30;
    for (const Diagnostic& error : again.diagnostics.all()) {
        if (error.severity != Severity::Error) {
            continue;
        }
        const int line = error.location.line;
        const bool checked = std::any_of(base.ir.code.begin(), base.ir.code.end(), [&](const Instruction& in) {
            return in.op == Opcode::Check && in.line == line && (!in.speculative || deopts.count(in.proofKey) != 0);
        });
        if (error.phase != Phase::Contract || !checked) {
            return false;
        }
        stop = std::min(stop, line);
    }
    const std::set<std::string> kept = removable(again);
    for (const ItemCertificate& item : base.certificate.items) {
        const int line = std::stoi(item.key);
        if (deopts.count(item.key) == 0 && kept.count(item.key) == 0 && line < stop) {
            return false;
        }
    }
    return true;
}

void testAnyFactsAreSafe() {
    std::cout << "Random facts, true or false, and random inputs\n";
    const std::string program = "LOAD \"test_private_t.csv\" AS s;\n"   // line 1
                                "EXPECT s.marks BETWEEN 0 AND 100;\n"   // line 2
                                "FILL_NULL s.age WITH 0;\n"             // line 3
                                "FILTER s WHERE marks >= 40;\n"         // line 4
                                "EXPECT s.name NOT_NULL;\n"             // line 5
                                "DROP_NULL s;\n"                        // line 6
                                "SAVE s AS \"test_private_out.csv\";\n";
    const double infinity = std::numeric_limits<double>::infinity();
    const std::vector<double> anchors = {-infinity, -10, 0, 40, 100, 150, infinity};
    std::mt19937 random(2026);
    const auto pick = [&](int n) { return static_cast<int>(random() % static_cast<unsigned>(n)); };
    const char* const missing[] = {"0", "1", "?"};
    int compiled = 0, admitted = 0, deoptimizing = 0, removals = 0, wrong = 0;
    for (int round = 0; round < 300; ++round) {
        // Facts, true or false.
        std::string facts = header;
        for (const char* column : {"name", "age", "marks"}) {
            const bool numeric = std::string(column) != "name";
            double low = numeric ? anchors[pick(7)] : -infinity, high = numeric ? anchors[pick(7)] : infinity;
            if (low > high) {
                std::swap(low, high);
            }
            const auto text = [](double v) { return std::isinf(v) ? std::string(v < 0 ? "-inf" : "inf") : std::to_string(v); };
            facts += std::string("column\t") + column + "\t" + (numeric ? "INTEGER" : "STRING") + "\t" + missing[pick(3)] +
                     "\t" + text(low) + "\t" + (low < high && !std::isinf(low) && pick(2) ? "1" : "0") + "\t" + text(high) +
                     "\t" + (low < high && !std::isinf(high) && pick(2) ? "1" : "0") + "\t-\t-\t-\t-\n";
        }
        writeFile("test_private_facts.tsv", facts);
        Compiled base;
        compile(program, base, "test_private_facts.tsv");
        if (base.diagnostics.hasErrors()) {
            continue;
        }
        ++compiled;
        removals += static_cast<int>(base.certificate.items.size());
        // An input; at least one present age and mark, so that both stay INTEGER.
        std::string text = "name,age,marks\n";
        const int rows = 1 + pick(7);
        const int lowMark = pick(170) - 10, spread = pick(100);
        for (int i = 0; i < rows; ++i) {
            const bool noName = pick(8) == 0;
            const bool noAge = i > 0 && pick(3) == 0;
            const bool noMarks = i > 0 && pick(12) == 0;
            text += (noName ? "" : "N" + std::to_string(i)) + "," + (noAge ? "" : std::to_string(18 + pick(10))) + "," +
                    (noMarks ? "" : std::to_string(lowMark + pick(spread + 1))) + "\n";
        }
        writeFile("test_private_t.csv", text);
        Diagnostics diagnostics;
        SchemaProvider schemas(diagnostics);
        const TableSchema input = *schemas.readTable("test_private_t.csv", "s", {});
        const TableCertificate* admission = base.certificate.table("s");
        if (!admission || !admissionProblems(*admission, input).empty()) {
            continue;  // refused: the program does not run at all
        }
        ++admitted;
        std::set<std::string> deopts;
        for (const ItemCertificate& item : base.certificate.items) {
            for (const TableCertificate& table : item.tables) {
                if (!admissionProblems(table, input).empty()) {
                    deopts.insert(item.key);
                }
            }
        }
        deoptimizing += !deopts.empty();
        Compiled again;
        compile(program, again);
        wrong += !sameRun(base, again, deopts);
    }
    check(compiled >= 250, std::to_string(compiled) + " of 300 random facts files compile (" +
                               std::to_string(removals) + " speculative removals)");
    check(admitted >= 200 && deoptimizing >= 50,
          std::to_string(admitted) + " inputs admitted, " + std::to_string(deoptimizing) + " of them deoptimize");
    check(wrong == 0, "every admitted input runs as the source program would (" + std::to_string(wrong) + " did not)");
}

}  // namespace

int main() {
    std::cout << std::unitbuf;  // keep the output if a test crashes
    testReadFacts();
    testCompilesWithoutTheFile();
    testNothingExact();
    testNoReleasedFactDecidesAdmission();
    testQueries();
    testCellsDecide();
    testAnyFactsAreSafe();
    std::cout << (failures == 0 ? "All private compilation tests passed\n"
                                : std::to_string(failures) + " private compilation test(s) FAILED\n");
    return failures == 0 ? 0 : 1;
}
