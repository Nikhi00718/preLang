// PrepLang compiler driver: runs every phase in order.
//
// usage: preplang <file.prep> [--source] [--tokens] [--ast] [--schema]
//                             [--semantic] [--ir] [--python] [--run]
//
// Each flag prints the output of one module. With no flags every stage is
// printed. The generated script is always written to output/generated.py;
// --run also executes it with Python.

#include "ast/AST.h"
#include "ast/ASTPrinter.h"
#include "backend/PandasCodegen.h"
#include "common/Diagnostics.h"
#include "common/SourceManager.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/Optimizer.h"
#include "parser/Parser.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

const std::filesystem::path outputFolder = "output";

struct Options {
    std::string file;
    bool showSource = false;
    bool showTokens = false;
    bool showAst = false;
    bool showSchema = false;
    bool showSemantic = false;
    bool showIr = false;
    bool showPython = false;
    bool run = false;
};

bool parseArguments(int argc, char* argv[], Options& options) {
    const std::vector<std::pair<std::string, bool*>> flags = {
        {"--source", &options.showSource}, {"--tokens", &options.showTokens},   {"--ast", &options.showAst},
        {"--schema", &options.showSchema}, {"--semantic", &options.showSemantic}, {"--ir", &options.showIr},
        {"--python", &options.showPython}, {"--run", &options.run},
    };
    bool anyShown = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        bool known = false;
        for (const auto& [name, flag] : flags) {
            if (arg == name) {
                *flag = true;
                known = true;
                anyShown = anyShown || name != "--run";
            }
        }
        if (!known && arg[0] != '-' && options.file.empty()) {
            options.file = arg;
        } else if (!known) {
            return false;
        }
    }
    if (!anyShown) {
        options.showSource = options.showTokens = options.showAst = options.showSchema = true;
        options.showSemantic = options.showIr = options.showPython = true;
    }
    return !options.file.empty();
}

void printHeader(const std::string& title) {
    std::cout << "\n==================== " << title << " ====================\n";
}

// Prints all errors and warnings, and returns the process exit code.
int finish(const Diagnostics& diagnostics, const SourceManager& source) {
    if (!diagnostics.all().empty()) {
        printHeader("DIAGNOSTICS (Diagnostics)");
        diagnostics.print(std::cout, source);
    }
    std::cout << '\n'
              << (diagnostics.hasErrors() ? "Compilation failed: " : "Compiled successfully: ")
              << diagnostics.errorCount() << " error(s), " << diagnostics.warningCount() << " warning(s).\n";
    return diagnostics.hasErrors() ? 1 : 0;
}

int runPython(const std::filesystem::path& script) {
#ifdef _WIN32
    const std::string command = "py -3 \"" + script.string() + "\"";
#else
    const std::string command = "python3 \"" + script.string() + "\"";
#endif
    printHeader("RUN (" + command + ")");
    std::cout.flush();
    return std::system(command.c_str()) == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parseArguments(argc, argv, options)) {
        std::cerr << "usage: preplang <file.prep> [--source] [--tokens] [--ast] [--schema]\n"
                  << "                            [--semantic] [--ir] [--python] [--run]\n";
        return 2;
    }

    SourceManager source;
    if (!source.loadFile(options.file)) {
        std::cerr << "error: cannot open '" << options.file << "'\n";
        return 1;
    }
    if (options.showSource) {
        printHeader("SOURCE (SourceManager)");
        source.print(std::cout);
    }

    Diagnostics diagnostics;

    // Phase 1: lexical analysis
    std::vector<Token> tokens = Lexer(source.text(), diagnostics).tokenize();
    if (options.showTokens) {
        printHeader("TOKENS (Lexer)");
        printTokens(std::cout, tokens);
    }
    if (diagnostics.hasErrors()) {
        return finish(diagnostics, source);
    }

    // Phase 2: syntax analysis
    const Program program = Parser(std::move(tokens), diagnostics).parse();
    if (diagnostics.hasErrors()) {
        return finish(diagnostics, source);
    }
    if (options.showAst) {
        printHeader("AST (Parser + ASTPrinter)");
        ASTPrinter(std::cout).print(program);
    }

    // Phase 3: semantic analysis, which reads the CSV schemas at compile time.
    // CSV paths in LOAD are relative to the folder of the .prep file.
    SymbolTable symbols;
    SchemaProvider schemas(diagnostics);
    const std::filesystem::path programFolder = std::filesystem::path(options.file).parent_path();
    const AnalysisResult analysis = SemanticAnalyzer(symbols, schemas, programFolder, diagnostics).analyze(program);
    if (options.showSchema) {
        printHeader("SCHEMA (SchemaProvider)");
        for (const DatasetSymbol& dataset : symbols.datasets()) {
            printSchema(std::cout, dataset.name, dataset.schema);
        }
    }
    if (options.showSemantic) {
        printHeader("SEMANTIC ANALYSIS (SemanticAnalyzer)");
        for (const std::string& line : analysis.log) {
            std::cout << line << '\n';
        }
        printHeader("SYMBOL TABLE (SymbolTable)");
        symbols.print(std::cout);
    }
    if (diagnostics.hasErrors()) {
        return finish(diagnostics, source);
    }

    // Phase 4: intermediate code generation
    IRProgram ir = IRBuilder(symbols, analysis).build(program);
    if (options.showIr) {
        printHeader("IR (IRBuilder)");
        printIR(std::cout, ir);
    }

    // Phase 5: optimization
    const OptimizationReport report = Optimizer().optimize(ir);
    if (options.showIr) {
        printHeader("OPTIMIZATION REPORT (Optimizer)");
        printReport(std::cout, report);
        printHeader("OPTIMIZED IR (Optimizer)");
        printIR(std::cout, ir);
    }

    // Phase 6: target code generation
    const std::string python = PandasCodegen(outputFolder, source.path()).generate(ir);
    std::filesystem::create_directories(outputFolder);
    const std::filesystem::path script = outputFolder / "generated.py";
    std::ofstream(script) << python;
    if (options.showPython) {
        printHeader("PYTHON (PandasCodegen) -> " + script.generic_string());
        std::cout << python;
    }

    const int status = finish(diagnostics, source);
    if (status == 0 && options.run) {
        return runPython(script);
    }
    return status;
}
