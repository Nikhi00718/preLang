#pragma once

#include "certify/Certifier.h"
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
    // The certificate says which facts another input file must have to be admitted.
    PandasCodegen(std::filesystem::path outputFolder, const Certificate& certificate);

    // The Python statements for one table instruction.
    std::string code(const Instruction& instruction, const IRProgram& program);

    // Helper functions the generated statements call: the CSV reader, the Parquet reader, or both.
    static std::string helpers(bool csv, bool parquet);
    // matplotlib and the helpers of PLOT and REPORT, for a program that has one.
    static std::string chartHelpers();

    // df_students for a TABLE, tables_company["staff"] for a table in a TABLESET
    static std::string tableVariable(const std::string& dataset, const std::string& table);

    // For a CHECK that runs only when an input changed: the Python condition
    // "one of its tables is not the compiled file".
    static std::string inputChanged(const Instruction& instruction);
    // The facts of one table certificate as a Python dict:
    // {"marks": {"type": "INTEGER", "max": 100, "max_for": "line 5: ..."}, ...}
    static std::string columnsDict(const TableCertificate& table);

private:
    std::filesystem::path outputFolder_;
    const Certificate& certificate_;

    std::string certificateDict(const std::string& tableKey) const;
    std::set<std::string> declaredSets_;  // TABLESET dictionaries already created
};

// Shared by both backends.
std::string pythonString(const std::string& text);
std::string pythonList(const std::vector<std::string>& items);
std::string pathFrom(const std::filesystem::path& outputFolder, const std::string& inputPath);
// A double written so that Python reads back exactly the same value.
std::string exactNumber(double value);
