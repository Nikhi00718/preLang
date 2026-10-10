#pragma once

#include "ast/AST.h"
#include "dataset/LineageTracker.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

// One concrete thing a statement works on, after its selector is resolved.
// company.*.age becomes one Target per table that has an age column.
struct Target {
    std::string dataset;
    std::string table;              // table inside a TABLESET; empty for a TABLE and for images
    std::string column;             // empty for table-level and image statements
    std::vector<std::string> args;  // extra facts the IR needs, e.g. the column a REFERENCES
                                    // contract points to, or the pixel range before NORMALIZE
};

// "dataset" for a TABLE, "dataset.table" for a table in a TABLESET
inline std::string tableKey(const std::string& dataset, const std::string& table) {
    return table.empty() ? dataset : dataset + "." + table;
}

inline std::string targetKey(const Target& target) {
    return target.dataset + "." + target.table + "." + target.column;
}

// A stable name for one no-effect fact (statement, target), the same in every run of
// the analysis: "line:column:dataset.table.column". The IR, the certificate and the
// generated program use it to refer to one removed check or operation.
inline std::string proofKey(const Stmt& statement, const std::string& target) {
    return std::to_string(statement.location.line) + ":" + std::to_string(statement.location.column) + ":" + target;
}

enum class Verdict { Proven, Violated, Deferred };

struct ContractRecord {
    int line;
    std::string contract;  // e.g. "students.marks BETWEEN 0 AND 100"
    Verdict verdict;
    std::string reason;
    bool fromValues = false;  // proven by reading the values (UNIQUE, REFERENCES, MATCH_RATE),
                              // so the proof holds for those exact files only
    bool runtimeCheck = false;  // a run-time check exists for it (proven: the check was removed);
                                // type and image contracts have none
};

// What the analysis knows about one column a SAVE writes: the facts every output
// file of the program has, for inputs with the analysed facts (a guarantee).
struct SavedColumn {
    std::string name;
    std::string type;    // INTEGER, FLOAT, STRING, BOOLEAN, DATETIME
    bool nullable = true;
    double min = 0, max = 0;
    bool hasRange = false;
};

struct SavedTable {
    std::string path;   // as written in the SAVE statement
    std::string table;  // table key: "trips", or "company.staff"
    std::vector<SavedColumn> columns;
};

// Everything semantic analysis hands to the later phases.
struct AnalysisResult {
    std::vector<SavedTable> saved;  // one per table a SAVE or SAVE_TABLES writes
    // Resolved targets of each statement, in order, for the IR builder.
    std::map<const Stmt*, std::vector<Target>> targets;
    // (statement, target key) -> why that part provably changes nothing.
    std::map<std::pair<const Stmt*, std::string>, std::string> noEffect;
    // The noEffect entries of contracts proven from the values -> the tables whose values
    // the proof read. If one of those files changes, the check must run after all.
    std::map<std::pair<const Stmt*, std::string>, std::vector<std::string>> valueProofs;
    std::vector<ContractRecord> contracts;
    std::vector<std::string> log;  // what the compiler learned at each statement
    LineageTracker lineage;
};
