#include "explain/ManifestWriter.h"

#include "backend/PandasCodegen.h"
#include "ir/IRBuilder.h"

#include <iomanip>
#include <sstream>

namespace {

std::string hex(std::uint64_t value) {
    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return text.str();
}

}  // namespace

std::string manifestCode(const IRProgram& program, const AnalysisResult& analysis, const std::string& sourceName,
                         const std::filesystem::path& outputFolder) {
    int proven = 0, deferred = 0;
    for (const ContractRecord& contract : analysis.contracts) {
        proven += contract.verdict == Verdict::Proven;
        deferred += contract.verdict == Verdict::Deferred;
    }

    std::ostringstream code;
    code << "# Reproducibility manifest\nmanifest = {\n"
         << "    \"source_program\": " << pythonString(sourceName) << ",\n"
         << "    \"compiler\": \"PrepLang 1.0\",\n    \"inputs\": [\n";
    for (const auto& [key, table] : program.tables) {
        code << "        {\"table\": " << pythonString(key) << ", \"path\": "
             << pythonString(pathFrom(outputFolder, table.file)) << ", \"fingerprint\": \"" << hex(table.fingerprint)
             << "\"},\n";
    }
    for (const auto& [name, images] : program.images) {
        code << "        {\"images\": " << pythonString(name) << ", \"path\": "
             << pythonString(pathFrom(outputFolder, images.root)) << ", \"files\": " << images.images.size()
             << ", \"fingerprint\": \"" << hex(images.fingerprint) << "\"},\n";
    }
    code << "    ],\n    \"contracts\": {\"proven_at_compile_time\": " << proven
         << ", \"checked_at_run_time\": " << deferred << "},\n    \"operations\": [\n";
    for (const Instruction& instruction : program.code) {
        code << "        " << pythonString(instructionText(instruction)) << ",\n";
    }
    code << "    ],\n    \"outputs\": outputs,\n}\n"
         << "(HERE / \"preplang_manifest.json\").write_text(json.dumps(manifest, indent=2), encoding=\"utf-8\")\n"
         << "print(\"Wrote\", HERE / \"preplang_manifest.json\")\n";
    return code.str();
}
