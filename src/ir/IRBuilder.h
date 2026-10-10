#pragma once

#include "ast/AST.h"
#include "ir/IR.h"
#include "sema/AnalysisResult.h"
#include "sema/SymbolTable.h"

#include <ostream>
#include <string>

// Intermediate code generation: walks the checked AST and emits IR
// instructions, one per resolved target of each statement.
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
    const SymbolTable& symbols_;
    const AnalysisResult& analysis_;
    IRProgram program_;

    const Stmt* statement_ = nullptr;  // statement being translated
    Target filterTarget_;              // table of the FILTER being translated
    int nextTemp_ = 1;
    int lastTemp_ = 0;  // temporary that holds the condition translated last

    const std::vector<Target>& targets() const;
    // Instruction for one target, tagged with its line and, if analysis proved
    // that this part of the statement changes nothing, with the reason.
    Instruction make(Opcode op, const Target& target) const;
    void emit(Instruction instruction);
};

// One instruction as text, e.g.  t1 = students.marks >= 50
std::string instructionText(const Instruction& instruction);

// Prints the numbered instruction list with the source line of each.
void printIR(std::ostream& out, const IRProgram& program);
