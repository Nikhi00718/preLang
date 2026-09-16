#pragma once

#include "Ast.h"

#include <ostream>
#include <string>
#include <vector>

namespace prep {

struct Instruction {
    std::string operation;
    std::string result;
    std::string argument;
    int line = 0;
    Statement source;
};

struct IntermediateProgram {
    std::vector<Instruction> instructions;
};

class IRGenerator {
public:
    IntermediateProgram generate(const Program& program) const;
};

void printIR(const IntermediateProgram& program, std::ostream& out);

}  // namespace prep
