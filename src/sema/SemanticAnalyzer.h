#pragma once

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "schema/SchemaProvider.h"
#include "sema/AnalysisResult.h"
#include "sema/ContractAnalyzer.h"
#include "sema/SelectorResolver.h"
#include "sema/SymbolTable.h"

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

// Semantic analysis:
//  * name resolution: datasets, tables and columns must exist (and not be removed yet);
//    wildcard and type-based selectors are expanded into concrete targets
//  * kind checking: table statements need a TABLE/TABLESET, image statements an IMAGE/IMAGESET
//  * type checking: e.g. NORMALIZE needs numeric columns; JPG cannot hold FLOAT32 pixels
//  * state tracking: after every statement it updates what is known about the data
//    (column types, missing values, ranges; image sizes, channels, pixel type)
//  * contracts: each EXPECT is PROVEN, VIOLATED or left for run time
class SemanticAnalyzer : public ASTVisitor {
public:
    // CSV and image paths in LOAD are relative to baseFolder (the folder of the .prep file).
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
    void visit(const ParseDateStmt& node) override;
    void visit(const DeriveStmt& node) override;
    void visit(const SaveStmt& node) override;
    void visit(const ExpectStmt& node) override;
    void visit(const ResizeStmt& node) override;
    void visit(const GrayscaleStmt& node) override;
    void visit(const CompressStmt& node) override;
    void visit(const ConvertStmt& node) override;
    void visit(const RemoveCorruptStmt& node) override;
    void visit(const LinkStmt& node) override;
    void visit(const ComparisonExpr& node) override;
    void visit(const LogicalExpr& node) override;
    void visit(const NotExpr& node) override;

private:
    // What a FILTER condition can be for the rows of the table.
    enum class Truth { AlwaysTrue, NeverTrue, Unknown };

    SymbolTable& symbols_;
    SchemaProvider& schemas_;
    std::filesystem::path baseFolder_;
    Diagnostics& diagnostics_;
    AnalysisResult result_;
    std::set<std::string> unreadable_;  // datasets whose files could not be read
    SelectorResolver resolver_;
    ContractAnalyzer contracts_;
    std::set<std::string> corruptReported_;  // image datasets whose broken files were already reported

    DatasetSymbol* filterDataset_ = nullptr;  // table that column names in WHERE belong to
    TableState* filterTable_ = nullptr;
    Truth truth_ = Truth::Unknown;            // result of the condition visited last
    std::string neverTrueReason_;             // why a comparison can never be true

    void normalizeImages(const NormalizeStmt& node);
    void saveImages(const SaveStmt& node);
    void expectColumns(const ExpectStmt& node);
    bool checkTypes(const Selector& selector, const DatasetSymbol& dataset, const std::vector<ColumnMatch>& matched,
                    const std::function<bool(const ColumnState&)>& fits, const std::string& rule,
                    const std::function<std::string(const ColumnState&)>& hintFor);
    bool decodable(DatasetSymbol& images, const Stmt& node);
    void refine(const Expr& condition, TableState& table, std::vector<std::string>& learned);

    void addTarget(const Stmt& node, const Target& target, const std::string& noEffectReason = "");
    Target tableTarget(const DatasetSymbol& dataset, const TableState& table, const std::string& column = "") const;
    void recordColumn(const DatasetSymbol& dataset, const TableState& table, const ColumnState& column, int line,
                      const std::string& operation);
    void recordImages(const DatasetSymbol& images, int line, const std::string& operation);
    void log(const Stmt& node, const std::string& text);
};
