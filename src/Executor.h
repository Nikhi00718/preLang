#pragma once

#include "Diagnostics.h"
#include "IR.h"

#include <iosfwd>

namespace prep {

class Executor {
public:
    bool run(const IntermediateProgram& program, ErrorReporter& reporter,
             std::ostream& log) const;
};

}  // namespace prep
