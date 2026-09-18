#pragma once

#include "ir/IR.h"

#include <memory>
#include <ostream>
#include <string>
#include <vector>

struct OptimizationReport {
    int instructionsBefore = 0;
    int instructionsAfter = 0;
    std::vector<std::string> changes;      // one line per change, in pass order
    std::vector<std::string> suggestions;  // approximate rewrites that were not applied
};

// Every optimization is a pass over the IR.
class Pass {
public:
    virtual ~Pass() = default;
    virtual void run(IRProgram& program, OptimizationReport& report) = 0;
};

struct OptimizerOptions {
    bool enabled = true;       // --O0 turns every pass off
    bool approximate = false;  // --approx also applies rewrites that change pixels slightly
};

// Runs the passes in order. Exact passes never change the program's output;
// approximate rewrites (like merging two RESIZEs) only run with --approx.
class PassManager {
public:
    explicit PassManager(OptimizerOptions options);
    OptimizationReport run(IRProgram& program);

private:
    OptimizerOptions options_;
    std::vector<std::unique_ptr<Pass>> passes_;
};

void printReport(std::ostream& out, const OptimizationReport& report);

// Shared by the passes.
std::string describe(const Instruction& instruction);  // "`DROP_NULL s` (line 4)"
void removeMarked(IRProgram& program, const std::vector<bool>& remove);
bool isImageOperation(Opcode op);  // changes the pixels or the encoding of an image dataset
