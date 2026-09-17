#pragma once

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "schema/SchemaProvider.h"
#include "sema/SymbolTable.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

// What semantic analysis found out, for the later phases and for printing.
struct AnalysisResult {
    // Statements proven to have no effect on the data, with the reason.
    // Their IR instructions are removed by the optimizer.
    std::map<const Stmt*, std::string> noEffect;

    // One line per statement: what the compiler learned at that point.
    std::vector<std::string> log;
};

// Semantic analysis:
//  * name resolution: datasets and columns must exist (and not be removed yet)
//  * type checking: e.g. NORMALIZE needs a numeric column
//  * flow analysis: after every statement it updates what is known about each
//    column (type, missing values, value range), e.g. DROP_NULL -> no missing values
//  * data contracts: each EXPECT is PROVEN, VIOLATED (error) or checked at run time
class SemanticAnalyzer : public ASTVisitor {
public:
    // CSV paths in LOAD are relative to baseFolder (the folder of the .prep file).
    SemanticAnalyzer(SymbolTable& symbols, SchemaProvider& schemas, std::filesystem::path baseFolder,
                     Diagnostics& diagnostics);

    AnalysisResult analyze(const Program& program);

    void visit(const LoadStmt& node) override;
    void visit(const DropNullStmt& node) override;
    void visit(const FillNullStmt& node) override;
    void visit(const NormalizeStmt& node) override;
    void visit(const FilterStmt& node) override;
    void visit(const SelectStmt& node) override;
    void visit(const DropStmt& node) override;
    void visit(const SaveStmt& node) override;
    void visit(const ExpectStmt& node) override;
    void visit(const ComparisonExpr& node) override;
    void visit(const LogicalExpr& node) override;
    void visit(const NotExpr& node) override;

private:
    // What a FILTER condition can be for the rows of the dataset.
    enum class Truth { AlwaysTrue, NeverTrue, Unknown };

    SymbolTable& symbols_;
    SchemaProvider& schemas_;
    std::filesystem::path baseFolder_;
    Diagnostics& diagnostics_;
    AnalysisResult result_;

    std::set<std::string> unreadable_;         // datasets whose CSV could not be read
    DatasetSymbol* filterDataset_ = nullptr;  // dataset that column names in WHERE belong to
    Truth truth_ = Truth::Unknown;            // result of the condition visited last
    std::string neverTrueReason_;             // why a comparison can never be true

    DatasetSymbol* findDataset(const std::string& name, SourceLocation location);
    ColumnState* findColumn(DatasetSymbol& dataset, const std::string& name, SourceLocation location);

    void expectType(const ExpectStmt& node, const DatasetSymbol& dataset, const ColumnState& column, bool holds);
    void expectNotNull(const ExpectStmt& node, const DatasetSymbol& dataset, ColumnState& column);
    void expectBetween(const ExpectStmt& node, const DatasetSymbol& dataset, ColumnState& column);

    void refine(const Expr& condition, DatasetSymbol& dataset, std::vector<std::string>& learned);
    void log(const Stmt& node, const std::string& text);
};
