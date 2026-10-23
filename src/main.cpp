// PrepLang compiler driver: runs every phase in order.
//
// usage: preplang <file.prep> [options]
//        preplang --version
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
// Private compilation (schema/ReleasedFacts.h):
//                --queries        for the data owner: write output/preplang_queries.tsv, what the
//                                 program asks about its data, for tools/release_facts.py
//                --facts <file>   compile from the facts the data owner released, without
//                                 opening any input file

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
#include "schema/ReleasedFacts.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
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
    bool queries = false;
    std::string facts;  // --facts <file>: a private compilation
};

bool parseArguments(int argc, char* argv[], Options& options) {
    const std::vector<std::pair<std::string, bool*>> stageFlags = {
        {"--source", &options.source}, {"--tokens", &options.tokens},           {"--ast", &options.ast},
        {"--schema", &options.schema}, {"--semantic", &options.semantic},       {"--lineage", &options.lineage},
        {"--ir", &options.ir},         {"--certificate", &options.certificate}, {"--python", &options.python},
    };
    const std::vector<std::pair<std::string, bool*>> otherFlags = {
        {"--explain", &options.explain},  {"--dry-run", &options.dryRun},  {"--run", &options.run},
        {"--dot", &options.dot},          {"--O0", &options.noOptimize},   {"--approx", &options.approximate},
        {"--no-deopt", &options.noDeopt}, {"--queries", &options.queries},
    };
    bool anyStage = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--facts") {
            if (++i == argc) {
                return false;
            }
            options.facts = argv[i];
            continue;
        }
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
    if (!anyStage && !options.explain && !options.dryRun && !options.queries) {
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

// What --queries asks about: every number written in the program (a number after a
// minus sign also as a negative one), which are the only places the analysis compares
// values with, and the names it uses (bounds are asked only about named columns, or
// about all of them if a selector uses *).
struct ProgramWords {
    std::vector<double> numbers;
    std::set<std::string> names;
    bool anyColumn = false;
};

ProgramWords programWords(const std::vector<Token>& tokens) {
    ProgramWords words;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::NUMBER_LITERAL) {
            const double value = std::stod(tokens[i].lexeme);
            words.numbers.push_back(value);
            if (i > 0 && tokens[i - 1].type == TokenType::MINUS) {
                words.numbers.push_back(-value);
            }
        } else if (tokens[i].type == TokenType::IDENTIFIER) {
            words.names.insert(tokens[i].lexeme);
        } else if (tokens[i].type == TokenType::STAR) {
            words.anyColumn = true;
        }
    }
    std::sort(words.numbers.begin(), words.numbers.end());
    words.numbers.erase(std::unique(words.numbers.begin(), words.numbers.end()), words.numbers.end());
    return words;
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
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "preplang " << PREPLANG_VERSION << '\n';
        return 0;
    }
    Options options;
    if (!parseArguments(argc, argv, options)) {
        std::cerr << "usage: preplang <file.prep> [--source] [--tokens] [--ast] [--schema] [--semantic]\n"
                  << "                            [--lineage] [--ir] [--certificate] [--python] [--explain]\n"
                  << "                            [--dry-run] [--run] [--dot] [--O0] [--approx] [--no-deopt]\n"
                  << "                            [--queries] [--facts <file>]\n"
                  << "       preplang --version\n";
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
    const ProgramWords words = programWords(tokens);  // for --queries

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
    if (!options.facts.empty()) {
        // A private compilation: the compiler knows the data only through the released facts.
        std::string problem;
        std::optional<ReleasedFacts> facts = readReleasedFacts(options.facts, programFolder, problem);
        if (!facts) {
            std::cerr << "error: " << problem << '\n';
            return 1;
        }
        std::cout << "Private compilation: no input file is opened; the facts are the data owner's released "
                     "answers ("
                  << facts->mechanism << ").\n";
        schemas.setReleased(std::move(facts->tables));
    }
    const AnalysisResult analysis = SemanticAnalyzer(symbols, schemas, programFolder, diagnostics).analyze(program);
    if (options.schema) {
        printHeader("SCHEMA (SchemaProvider)");
        for (const DatasetSymbol& dataset : symbols.datasets()) {
            for (const TableState& table : dataset.tables) {
                printTableSchema(std::cout,
                                 dataset.name + (dataset.kind == DatasetKind::TableSet ? "." + table.name : ""),
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
    if (options.queries) {
        // The data owner's side: what a private compilation of this program asks about
        // the data, for tools/release_facts.py. It needs only the public schema and the
        // program's numbers, so errors the data causes (a violated contract) do not stop
        // it; they are the owner's to see, and are not written.
        std::vector<QueryTable> tables;
        // A DROP_NULL of whole rows looks at every column's missing values.
        const bool wholeRows = std::any_of(program.statements.begin(), program.statements.end(), [](const auto& s) {
            const auto* dropNull = dynamic_cast<const DropNullStmt*>(s.get());
            return dropNull && dropNull->columns.empty();
        });
        for (const std::unique_ptr<Stmt>& statement : program.statements) {
            const auto* load = dynamic_cast<const LoadStmt*>(statement.get());
            if (!load) {
                continue;
            }
            const DatasetSymbol* dataset = symbols.lookup(load->dataset);
            if (load->kind != DataKind::Table || !dataset || dataset->tables.empty()) {
                std::cerr << "error: line " << load->location.line
                          << ": a private compilation can only LOAD tables, and this one could not be read\n";
                return 1;
            }
            QueryTable table{load->path, load->dataset, dataset->tables.front().schema, {}, {}};
            for (const ColumnInfo& column : table.schema.columns) {
                if (words.anyColumn || words.names.count(column.name) != 0) {
                    table.ranged.insert(column.name);
                } else if (wholeRows) {
                    table.missing.insert(column.name);
                }
            }
            tables.push_back(table);
        }
        std::filesystem::create_directories(outputFolder);
        std::ofstream queries(outputFolder / "preplang_queries.tsv");
        writeQueries(queries, tables, words.numbers);
        std::cout << "Wrote " << (outputFolder / "preplang_queries.tsv").generic_string()
                  << ": review the column types (they are published as the schema), then give it to "
                     "tools/release_facts.py to release facts for a private compilation.\n";
        return 0;
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
    // The certificates and output guarantees, for composing pipeline stages (tools/contracts.py).
    std::ofstream contract(outputFolder / "preplang_contract.json");
    writeContract(contract, certificate, source.path());
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
