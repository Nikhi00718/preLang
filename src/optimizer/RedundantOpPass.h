#pragma once

#include "optimizer/PassManager.h"

// Removes operations that semantic analysis proved to change nothing, using
// the dataset state at that point: a second DROP_NULL, FILL_NULL on a column
// without missing values, a FILTER that keeps every row, GRAYSCALE of 1-channel
// images, a CHECK for a contract proven at compile time, ...
//
// With --approx it also merges RESIZE A; RESIZE B into RESIZE B. That saves one
// resize per image but changes pixels slightly, so it is not done by default.
class RedundantOpPass : public Pass {
public:
    explicit RedundantOpPass(bool approximate);
    void run(IRProgram& program, OptimizationReport& report) override;

private:
    bool approximate_;

    void mergeResizes(IRProgram& program, OptimizationReport& report);
};
