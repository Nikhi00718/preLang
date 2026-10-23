#pragma once

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "sema/AnalysisResult.h"
#include "sema/SymbolTable.h"

#include <string>

// Decides every EXPECT contract at compile time where it can:
//   PROVEN    the known facts imply it      -> no run-time check is needed
//   VIOLATED  the data certainly breaks it  -> compile error, naming the bad rows or files
//   DEFERRED  it cannot be decided here     -> a run-time check is generated
// A deferred contract also refines the state: once its run-time check has
// passed, the fact it states holds (e.g. NOT_NULL -> no missing values).
class ContractAnalyzer {
public:
    ContractAnalyzer(Diagnostics& diagnostics, AnalysisResult& result);

    // NUMBER, STRING, BOOLEAN, NOT_NULL, BETWEEN or UNIQUE on one matched column.
    void columnContract(const ExpectStmt& node, const std::string& name, const Target& target, const TableState& table,
                        ColumnState& column);
    // column REFERENCES otherColumn (a foreign key)
    void references(const ExpectStmt& node, const std::string& name, const Target& target, const TableState& table,
                    const ColumnState& column, const std::string& otherName, const TableState& otherTable,
                    const ColumnState& otherColumn);
    void schemaSameAs(const ExpectStmt& node, const std::string& name, const TableState& table,
                      const std::string& otherName, const TableState& other);
    // CHANNELS, MIN_WIDTH, MIN_HEIGHT
    void imageContract(const ExpectStmt& node, const DatasetSymbol& images);
    void matchRate(const ExpectStmt& node, const DatasetSymbol& linked, const Target& target);

private:
    Diagnostics& diagnostics_;
    AnalysisResult& result_;

    void between(const ExpectStmt& node, const std::string& contract, const std::string& name, const Target& target,
                 const TableState& table, ColumnState& column);
    void unique(const ExpectStmt& node, const std::string& contract, const std::string& name, const Target& target,
                const TableState& table, const ColumnState& column);

    // `runtimeTarget` is set for contracts that would otherwise need a run-time CHECK.
    // `valueTables` lists the tables whose values were read to prove it (empty if
    // the proof uses only the abstract state).
    void proven(const ExpectStmt& node, const std::string& contract, const std::string& reason,
                const Target* runtimeTarget, const std::vector<std::string>& valueTables = {});
    Diagnostic& violated(const ExpectStmt& node, const std::string& contract, const std::string& message,
                         const std::string& hint);
    void deferred(const ExpectStmt& node, const std::string& contract, const std::string& reason);
    void likelyViolated(const ExpectStmt& node, const std::string& contract, const std::string& evidence);
};
