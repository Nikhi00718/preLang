#include "optimizer/PassManager.h"

#include "ir/IRBuilder.h"
#include "optimizer/ColumnPruningPass.h"
#include "optimizer/DeadOpPass.h"
#include "optimizer/RedundantOpPass.h"

#include <utility>

PassManager::PassManager(OptimizerOptions options) : options_(options) {
    if (options_.enabled) {
        passes_.push_back(std::make_unique<RedundantOpPass>(options_.approximate, options_.speculate));
        passes_.push_back(std::make_unique<DeadOpPass>());
        passes_.push_back(std::make_unique<ColumnPruningPass>());
    }
}

OptimizationReport PassManager::run(IRProgram& program) {
    OptimizationReport report;
    report.instructionsBefore = static_cast<int>(program.code.size());
    for (const std::unique_ptr<Pass>& pass : passes_) {
        pass->run(program, report);
    }
    report.instructionsAfter = 0;  // the instructions that run while no input breaks a certificate
    for (const Instruction& instruction : program.code) {
        report.instructionsAfter += !instruction.speculative;
    }
    return report;
}

void printReport(std::ostream& out, const OptimizationReport& report) {
    for (const std::string& change : report.changes) {
        out << "- " << change << '\n';
    }
    for (const std::string& suggestion : report.suggestions) {
        out << "? " << suggestion << '\n';
    }
    out << "Instructions: " << report.instructionsBefore << " -> " << report.instructionsAfter << '\n';
}

std::string describe(const Instruction& instruction) {
    return "`" + instructionText(instruction) + "` (line " + std::to_string(instruction.line) + ")";
}

void removeMarked(IRProgram& program, const std::vector<bool>& remove) {
    std::vector<Instruction> kept;
    for (size_t i = 0; i < program.code.size(); ++i) {
        if (!remove[i]) {
            kept.push_back(std::move(program.code[i]));
        }
    }
    program.code = std::move(kept);
}

bool isImageOperation(Opcode op) {
    switch (op) {
        case Opcode::Resize:
        case Opcode::Grayscale:
        case Opcode::NormalizeImages:
        case Opcode::Compress:
        case Opcode::Convert:
        case Opcode::RemoveCorrupt:   return true;
        default:                      return false;
    }
}
