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
    src/ast/ASTDot.cpp
    src/schema/Checksum.cpp
    src/schema/CsvScanner.cpp
    src/schema/ParquetFooter.cpp
    src/schema/ReleasedFacts.cpp
    src/schema/SchemaProvider.cpp
    src/sema/SymbolTable.cpp
    src/sema/SelectorResolver.cpp
    src/sema/ContractAnalyzer.cpp
    src/sema/SemanticAnalyzer.cpp
    src/dataset/LineageTracker.cpp
    src/dataset/DatasetLinker.cpp
    src/ir/IRBuilder.cpp
    src/certify/Certifier.cpp
    src/optimizer/PassManager.cpp
    src/optimizer/RedundantOpPass.cpp
    src/optimizer/DeadOpPass.cpp
    src/optimizer/ColumnPruningPass.cpp
    src/backend/PandasCodegen.cpp
    src/backend/OpenCVCodegen.cpp
    src/backend/ExecutionPlanner.cpp
    src/explain/ManifestWriter.cpp
    src/explain/LineageReport.cpp
    src/explain/ExplainPlan.cpp
)
target_include_directories(preplang_core PUBLIC src)

if(MSVC)
    target_compile_options(preplang_core PUBLIC /W4 /permissive-)
    # fopen is the portable C function; MSVC would rather have fopen_s
    target_compile_definitions(preplang_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(preplang_core PUBLIC -Wall -Wextra -pedantic)
endif()

# The compiler executable
add_executable(preplang src/main.cpp)
target_link_libraries(preplang PRIVATE preplang_core)

# Unit tests: ctest --test-dir build -C Debug --output-on-failure
enable_testing()
foreach(name lexer parser semantic tableset imageset parquet optimizer scanner certificate private)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE preplang_core)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()

# The data owner's release (tools/release_facts.py) is Python, tested if an interpreter is found.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME release_facts COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_release_facts.py)
endif()

# The Parquet tests read small files made by tests/data/make_parquet.py
target_compile_definitions(test_parquet PRIVATE PREPLANG_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
