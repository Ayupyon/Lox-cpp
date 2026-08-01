add_library(lox_scanner STATIC scanner/scanner.cpp scanner/token.cpp)
target_link_libraries(lox_scanner ${llvm_libs})

add_executable(lox-cpp main.cpp)
target_link_libraries(lox-cpp ${llvm_libs} lox_scanner)
