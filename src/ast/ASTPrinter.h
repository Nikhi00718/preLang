#pragma once

#include "ast/AST.h"

#include <functional>
#include <ostream>
#include <string>
#include <vector>

// Prints the AST as an indented tree:
//   Program
//   |-- Load
//   |   |-- file: "students.csv"
//   |   `-- dataset: students
class ASTPrinter : public ASTVisitor {
public:
    explicit ASTPrinter(std::ostream& out);

    void print(const Program& program);

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
    std::ostream& out_;
    std::string indent_;       // prefix for lines at the current depth
    bool isLastChild_ = true;  // whether the node being visited is its parent's last child

    void printLine(const std::string& text, bool isLast);
    void printChildren(bool parentIsLast, const std::function<void()>& printBody);
    void printNode(const std::string& label, const std::vector<std::string>& fields);
};
