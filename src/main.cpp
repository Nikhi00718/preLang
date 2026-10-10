// PrepLang compiler driver: runs every phase in order.
//
// usage: preplang <file.prep> [options]
//
// Stage output:  --source --tokens --ast --schema --semantic --lineage --ir --certificate --python
//                (with none of these, every stage except --lineage is printed)
// Modes:         --explain   print the execution plan; write nothing
//                --dry-run   print what running would change and write; write nothing
//                --run       run the generated program with Python afterwards
// Other:         --dot       also write the AST as output/ast.dot (Graphviz)
//                --O0        turn the optimizer off
//                --approx    also apply approximate rewrites (RESIZE fusion)
//                --no-deopt  remove proven-useless instructions outright (no deoptimization):
//                            a new input must then satisfy the full data certificate

#include "ast/AST.h"
#include "ast/ASTDot.h"
#include "ast/ASTPrinter.h"
#include "backend/ExecutionPlanner.h"
#include "certify/Certifier.h"
#include "common/Diagnostics.h"
#include "common/SourceManager.h"
#include "explain/ExplainPlan.h"
#include "explain/LineageReport.h"
#include "explain/ManifestWriter.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/PassManager.h"
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
    bool source = false, tokens = false, ast = false, schema = false, semantic = false, lineage = false;
    bool ir = false, certificate = false, python = false;
    bool explain = false, dryRun = false, run = false, dot = false, noOptimize = false, approximate = false;
    bool noDeopt = false;
};

