#pragma once

#include "ir/IR.h"

#include <filesystem>
#include <string>

// Target code generation: turns the optimized IR into a Python script that
// uses pandas. Column types from the schema become explicit pandas dtypes,
// so pandas reads the data with exactly the types the compiler checked.
class PandasCodegen {
public:
    // The script is saved in outputFolder: input CSV paths are written relative
    // to it, and SAVE writes its files into it.
    PandasCodegen(std::filesystem::path outputFolder, std::string sourceName);

    std::string generate(const IRProgram& program) const;

private:
    std::filesystem::path outputFolder_;
    std::string sourceName_;

    std::string code(const Instruction& instruction, const IRProgram& program) const;
    std::string inputPath(const std::string& csvPath) const;
};
