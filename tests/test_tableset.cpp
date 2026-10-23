// Unit tests for TABLESETs: wildcard and type-based selectors, IF_EXISTS,
// cross-table contracts, and how the IR expands them per table.
// Run: build/Debug/test_tableset.exe  (it writes a small folder of CSV files)

#include "common/Diagnostics.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/PassManager.h"
#include "parser/Parser.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

// staff: id, name, age (one missing), salary (one missing), temp
// interns: id, name, age (one missing), team, stipend (one missing)
// teams: name, year
void writeTables() {
    std::filesystem::create_directories("test_tableset_company");
    std::ofstream("test_tableset_company/staff.csv")
        << "id,name,age,salary,temp\n1,Anita,34,72000,9\n2,Bharat,,65000,8\n3,Chitra,45,,7\n";
    std::ofstream("test_tableset_company/interns.csv")
        << "id,name,age,team,stipend\n11,Isha,21,core,15000\n12,Jay,,web,\n13,Kiran,22,core,15000\n";
    std::ofstream("test_tableset_company/teams.csv") << "name,year\ncore,2011\nweb,2014\n";
}

struct Compiled {
    Diagnostics diagnostics;
    SymbolTable symbols;
    Program program;
    AnalysisResult analysis;
    IRProgram ir;
};

// Compiles `body`, placed after  LOAD_TABLES "test_tableset_company/" AS company;
void compile(const std::string& body, Compiled& c) {
    const std::string source = "LOAD_TABLES \"test_tableset_company/\" AS company;\n" + body;
    c.program = Parser(Lexer(source, c.diagnostics).tokenize(), c.diagnostics).parse();
    SchemaProvider schemas(c.diagnostics);
    c.analysis = SemanticAnalyzer(c.symbols, schemas, ".", c.diagnostics).analyze(c.program);
    if (!c.diagnostics.hasErrors()) {
        c.ir = IRBuilder(c.symbols, c.analysis).build(c.program);
        PassManager({true, false}).run(c.ir);
    }
}

const Diagnostic* reported(const Compiled& c, Phase phase, const std::string& text) {
    for (const Diagnostic& d : c.diagnostics.all()) {
        if (d.severity == Severity::Error && d.phase == phase && d.message.find(text) != std::string::npos) {
            return &d;
        }
    }
    return nullptr;
}

int countOf(const IRProgram& ir, Opcode op) {
    return static_cast<int>(std::count_if(ir.code.begin(), ir.code.end(),
                                          [op](const Instruction& instruction) { return instruction.op == op; }));
}

size_t targetsOf(const Compiled& c, size_t statement) {
    const auto found = c.analysis.targets.find(c.program.statements.at(statement).get());
    return found == c.analysis.targets.end() ? 0 : found->second.size();
}

int proven(const Compiled& c) {
    return static_cast<int>(std::count_if(c.analysis.contracts.begin(), c.analysis.contracts.end(),
                                          [](const ContractRecord& r) { return r.verdict == Verdict::Proven; }));
}

const std::string save = "\nSAVE_TABLES company TO \"out/\";";

void testLoadTables() {
    Compiled c;
    compile(save, c);
    const DatasetSymbol* company = c.symbols.lookup("company");
    check(!c.diagnostics.hasErrors() && company && company->kind == DatasetKind::TableSet &&
              company->tableNames() == std::vector<std::string>{"interns", "staff", "teams"},
          "LOAD_TABLES reads every CSV of the folder as one table");
}

void testWildcardIfExists() {
    Compiled c;
    compile("FILL_NULL company.*.age WITH 0 IF_EXISTS;" + save, c);
    check(!c.diagnostics.hasErrors() && targetsOf(c, 1) == 2 && countOf(c.ir, Opcode::FillNull) == 2,
          "company.*.age with IF_EXISTS expands to the 2 tables that have age");
}

void testWildcardWithoutIfExists() {
    Compiled c;
    compile("FILL_NULL company.*.age WITH 0;" + save, c);
    check(reported(c, Phase::Semantic, "missing in company.teams") != nullptr,
          "without IF_EXISTS a missing column names the tables");
}

void testTypeSelector() {
    Compiled c;
    compile("NORMALIZE company.*.* WHERE TYPE NUMERIC EXCEPT [id, year];" + save, c);
    check(!c.diagnostics.hasErrors() && targetsOf(c, 1) == 5,
          "WHERE TYPE NUMERIC EXCEPT [id, year] matches age, salary, temp and stipend (5 columns)");
}

