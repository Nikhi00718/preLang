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
struct ParseDateStmt;
struct DeriveStmt;
struct JoinStmt;
struct AggregateStmt;
struct SaveStmt;
struct ExpectStmt;
struct ResizeStmt;
struct GrayscaleStmt;
struct CompressStmt;
struct ConvertStmt;
struct RemoveCorruptStmt;
struct LinkStmt;
struct ComparisonExpr;
struct LogicalExpr;
struct NotExpr;

// Visitor pattern: code that walks the tree (printers, semantic analysis,
// IR generation) implements one visit() per node type.
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
    virtual void visit(const ParseDateStmt& node) = 0;
    virtual void visit(const DeriveStmt& node) = 0;
    virtual void visit(const JoinStmt& node) = 0;
    virtual void visit(const AggregateStmt& node) = 0;
    virtual void visit(const SaveStmt& node) = 0;
    virtual void visit(const ExpectStmt& node) = 0;
    virtual void visit(const ResizeStmt& node) = 0;
    virtual void visit(const GrayscaleStmt& node) = 0;
    virtual void visit(const CompressStmt& node) = 0;
    virtual void visit(const ConvertStmt& node) = 0;
    virtual void visit(const RemoveCorruptStmt& node) = 0;
    virtual void visit(const LinkStmt& node) = 0;
    virtual void visit(const ComparisonExpr& node) = 0;
    virtual void visit(const LogicalExpr& node) = 0;
    virtual void visit(const NotExpr& node) = 0;
};

// ---------- Values and selectors ----------

enum class LiteralKind { Number, String, Boolean };

struct Literal {
    LiteralKind kind;
    std::string value;  // e.g. "50", "-2.5", "UNKNOWN", "TRUE"
};

// Extra condition of a column selector: WHERE TYPE NUMERIC, WHERE NULLABLE, ...
enum class SelectorFilter { None, Numeric, String, Boolean, Nullable };

// A path to a dataset, its tables or its columns. "*" matches every name.
//   students.age      column age of table students
//   company.*.age     column age of every table in TABLESET company
//   company.*.* WHERE TYPE NUMERIC EXCEPT [id]    every numeric column but id
struct Selector {
    SourceLocation location;
    std::vector<std::string> parts;
    SelectorFilter filter = SelectorFilter::None;
    std::vector<std::string> except;
    bool ifExists = false;  // IF_EXISTS: silently skip tables without the column

    std::string text() const {
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i) {
            result += (i > 0 ? "." : "") + parts[i];
        }
        return result;
    }
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

enum class DataKind { Table, TableSet, Image, ImageSet };

