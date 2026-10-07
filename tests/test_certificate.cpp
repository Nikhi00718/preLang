// Unit tests for data certificates: which facts are certified, why, and the
// soundness property: every input the certificate admits, analysed on its own,
// gives at least the proofs and removed operations of the compiled program.
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
#include <fstream>
#include <iostream>
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

void compile(const std::string& source, Compiled& c) {
    c.program = Parser(Lexer(source, c.diagnostics).tokenize(), c.diagnostics).parse();
    SchemaProvider schemas(c.diagnostics);
    c.analysis = SemanticAnalyzer(c.symbols, schemas, ".", c.diagnostics).analyze(c.program);
    if (c.diagnostics.hasErrors()) {
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

void writeFile(const std::string& name, const std::string& text) {
    std::ofstream(name, std::ios::binary) << text;
}

const ColumnFacts* fact(const Certificate& certificate, const std::string& table, const std::string& column) {
    const TableCertificate* found = certificate.table(table);
    if (!found) {
        return nullptr;
    }
    for (const ColumnFacts& facts : found->columns) {
        if (facts.name == column) {
            return &facts;
        }
    }
    return nullptr;
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
    check(!c.diagnostics.hasErrors() && c.certificate.tables.size() == 1, "compiles, one table certificate");
    const ColumnFacts* marks = fact(c.certificate, "s", "marks");
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
    const ColumnFacts* name = fact(c.certificate, "s", "name");
    check(name && name->notNull && name->notNullFor == std::vector<std::string>{"line 3: EXPECT s.name NOT_NULL"},
          "name must have no missing values, for the contract at line 3");
    const ColumnFacts* city = fact(c.certificate, "s", "city");
    check(city && !city->notNull && city->type == ColumnType::String, "city: any values, only its type");
    const ColumnFacts* age = fact(c.certificate, "s", "age");
    check(age && !age->notNull && std::isinf(age->min) && std::isinf(age->max),
          "age: no proof depends on its values, so no bounds");
}

void testAdmission() {
    std::cout << "Admission of other inputs\n";
    writeFile("test_certificate_students.csv", students);
    Compiled c;
    compile(program, c);
    Diagnostics diagnostics;
    SchemaProvider schemas(diagnostics);
    const auto problems = [&](const std::string& text) {
        writeFile("test_certificate_batch.csv", text);
        return admissionProblems(*c.certificate.table("s"), *schemas.readTable("test_certificate_batch.csv", "s", {}));
    };
    check(problems("name,age,marks,city\nZoe,,100,\nLi,,10,Goa\n").empty(),
          "new rows with marks 10 and 100 and missing ages and cities are admitted");
    const std::vector<std::string> high = problems("name,age,marks,city\nZoe,30,101,Goa\n");
    check(high.size() == 1 && high[0].find("line 2") != std::string::npos, "marks 101 is refused, naming line 2");
    check(problems("name,age,marks,city\n,30,50,Goa\n").size() == 1, "a missing name is refused");
    check(problems("name,age,marks,city\nZoe,30,50.5,Goa\n").size() == 1, "FLOAT marks are refused (INTEGER certified)");
    check(problems("name,age,city\nZoe,30,Goa\n").size() == 1, "a missing column is refused");
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
void testSoundness() {
    std::cout << "Soundness on 400 random inputs\n";
    writeFile("test_certificate_students.csv", students);
    Compiled base;
    compile(program, base);
    const std::set<std::string> baseProofs = proofs(base);
    const std::string batchProgram = [] {
        std::string text = program;
        const std::string from = "test_certificate_students.csv";
        text.replace(text.find(from), from.size(), "test_certificate_random.csv");
        return text;
    }();

    std::mt19937 random(2026);
    int admitted = 0, unsound = 0, outsideData = 0;
    for (int round = 0; round < 400; ++round) {
        std::string text = "name,age,marks,city\n";
        const int rows = 1 + static_cast<int>(random() % 6);
        for (int i = 0; i < rows; ++i) {
            const bool noName = random() % 25 == 0;
            const bool noMarks = random() % 25 == 0;
            text += (noName ? "" : "N" + std::to_string(i)) + "," + (random() % 3 ? std::to_string(18 + random() % 10) : "") +
                    "," + (noMarks ? "" : std::to_string(static_cast<int>(random() % 115) - 5)) + ",C\n";
        }
        writeFile("test_certificate_random.csv", text);
        Diagnostics diagnostics;
        SchemaProvider schemas(diagnostics);
        const TableSchema facts = *schemas.readTable("test_certificate_random.csv", "s", {});
        if (!admissionProblems(*base.certificate.table("s"), facts).empty()) {
            continue;
        }
        ++admitted;
        const ColumnInfo& marks = facts.columns[2];
        outsideData += marks.min < 35 || marks.max > 95;  // the compiled file's facts would refuse it
        Compiled again;
        compile(batchProgram, again);
        const std::set<std::string> found = proofs(again);
        const bool keepsAll = !again.diagnostics.hasErrors() &&
                              std::includes(found.begin(), found.end(), baseProofs.begin(), baseProofs.end());
        unsound += !keepsAll;
    }
    check(admitted >= 40, std::to_string(admitted) + " of 400 random inputs admitted");
    check(outsideData > 0, std::to_string(outsideData) + " admitted inputs lie outside the compiled file's own ranges");
    check(unsound == 0, "every admitted input keeps every proof (" + std::to_string(unsound) + " did not)");
}

}  // namespace

int main() {
    std::cout << std::unitbuf;  // keep the output if a test crashes
    testFacts();
    testAdmission();
    testValueProofs();
    testSoundness();
    std::cout << (failures == 0 ? "All certificate tests passed\n"
                                : std::to_string(failures) + " certificate test(s) FAILED\n");
    return failures == 0 ? 0 : 1;
}
