cmake_minimum_required(VERSION 3.20)
project(PrepLang LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(preplang_core
    src/Token.cpp
    src/Diagnostics.cpp
    src/Lexer.cpp
    src/Ast.cpp
    src/ParseTree.cpp
    src/Parser.cpp
    src/ProgramIO.cpp
    src/SymbolTable.cpp
    src/SemanticAnalyzer.cpp
    src/IR.cpp
    src/Optimizer.cpp
    src/CodeGenerator.cpp
    src/Executor.cpp
)
target_include_directories(preplang_core PUBLIC src)

if (MSVC)
    target_compile_options(preplang_core PRIVATE /W4 /WX)
else()
    target_compile_options(preplang_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(preplang_r1 Review-1/main.cpp)
target_link_libraries(preplang_r1 PRIVATE preplang_core)

add_executable(preplang_r2 Review-2/main.cpp)
target_link_libraries(preplang_r2 PRIVATE preplang_core)

add_executable(preplang_r3 Review-3/main.cpp)
target_link_libraries(preplang_r3 PRIVATE preplang_core)
