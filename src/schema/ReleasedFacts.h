#pragma once

#include "schema/TableSchema.h"

#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

// Private compilation: compiling against data the compiler may not see.
//
// Normally the compiler reads every input file, and what it writes shows facts
// about them: the removed operations say a column has no missing values, the
// certificate bounds say no value is below a constant, a violated contract names a
// row, and the fingerprint lets anyone test whether a guessed file is the input.
//
// In a private compilation the compiler never opens the input files. The data
// owner runs the compiler with --queries, which writes what the program asks about
// the data (the public schema: column names and types, and the numbers in the
// program), and then tools/release_facts.py, which answers only threshold
// questions at those numbers ("is any value below 0?", "is any value missing?")
// with a differentially private mechanism. The compiler builds its abstract state
// from those answers alone, so everything it writes is differentially private
// too: it is computed from the released answers and nothing else.
//
// The answers may be wrong, because they are noisy. Nothing the compiler derives
// from them is treated as exact: no contract is reported as violated, contracts
// that need the values are checked at run time, and every removal is speculative.
// The generated program, run by the data owner, checks the item certificates on
// the real data, so a wrong answer costs a deoptimization, never a wrong result
// (see the section on selective deoptimization in the paper).

struct ReleasedFacts {
    std::string mechanism;                      // how the answers were released, e.g. its epsilon
    std::map<std::string, TableSchema> tables;  // by SchemaProvider::overrideKey(path, name)
};

// Reads a facts file written by tools/release_facts.py. Table paths in it are as the
// LOAD statements write them; they are resolved against `programFolder` like LOAD
// paths. Returns nullopt and sets `problem` if the file cannot be used.
std::optional<ReleasedFacts> readReleasedFacts(const std::filesystem::path& file,
                                               const std::filesystem::path& programFolder, std::string& problem);

// One LOAD of a table, as the data owner needs it to release facts.
struct QueryTable {
    std::string loadPath;  // as written in the LOAD statement
    std::string name;      // the dataset name after AS
    TableSchema schema;    // read from the file by the data owner's compiler
    std::set<std::string> ranged;  // columns whose bounds the analysis uses (compared, filled,
                                   // normalized or derived from); only missing values are asked
                                   // about the others
};

// The questions a private compilation of this program asks: the public schema of
// every table it loads and the numbers in the program, at which the answers are
// thresholds. Written by --queries, read by tools/release_facts.py.
void writeQueries(std::ostream& out, const std::vector<QueryTable>& tables, const std::vector<double>& constants);
