#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace prep {

enum class LiteralKind { Number, String, Boolean };

struct Literal {
    LiteralKind kind = LiteralKind::String;
    std::string value;
};

std::string literalKindName(LiteralKind kind);

enum class StatementKind {
    Load,
    Expect,
    DropNull,
    FillNull,
    Normalize,
    Filter,
    Select,
    Drop,
    Save
};

std::string statementKindName(StatementKind kind);

// One AST node represents one complete PrepLang statement.
struct Statement {
    StatementKind kind = StatementKind::Load;
    int line = 0;

    std::string file;
    std::string dataset;
    std::string column;
    std::vector<std::string> columns;

    std::string contract;      // TYPE, RANGE or NOT_NULL
    std::string expectedType;  // NUMBER, STRING or BOOLEAN
    std::string low;
    std::string high;
    std::string operation;
    Literal value;
    bool hasValue = false;
};

struct Program {
    std::vector<Statement> statements;
};

void printAst(const Program& program, std::ostream& out);

}  // namespace prep
