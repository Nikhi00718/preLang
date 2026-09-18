#include "optimizer/DeadOpPass.h"

#include <set>

// Both checks walk backwards, so a use is always seen before its definition.
// Changes are found in reverse and reported in program order.
void DeadOpPass::run(IRProgram& program, OptimizationReport& report) {
    std::vector<bool> remove(program.code.size(), false);
    std::vector<std::string> changes;
    std::set<int> usedTemporaries;
    std::set<std::string> savedLater;  // image datasets with a SAVE_IMAGES further down

    for (size_t i = program.code.size(); i-- > 0;) {
        const Instruction& instruction = program.code[i];
        if (instruction.result != 0 && usedTemporaries.count(instruction.result) == 0) {
            remove[i] = true;
            changes.push_back("dead code elimination: removed " + describe(instruction) + ", t" +
                              std::to_string(instruction.result) + " is never used");
            continue;
        }
        usedTemporaries.insert(instruction.uses.begin(), instruction.uses.end());

        if (instruction.op == Opcode::SaveImages) {
            savedLater.insert(instruction.dataset);
        } else if (isImageOperation(instruction.op) && savedLater.count(instruction.dataset) == 0) {
            remove[i] = true;
            changes.push_back("dead image operation: removed " + describe(instruction) + ", no SAVE_IMAGES of " +
                              instruction.dataset + " comes after it");
        }
    }
    report.changes.insert(report.changes.end(), changes.rbegin(), changes.rend());
    removeMarked(program, remove);
}
