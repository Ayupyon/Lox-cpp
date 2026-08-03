include(FetchContent)

# googletest, pinned; see https://google.github.io/googletest/quickstart-cmake.html
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Prevent overriding the parent project's compiler/linker settings on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Golden test: for every tests/lox-source/**/*.lox, run the real lox-cpp binary
# with --run-stage scanner and compare stdout/stderr/exit code against the
# golden files under tests/lox-source-out/scanner/ (same relative layout).
add_executable(scanner_golden_test scanner_golden_test.cpp)
target_link_libraries(scanner_golden_test PRIVATE GTest::gtest_main ${llvm_libs})
target_compile_definitions(scanner_golden_test PRIVATE
  LOX_CPP_BINARY="$<TARGET_FILE:lox-cpp>"
  LOX_REPO_ROOT="${CMAKE_SOURCE_DIR}"
  LOX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}/tests/lox-source"
  LOX_GOLDEN_ROOT="${CMAKE_SOURCE_DIR}/tests/lox-source-out/scanner"
)

include(GoogleTest)
gtest_discover_tests(scanner_golden_test)

# Golden test: for every tests/lox-source/**/*.lox, run the real lox-cpp binary
# with --run-stage parser and compare stdout/stderr/exit code against the
# golden files under tests/lox-source-out/parser/ (same relative layout).
add_executable(parser_golden_test parser_golden_test.cpp)
target_link_libraries(parser_golden_test PRIVATE GTest::gtest_main ${llvm_libs})
target_compile_definitions(parser_golden_test PRIVATE
  LOX_CPP_BINARY="$<TARGET_FILE:lox-cpp>"
  LOX_REPO_ROOT="${CMAKE_SOURCE_DIR}"
  LOX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}/tests/lox-source"
  LOX_GOLDEN_ROOT="${CMAKE_SOURCE_DIR}/tests/lox-source-out/parser"
)
gtest_discover_tests(parser_golden_test)

# AST dump unit test: constructs sample ASTs by hand and checks the Dump
# output.
add_executable(ast_dump_test ast_dump_test.cpp)
target_link_libraries(ast_dump_test PRIVATE GTest::gtest_main lox_parser ${llvm_libs})
gtest_discover_tests(ast_dump_test)
