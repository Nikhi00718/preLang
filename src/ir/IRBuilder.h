#pragma once

#include "ast/AST.h"
#include "ir/IR.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <ostream>
#include <string>

// Intermediate code generation: walks the checked AST and emits IR instructions.
class IRBuilder : public ASTVisitor {
public:
    IRBuilder(const SymbolTable& symbols, const AnalysisResult& analysis);

    IRProgram build(const Program& program);

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
    const SymbolTable& symbols_;
    const AnalysisResult& analysis_;
    IRProgram program_;

    const Stmt* statement_ = nullptr;  // statement being translated
    std::string filterDataset_;        // dataset of the FILTER being translated
    int nextTemp_ = 1;
    int lastTemp_ = 0;  // temporary that holds the condition translated last

    void emit(Instruction instruction);
};

// One instruction as text, e.g.  t1 = students.marks >= 50
std::string instructionText(const Instruction& instruction);

// Prints the numbered instruction list with the source line of each.
void printIR(std::ostream& out, const IRProgram& program);
