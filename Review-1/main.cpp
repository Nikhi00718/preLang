#include "Diagnostics.h"
#include "Lexer.h"
#include "ParseTree.h"
#include "Parser.h"
#include "ProgramIO.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool compileSource(const std::string& source, const std::string& name, bool showTokens) {
    std::cout << "\n============================================================\n"
              << "  PrepLang Review 1 -- " << name
              << "\n============================================================\n";
    prep::printSource(source);

    prep::ErrorReporter reporter(source);
    std::vector<prep::Token> tokens = prep::Lexer(source, reporter).tokenize();
    if (showTokens) {
        std::cout << "\n--- PHASE 1: LEXICAL ANALYSIS ------------------------------\n"
                  << prep::formatTokenTable(tokens) << '\n';
    }

    prep::ParseResult parsed = prep::Parser(tokens, reporter).parse();
    std::cout << "\n--- PHASE 2: SYNTAX ANALYSIS + PARSE TREE ------------------\n";
    prep::printParseTree(parsed.tree, std::cout);

    std::cout << "\n--- DIAGNOSTICS --------------------------------------------\n";
    if (reporter.ok()) {
        std::cout << "OK  The program is lexically and syntactically valid.\n"
                  << "    " << tokens.size() - 1 << " tokens recognised.\n";
        return true;
    }

    std::cout << reporter.render() << "\n\nCompilation failed: " << reporter.summary() << '\n';
    return false;
}

bool runFile(const std::string& path, bool showTokens) {
    std::string source;
    if (!prep::readTextFile(path, source)) {
        std::cerr << "Could not read " << path << '\n';
        return false;
    }
    return compileSource(source, path, showTokens);
}

int runDemo() {
    std::string probe;
    const std::string base = prep::readTextFile("examples/demo_valid.prep", probe)
                                 ? "examples/"
                                 : "Review-1/examples/";
    const std::vector<std::string> files = {
        base + "demo_valid.prep",
        base + "demo_lexical_error.prep",
        base + "demo_syntax_error.prep"
    };
    int failures = 0;
    for (const std::string& file : files) {
        bool valid = runFile(file, file.find("valid") != std::string::npos);
        bool expectedError = file.find("error") != std::string::npos;
        if (valid == expectedError) ++failures;
    }
    std::cout << "\nDemo result: " << files.size() - failures << "/" << files.size()
              << " examples behaved as expected.\n";
    return failures == 0 ? 0 : 1;
}

void usage() {
    std::cout << "PrepLang Review 1\n\n"
              << "Build: g++ -std=c++17 src/*.cpp main.cpp -Isrc -o preplang_r1\n\n"
              << "Usage:\n"
              << "  preplang_r1 <file.prep>\n"
              << "  preplang_r1 -c \"NORMALIZE students.age;\"\n"
              << "  preplang_r1 --demo\n"
              << "  add --no-tokens to hide the token table\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    bool noTokens = false;
    bool demo = false;
    std::string command;
    std::string file;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--no-tokens") {
            noTokens = true;
        } else if (argument == "--demo") {
            demo = true;
        } else if (argument == "-c" || argument == "--command") {
            if (i + 1 >= argc) {
                std::cerr << "Missing source after " << argument << '\n';
                return 1;
            }
            command = argv[++i];
        } else if (argument == "--help" || argument == "-h") {
            usage();
            return 0;
        } else if (file.empty()) {
            file = argument;
        } else {
            std::cerr << "Unexpected argument: " << argument << '\n';
            return 1;
        }
    }

    if (demo) return runDemo();
    if (!command.empty()) return compileSource(command, "<command line>", !noTokens) ? 0 : 1;
    if (!file.empty()) return runFile(file, !noTokens) ? 0 : 1;
    usage();
    return 0;
}
