#pragma once

#include "ir/IR.h"

#include <filesystem>
#include <set>
#include <string>

// Target code for TABLE and TABLESET instructions: Python using pandas.
// Column types from the schema become explicit pandas dtypes, so pandas reads
// the data with exactly the types the compiler checked.
class PandasCodegen {
public:
    // Input paths are written relative to outputFolder, where the script is saved.
    explicit PandasCodegen(std::filesystem::path outputFolder);

    // The Python statements for one table instruction.
    std::string code(const Instruction& instruction, const IRProgram& program);

    // Helper functions the generated statements call.
    static std::string helpers();

    // df_students for a TABLE, tables_company["staff"] for a table in a TABLESET
    static std::string tableVariable(const std::string& dataset, const std::string& table);

private:
    std::filesystem::path outputFolder_;
    std::set<std::string> declaredSets_;  // TABLESET dictionaries already created
};

// Shared by both backends.
std::string pythonString(const std::string& text);
std::string pythonList(const std::vector<std::string>& items);
std::string pathFrom(const std::filesystem::path& outputFolder, const std::string& inputPath);
