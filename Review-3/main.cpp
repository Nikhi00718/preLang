#include "Ast.h"
#include "CodeGenerator.h"
#include "Diagnostics.h"
#include "Executor.h"
#include "IR.h"
#include "Lexer.h"
#include "Optimizer.h"
#include "Parser.h"
#include "ProgramIO.h"
#include "SemanticAnalyzer.h"
#include "SymbolTable.h"

#include <iostream>
#include <string>

namespace {

bool runCompiler(const std::string& path, bool showTokens, bool execute) {
    std::string source;
    if (!prep::readTextFile(path, source)) {
        std::cerr << "Could not read " << path << '\n';
        return false;
    }

    std::cout << "\n============================================================\n"
              << "  PrepLang Review 3 -- " << path
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
        std::cout << "\n--- FRONT-END ERRORS --------------------------------------\n"
                  << reporter.render() << '\n';
        return false;
    }

    prep::SymbolTable symbols;
    prep::SemanticAnalyzer semantic(reporter);
    semantic.analyze(parsed.program, symbols);
    if (!reporter.ok()) {
        std::cout << "\n--- SEMANTIC ERRORS ---------------------------------------\n"
                  << reporter.render() << '\n';
        return false;
    }

    prep::IntermediateProgram ir = prep::IRGenerator().generate(parsed.program);
    std::cout << "\n--- ABSTRACT SYNTAX TREE ----------------------------------\n";
    prep::printAst(parsed.program, std::cout);
    std::cout << "\n--- SYMBOL TABLE ------------------------------------------\n";
    symbols.print(std::cout);
    std::cout << "\n--- INTERMEDIATE CODE -------------------------------------\n";
    prep::printIR(ir, std::cout);

    prep::OptimizationResult optimized = prep::Optimizer().optimize(ir);
    std::cout << "\n--- OPTIMIZER ---------------------------------------------\n";
    for (const std::string& note : optimized.notes) std::cout << "  " << note << '\n';
    std::cout << "\nOptimized IR:\n";
    prep::printIR(optimized.program, std::cout);

    std::cout << "\n--- GENERATED C++-STYLE PLAN ------------------------------\n"
              << prep::CodeGenerator().generate(optimized.program);

    if (execute) {
        std::cout << "\n--- EXECUTION ENGINE --------------------------------------\n";
        if (!prep::Executor().run(optimized.program, reporter, std::cout)) {
            std::cout << reporter.render() << '\n';
            return false;
        }
        std::cout << "Execution completed successfully.\n";
    }
    return true;
}

void usage() {
    std::cout << "PrepLang Review 3\n\n"
              << "Build with the root CMake project, then run:\n"
              << "  preplang_r3 Review-3/examples/full_pipeline.prep --run\n"
              << "  add --no-tokens to hide the token table\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    bool noTokens = false;
    bool execute = false;
    std::string file;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--no-tokens") noTokens = true;
        else if (argument == "--run") execute = true;
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
    return runCompiler(file, !noTokens, execute) ? 0 : 1;
}
