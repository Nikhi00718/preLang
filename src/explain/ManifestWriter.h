#pragma once

#include "ir/IR.h"
#include "sema/AnalysisResult.h"

#include <filesystem>
#include <string>

// Python code, placed at the end of the generated program, that writes
// preplang_manifest.json: which program produced the output, from which inputs
// (with their fingerprints), what it did, and what it wrote. This makes every
// output dataset reproducible.
std::string manifestCode(const IRProgram& program, const AnalysisResult& analysis, const std::string& sourceName,
                         const std::filesystem::path& outputFolder);
