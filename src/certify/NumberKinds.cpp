#include "certify/NumberKinds.h"

#include "backend/ExecutionPlanner.h"
#include "common/Diagnostics.h"
#include "explain/ManifestWriter.h"
#include "ir/IRBuilder.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <map>
#include <optional>
#include <vector>

std::set<TableColumn> combinedColumns(const IRProgram& code) {
    std::set<TableColumn> combined;
    for (const Instruction& in : code.code) {
        const std::string key = tableKey(in.dataset, in.table);
        switch (in.op) {
            case Opcode::Derive:
            case Opcode::Aggregate:
                combined.insert({key, in.column});
                for (const std::string& name : in.columns) {
                    combined.insert({key, name});
                }
                for (const AggregateResult& item : in.aggregates) {
                    combined.insert({key, item.column});
                    combined.insert({key, item.name});
                }
                break;
            case Opcode::Join:
                combined.insert({key, in.column});
                combined.insert({tableKey(in.withDataset, in.withTable), in.column});
                for (const std::string& name : in.columns) {
                    combined.insert({key, name});
                    combined.insert({tableKey(in.withDataset, in.withTable), name});
                }
                break;
            case Opcode::Check:
                if (in.text == "REFERENCES" || in.text == "MATCH_RATE") {
                    combined.insert({key, in.column});
                    if (in.columns.size() >= 3) {
                        combined.insert({tableKey(in.columns[0], in.columns[1]), in.columns[2]});
                        combined.insert({in.columns[1], in.columns[2]});
                    }
                }
                break;
            default: break;
        }
    }
    return combined;
}

std::set<TableColumn> numberKindColumns(const Program& program, SchemaProvider& schemas,
                                        const std::filesystem::path& programFolder,
                                        const std::filesystem::path& outputFolder, const std::string& sourcePath,
                                        const IRProgram& unoptimized, const IRProgram& optimized,
                                        const Certificate& certificate, const OptimizerOptions& passes) {
    std::set<TableColumn> accepted;
    if (certificate.exactOnly) {
        return accepted;
    }
    const std::set<TableColumn> combined = combinedColumns(unoptimized);
    std::vector<TableColumn> candidates;
    for (const TableCertificate& table : certificate.tables) {
        for (const ColumnFacts& column : table.columns) {
            if (column.read && column.typeFixed && isNumeric(column.type) &&
                combined.count({table.key, column.name}) == 0) {
                candidates.push_back({table.key, column.name});
            }
        }
    }
    if (candidates.empty()) {
        return accepted;
    }
    std::map<std::string, TableSchema> base;
    std::map<std::string, std::string> overrideOf;
    for (const auto& [key, schema] : optimized.tables) {
        overrideOf[key] = SchemaProvider::overrideKey(schema.file, schema.name);
        base[overrideOf[key]] = schema;
    }
    const auto compile = [&](const std::set<TableColumn>& flipped,
                             const std::set<TableColumn>& marked) -> std::optional<std::string> {
        std::map<std::string, TableSchema> facts = base;
        for (const auto& [key, name] : flipped) {
            for (ColumnInfo& info : facts.at(overrideOf.at(key)).columns) {
                if (info.name == name) {
                    info.type = info.type == ColumnType::Integer ? ColumnType::Float : ColumnType::Integer;
                }
            }
        }
        schemas.setOverrides(facts);
        Diagnostics quiet;
        SymbolTable symbols;
        const AnalysisResult analysis =
            SemanticAnalyzer(symbols, schemas, programFolder, quiet).withoutLineage().analyze(program);
        if (quiet.hasErrors()) {
            schemas.setOverrides({});
            return std::nullopt;
        }
        IRProgram code = IRBuilder(symbols, analysis).build(program);
        const std::vector<double> constants = programConstants(code);
        PassManager(passes).run(code);
        Certificate certified = Certifier(program, schemas, programFolder).certify(code, analysis, constants);
        schemas.setOverrides({});
        markNumberKinds(certified, marked);
        return ExecutionPlanner(outputFolder, sourcePath, certified)
            .generate(code, manifestCode(code, analysis, sourcePath, outputFolder));
    };
    // A candidate that no instruction names besides LOAD, SELECT, DROP, SAVE, REPORT and a DROP_NULL
    // of whole rows is only carried to the output: no decision of the analysis looks at its type.
    // These are tried together, in one compilation, so that a wide table does not cost one
    // compilation per column; each named candidate is tried on its own.
    std::set<TableColumn> named;
    for (const Instruction& in : unoptimized.code) {
        const std::string key = tableKey(in.dataset, in.table);
        switch (in.op) {
            case Opcode::Load:
            case Opcode::Select:
            case Opcode::Drop:
            case Opcode::Save:
            case Opcode::Report: break;
            default:
                named.insert({key, in.column});
                for (const std::string& name : in.columns) {
                    named.insert({key, name});
                }
        }
    }
    std::set<TableColumn> carried;
    std::vector<TableColumn> tried;
    for (const TableColumn& column : candidates) {
        if (named.count(column) == 0) {
            carried.insert(column);
        } else {
            tried.push_back(column);
        }
    }
    if (!carried.empty()) {
        const std::optional<std::string> same = compile({}, carried);
        if (same && compile(carried, carried) == same) {
            accepted = carried;
        } else {
            tried.insert(tried.end(), carried.begin(), carried.end());  // then one by one, as below
        }
    }
    for (const TableColumn& column : tried) {
        const std::set<TableColumn> marked = {column};
        const std::optional<std::string> same = compile({}, marked);
        if (same && compile(marked, marked) == same) {
            accepted.insert(column);
        }
    }
    if (accepted.size() > 1 && compile(accepted, accepted) != compile({}, accepted)) {
        accepted.clear();  // not expected (see above); then no column may change its type
    }
    return accepted;
}
