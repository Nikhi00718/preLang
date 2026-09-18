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

inline std::string targetKey(const Target& target) {
    return target.dataset + "." + target.table + "." + target.column;
}

enum class Verdict { Proven, Violated, Deferred };

struct ContractRecord {
    int line;
    std::string contract;  // e.g. "students.marks BETWEEN 0 AND 100"
    Verdict verdict;
    std::string reason;
};

// Everything semantic analysis hands to the later phases.
struct AnalysisResult {
    // Resolved targets of each statement, in order, for the IR builder.
    std::map<const Stmt*, std::vector<Target>> targets;
    // (statement, target key) -> why that part provably changes nothing.
    std::map<std::pair<const Stmt*, std::string>, std::string> noEffect;
    std::vector<ContractRecord> contracts;
    std::vector<std::string> log;  // what the compiler learned at each statement
    LineageTracker lineage;
};
