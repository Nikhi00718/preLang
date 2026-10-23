#include "explain/ExplainPlan.h"

#include "backend/ExecutionPlanner.h"

#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <vector>

namespace {

void section(std::ostream& out, const std::string& title) {
    out << '\n' << title << "\n" << std::string(title.size(), '-') << '\n';
}

void row(std::ostream& out, const std::string& label, const std::string& value) {
    out << "  " << std::left << std::setw(30) << label << value << '\n';
}

std::string join(const std::vector<std::string>& items) {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i > 0 ? ", " : "") + items[i];
    }
    return text;
}

std::string inputDetails(const DatasetSymbol& dataset) {
    switch (dataset.kind) {
        case DatasetKind::Table: {
            const TableSchema& schema = dataset.tables.front().schema;
            if (schema.released) {
                return schema.file + ", " + std::to_string(schema.columns.size()) +
                       " columns (released facts only; the file was not read)";
            }
            return schema.file + ", " + std::to_string(schema.rowCount) + " rows, " +
                   std::to_string(schema.columns.size()) + " columns" +
                   (schema.format == TableFormat::Parquet ? " (footer statistics only)" : "");
        }
        case DatasetKind::TableSet: {
            std::int64_t rows = 0;
            for (const TableState& table : dataset.tables) {
                rows += table.schema.rowCount;
            }
            return std::to_string(dataset.tables.size()) + " tables (" + join(dataset.tableNames()) + "), " +
                   std::to_string(rows) + " rows";
        }
        case DatasetKind::Image:
        case DatasetKind::ImageSet:
            return dataset.imageSchema.root + ", " + std::to_string(dataset.imageSchema.images.size()) + " file(s)";
        case DatasetKind::Dataset:
            return dataset.link.images + " linked with " + dataset.link.table + "." + dataset.link.column +
                   (dataset.link.exact ? ", " + std::to_string(dataset.link.matched) + " matched" : "");
    }
    return "";
}

}  // namespace

void printExplain(std::ostream& out, const SymbolTable& symbols, const AnalysisResult& analysis,
                  const OptimizationReport& report, const IRProgram& program, const Certificate& certificate,
                  int warnings) {
    out << "PREPLANG EXECUTION PLAN\n";

    section(out, "INPUTS");
    int schemas = 0;
    for (const DatasetSymbol& dataset : symbols.datasets()) {
        out << "  " << std::left << std::setw(16) << dataset.name << std::setw(10) << datasetKindName(dataset.kind)
            << inputDetails(dataset) << '\n';
        schemas += dataset.isTabular() ? static_cast<int>(dataset.tables.size()) : dataset.isImage() ? 1 : 0;
    }

    section(out, "STATIC ANALYSIS");
    std::map<Verdict, int> verdicts;
    for (const ContractRecord& contract : analysis.contracts) {
        ++verdicts[contract.verdict];
    }
    row(out, "schemas inferred", std::to_string(schemas));
    row(out, "contracts proven", std::to_string(verdicts[Verdict::Proven]));
    row(out, "contracts violated", std::to_string(verdicts[Verdict::Violated]));
    row(out, "contracts checked at run time", std::to_string(verdicts[Verdict::Deferred]));
    row(out, "semantic errors", "0");
    row(out, "warnings", std::to_string(warnings));

    section(out, "OPTIMIZATIONS");
    std::map<std::string, int> kinds;
    for (const std::string& change : report.changes) {
        ++kinds[change.substr(0, change.find(':'))];
    }
    for (const auto& [kind, count] : kinds) {
        row(out, kind, std::to_string(count));
    }
    row(out, "instructions",
        std::to_string(report.instructionsBefore) + " -> " + std::to_string(report.instructionsAfter));
    if (!report.suggestions.empty()) {
        row(out, "approximate rewrites skipped", std::to_string(report.suggestions.size()) + " (use --approx)");
    }

    section(out, "DATA CERTIFICATE");
    int columns = 0, facts = 0;
    for (const TableCertificate& table : certificate.tables) {
        for (const ColumnFacts& column : table.columns) {
            ++columns;
            facts += column.notNull + !std::isinf(column.min) + !std::isinf(column.max);
        }
    }
    row(out, "columns read", std::to_string(columns) + " (type fixed)");
    row(out, "value facts the proofs need", std::to_string(facts) + " (see --certificate)");
    row(out, "re-checked if inputs change", std::to_string(certificate.recheckedIfChanged.size()));
    int folders = 0;
    for (const ImageCertificate& image : certificate.images) {
        folders += !image.fixed;
    }
    row(out, "image folders certified", std::to_string(folders) + " of " + std::to_string(certificate.images.size()));
    row(out, "other inputs",
        certificate.exactOnly ? "only the compiled files are accepted"
                              : "admitted by generated.py name=file-or-folder");

    section(out, "BACKENDS");
    int tableInstructions = 0;
    int imageInstructions = 0;
    for (const Instruction& instruction : program.code) {
        ++(isImageInstruction(instruction) ? imageInstructions : tableInstructions);
    }
    if (tableInstructions > 0) {
        row(out, "tables", "Python + pandas (" + std::to_string(tableInstructions) + " instructions)");
    }
    if (imageInstructions > 0) {
        row(out, "images", "Python + OpenCV, streamed one image at a time");
        for (const std::string& pass : ExecutionPlanner::imagePasses(program)) {
            out << "    " << pass << '\n';
        }
    }
    out << "\nNo files have been modified.\n";
}

