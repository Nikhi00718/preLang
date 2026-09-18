#pragma once

#include "optimizer/PassManager.h"

// Backward liveness analysis where the "variables" are table columns.
// A column is live if its current values may still be read or saved later.
//  * dead store elimination: FILL_NULL / NORMALIZE of a column that is never
//    read or saved afterwards is removed
//  * column pruning: each LOAD reads only the columns live at that point
// DROP_NULL counts as reading every column, since it looks at all of them to
// decide which rows to remove; pruning before it would change the result.
class ColumnPruningPass : public Pass {
public:
    void run(IRProgram& program, OptimizationReport& report) override;

private:
    void fitColumnLists(IRProgram& program, OptimizationReport& report);
};
