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
// the data (the schema: column names and types, and the numbers in the program),
// and then tools/release_facts.py. The numbers cut each column into cells (below
// k1, exactly k1, between k1 and k2, ..., missing), and every decision of the
// analysis depends only on which cells hold some row; the tool releases a noisy
// (differentially private) histogram of each column over those cells. The compiler
// builds its abstract state from the released facts alone, so everything it writes
// is differentially private too: it is computed from them and nothing else. The
// schema is published as the owner declares it: a type inferred from the values
// would itself be a fact about them.
//
// The facts may be wrong, because they are noisy, and no differentially private
// release can certify that no value is missing or below k. Nothing the compiler
// derives from them is treated as exact: no contract is reported as violated,
// contracts that need the values are checked at run time, and every removal is
// speculative. The generated program, run by the data owner, checks the item
// certificates on the real data, so a wrong fact costs a deoptimization, never a
// wrong result (see the section on selective deoptimization in the paper).

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
    TableSchema schema;    // read from the file by the data owner's compiler: its names and types
                           // are the owner's draft of the public schema
    std::set<std::string> ranged;   // columns the program names: missing values and bounds are asked
    std::set<std::string> missing;  // other columns whose missing values matter (DROP_NULL of whole rows)
};

// What a private compilation of this program asks about the data: the schema of every
// table it loads (the owner's draft, to be reviewed before it is published), which
// columns are asked about, and the numbers in the program, which define the cells.
// Written by --queries, read by tools/release_facts.py.
void writeQueries(std::ostream& out, const std::vector<QueryTable>& tables, const std::vector<double>& constants);
