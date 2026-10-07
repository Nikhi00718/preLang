#include "certify/Certifier.h"

#include "common/Diagnostics.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <utility>

namespace {

std::string join(const std::vector<std::string>& items) {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i > 0 ? "; " : "") + items[i];
    }
    return text;
}

}  // namespace

const TableCertificate* Certificate::table(const std::string& key) const {
    for (const TableCertificate& certificate : tables) {
        if (certificate.key == key) {
            return &certificate;
        }
    }
    return nullptr;
}

Certifier::Certifier(const Program& program, SchemaProvider& schemas, std::filesystem::path baseFolder)
    : program_(program), schemas_(schemas), baseFolder_(std::move(baseFolder)) {}

// One more run of the whole semantic analysis, with facts_ in place of the files.
Certifier::Proofs Certifier::analyse() {
    Diagnostics diagnostics;
    SymbolTable symbols;
    schemas_.setOverrides(facts_);
    const AnalysisResult result = SemanticAnalyzer(symbols, schemas_, baseFolder_, diagnostics).analyze(program_);
    ++runs_;
    return proofsOf(result, diagnostics.hasErrors());
}

Certifier::Proofs Certifier::proofsOf(const AnalysisResult& result, bool failed) {
    Proofs proofs;
    proofs.failed = failed;
    for (const ContractRecord& contract : result.contracts) {
        if (contract.verdict == Verdict::Proven && !contract.fromValues) {
            proofs.items["contract " + std::to_string(contract.line) + " " + contract.contract] =
                "line " + std::to_string(contract.line) + ": EXPECT " + contract.contract;
        }
    }
    // Removed operations. The removed check of a proven contract is already
    // covered by the contract itself.
    for (const auto& [key, reason] : result.noEffect) {
        if (!dynamic_cast<const ExpectStmt*>(key.first)) {
            const std::string where = std::to_string(reinterpret_cast<std::uintptr_t>(key.first));
            proofs.items["removed " + where + " " + key.second] =
                "line " + std::to_string(key.first->location.line) + ": removed because " + reason;
        }
    }
    return proofs;
}

std::vector<std::string> Certifier::lost(const Proofs& base, const Proofs& run) {
    if (run.failed) {
        return {"the program would no longer compile"};
    }
    std::vector<std::string> missing;
    for (const auto& [key, description] : base.items) {
        if (run.items.count(key) == 0) {
            missing.push_back(description);
        }
    }
    return missing;
}

Certificate Certifier::certify(const IRProgram& optimized, const AnalysisResult& analysis,
                               const std::vector<double>& constants) {
    // Start from the facts of the files, but as bounds that hold for every
    // admitted file: no value of the compiled file is known to occur in another one.
    for (const auto& [key, schema] : optimized.tables) {
        TableSchema facts = schema;
        facts.valuesKnown = false;
        for (ColumnInfo& column : facts.columns) {
            column.boundsExact = false;
        }
        facts_[SchemaProvider::overrideKey(facts.file, facts.name)] = facts;
    }
    const Proofs base = analyse();

    Certificate certificate;
    // The facts as bounds must give exactly the proofs the compiled program uses
    // (only proofs that read the values may need the exact file). If they do not,
    // no other input can be admitted safely.
    const Proofs compiled = proofsOf(analysis, false);
    if (!lost(base, compiled).empty() || !lost(compiled, base).empty()) {
        certificate.exactOnly = true;
        certificate.analysisRuns = runs_;
        schemas_.setOverrides({});
        return certificate;
    }
    for (const Instruction& load : optimized.code) {
        if (load.op != Opcode::Load) {
            continue;
        }
        TableCertificate table;
        table.key = tableKey(load.dataset, load.table);
        const TableSchema& schema = optimized.tables.at(table.key);
        table.file = schema.file;
        TableSchema& facts = facts_[SchemaProvider::overrideKey(schema.file, schema.name)];
        for (const std::string& name : load.columns) {
            ColumnInfo& info = *std::find_if(facts.columns.begin(), facts.columns.end(),
                                             [&](const ColumnInfo& column) { return column.name == name; });
            ColumnFacts column;
            column.name = name;
            column.type = info.type;

            // Missing values: allow them, and keep "no missing values" only if a proof needs it.
            if (!base.failed && info.missingCount == 0 && info.missingKnown) {
                info.missingKnown = false;
                column.notNullFor = lost(base, analyse());
                if (!column.notNullFor.empty()) {
                    info.missingKnown = true;
                }
            }
            column.notNull = info.missingCount == 0 && info.missingKnown;

            // A bound is widened to the loosest value that keeps every proof: first
            // no bound at all, then the program's constants from the loosest one on.
            // The analysis compares ranges only with these constants, so no other
            // value could do better. `neededBy` gets what dropping the bound breaks.
            const auto widen = [&](double& bound, std::vector<double> looser, std::vector<std::string>& neededBy) {
                const double original = bound;
                for (size_t k = 0; k < looser.size() && !base.failed; ++k) {
                    bound = looser[k];
                    const std::vector<std::string> broken = lost(base, analyse());
                    if (k == 0) {
                        neededBy = broken;
                    }
                    if (broken.empty()) {
                        return;
                    }
                }
                bound = original;
            };
            if (isNumeric(info.type) && info.hasRange) {
                std::vector<double> lower = {-std::numeric_limits<double>::infinity()};
                std::vector<double> upper = {std::numeric_limits<double>::infinity()};
                for (double constant : constants) {  // constants are sorted
                    if (constant < info.min) {
                        lower.push_back(constant);
                    }
                }
                for (auto it = constants.rbegin(); it != constants.rend(); ++it) {
                    if (*it > info.max) {
                        upper.push_back(*it);
                    }
                }
                widen(info.min, lower, column.minFor);
                widen(info.max, upper, column.maxFor);
                column.min = info.min;
                column.max = info.max;
                if (std::isinf(column.min)) {
                    column.minFor.clear();
                }
                if (std::isinf(column.max)) {
                    column.maxFor.clear();
                }
            }
            if (!column.notNull) {
                column.notNullFor.clear();
            }
            table.columns.push_back(column);
        }
        certificate.tables.push_back(table);
    }

    for (const ContractRecord& contract : analysis.contracts) {
        if (contract.fromValues) {
            certificate.recheckedIfChanged.push_back("line " + std::to_string(contract.line) + ": EXPECT " +
                                                     contract.contract);
        }
    }
    for (const auto& [name, images] : optimized.images) {
        certificate.fixedImageFolders.push_back(name + " (" + images.root + ")");
    }
    certificate.analysisRuns = runs_;
    schemas_.setOverrides({});
    return certificate;
}

