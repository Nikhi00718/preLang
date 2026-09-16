#pragma once

#include "IR.h"

#include <string>

namespace prep {

class CodeGenerator {
public:
    std::string generate(const IntermediateProgram& program) const;
};

}  // namespace prep
