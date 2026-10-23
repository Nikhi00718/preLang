#pragma once

#include "ast/AST.h"
#include "certify/Certifier.h"
#include "ir/IR.h"
#include "optimizer/PassManager.h"
#include "schema/SchemaProvider.h"

#include <filesystem>
#include <set>
#include <string>
#include <utility>

// Columns whose numeric type may change between inputs.
//
// A data certificate requires every column the program reads to have the type of the
// compiled file, because the type can change what the analysis decides. For many INTEGER
// and FLOAT columns it does not: compiled with the other numeric type, the program is the
// same generated program, certificates included. An input in which such a column has the
// other type is then compiled exactly as the program already is, and the program may read
// the column with the type the input has (ColumnFacts::numberKind, "NUMBER").

using TableColumn = std::pair<std::string, std::string>;  // table key, column name

// The columns of `code` that some instruction combines with another column: DERIVE inputs
// and results, JOIN keys and added columns, AGGREGATE groups, inputs and results, and the
// keys of REFERENCES and MATCH_RATE. The type of any other column only meets constants.
std::set<TableColumn> combinedColumns(const IRProgram& code);

// The INTEGER and FLOAT columns the program reads whose other numeric type gives the same
// generated program. Only columns that no instruction combines with another column are
// tried: every decision of the analysis then involves the type of at most one of them, so
// flipping several at once changes nothing that flipping each did not; the compilation
// with all of them flipped is checked as well. No file is read again: the facts of the
// first compilation (`optimized.tables`) are given to `schemas` as overrides.
// `unoptimized` is the IR before the optimizer ran.
std::set<TableColumn> numberKindColumns(const Program& program, SchemaProvider& schemas,
                                        const std::filesystem::path& programFolder,
                                        const std::filesystem::path& outputFolder, const std::string& sourcePath,
                                        const IRProgram& unoptimized, const IRProgram& optimized,
                                        const Certificate& certificate, const OptimizerOptions& passes);
