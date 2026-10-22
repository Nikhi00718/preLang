#pragma once

#include "ast/AST.h"

#include <ostream>
#include <string>
#include <vector>

// Writes the AST as a Graphviz graph, for a picture of the tree:
//   dot -Tpng output/ast.dot -o output/ast.png
class ASTDot : public ASTVisitor {
public:
    explicit ASTDot(std::ostream& out);

    void write(const Program& program);

    void visit(const LoadStmt& node) override;
    void visit(const DropNullStmt& node) override;
    void visit(const FillNullStmt& node) override;
    void visit(const NormalizeStmt& node) override;
    void visit(const FilterStmt& node) override;
    void visit(const SelectStmt& node) override;
    void visit(const DropStmt& node) override;
    void visit(const ParseDateStmt& node) override;
    void visit(const DeriveStmt& node) override;
    void visit(const JoinStmt& node) override;
    void visit(const AggregateStmt& node) override;
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
    std::ostream& out_;
    int nextId_ = 0;
    int parent_ = 0;  // node that new nodes are attached to

    int addNode(const std::string& label, bool isField);
    void addStatement(const std::string& label, const std::vector<std::string>& fields);
};
