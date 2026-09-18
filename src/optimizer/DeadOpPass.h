#pragma once

#include "optimizer/PassManager.h"

// Dead code elimination:
//  * temporaries that no later instruction reads (for example the condition
//    of a FILTER that the previous pass removed)
//  * image operations with no SAVE_IMAGES after them, since their pixels are
//    never written
class DeadOpPass : public Pass {
public:
    void run(IRProgram& program, OptimizationReport& report) override;
};
