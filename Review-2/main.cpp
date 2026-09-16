#include "Ast.h"
#include "Diagnostics.h"
#include "IR.h"
#include "Lexer.h"
#include "Parser.h"
#include "ProgramIO.h"
#include "SemanticAnalyzer.h"
#include "SymbolTable.h"

#include <iostream>
#include <string>

namespace {

bool runCompiler(const std::string& path, bool showTokens) {
    std::string source;
    if (!prep::readTextFile(path, source)) {
        std::cerr << "Could not read " << path << '\n';
        return false;
    }

    std::cout << "\n============================================================\n"
              << "  PrepLang Review 2 -- " << path
              << "\n============================================================\n";
    prep::printSource(source);

    prep::ErrorReporter reporter(source);
    std::vector<prep::Token> tokens = prep::Lexer(source, reporter).tokenize();
    if (showTokens) {
        std::cout << "\n--- PHASE 1: TOKENS ----------------------------------------\n"
                  << prep::formatTokenTable(tokens) << '\n';
    }

    prep::ParseResult parsed = prep::Parser(tokens, reporter).parse();
    if (!reporter.ok()) {
        std::cout << "\n--- ERRORS -----------------------------------------------\n"
                  << reporter.render() << '\n';
        return false;
    }

    std::cout << "\n--- PHASE 2: ABSTRACT SYNTAX TREE --------------------------\n";
    prep::printAst(parsed.program, std::cout);

    prep::SymbolTable symbols;
    prep::SemanticAnalyzer semantic(reporter);
    semantic.analyze(parsed.program, symbols);

    std::cout << "\n--- PHASE 3: SYMBOL TABLE + SEMANTIC ANALYSIS -------------\n";
    symbols.print(std::cout);
    if (!reporter.ok()) {
        std::cout << "\n" << reporter.render() << '\n';
        return false;
    }
    std::cout << "Semantic analysis passed.\n";

    prep::IntermediateProgram ir = prep::IRGenerator().generate(parsed.program);
    std::cout << "\n--- PHASE 4: INTERMEDIATE CODE -----------------------------\n";
    prep::printIR(ir, std::cout);
    return true;
}

void usage() {
    std::cout << "PrepLang Review 2\n\n"
              << "Build with the root CMake project, then run:\n"
              << "  preplang_r2 Review-2/examples/front_end.prep\n"
              << "  preplang_r2 Review-2/examples/semantic_error.prep\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    bool noTokens = false;
    std::string file;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--no-tokens") noTokens = true;
        else if (argument == "--help" || argument == "-h") {
            usage();
            return 0;
        } else if (file.empty()) file = argument;
        else {
            std::cerr << "Unexpected argument: " << argument << '\n';
            return 1;
        }
    }
    if (file.empty()) {
        usage();
        return 0;
    }
    return runCompiler(file, !noTokens) ? 0 : 1;
}
