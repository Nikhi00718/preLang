// Fuzz target: any bytes as a .prep program, through the lexer, the parser and the AST printer.
// Syntax errors are expected; a crash, a hang or a sanitizer report is a bug.

#include "ast/ASTPrinter.h"
#include "common/Diagnostics.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    Diagnostics diagnostics;
    std::vector<Token> tokens = Lexer(std::string(reinterpret_cast<const char*>(data), size), diagnostics).tokenize();
    const Program program = Parser(std::move(tokens), diagnostics).parse();
    std::ostringstream out;
    ASTPrinter(out).print(program);
    return 0;
}
