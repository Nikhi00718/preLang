#pragma once

#include "ir/IR.h"
#include "optimizer/PassManager.h"
#include "sema/AnalysisResult.h"
#include "sema/SymbolTable.h"

#include <ostream>
#include <string>

// --explain: what the compiler understood (inputs, contracts, optimizations,
// backends) without writing or running anything.
void printExplain(std::ostream& out, const SymbolTable& symbols, const AnalysisResult& analysis,
                  const OptimizationReport& report, const IRProgram& program, int warnings);

// --dry-run: which tables, columns and images running the program would
// change, and which files it would write.
void printDryRun(std::ostream& out, const IRProgram& program, const std::string& outputFolder);
