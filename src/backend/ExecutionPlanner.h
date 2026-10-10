#pragma once

#include "certify/Certifier.h"
#include "ir/IR.h"

#include <filesystem>
#include <string>
#include <vector>

// Plans how the optimized IR runs and writes the final Python program:
// table instructions go to the pandas backend, image instructions to the
// OpenCV backend, in IR order. Only the imports and helpers the program
// needs are included.
class ExecutionPlanner {
public:
    ExecutionPlanner(std::filesystem::path outputFolder, std::string sourceName, const Certificate& certificate);

    // The complete generated program; `manifestCode` is appended at the end.
    std::string generate(const IRProgram& program, const std::string& manifestCode) const;

    // One line per streaming image pass, e.g. for --explain:
    // "SAVE_IMAGES train (line 14): 1 pass over 12 files: RESIZE 224x224 -> GRAYSCALE -> write JPG"
    static std::vector<std::string> imagePasses(const IRProgram& program);

private:
    std::filesystem::path outputFolder_;
    std::string sourceName_;
    const Certificate& certificate_;

    std::string deoptimization(const IRProgram& program) const;
};

// Whether the OpenCV backend handles this instruction.
bool isImageInstruction(const Instruction& instruction);
