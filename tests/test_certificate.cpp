// Unit tests for data certificates: which facts are certified, why, and the
// soundness properties:
//  * every input the full certificate admits, analysed on its own, gives at least
//    the proofs and removed operations of the compiled program;
//  * every input the admission certificate admits compiles without errors, keeps
//    every proof no run-time code replaces, and keeps each speculative removal
//    whose item certificate it satisfies (the others are deoptimized).
// Run: build/Debug/test_certificate.exe  (it writes small CSV files in the current folder)

#include "certify/Certifier.h"
#include "common/Diagnostics.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/PassManager.h"
#include "parser/Parser.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>

namespace {

int failures = 0;

void check(bool passed, const std::string& name) {
    std::cout << (passed ? "  PASS  " : "  FAIL  ") << name << '\n';
    if (!passed) {
        ++failures;
    }
}

struct Compiled {
    Diagnostics diagnostics;
    SymbolTable symbols;
    Program program;
    AnalysisResult analysis;
    IRProgram ir;
    Certificate certificate;
};

// With `images`, image folders are replaced by these files (by SchemaProvider::imageKey)
// and only the analysis runs.
void compile(const std::string& source, Compiled& c, const std::map<std::string, ImageSetSchema>& images = {}) {
    c.program = Parser(Lexer(source, c.diagnostics).tokenize(), c.diagnostics).parse();
    SchemaProvider schemas(c.diagnostics);
    schemas.setImageOverrides(images);
    c.analysis = SemanticAnalyzer(c.symbols, schemas, ".", c.diagnostics).analyze(c.program);
    if (c.diagnostics.hasErrors() || !images.empty()) {
        return;
    }
    c.ir = IRBuilder(c.symbols, c.analysis).build(c.program);
    const std::vector<double> constants = programConstants(c.ir);
    PassManager({true, false}).run(c.ir);
    c.certificate = Certifier(c.program, schemas, ".").certify(c.ir, c.analysis, constants);
}

// What a compilation proved, comparable between two compilations of the same
// program text: proven contracts, and removed operations by line and target.
std::set<std::string> proofs(const Compiled& c) {
    std::set<std::string> items;
    for (const ContractRecord& contract : c.analysis.contracts) {
        if (contract.verdict == Verdict::Proven && !contract.fromValues) {
            items.insert(std::to_string(contract.line) + " " + contract.contract);
        }
    }
    for (const auto& [key, reason] : c.analysis.noEffect) {
        items.insert(std::to_string(key.first->location.line) + " removed " + key.second);
    }
    return items;
}

bool mentions(const std::vector<std::string>& proofs, const std::string& text);

void writeFile(const std::string& name, const std::string& text) {
    std::ofstream(name, std::ios::binary) << text;
}

const ColumnFacts* factIn(const std::vector<TableCertificate>& tables, const std::string& table,
                          const std::string& column) {
    for (const TableCertificate& found : tables) {
        for (const ColumnFacts& facts : found.columns) {
            if (found.key == table && facts.name == column) {
                return &facts;
            }
        }
    }
    return nullptr;
}

// A fact of the admission certificate, and of the full certificate (every proof).
const ColumnFacts* fact(const Certificate& certificate, const std::string& table, const std::string& column) {
    return factIn(certificate.tables, table, column);
}
const ColumnFacts* fullFact(const Certificate& certificate, const std::string& table, const std::string& column) {
    return factIn(certificate.fullTables, table, column);
}

// The speculative removals an admitted input with these facts deoptimizes (their descriptions).
std::vector<std::string> deoptimized(const Certificate& certificate, const std::map<std::string, TableSchema>& tables,
                                     const std::map<std::string, ImageSetSchema>& images = {}) {
    std::vector<std::string> names;
    for (const ItemCertificate& item : certificate.items) {
        bool broken = false;
        for (const TableCertificate& table : item.tables) {
            broken = broken || !admissionProblems(table, tables.at(table.key)).empty();
        }
        for (const ImageCertificate& image : item.images) {
            broken = broken || !admissionProblems(image, images.at(image.dataset)).empty();
        }
        if (broken) {
            names.push_back(item.description);
        }
    }
    return names;
}

// The proofKeys of the no-effect facts a compilation found.
std::set<std::string> removable(const Compiled& c) {
    std::set<std::string> keys;
    for (const auto& [key, reason] : c.analysis.noEffect) {
        keys.insert(proofKey(*key.first, key.second));
    }
    return keys;
}

// Contracts proven without a run-time check (types, image contracts), as "line contract".
std::set<std::string> hardProofs(const Compiled& c) {
    std::set<std::string> items;
    for (const ContractRecord& contract : c.analysis.contracts) {
        if (contract.verdict == Verdict::Proven && !contract.fromValues && !contract.runtimeCheck) {
            items.insert(std::to_string(contract.line) + " " + contract.contract);
        }
    }
    return items;
}

// Deoptimization soundness for one admitted input, compiled again as `again`.
// A fresh compilation of that input may fail only where the generated program
// fails too: a violated contract whose check runs (deoptimized, or never removed).
// Every hard proof is kept, and every removal whose item certificate holds is
// kept, up to the first such failure (the program stops there).
bool deoptSound(const Compiled& base, const Compiled& again, const std::vector<std::string>& deopts) {
    const auto lineOf = [](const std::string& description) { return std::stoi(description.substr(5)); };
    int stop = 1 << 30;  // the line where the generated program stops
    for (const Diagnostic& error : again.diagnostics.all()) {
        if (error.severity != Severity::Error) {
            continue;
        }
        const int line = error.location.line;
        const bool deoptimizedCheck = std::any_of(deopts.begin(), deopts.end(), [&](const std::string& d) {
            return lineOf(d) == line && d.find(": EXPECT") != std::string::npos;
        });
        const bool keptCheck = std::any_of(base.ir.code.begin(), base.ir.code.end(), [&](const Instruction& in) {
            return in.op == Opcode::Check && !in.speculative && in.line == line;
        });
        if (error.phase != Phase::Contract || !(deoptimizedCheck || keptCheck)) {
            return false;
        }
        stop = std::min(stop, line);
    }
    const std::set<std::string> hard = hardProofs(base), found = hardProofs(again);
    for (const std::string& proof : hard) {
        if (found.count(proof) == 0 && std::stoi(proof) < stop) {
            return false;
        }
    }
    const std::set<std::string> kept = removable(again);
    for (const ItemCertificate& item : base.certificate.items) {
        const bool deopt = std::find(deopts.begin(), deopts.end(), item.description) != deopts.end();
        if (!deopt && kept.count(item.key) == 0 && lineOf(item.description) < stop) {
            return false;
        }
    }
    return true;
}

// marks: INTEGER in [35, 95], no missing values; age: one missing value; city: text.
const char* const students = "name,age,marks,city\n"
                             "Asha,20,85,Chennai\n"
                             "Ravi,,35,Pune\n"
                             "Mina,22,95,Delhi\n"
                             "Joe,21,60,Pune\n";

const std::string program = "LOAD \"test_certificate_students.csv\" AS s;\n"   // line 1
                            "EXPECT s.marks BETWEEN 0 AND 100;\n"  // line 2: proven, needs 0 <= marks <= 100
                            "EXPECT s.name NOT_NULL;\n"            // line 3: proven, needs name without missing values
                            "FILL_NULL s.marks WITH 0;\n"          // line 4: removed, needs marks without missing values
                            "FILTER s WHERE marks >= 10;\n"        // line 5: removed (always true), needs marks >= 10
                            "SAVE s AS \"out.csv\";\n";

void testFacts() {
    std::cout << "Certified facts\n";
    writeFile("test_certificate_students.csv", students);
    Compiled c;
    compile(program, c);
    check(!c.diagnostics.hasErrors() && c.certificate.fullTables.size() == 1, "compiles, one table certificate");
    const ColumnFacts* marks = fullFact(c.certificate, "s", "marks");
    check(marks && marks->min == 10 && marks->max == 100,
          "marks is widened from the data's [35, 95] to [10, 100], the program's constants");
    const auto mentions = [](const std::vector<std::string>& proofs, const std::string& text) {
        return std::any_of(proofs.begin(), proofs.end(),
                           [&](const std::string& proof) { return proof.find(text) != std::string::npos; });
    };
    check(marks && marks->notNull && marks->notNullFor.size() == 2 && mentions(marks->notNullFor, "line 4") &&
              mentions(marks->notNullFor, "line 5"),
          "marks must have no missing values: the removed FILL_NULL (line 4) and FILTER (line 5) need it");
    check(marks && mentions(marks->minFor, "line 5") && mentions(marks->maxFor, "line 2") &&
              !mentions(marks->maxFor, "line 5"),
          "marks >= 10 is needed by the FILTER at line 5, marks <= 100 only by the contract at line 2");
    const ColumnFacts* name = fullFact(c.certificate, "s", "name");
    check(name && name->notNull && name->notNullFor == std::vector<std::string>{"line 3: EXPECT s.name NOT_NULL"},
          "name must have no missing values, for the contract at line 3");
    const ColumnFacts* city = fullFact(c.certificate, "s", "city");
    check(city && !city->notNull && city->type == ColumnType::String, "city: any values, only its type");
    const ColumnFacts* age = fullFact(c.certificate, "s", "age");
    check(age && !age->notNull && std::isinf(age->min) && std::isinf(age->max),
          "age: no proof depends on its values, so no bounds");

    // With deoptimization every proof of this program has a run-time stand-in, so
    // admission needs only the types, and each removal gets its own facts.
    const ColumnFacts* admitted = fact(c.certificate, "s", "marks");
    check(admitted && !admitted->notNull && std::isinf(admitted->min) && std::isinf(admitted->max),
          "admission: marks needs only its type");
    const auto item = [&](const std::string& line) -> const ItemCertificate* {
        for (const ItemCertificate& found : c.certificate.items) {
            if (found.description.rfind(line, 0) == 0) {
                return &found;
            }
        }
        return nullptr;
    };
    const auto itemFact = [&](const std::string& line, const std::string& column) {
        const ItemCertificate* found = item(line);
        return found ? factIn(found->tables, "s", column) : nullptr;
    };
    check(c.certificate.items.size() == 4, "four speculative removals: two checks, FILL_NULL and FILTER");
    const ColumnFacts* contract = itemFact("line 2", "marks");
    check(contract && !contract->notNull && contract->min == 0 && contract->max == 100,
          "the check at line 2 needs 0 <= marks <= 100, not 'no missing values'");
    const ColumnFacts* fill = itemFact("line 4", "marks");
    check(fill && fill->notNull && std::isinf(fill->min) && std::isinf(fill->max),
          "the FILL_NULL at line 4 needs only marks without missing values");
    const ColumnFacts* filter = itemFact("line 5", "marks");
    check(filter && filter->notNull && filter->min == 10 && std::isinf(filter->max),
          "the FILTER at line 5 needs marks >= 10 without missing values, and no upper bound");
}

void testAdmission() {
    std::cout << "Admission and deoptimization of other inputs\n";
    writeFile("test_certificate_students.csv", students);
    Compiled c;
    compile(program, c);
    Diagnostics diagnostics;
    SchemaProvider schemas(diagnostics);
    std::map<std::string, TableSchema> facts;
    const auto problems = [&](const std::string& text) {
        writeFile("test_certificate_batch.csv", text);
        facts["s"] = *schemas.readTable("test_certificate_batch.csv", "s", {});
        return admissionProblems(*c.certificate.table("s"), facts.at("s"));
    };
    const auto lines = [&](const std::vector<std::string>& found) {
        std::string text;
        for (const std::string& item : found) {
            text += item.substr(0, item.find(':')) + ";";
        }
        return text;
    };
    check(problems("name,age,marks,city\nZoe,,100,\nLi,19,10,Goa\n").empty() &&
              deoptimized(c.certificate, facts).empty(),
          "marks 10 and 100, a missing age and city: admitted, nothing deoptimized");
    check(problems("name,age,marks,city\nZoe,,100,\nLi,,10,Goa\n").size() == 1,
          "a batch without any age is refused: the compiler would type that column STRING");
    check(problems("name,age,marks,city\nZoe,30,101,Goa\n").empty() &&
              lines(deoptimized(c.certificate, facts)) == "line 2;",
          "marks 101 is admitted; only the check at line 2 runs again");
    const std::vector<std::string> full = admissionProblems(c.certificate.fullTables.front(), facts.at("s"));
    check(full.size() == 1, "without deoptimization (the full certificate) that batch would be refused");
    check(problems("name,age,marks,city\nZoe,30,5,Goa\n").empty() &&
              lines(deoptimized(c.certificate, facts)) == "line 5;",
          "marks 5 is admitted; only the FILTER at line 5 runs again");
    check(problems("name,age,marks,city\n,30,50,Goa\n").empty() &&
              lines(deoptimized(c.certificate, facts)) == "line 3;",
          "a missing name is admitted; only the check at line 3 runs again");
    check(problems("name,age,marks,city\nZoe,30,50.5,Goa\n").size() == 1, "FLOAT marks are refused (INTEGER certified)");
    check(problems("name,age,city\nZoe,30,Goa\n").size() == 1, "a missing column is refused");
}

// A proof can depend on a column the optimized program never loads: the check of
// a proven contract is removed, and then column pruning drops the column.
void testUnreadColumns() {
    std::cout << "Columns the program does not read\n";
    writeFile("test_certificate_students.csv", students);
    Compiled c;
    compile("LOAD \"test_certificate_students.csv\" AS s;\nEXPECT s.name NOT_NULL;\nEXPECT s.marks NUMBER;\n"
            "SELECT s [age];\nSAVE s AS \"out.csv\";\n",
            c);
    const ColumnFacts* name = fact(c.certificate, "s", "name");
    const ColumnFacts* marks = fact(c.certificate, "s", "marks");
    check(name && !name->read && name->notNull && !name->typeFixed,
          "name is not read, but must have no missing values (line 2); its type does not matter");
    check(marks && !marks->read && marks->typeFixed && mentions(marks->typeFor, "line 3"),
          "marks is not read, but must stay INTEGER for EXPECT s.marks NUMBER at line 3");
    check(fact(c.certificate, "s", "city") == nullptr, "city is neither read nor needed: not in the certificate");
    Diagnostics diagnostics;
    SchemaProvider schemas(diagnostics);
    writeFile("test_certificate_batch.csv", "name,age,marks,city\n,30,50,Goa\n");
    check(admissionProblems(*c.certificate.table("s"), *schemas.readTable("test_certificate_batch.csv", "s", {})).size() == 1,
          "a batch with a missing name is refused although the program never reads name");
}

// A fill of a column that is dropped right after is a dead store, removed whatever the data. That
// the compiled file also has no missing values in it must not become an admission fact: the
// column is not loaded, so the fill could not run again, and a batch would be refused for nothing.
void testDeadStoreNeedsNoFact() {
    std::cout << "A dead store needs no fact\n";
    writeFile("test_certificate_students.csv", students);  // marks has no missing values
    Compiled c;
    compile("LOAD \"test_certificate_students.csv\" AS s;\nFILL_NULL s.marks WITH 0;\nDROP s [marks];\n"
            "SAVE s AS \"out.csv\";\n",
            c);
    const ColumnFacts* marks = fact(c.certificate, "s", "marks");
    check(!marks || !marks->notNull, "marks may have missing values: the dead fill needs no fact");
    Diagnostics diagnostics;
    SchemaProvider schemas(diagnostics);
    writeFile("test_certificate_batch.csv", "name,age,marks,city\nAsha,20,,Chennai\nRavi,21,40,Pune\n");
    check(admissionProblems(*c.certificate.table("s"), *schemas.readTable("test_certificate_batch.csv", "s", {})).empty(),
          "a batch with missing marks is admitted");
}

// WHERE NULLABLE selects the columns with missing values, which depends on the batch. A fill of a
// column without missing values must stay in the program behind a "no missing values" certificate,
// or a batch with a missing value there is admitted and not filled (a fresh compilation fills it).
void testNullableSelector() {
    std::cout << "WHERE NULLABLE: a fill that matched nothing here runs on a batch with missing values\n";
    writeFile("test_certificate_students.csv", students);  // age has a missing value, marks none
    Compiled c;
    compile("LOAD \"test_certificate_students.csv\" AS s;\nFILL_NULL s.* WHERE NULLABLE WITH 0;\n"
            "SAVE s AS \"out.csv\";\n",
            c);
    const bool guarded = std::any_of(c.certificate.items.begin(), c.certificate.items.end(), [](const ItemCertificate& item) {
        return item.description.rfind("line 2:", 0) == 0 && std::any_of(item.tables.begin(), item.tables.end(),
            [](const TableCertificate& table) {
                return std::any_of(table.columns.begin(), table.columns.end(),
                                   [](const ColumnFacts& column) { return column.name == "marks" && column.notNull; });
            });
    });
    check(!c.diagnostics.hasErrors() && guarded, "the fill of marks is removed behind 'no missing values in marks'");
    const ColumnFacts* name = fact(c.certificate, "s", "name");
    check(name && name->notNull, "a text column the number cannot fill must keep no missing values to be admitted");
    Compiled normalize;
    compile("LOAD \"test_certificate_students.csv\" AS s;\nNORMALIZE s.* WHERE NULLABLE;\nSAVE s AS \"out.csv\";\n",
            normalize);
    check(normalize.diagnostics.hasErrors(), "WHERE NULLABLE outside FILL_NULL is a compile error");
}

void testValueProofs() {
    std::cout << "Proofs that read the values\n";
    writeFile("test_certificate_keys.csv", "id,city\n1,Pune\n2,Goa\n3,Pune\n");
    Compiled c;
    compile("LOAD \"test_certificate_keys.csv\" AS k;\nEXPECT k.id UNIQUE;\nSAVE k AS \"out.csv\";\n", c);
    const auto check = std::find_if(c.ir.code.begin(), c.ir.code.end(),
                                    [](const Instruction& in) { return in.op == Opcode::Check; });
    ::check(check != c.ir.code.end() && check->onlyIfInputChanged && check->provenFromValuesOf ==
                                                                          std::vector<std::string>{"k"},
            "UNIQUE proven from the values keeps a check that runs only if k changes");
    ::check(c.certificate.recheckedIfChanged.size() == 1, "the certificate lists it as checked again");
}

// The soundness property, on random inputs: if the certificate admits an input,
// compiling the same program for that input proves at least as much.
void testSoundness(const std::string& source, const std::string& title) {
    std::cout << title << '\n';
    writeFile("test_certificate_students.csv", students);
    Compiled base;
    compile(source, base);
    const std::set<std::string> baseProofs = proofs(base);
    const std::string batchProgram = [&] {
        std::string text = source;
        const std::string from = "test_certificate_students.csv";
        text.replace(text.find(from), from.size(), "test_certificate_random.csv");
        return text;
    }();

    std::mt19937 random(2026);
    int admitted = 0, unsound = 0, outsideData = 0, admittedDeopt = 0, deopts = 0, unsoundDeopt = 0;
    for (int round = 0; round < 400; ++round) {
        // Some batches have no city column at all (a column the program only carries).
        const bool city = random() % 4 != 0;
        std::string text = city ? "name,age,marks,city\n" : "name,age,marks\n";
        const int rows = 1 + static_cast<int>(random() % 6);
        for (int i = 0; i < rows; ++i) {
            const bool noName = random() % 25 == 0;
            const bool noMarks = random() % 25 == 0;
            text += (noName ? "" : "N" + std::to_string(i)) + "," + (random() % 3 ? std::to_string(18 + random() % 10) : "") +
                    "," + (noMarks ? "" : std::to_string(static_cast<int>(random() % 115) - 5)) + (city ? ",C\n" : "\n");
        }
        writeFile("test_certificate_random.csv", text);
        Diagnostics diagnostics;
        SchemaProvider schemas(diagnostics);
        const TableSchema facts = *schemas.readTable("test_certificate_random.csv", "s", {});
        if (!admissionProblems(*base.certificate.table("s"), facts).empty()) {
            continue;
        }
        Compiled again;
        compile(batchProgram, again);
        ++admittedDeopt;
        const std::vector<std::string> deopt = deoptimized(base.certificate, {{"s", facts}});
        deopts += !deopt.empty();
        unsoundDeopt += !deoptSound(base, again, deopt);
        if (!admissionProblems(base.certificate.fullTables.front(), facts).empty()) {
            continue;
        }
        ++admitted;
        const ColumnInfo& marks = facts.columns[2];
        outsideData += marks.min < 35 || marks.max > 95;  // the compiled file's facts would refuse it
        const std::set<std::string> found = proofs(again);
        const bool keepsAll = !again.diagnostics.hasErrors() &&
                              std::includes(found.begin(), found.end(), baseProofs.begin(), baseProofs.end());
        unsound += !keepsAll;
    }
    check(admitted >= 40, std::to_string(admitted) + " of 400 random inputs admitted by the full certificate");
    if (source == program) {
        check(outsideData > 0, std::to_string(outsideData) + " admitted inputs lie outside the compiled file's own ranges");
    }
    check(unsound == 0, "every input the full certificate admits keeps every proof (" + std::to_string(unsound) +
                            " did not)");
    check(admittedDeopt >= admitted, std::to_string(admittedDeopt) + " admitted with deoptimization, " +
                                         std::to_string(deopts) + " of them deoptimize something");
    check(unsoundDeopt == 0, "every input admitted with deoptimization keeps every removal it does not deoptimize (" +
                                 std::to_string(unsoundDeopt) + " did not)");
}

// ---------- Dates and derived columns ----------

// Trips from March to June 2024; fares 7 to 30, distances 1 to 8.
const char* const trips = "day,fare,km\n2024-03-05,12,3\n2024-04-10,30,8\n2024-06-20,7,1\n2024-05-02,25,6\n";

const std::string tripProgram = "LOAD \"test_certificate_trips.csv\" AS t;\n"  // line 1
                                "PARSE_DATE t.day;\n"                            // line 2: needs dates (admission)
                                "DERIVE t.month = MONTH(day);\n"                 // line 3
                                "FILTER t WHERE month >= 3;\n"                   // line 4: removed, needs dates Mar-Dec of one year
                                "DERIVE t.per_km = fare / km;\n"                 // line 5: needs km > 0 (admission)
                                "EXPECT t.per_km BETWEEN 0 AND 100;\n"           // line 6: proven from fare and km
                                "FILL_NULL t.fare WITH 0;\n"                     // line 7: removed, needs fare without missing values
                                "SAVE t AS \"out.csv\";\n";

// The facts of a batch file as the compiler would see them, with the date facts of `day`.
TableSchema tripFacts(const std::string& file) {
    Diagnostics diagnostics;
    TableSchema facts = *SchemaProvider(diagnostics).readTable(file, "t", {});
    for (ColumnInfo& column : facts.columns) {
        if (column.name == "day" && column.type == ColumnType::String) {
            readDateFacts(facts, column);
        }
    }
    return facts;
}

void testDateFacts() {
    writeFile("test_certificate_trips.csv", trips);
    Compiled c;
    compile(tripProgram, c);
    const ColumnFacts* day = fact(c.certificate, "t", "day");
    const ColumnFacts* km = fact(c.certificate, "t", "km");
    check(!c.diagnostics.hasErrors() && day && day->dates && std::isinf(day->dateMin) && std::isinf(day->dateMax),
          "admission requires dates in day (PARSE_DATE fails without them), but no date range");
    check(km && km->min == 0 && km->minStrict, "admission requires km > 0: line 5 divides by it");
    const ItemCertificate* filter = nullptr;
    for (const ItemCertificate& item : c.certificate.items) {
        filter = item.description.find("line 4") == 0 ? &item : filter;
    }
    const ColumnFacts* bounds = filter ? factIn(filter->tables, "t", "day") : nullptr;
    check(bounds && dateText(bounds->dateMin) == "2024-03-01 00:00:00" &&
              dateText(bounds->dateMax) == "2024-12-31 23:59:59",
          "the FILTER on MONTH needs dates from 1 March to 31 December of the same year");
}

void testDateSoundness() {
    std::cout << "Soundness on 400 random inputs, with dates and derived columns\n";
    writeFile("test_certificate_trips.csv", trips);
    Compiled base;
    compile(tripProgram, base);
    std::string batchProgram = tripProgram;
    batchProgram.replace(batchProgram.find("test_certificate_trips.csv"), 26, "test_certificate_rtrips.csv");

    std::mt19937 random(2027);
    int admitted = 0, deopts = 0, unsound = 0, notDates = 0;
    for (int round = 0; round < 400; ++round) {
        std::string text = "day,fare,km\n";
        const int year = 2023 + static_cast<int>(random() % 3);
        const bool oneYear = random() % 2 == 0;
        const int rows = 1 + static_cast<int>(random() % 5);
        for (int i = 0; i < rows; ++i) {
            char day[16];
            std::snprintf(day, sizeof day, "%04d-%02d-%02d", oneYear ? year : 2023 + static_cast<int>(random() % 3),
                          1 + static_cast<int>(random() % 12), 1 + static_cast<int>(random() % 28));
            const int kind = static_cast<int>(random() % 40);
            text += std::string(kind == 0 ? "soon" : kind == 1 ? "" : day) + "," +
                    (random() % 20 == 0 ? "" : std::to_string(random() % 150)) + "," + std::to_string(random() % 10) +
                    "\n";
        }
        writeFile("test_certificate_rtrips.csv", text);
        const TableSchema facts = tripFacts("test_certificate_rtrips.csv");
        notDates += !facts.columns[0].allDates;
        if (!admissionProblems(*base.certificate.table("t"), facts).empty()) {
            continue;
        }
        ++admitted;
        Compiled again;
        compile(batchProgram, again);
        const std::vector<std::string> deopt = deoptimized(base.certificate, {{"t", facts}});
        deopts += !deopt.empty();
        unsound += !deoptSound(base, again, deopt);
    }
    check(admitted >= 40 && notDates > 0,
          std::to_string(admitted) + " of 400 random inputs admitted with deoptimization, " + std::to_string(deopts) +
              " of them deoptimize something");
    check(unsound == 0, "every admitted input compiles without errors and keeps every removal it does not deoptimize (" +
                            std::to_string(unsound) + " did not)");
}

// ---------- Image folders ----------

std::string bigEndian(unsigned value, int bytes) {
    std::string text;
    for (int i = bytes - 1; i >= 0; --i) {
        text += static_cast<char>((value >> (8 * i)) & 0xFF);
    }
    return text;
}

// Only the header is read at compile time: PNG signature + IHDR (color type 0 gray, 2 RGB).
void writePng(const std::string& path, unsigned width, unsigned height, char colorType) {
    std::string bytes = "\x89PNG\r\n\x1A";
    bytes += "\n" + bigEndian(13, 4) + "IHDR" + bigEndian(width, 4) + bigEndian(height, 4);
    bytes += std::string{8, colorType, 0, 0, 0} + bigEndian(0, 4);
    writeFile(path, bytes);
}

void writeJpeg(const std::string& path, unsigned width, unsigned height) {
    writeFile(path, "\xFF\xD8\xFF\xC0" + bigEndian(17, 2) + std::string(1, 8) + bigEndian(height, 2) +
                        bigEndian(width, 2) + std::string(1, 3) + std::string(9, '\x01') + "\xFF\xD9");
}

// Color images; the contract needs width >= 100, GRAYSCALE needs readable files.
const std::string colorProgram = "LOAD_IMAGES \"test_certificate_color/\" AS imgs;\n"  // line 1
                                 "EXPECT imgs MIN_WIDTH 100;\n"                         // line 2
                                 "GRAYSCALE imgs;\n"                                    // line 3
                                 "EXPECT imgs CHANNELS 1;\n"                            // line 4
                                 "RESIZE imgs TO 64 64;\n"                              // line 5
                                 "RESIZE imgs TO 64 64;\n"                              // line 6: removed
                                 "CONVERT imgs TO JPG;\n"
                                 "SAVE_IMAGES imgs TO \"out/\";\n";

// Gray 64x64 images: GRAYSCALE and RESIZE are removed, so the shapes matter.
const std::string grayProgram = "LOAD_IMAGES \"test_certificate_gray/\" AS g;\n"  // line 1
                                "GRAYSCALE g;\n"                                   // line 2: removed
                                "RESIZE g TO 64 64;\n"                             // line 3: removed
                                "CONVERT g TO PNG;\n"
                                "SAVE_IMAGES g TO \"out2/\";\n";

void writeImageFolders() {
    std::filesystem::create_directories("test_certificate_color");
    writePng("test_certificate_color/a.png", 120, 90, 2);
    writePng("test_certificate_color/b.png", 200, 150, 2);
    writeJpeg("test_certificate_color/c.jpg", 160, 120);
    std::filesystem::create_directories("test_certificate_gray");
    writePng("test_certificate_gray/a.png", 64, 64, 0);
    writePng("test_certificate_gray/b.png", 64, 64, 0);
}

bool mentions(const std::vector<std::string>& proofs, const std::string& text) {
    return std::any_of(proofs.begin(), proofs.end(),
                       [&](const std::string& proof) { return proof.find(text) != std::string::npos; });
}

void testImageFacts() {
    std::cout << "Certified facts of image folders\n";
    writeImageFolders();
    Compiled color;
    compile(colorProgram, color);
    const auto fullImage = [](const Certificate& certificate, const std::string& dataset) -> const ImageCertificate* {
        for (const ImageCertificate& image : certificate.fullImages) {
            if (image.dataset == dataset) {
                return &image;
            }
        }
        return nullptr;
    };
    const ImageCertificate* imgs = fullImage(color.certificate, "imgs");
    check(!color.diagnostics.hasErrors() && imgs && !imgs->fixed && imgs->groups.size() == 2,
          "the color folder is certified with one box per extension (jpg, png)");
    check(imgs && !imgs->brokenAllowed && mentions(imgs->brokenFor, "line 3"),
          "broken files are not allowed: GRAYSCALE at line 3 must decode every file");
    check(imgs && std::all_of(imgs->groups.begin(), imgs->groups.end(), [](const ImageGroupFacts& g) {
              return g.channels == std::vector<int>{1, 3, 4} && g.minWidth == 100 && mentions(g.minWidthFor, "line 2") &&
                     g.maxWidth == 65535 && g.minHeight == 1 && g.maxHeight == 65535;
          }),
          "any channels and heights; width >= 100 for MIN_WIDTH 100 at line 2 (the data had 120 to 200)");

    Compiled gray;
    compile(grayProgram, gray);
    const ImageCertificate* g = fullImage(gray.certificate, "g");
    check(g && g->groups.size() == 1 && g->groups[0].channels == std::vector<int>{1} &&
              mentions(g->groups[0].channelsFor, "line 2"),
          "gray folder: only 1-channel images, because the removed GRAYSCALE at line 2 needs it");
    check(g && g->groups[0].minWidth == 64 && g->groups[0].maxWidth == 64 && g->groups[0].minHeight == 64 &&
              g->groups[0].maxHeight == 64 && mentions(g->groups[0].maxWidthFor, "line 3"),
          "gray folder: exactly 64x64, because the removed RESIZE at line 3 needs it");

    std::vector<ImageInfo> files(2);
    files[0] = {"x.png", "PNG", 64, 64, 1, 8, false, "", 0};
    files[1] = {"y.png", "PNG", 64, 64, 3, 8, false, "", 0};
    ImageSetSchema input;
    input.images = files;
    check(g && admissionProblems(*g, input).size() == 1,
          "a 3-channel image breaks the full certificate of the gray program");
    const ImageCertificate* admitted = gray.certificate.image("g");
    const std::vector<std::string> deopt = deoptimized(gray.certificate, {}, {{"g", input}});
    check(admitted && admissionProblems(*admitted, input).empty() && deopt.size() == 1 &&
              deopt[0].rfind("line 2", 0) == 0,
          "with deoptimization it is admitted, and only the GRAYSCALE at line 2 runs again");
    input.images.pop_back();
    check(g && admissionProblems(*g, input).empty(), "another 1-channel 64x64 folder is admitted");
}

// The image soundness property: any folder the box admits keeps every proof.
// This also checks that the corner images stand for every folder in the box.
void testImageSoundness() {
    std::cout << "Soundness on 600 random image folders\n";
    writeImageFolders();
    std::mt19937 random(7);
    for (const std::string& source : {colorProgram, grayProgram}) {
        const bool gray = source == grayProgram;  // gray folders need 1-channel 64x64 PNG files
        Compiled base;
        compile(source, base);
        const std::set<std::string> baseProofs = proofs(base);
        const ImageCertificate& certificate = base.certificate.fullImages.front();
        const ImageCertificate& admission = base.certificate.images.front();
        const ImageSetSchema& compiled = base.ir.images.begin()->second;
        const std::string key = SchemaProvider::imageKey(compiled.root, false, false);
        const int sizes[] = {16, 63, 64, 65, 99, 100, 150, 400};
        int admitted = 0, unsound = 0, admittedDeopt = 0, unsoundDeopt = 0;
        for (int round = 0; round < 300; ++round) {
            ImageSetSchema folder = compiled;
            folder.namesKnown = false;
            folder.images.clear();
            const int count = 1 + static_cast<int>(random() % 5);
            for (int i = 0; i < count; ++i) {
                ImageInfo file;
                const bool png = gray || random() % 2 == 0;
                file.path = "f" + std::to_string(i) + (png ? ".png" : ".jpg");
                file.format = png ? "PNG" : "JPG";
                file.width = gray && random() % 4 ? 64 : sizes[random() % 8];
                file.height = gray && random() % 4 ? 64 : sizes[random() % 8];
                file.channels = gray && random() % 4 ? 1 : std::vector<int>{1, 3, 4}[random() % 3];
                file.corrupt = random() % 12 == 0;
                folder.images.push_back(file);
            }
            if (!admissionProblems(admission, folder).empty()) {
                continue;
            }
            Compiled again;
            compile(source, again, {{key, folder}});
            ++admittedDeopt;
            unsoundDeopt += !deoptSound(base, again, deoptimized(base.certificate, {}, {{admission.dataset, folder}}));
            if (!admissionProblems(certificate, folder).empty()) {
                continue;
            }
            ++admitted;
            const std::set<std::string> found = proofs(again);
            unsound += again.diagnostics.hasErrors() ||
                       !std::includes(found.begin(), found.end(), baseProofs.begin(), baseProofs.end());
        }
        check(admitted >= 20 && unsound == 0, std::to_string(admitted) + " of 300 admitted folders keep every proof (" +
                                                  std::to_string(unsound) + " did not)");
        check(admittedDeopt >= admitted && unsoundDeopt == 0,
              std::to_string(admittedDeopt) + " folders admitted with deoptimization keep every removal they do not " +
                  "deoptimize (" + std::to_string(unsoundDeopt) + " did not)");
    }
}

}  // namespace

