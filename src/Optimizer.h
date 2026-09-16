#pragma once

#include "IR.h"

#include <string>
#include <vector>

namespace prep {

struct OptimizationResult {
    IntermediateProgram program;
    std::vector<std::string> notes;
};

class Optimizer {
public:
    OptimizationResult optimize(const IntermediateProgram& program) const;
};

}  // namespace prep