// LOAD "a.csv" AS t;   LOAD_TABLES "dir/" AS ts;
// LOAD_IMAGE "a.png" AS img;   LOAD_IMAGES "dir/" AS imgs RECURSIVE;
struct LoadStmt : Stmt {
    DataKind kind;
    std::string path;
    std::string dataset;
    bool recursive = false;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// DROP_NULL students;   DROP_NULL company.*;
// DROP_NULL students;   DROP_NULL students [age, city];  (only rows missing one of these)
struct DropNullStmt : Stmt {
    Selector target;
    std::vector<std::string> columns;  // empty: rows with a missing value in any column
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// How FILL_NULL finds the fill value: a literal, or a statistic of the column
// computed when the program runs (pandas' mean(), median(), mode()).
enum class FillMethod { Value, Mean, Median, Mode };

inline const char* fillMethodName(FillMethod method) {
    switch (method) {
        case FillMethod::Mean:   return "MEAN";
        case FillMethod::Median: return "MEDIAN";
        case FillMethod::Mode:   return "MODE";
        case FillMethod::Value:  break;
    }
    return "";
}

// FILL_NULL company.*.age WITH 0 IF_EXISTS;   FILL_NULL titanic.Age WITH MEDIAN;
struct FillNullStmt : Stmt {
    Selector target;
    FillMethod method = FillMethod::Value;
    Literal value;  // for FillMethod::Value
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

enum class NormalizeMethod { ZScore, MinMax, Range };

// NORMALIZE students.age USING MINMAX;   NORMALIZE imgs RANGE 0 1;
struct NormalizeStmt : Stmt {
    Selector target;
    NormalizeMethod method = NormalizeMethod::ZScore;
    std::string low;   // RANGE only
    std::string high;  // RANGE only
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// FILTER students WHERE marks >= 50;
struct FilterStmt : Stmt {
    Selector target;
    std::unique_ptr<Expr> condition;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// SELECT students [name, age];
struct SelectStmt : Stmt {
    Selector target;
    std::vector<std::string> columns;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// DROP company.* [temporary_id] IF_EXISTS;
struct DropStmt : Stmt {
    Selector target;
    std::vector<std::string> columns;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// PARSE_DATE trips.pickup;   (a STRING column of ISO dates becomes DATETIME)
struct ParseDateStmt : Stmt {
    Selector target;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// The value of a new column in DERIVE: arithmetic over numeric columns and
// numbers, or a part of a DATETIME column. One node type for every kind keeps
// the tree small; IR instructions share it, so it is immutable.
enum class ValueKind { Number, Column, Binary, DatePart };

struct ValueExpr {
    ValueKind kind = ValueKind::Number;
    SourceLocation location;
    std::string text;  // Number: "273.15"; Column: its name; Binary: + - * /; DatePart: HOUR, DAY, ...
    std::shared_ptr<const ValueExpr> left;   // Binary: left operand; DatePart: the DATETIME column
    std::shared_ptr<const ValueExpr> right;  // Binary: right operand
};

// The parts of a date DERIVE can take, as pandas' .dt accessors name them.
inline const std::vector<std::string>& dateParts() {
    static const std::vector<std::string> parts = {"YEAR", "MONTH", "DAY", "HOUR", "WEEKDAY"};
    return parts;
}

// "temp - 273.15", "HOUR(date_time)", "(a + b) / 2"
std::string valueText(const ValueExpr& expr);

// The columns a value reads, each once, in order of appearance.
std::vector<std::string> valueColumns(const ValueExpr& expr);

// DERIVE weather.temp_c = temp - 273.15;   DERIVE traffic.hour = HOUR(date_time);
struct DeriveStmt : Stmt {
    Selector target;  // the table, with the new (or replaced) column as the last part
    std::shared_ptr<const ValueExpr> value;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// JOIN trips WITH zones ON zone_id;   (a lookup: each row of trips gets the columns of the
// one row of zones with its zone_id, or missing values if there is none)
struct JoinStmt : Stmt {
    Selector target;  // the table that gets the columns
    Selector other;   // the lookup table
    std::string key;  // the column both tables have
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// One result column of AGGREGATE: COUNT AS trips, SUM fare AS total, MEAN fare AS average.
struct AggregateItem {
    SourceLocation location;
    std::string function;  // COUNT, SUM, MEAN, MIN or MAX
    std::string column;    // the column it reads; empty for COUNT (the rows of the group)
    std::string name;      // the new column
};

// AGGREGATE trips BY [zone, hour] COUNT AS trips, MEAN fare AS average;
// (one row per group; rows with a missing BY value belong to no group)
struct AggregateStmt : Stmt {
    Selector target;
    std::vector<std::string> by;
    std::vector<AggregateItem> items;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// SAVE t AS "a.csv";   SAVE_TABLES ts TO "dir/";
// SAVE_IMAGE img AS "a.png";   SAVE_IMAGES imgs TO "dir/";
struct SaveStmt : Stmt {
    DataKind kind;
    Selector target;
    std::string path;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

enum class ConstraintKind {
    IsNumber,
    IsString,
    IsBoolean,
    NotNull,
    Between,
    Unique,  // column contracts
    References,
    SchemaSameAs,  // cross-table contracts
    Channels,
    MinWidth,
    MinHeight,  // image contracts
    MatchRate,  // linked dataset contract
};

// EXPECT students.marks BETWEEN 0 AND 100;   EXPECT company.*.id UNIQUE IF_EXISTS;
// EXPECT orders.customer_id REFERENCES customers.id;
// EXPECT train SCHEMA_SAME_AS test EXCEPT [label];   EXPECT imgs CHANNELS 3;
struct ExpectStmt : Stmt {
    Selector target;
    ConstraintKind constraint;
    std::string low;                  // BETWEEN lower bound, or the number of CHANNELS, MIN_*, MATCH_RATE
    std::string high;                 // BETWEEN upper bound
    Selector other;                   // REFERENCES column, SCHEMA_SAME_AS table
    std::vector<std::string> except;  // SCHEMA_SAME_AS ... EXCEPT [columns]
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// RESIZE imgs TO 224 224 KEEP_ASPECT;
struct ResizeStmt : Stmt {
    std::string dataset;
    int width = 0;
    int height = 0;
    bool keepAspect = false;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// GRAYSCALE imgs;
struct GrayscaleStmt : Stmt {
    std::string dataset;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// COMPRESS imgs QUALITY 80;
struct CompressStmt : Stmt {
    std::string dataset;
    int quality = 0;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// CONVERT imgs TO JPG;   (JPG, PNG or NPY)
struct ConvertStmt : Stmt {
    std::string dataset;
    std::string format;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// REMOVE_CORRUPT imgs;
struct RemoveCorruptStmt : Stmt {
    std::string dataset;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// LINK imgs WITH labels ON filename AS training;
struct LinkStmt : Stmt {
    std::string images;
    std::string table;
    std::string column;
    std::string dataset;
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

// The root of the tree.
struct Program {
    std::vector<std::unique_ptr<Stmt>> statements;
};