std::vector<double> programConstants(const IRProgram& program) {
    std::vector<double> constants;
    for (const Instruction& in : program.code) {
        const bool numbers = in.op == Opcode::Compare || in.op == Opcode::FillNull ||
                             (in.op == Opcode::Check && in.text == "BETWEEN");
        for (const Literal& value : in.values) {
            if (numbers && value.kind == LiteralKind::Number) {
                constants.push_back(std::stod(value.value));
            }
        }
    }
    std::sort(constants.begin(), constants.end());
    constants.erase(std::unique(constants.begin(), constants.end()), constants.end());
    return constants;
}

std::vector<std::string> admissionProblems(const TableCertificate& certificate, const TableSchema& input) {
    std::vector<std::string> problems;
    for (const ColumnFacts& column : certificate.columns) {
        const auto found = std::find_if(input.columns.begin(), input.columns.end(),
                                        [&](const ColumnInfo& info) { return info.name == column.name; });
        if (found == input.columns.end()) {
            problems.push_back(column.name + " is missing");
            continue;
        }
        const ColumnInfo& info = *found;
        // INTEGER values fit a FLOAT column, and a column with no values at all fits any type.
        const bool widens = column.type == ColumnType::Float && info.type == ColumnType::Integer;
        const bool empty = info.missingKnown && info.missingCount == input.rowCount;
        if (info.type != column.type && !widens && !empty) {
            problems.push_back(column.name + " is " + columnTypeName(info.type) + ", not " +
                               columnTypeName(column.type));
            continue;
        }
        if (column.notNull && (info.missingCount > 0 || !info.missingKnown)) {
            problems.push_back(column.name + " has missing values; needed by " + join(column.notNullFor));
        }
        if (info.hasRange && info.min < column.min) {
            problems.push_back(column.name + " has " + formatNumber(info.min) + " < " + formatNumber(column.min) +
                               "; needed by " + join(column.minFor));
        }
        if (info.hasRange && info.max > column.max) {
            problems.push_back(column.name + " has " + formatNumber(info.max) + " > " + formatNumber(column.max) +
                               "; needed by " + join(column.maxFor));
        }
    }
    return problems;
}

void printCertificate(std::ostream& out, const Certificate& certificate) {
    if (certificate.exactOnly) {
        out << "The proofs depend on the exact values of the input files, so the generated program\n"
            << "accepts only the files it was compiled with.\n";
        return;
    }
    out << "The compiled program stays correct for any input with these facts\n"
        << "(found by analysing the program " << certificate.analysisRuns << " more times with weaker facts):\n";
    for (const TableCertificate& table : certificate.tables) {
        out << '\n' << table.key << "  (compiled with " << table.file << ")\n";
        size_t width = 8;
        for (const ColumnFacts& column : table.columns) {
            width = std::max(width, column.name.size() + 2);
        }
        for (const ColumnFacts& column : table.columns) {
            std::vector<std::pair<std::string, std::string>> facts;
            if (column.notNull) {
                facts.emplace_back("no missing values", join(column.notNullFor));
            }
            if (!std::isinf(column.min)) {
                facts.emplace_back(">= " + formatNumber(column.min), join(column.minFor));
            }
            if (!std::isinf(column.max)) {
                facts.emplace_back("<= " + formatNumber(column.max), join(column.maxFor));
            }
            out << "  " << std::left << std::setw(static_cast<int>(width)) << column.name << std::setw(9)
                << columnTypeName(column.type);
            if (facts.empty()) {
                out << "(any values)\n";
            }
            for (size_t i = 0; i < facts.size(); ++i) {
                if (i > 0) {
                    out << "  " << std::setw(static_cast<int>(width + 9)) << "";
                }
                out << std::setw(20) << facts[i].first << "<- " << facts[i].second << '\n';
            }
        }
    }
    if (!certificate.recheckedIfChanged.empty()) {
        out << "\nProven from the values, so checked again when an input changes:\n";
        for (const std::string& contract : certificate.recheckedIfChanged) {
            out << "  " << contract << '\n';
        }
    }
    if (!certificate.fixedImageFolders.empty()) {
        out << "\nImage inputs must stay exactly as compiled (fingerprint): ";
        for (size_t i = 0; i < certificate.fixedImageFolders.size(); ++i) {
            out << (i > 0 ? ", " : "") << certificate.fixedImageFolders[i];
        }
        out << '\n';
    }
}
