// Unit tests for the IR builder and the optimizer passes.
// Run: build/Debug/test_optimizer.exe  (it writes a small CSV file in the current folder)

#include "common/Diagnostics.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/Optimizer.h"
#include "parser/Parser.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
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

// age has a missing value; marks is in [42, 91] with none missing.
const char* const csvFile = "test_optimizer_students.csv";
const char* const csvText = "name,age,marks,city,code\n"
                            "Asha,20,85,Chennai,10\n"
                            "Ravi,,42,,17\n"
                            "Mina,22,91,Pune,12\n"
                            "Joe,21,60,Delhi,15\n";

struct Compiled {
    IRProgram ir;
    OptimizationReport report;
    bool ok = false;
};

// Compiles `body`, placed between  LOAD "<test csv>" AS s;  and  SAVE s AS "out.csv";
Compiled compile(const std::string& body) {
    const std::string source =
        "LOAD \"" + std::string(csvFile) + "\" AS s;\n" + body + "\nSAVE s AS \"out.csv\";";
    Diagnostics diagnostics;
    const Program program = Parser(Lexer(source, diagnostics).tokenize(), diagnostics).parse();
    SymbolTable symbols;
    SchemaProvider schemas(diagnostics);
    const AnalysisResult analysis = SemanticAnalyzer(symbols, schemas, ".", diagnostics).analyze(program);

    Compiled compiled;
    compiled.ok = !diagnostics.hasErrors();
    if (compiled.ok) {
        compiled.ir = IRBuilder(symbols, analysis).build(program);
        compiled.report = Optimizer().optimize(compiled.ir);
    }
    return compiled;
}

int countOf(const IRProgram& ir, Opcode op) {
    return static_cast<int>(std::count_if(ir.code.begin(), ir.code.end(),
                                          [op](const Instruction& instruction) { return instruction.op == op; }));
}

std::vector<std::string> loadedColumns(const IRProgram& ir) {
    return ir.code.front().columns;  // the LOAD is always the first instruction here
}

bool reportMentions(const Compiled& compiled, const std::string& text) {
    return std::any_of(compiled.report.changes.begin(), compiled.report.changes.end(),
                       [&](const std::string& change) { return change.find(text) != std::string::npos; });
}

void testFilterLoweredToThreeAddressCode() {
    Diagnostics diagnostics;
    const std::string source = "LOAD \"" + std::string(csvFile) +
                               "\" AS s;\nFILTER s WHERE marks > 50 AND NOT city == \"Pune\";\nSAVE s AS \"o.csv\";";
    const Program program = Parser(Lexer(source, diagnostics).tokenize(), diagnostics).parse();
    SymbolTable symbols;
    SchemaProvider schemas(diagnostics);
    const AnalysisResult analysis = SemanticAnalyzer(symbols, schemas, ".", diagnostics).analyze(program);
    const IRProgram ir = IRBuilder(symbols, analysis).build(program);
    check(countOf(ir, Opcode::Compare) == 2 && countOf(ir, Opcode::Not) == 1 && countOf(ir, Opcode::And) == 1 &&
              ir.code[5].op == Opcode::Filter && ir.code[5].uses == std::vector<int>{4},
          "a condition becomes COMPARE/NOT/AND temporaries used by FILTER");
}

void testProvenCheckRemoved() {
    const Compiled c = compile("EXPECT s.marks BETWEEN 0 AND 100;");
    check(c.ok && countOf(c.ir, Opcode::Check) == 0 && reportMentions(c, "contract check elimination"),
          "a CHECK proven at compile time is removed");
}

void testDeferredCheckKept() {
    const Compiled c = compile("FILTER s WHERE age > 20;\nEXPECT s.marks BETWEEN 50 AND 100;");
    check(c.ok && countOf(c.ir, Opcode::Check) == 1, "a CHECK that cannot be proven stays for run time");
}

void testAlwaysTrueFilterRemovedWithItsTemporaries() {
    const Compiled c = compile("FILTER s WHERE marks > 0 AND marks < 100;");
    check(c.ok && countOf(c.ir, Opcode::Filter) == 0 && countOf(c.ir, Opcode::Compare) == 0 &&
              countOf(c.ir, Opcode::And) == 0,
          "an always-true FILTER and its unused temporaries are removed");
    check(c.report.instructionsBefore == 6 && c.report.instructionsAfter == 2, "instruction count goes 6 -> 2");
}

void testDeadStoreRemoved() {
    const Compiled c = compile("NORMALIZE s.marks;\nDROP s [marks];");
    const std::vector<std::string> columns = loadedColumns(c.ir);
    check(c.ok && countOf(c.ir, Opcode::Normalize) == 0, "NORMALIZE of a column dropped later is removed");
    check(std::find(columns.begin(), columns.end(), "marks") == columns.end() && countOf(c.ir, Opcode::Drop) == 0,
          "the dropped column is not loaded, so the DROP disappears too");
}

void testLiveStoreKept() {
    const Compiled c = compile("NORMALIZE s.marks;");
    check(c.ok && countOf(c.ir, Opcode::Normalize) == 1, "NORMALIZE of a saved column is kept");
}

void testColumnPruning() {
    const Compiled c = compile("SELECT s [name];");
    check(c.ok && loadedColumns(c.ir) == std::vector<std::string>{"name"}, "LOAD reads only the selected column");
}

void testFilterColumnsStayLive() {
    const Compiled c = compile("FILTER s WHERE marks > 50;\nSELECT s [name];");
    check(c.ok && loadedColumns(c.ir) == std::vector<std::string>{"name", "marks"},
          "a column used by FILTER is loaded even if it is not saved");
}

void testDropNullBlocksPruning() {
    const Compiled c = compile("DROP_NULL s;\nSELECT s [name];");
    check(c.ok && loadedColumns(c.ir).size() == 5,
          "DROP_NULL reads every column, so nothing is pruned before it");
}

}  // namespace

int main() {
    std::ofstream(csvFile) << csvText;

    std::cout << "IR and optimizer tests\n";
    testFilterLoweredToThreeAddressCode();
    testProvenCheckRemoved();
    testDeferredCheckKept();
    testAlwaysTrueFilterRemovedWithItsTemporaries();
    testDeadStoreRemoved();
    testLiveStoreKept();
    testColumnPruning();
    testFilterColumnsStayLive();
    testDropNullBlocksPruning();

    std::cout << (failures == 0 ? "All optimizer tests passed.\n" : "Some optimizer tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
