#include "Optimizer.h"

namespace prep {

OptimizationResult Optimizer::optimize(const IntermediateProgram& program) const {
    OptimizationResult result;
    for (const Instruction& instruction : program.instructions) {
        if (!result.program.instructions.empty()) {
            const Instruction& previous = result.program.instructions.back();
            if (instruction.operation == "DROP_NULL" && previous.operation == "DROP_NULL" &&
                instruction.result == previous.result) {
                result.notes.push_back("Removed repeated DROP_NULL on " + instruction.result + ".");
                continue;
            }
        }
        result.program.instructions.push_back(instruction);
    }
    if (result.notes.empty()) result.notes.push_back("No simple redundant operations found.");
    return result;
}

}  // namespace prep
