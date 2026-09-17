#pragma once

#include "common/Diagnostics.h"

#include <memory>
#include <string>
#include <vector>

// Abstract Syntax Tree: the structure of a PrepLang program without the
// punctuation (';', '[', ...) that only the parser needs.

struct LoadStmt;
struct DropNullStmt;
struct FillNullStmt;
struct NormalizeStmt;
struct FilterStmt;
struct SelectStmt;
struct DropStmt;
struct SaveStmt;
struct ExpectStmt;
struct ComparisonExpr;
struct LogicalExpr;
struct NotExpr;

// Visitor pattern: code that walks the tree (the printer now, the semantic
// analyzer later) implements one visit() per node type.
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(const LoadStmt& node) = 0;
    virtual void visit(const DropNullStmt& node) = 0;
    virtual void visit(const FillNullStmt& node) = 0;
    virtual void visit(const NormalizeStmt& node) = 0;
    virtual void visit(const FilterStmt& node) = 0;
    virtual void visit(const SelectStmt& node) = 0;
    virtual void visit(const DropStmt& node) = 0;
    virtual void visit(const SaveStmt& node) = 0;
    virtual void visit(const ExpectStmt& node) = 0;
    virtual void visit(const ComparisonExpr& node) = 0;
    virtual void visit(const LogicalExpr& node) = 0;
    virtual void visit(const NotExpr& node) = 0;
};

// ---------- Values ----------

enum class LiteralKind { Number, String, Boolean };

struct Literal {
    LiteralKind kind;
    std::string value;  // e.g. "50", "-2.5", "UNKNOWN", "TRUE"
};

// dataset.column, e.g. students.age
struct ColumnRef {
    SourceLocation location;  // where the reference starts
    std::string dataset;
    std::string column;
};

// ---------- FILTER conditions ----------

struct Expr {
    virtual ~Expr() = default;
    virtual void accept(ASTVisitor& visitor) const = 0;
};

// column <op> value, e.g. marks >= 50
struct ComparisonExpr : Expr {
    SourceLocation location;  // where the column name is
    std::string column;
    std::string op;  // ==  !=  <  <=  >  >=
    Literal value;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

enum class LogicalOp { And, Or };

// left AND right, left OR right
struct LogicalExpr : Expr {
    LogicalOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// NOT operand
struct NotExpr : Expr {
    std::unique_ptr<Expr> operand;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// ---------- Statements ----------

struct Stmt {
    SourceLocation location;  // position of the statement's first keyword
    virtual ~Stmt() = default;
    virtual void accept(ASTVisitor& visitor) const = 0;
};

// LOAD "students.csv" AS students;
struct LoadStmt : Stmt {
    std::string path;
    std::string dataset;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// DROP_NULL students;
struct DropNullStmt : Stmt {
    std::string dataset;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// FILL_NULL students.age WITH 0;
struct FillNullStmt : Stmt {
    ColumnRef target;
    Literal value;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

enum class NormalizeMethod { ZScore, MinMax };

// NORMALIZE students.age USING MINMAX;
struct NormalizeStmt : Stmt {
    ColumnRef target;
    NormalizeMethod method = NormalizeMethod::ZScore;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// FILTER students WHERE marks >= 50;
struct FilterStmt : Stmt {
    std::string dataset;
    std::unique_ptr<Expr> condition;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// SELECT students [name, age];
struct SelectStmt : Stmt {
    std::string dataset;
    std::vector<std::string> columns;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// DROP students [phone];
struct DropStmt : Stmt {
    std::string dataset;
    std::vector<std::string> columns;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// SAVE students AS "clean.csv";
struct SaveStmt : Stmt {
    std::string dataset;
    std::string path;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

enum class ConstraintKind { IsNumber, IsString, IsBoolean, NotNull, Between };

// EXPECT students.marks BETWEEN 0 AND 100;
struct ExpectStmt : Stmt {
    ColumnRef target;
    ConstraintKind constraint;
    std::string low;   // only used by BETWEEN
    std::string high;  // only used by BETWEEN
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// The root of the tree.
struct Program {
    std::vector<std::unique_ptr<Stmt>> statements;
};
