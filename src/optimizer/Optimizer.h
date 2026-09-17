#pragma once

#include "ir/IR.h"

#include <ostream>
#include <set>
#include <string>
#include <vector>

struct OptimizationReport {
    int instructionsBefore = 0;
    int instructionsAfter = 0;
    std::vector<std::string> changes;  // one line per change, in pass order
};

// Optimizes the IR in four passes. None of them changes the program's output.
//  1. no-effect elimination: removes instructions semantic analysis proved
//     useless, including CHECKs for contracts proven at compile time
//  2. dead code elimination: removes temporaries that are never used
//  3. dead store elimination: removes FILL_NULL / NORMALIZE whose results are
//     never read or saved (found with a backward liveness analysis on columns)
//  4. column pruning: LOAD reads only the columns that are live at that point
class Optimizer {
public:
    OptimizationReport optimize(IRProgram& program);

private:
    OptimizationReport report_;

    void removeNoEffectInstructions(IRProgram& program);
    void removeDeadTemporaries(IRProgram& program);
    void removeDeadStoresAndPruneColumns(IRProgram& program);
    void fitColumnLists(IRProgram& program);

    void removeMarked(IRProgram& program, const std::vector<bool>& remove);
};

void printReport(std::ostream& out, const OptimizationReport& report);
