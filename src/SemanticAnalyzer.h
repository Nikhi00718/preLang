#pragma once

#include "Ast.h"
#include "Diagnostics.h"
#include "SymbolTable.h"

namespace prep {

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(ErrorReporter& reporter);
    bool analyze(const Program& program, SymbolTable& symbols);

private:
    bool loaded(const Statement& statement, const SymbolTable& symbols) const;
    const ColumnSymbol* requireColumn(const Statement& statement,
                                      const SymbolTable& symbols,
                                      const std::string& use);
    void error(const Statement& statement, const std::string& problem,
               const std::string& hint = "");

    ErrorReporter& reporter_;
};

}  // namespace prep
