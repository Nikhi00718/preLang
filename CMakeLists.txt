cmake_minimum_required(VERSION 3.16)
project(PrepLang VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build variants used by CI (.github/workflows/ci.yml); all are off by default.
option(PREPLANG_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC, Clang)" OFF)
option(PREPLANG_COVERAGE "Build with coverage counters for gcovr (GCC)" OFF)
option(PREPLANG_FUZZ "Link the fuzz targets with libFuzzer (Clang); otherwise they replay their corpus" OFF)
option(PREPLANG_STATIC_RUNTIME "Link the C++ runtime statically, for release binaries (GCC)" OFF)
option(PREPLANG_TESTS "Build the tests and the fuzz targets (pip install turns this off)" ON)

if(PREPLANG_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()
if(PREPLANG_COVERAGE)
    # atomic counters: the CSV scanner's threads would otherwise race on them
    add_compile_options(--coverage -O0 -fprofile-update=atomic)
    add_link_options(--coverage)
endif()
if(PREPLANG_FUZZ)
    # Every object gets the fuzzer's coverage instrumentation; only the fuzz targets link its main.
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()
if(PREPLANG_STATIC_RUNTIME)
    add_link_options(-static-libstdc++ -static-libgcc)
endif()

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
target_compile_definitions(preplang PRIVATE PREPLANG_VERSION="${PROJECT_VERSION}")

if(SKBUILD)
    # pip install (pyproject.toml): the compiler becomes the `preplang` command, and the Python
    # tools join the package python/preplang, where pyproject.toml makes them commands too.
    install(TARGETS preplang RUNTIME DESTINATION ${SKBUILD_SCRIPTS_DIR})
    install(FILES frontend/pandas2prep.py tools/contracts.py tools/release_facts.py DESTINATION preplang)
else()
    install(TARGETS preplang RUNTIME DESTINATION bin)
endif()

if(NOT PREPLANG_TESTS)
    return()
endif()

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
    # Every example program, compiled end to end (it writes output/ in the build folder).
    add_test(NAME examples COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_examples.py $<TARGET_FILE:preplang>
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# The Parquet tests read small files made by tests/data/make_parquet.py
target_compile_definitions(test_parquet PRIVATE PREPLANG_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

# Fuzz targets for the parts that read untrusted bytes: the .prep front end, the CSV scanner and
# the Parquet footer reader. With PREPLANG_FUZZ they are libFuzzer programs; otherwise each one
# replays its seed corpus (the examples and test data) as a test, so every build runs them once.
foreach(target parser csv parquet)
    add_executable(fuzz_${target} fuzz/fuzz_${target}.cpp)
    target_link_libraries(fuzz_${target} PRIVATE preplang_core)
    if(PREPLANG_FUZZ)
        target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
    else()
        target_sources(fuzz_${target} PRIVATE fuzz/replay_main.cpp)
    endif()
endforeach()
if(NOT PREPLANG_FUZZ)
    add_test(NAME fuzz_parser_replay COMMAND fuzz_parser ${CMAKE_SOURCE_DIR}/examples ${CMAKE_SOURCE_DIR}/fuzz/corpus/parser)
    add_test(NAME fuzz_csv_replay COMMAND fuzz_csv ${CMAKE_SOURCE_DIR}/examples ${CMAKE_SOURCE_DIR}/fuzz/corpus/csv)
    add_test(NAME fuzz_parquet_replay COMMAND fuzz_parquet ${CMAKE_SOURCE_DIR}/tests/data)
endif()