bool parseArguments(int argc, char* argv[], Options& options) {
    const std::vector<std::pair<std::string, bool*>> stageFlags = {
        {"--source", &options.source},     {"--tokens", &options.tokens}, {"--ast", &options.ast},
        {"--schema", &options.schema},     {"--semantic", &options.semantic}, {"--lineage", &options.lineage},
        {"--ir", &options.ir},             {"--certificate", &options.certificate},
        {"--python", &options.python},
    };
    const std::vector<std::pair<std::string, bool*>> otherFlags = {
        {"--explain", &options.explain}, {"--dry-run", &options.dryRun}, {"--run", &options.run},
        {"--dot", &options.dot},         {"--O0", &options.noOptimize},  {"--approx", &options.approximate},
        {"--no-deopt", &options.noDeopt},
    };
    bool anyStage = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        bool known = false;
        for (const auto& [name, flag] : stageFlags) {
            if (arg == name) {
                *flag = known = anyStage = true;
            }
        }
        for (const auto& [name, flag] : otherFlags) {
            if (arg == name) {
                *flag = known = true;
            }
        }
        if (!known && arg[0] != '-' && options.file.empty()) {
            options.file = arg;
        } else if (!known) {
            return false;
        }
    }
    if (!anyStage && !options.explain && !options.dryRun) {
        options.source = options.tokens = options.ast = options.schema = options.semantic = true;
        options.ir = options.certificate = options.python = true;
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
        std::cerr << "usage: preplang <file.prep> [--source] [--tokens] [--ast] [--schema] [--semantic]\n"
                  << "                            [--lineage] [--ir] [--certificate] [--python] [--explain]\n"
                  << "                            [--dry-run] [--run] [--dot] [--O0] [--approx] [--no-deopt]\n";
        return 2;
    }

    SourceManager source;
    if (!source.loadFile(options.file)) {
        std::cerr << "error: cannot open '" << options.file << "'\n";
        return 1;
    }
    if (options.source) {
        printHeader("SOURCE (SourceManager)");
        source.print(std::cout);
    }

    Diagnostics diagnostics;

    // Phase 1: lexical analysis
    std::vector<Token> tokens = Lexer(source.text(), diagnostics).tokenize();
    if (options.tokens) {
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
    if (options.ast) {
        printHeader("AST (Parser + ASTPrinter)");
        ASTPrinter(std::cout).print(program);
    }
    if (options.dot) {
        std::filesystem::create_directories(outputFolder);
        std::ofstream dot(outputFolder / "ast.dot");
        ASTDot(dot).write(program);
        std::cout << "\nWrote " << (outputFolder / "ast.dot").generic_string()
                  << " (picture: dot -Tpng output/ast.dot -o output/ast.png)\n";
    }

    // Phase 3: semantic analysis. It reads the input data (CSV files, image
    // headers) at compile time; paths are relative to the .prep file's folder.
    SymbolTable symbols;
    SchemaProvider schemas(diagnostics);
    const std::filesystem::path programFolder = std::filesystem::path(options.file).parent_path();
    const AnalysisResult analysis = SemanticAnalyzer(symbols, schemas, programFolder, diagnostics).analyze(program);
    if (options.schema) {
        printHeader("SCHEMA (SchemaProvider)");
        for (const DatasetSymbol& dataset : symbols.datasets()) {
            for (const TableState& table : dataset.tables) {
                printTableSchema(std::cout, dataset.name + (dataset.kind == DatasetKind::TableSet ? "." + table.name : ""),
                                 table.schema);
            }
            if (dataset.isImage()) {
                printImageSetSchema(std::cout, dataset.name, dataset.imageSchema);
            }
        }
    }
    if (options.semantic) {
        printHeader("SEMANTIC ANALYSIS (SemanticAnalyzer)");
        for (const std::string& line : analysis.log) {
            std::cout << line << '\n';
        }
        printHeader("SYMBOL TABLE (SymbolTable)");
        symbols.print(std::cout);
    }
    if (options.lineage) {
        printHeader("LINEAGE (LineageTracker)");
        printLineage(std::cout, analysis.lineage);
    }
    if (diagnostics.hasErrors()) {
        return finish(diagnostics, source);
    }

    // Phase 4: intermediate code generation
    IRProgram ir = IRBuilder(symbols, analysis).build(program);
    if (options.ir) {
        printHeader("IR (IRBuilder)");
        printIR(std::cout, ir);
    }

    // Phase 5: optimization
    const std::vector<double> constants = programConstants(ir);  // before the optimizer removes instructions
    const OptimizationReport report = PassManager({!options.noOptimize, options.approximate, !options.noDeopt}).run(ir);
    if (options.ir) {
        printHeader("OPTIMIZATION REPORT (PassManager)");
        printReport(std::cout, report);
        printHeader("OPTIMIZED IR");
        printIR(std::cout, ir);
    }

    // Phase 6: data certificate. The program is analysed again with weaker facts
    // to find the facts its proofs really need; another input with those facts
    // can then be given to the generated program without compiling again.
    const Certificate certificate = Certifier(program, schemas, programFolder).certify(ir, analysis, constants);
    if (options.certificate) {
        printHeader("DATA CERTIFICATE (Certifier)");
        printCertificate(std::cout, certificate);
    }

    // --explain and --dry-run describe the program without writing anything.
    if (options.explain || options.dryRun) {
        if (options.explain) {
            printHeader("EXPLAIN (ExplainPlan)");
            printExplain(std::cout, symbols, analysis, report, ir, certificate, diagnostics.warningCount());
        }
        if (options.dryRun) {
            printHeader("DRY RUN (ExplainPlan)");
            printDryRun(std::cout, ir, outputFolder.generic_string());
        }
        return finish(diagnostics, source);
    }

    // Phase 7: target code generation
    const std::string python = ExecutionPlanner(outputFolder, source.path(), certificate)
                                   .generate(ir, manifestCode(ir, analysis, source.path(), outputFolder));
    std::filesystem::create_directories(outputFolder);
    const std::filesystem::path script = outputFolder / "generated.py";
    std::ofstream(script) << python;
    if (options.python) {
        printHeader("PYTHON (ExecutionPlanner + PandasCodegen + OpenCVCodegen) -> " + script.generic_string());
        std::cout << python;
    }

    const int status = finish(diagnostics, source);
    if (status == 0 && options.run) {
        return runPython(script);
    }
    return status;
}
