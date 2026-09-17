cmake_minimum_required(VERSION 3.16)
project(PrepLang LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All compiler phases live in one library that the compiler and the tests share.
add_library(preplang_core
    src/common/SourceManager.cpp
    src/common/Diagnostics.cpp
    src/lexer/Lexer.cpp
    src/parser/Parser.cpp
    src/ast/ASTPrinter.cpp
    src/schema/SchemaProvider.cpp
    src/sema/SymbolTable.cpp
    src/sema/SemanticAnalyzer.cpp
    src/ir/IRBuilder.cpp
    src/optimizer/Optimizer.cpp
    src/backend/PandasCodegen.cpp
)
target_include_directories(preplang_core PUBLIC src)

if(MSVC)
    target_compile_options(preplang_core PUBLIC /W4 /permissive-)
else()
    target_compile_options(preplang_core PUBLIC -Wall -Wextra -pedantic)
endif()

# The compiler executable
add_executable(preplang src/main.cpp)
target_link_libraries(preplang PRIVATE preplang_core)

# Unit tests: ctest --test-dir build -C Debug --output-on-failure
enable_testing()
foreach(name lexer parser semantic optimizer)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE preplang_core)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