void printDryRun(std::ostream& out, const IRProgram& program, const std::string& outputFolder) {
    std::vector<std::string> reads, changes, writes;
    for (const Instruction& in : program.code) {
        const std::string table = tableKey(in.dataset, in.table);
        const std::string line = " (line " + std::to_string(in.line) + ")";
        switch (in.op) {
            case Opcode::Load:
                reads.push_back(table + ": " + std::to_string(in.columns.size()) + " of " +
                                std::to_string(program.tables.at(table).columns.size()) + " columns");
                break;
            case Opcode::LoadImages:
                reads.push_back(in.dataset + ": " + std::to_string(program.images.at(in.dataset).images.size()) +
                                " image file(s)");
                break;
            case Opcode::FillNull: changes.push_back(table + "." + in.column + ": missing values filled" + line); break;
            case Opcode::Normalize:
                changes.push_back(table + "." + in.column + ": rescaled with " + in.text + line);
                break;
            case Opcode::DropNull:
            case Opcode::Filter:   changes.push_back(table + ": rows removed" + line); break;
            case Opcode::Select:
            case Opcode::Drop:     changes.push_back(table + ": columns removed" + line); break;
            case Opcode::Join:
                changes.push_back(table + ": columns added from " + tableKey(in.withDataset, in.withTable) +
                                  (in.text == "MANY" ? ", rows repeated for each match" : "") + line);
                break;
            case Opcode::Aggregate: changes.push_back(table + ": one row per group" + line); break;
            case Opcode::Save:
            case Opcode::Plot:
            case Opcode::Report:    writes.push_back(outputFolder + "/" + in.text); break;
            case Opcode::SaveImages:
                writes.push_back(outputFolder + "/" + in.text + " (" +
                                 std::to_string(program.images.at(in.dataset).images.size()) + " image file(s))");
                break;
            default: break;
        }
    }
    writes.push_back(outputFolder + "/preplang_manifest.json");

    out << "DRY RUN\n";
    section(out, "INPUTS READ");
    for (const std::string& item : reads) {
        out << "  " << item << '\n';
    }
    section(out, "CHANGES");
    for (const std::string& item : changes) {
        out << "  " << item << '\n';
    }
    for (const std::string& pass : ExecutionPlanner::imagePasses(program)) {
        out << "  " << pass << '\n';
    }
    section(out, "FILES THE PROGRAM WOULD WRITE");
    for (const std::string& item : writes) {
        out << "  " << item << '\n';
    }
    out << "\nNo files were written.\n";
}