int main() {
    std::cout << std::unitbuf;  // keep the output if a test crashes
    testFacts();
    testAdmission();
    testUnreadColumns();
    testDeadStoreNeedsNoFact();
    testNullableSelector();
    testValueProofs();
    testSoundness(program, "Soundness on 400 random inputs");
    // name and marks are not read after the SELECT, but proofs depend on them.
    testSoundness("LOAD \"test_certificate_students.csv\" AS s;\nEXPECT s.name NOT_NULL;\n"
                  "EXPECT s.marks BETWEEN 0 AND 100;\nSELECT s [age];\nSAVE s AS \"out.csv\";\n",
                  "Soundness on 400 random inputs, with proofs about columns that are not read");
    // DROP of a column that is never used is removed by the optimizer, but a file
    // without that column would not compile: it must exist.
    testSoundness("LOAD \"test_certificate_students.csv\" AS s;\nDROP s [city];\nSAVE s AS \"out.csv\";\n",
                  "Soundness on 400 random inputs, with a dropped column that some inputs lack");
    testDateFacts();
    testDateSoundness();
    testImageFacts();
    testImageSoundness();
    std::cout << (failures == 0 ? "All certificate tests passed\n"
                                : std::to_string(failures) + " certificate test(s) FAILED\n");
    return failures == 0 ? 0 : 1;
}