void testBatchTypeError() {
    Compiled c;
    compile("NORMALIZE company.*.name;" + save, c);
    const Diagnostic* error = reported(c, Phase::Type, "3 of 3 columns");
    check(error && error->notes.size() == 3, "a type error over a wildcard lists every matched column");
}

void testCrossTableContracts() {
    Compiled c;
    compile(
        "EXPECT company.*.id UNIQUE IF_EXISTS;\n"
        "EXPECT company.interns.team REFERENCES company.teams.name;" +
            save,
        c);
    // The proofs read the values, so they hold for these files only: the checks
    // stay, but run only if the program is given other files.
    const bool allConditional = std::all_of(c.ir.code.begin(), c.ir.code.end(), [](const Instruction& in) {
        return in.op != Opcode::Check || in.onlyIfInputChanged;
    });
    check(!c.diagnostics.hasErrors() && proven(c) == 3 && countOf(c.ir, Opcode::Check) == 3 && allConditional,
          "UNIQUE and REFERENCES are proven from the files; their checks run only if an input changes");

    Compiled bad;
    compile("EXPECT company.staff.name REFERENCES company.teams.name;" + save, bad);
    check(reported(bad, Phase::Contract, "3 of 3 values") != nullptr, "a foreign key without matches is an error");
}

void testSchemaSameAs() {
    Compiled c;
    compile("EXPECT company.staff SCHEMA_SAME_AS company.interns EXCEPT [salary, temp, team, stipend];" + save, c);
    check(!c.diagnostics.hasErrors() && proven(c) == 1, "SCHEMA_SAME_AS with EXCEPT is proven");

    Compiled bad;
    compile("EXPECT company.staff SCHEMA_SAME_AS company.interns;" + save, bad);
    const Diagnostic* error = reported(bad, Phase::Contract, "different schemas");
    check(error && error->notes.size() == 4, "each missing or unexpected column is listed");
}

void testFilterNeedsOneTable() {
    Compiled c;
    compile("FILTER company WHERE age > 30;" + save, c);
    check(reported(c, Phase::Semantic, "one table at a time") != nullptr, "FILTER on a whole TABLESET is an error");
}

void testSaveTablesExpands() {
    Compiled c;
    compile(save, c);
    std::vector<std::string> files;
    for (const Instruction& instruction : c.ir.code) {
        if (instruction.op == Opcode::Save) {
            files.push_back(instruction.text);
        }
    }
    check(files == std::vector<std::string>{"out/interns.csv", "out/staff.csv", "out/teams.csv"},
          "SAVE_TABLES writes one file per table");
}

void testDeadStoreAcrossWildcard() {
    Compiled c;
    compile("NORMALIZE company.*.temp IF_EXISTS;\nDROP company.* [temp] IF_EXISTS;" + save, c);
    const Instruction& staff = *std::find_if(c.ir.code.begin(), c.ir.code.end(), [](const Instruction& i) {
        return i.op == Opcode::Load && i.table == "staff";
    });
    check(countOf(c.ir, Opcode::Normalize) == 0 && countOf(c.ir, Opcode::Drop) == 0 &&
              std::find(staff.columns.begin(), staff.columns.end(), "temp") == staff.columns.end(),
          "a column normalized and then dropped is never loaded");
}

void testSaveTablesNeedsTableSet() {
    Diagnostics diagnostics;
    const Program program =
        Parser(Lexer("LOAD \"test_tableset_company/staff.csv\" AS one;\nSAVE_TABLES one TO \"x/\";", diagnostics)
                   .tokenize(),
               diagnostics)
            .parse();
    SymbolTable symbols;
    SchemaProvider schemas(diagnostics);
    SemanticAnalyzer(symbols, schemas, ".", diagnostics).analyze(program);
    const bool found = std::any_of(diagnostics.all().begin(), diagnostics.all().end(), [](const Diagnostic& d) {
        return d.message.find("SAVE_TABLES expects a TABLESET") != std::string::npos;
    });
    check(found, "SAVE_TABLES on a TABLE suggests SAVE");
}

}  // namespace

int main() {
    writeTables();

    std::cout << "TABLESET tests\n";
    testLoadTables();
    testWildcardIfExists();
    testWildcardWithoutIfExists();
    testTypeSelector();
    testBatchTypeError();
    testCrossTableContracts();
    testSchemaSameAs();
    testFilterNeedsOneTable();
    testSaveTablesExpands();
    testDeadStoreAcrossWildcard();
    testSaveTablesNeedsTableSet();

    std::cout << (failures == 0 ? "All TABLESET tests passed.\n" : "Some TABLESET tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
