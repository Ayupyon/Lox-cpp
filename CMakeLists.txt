cmake_minimum_required(VERSION 3.30)

# C compiler (default Clang)
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang)
endif()

# C++ compiler (default Clang++)
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()

project(lox-cpp)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_CXX_STANDARD 20)

# build type configuration (default Debug)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
message(STATUS "lox-cpp build type: ${CMAKE_BUILD_TYPE}")

# enforce dump compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# parallel linking jobs configuration (default 4)
set(LOX_PARALLEL_LINK_JOBS 4 CACHE STRING "Number of parallel link jobs")
message(STATUS "Parallel link jobs set to ${LOX_PARALLEL_LINK_JOBS}")

# enable ccmake build configuration (default ON)
option(LOX_CCACHE_BUILD "Enable ccache for faster build" ON)
if(LOX_CCACHE_BUILD)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        message(STATUS "Ccache enabled for lox-cpp")
    else()
        message(WARNING "Ccache requested but not found")
    endif()
endif()

## dependency configuration
# llvm-project dependency
set(LLVM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/llvm-build/lib/cmake/llvm/")
find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
llvm_map_components_to_libnames(llvm_libs Support Core)

## project source configuration
include_directories(include)

add_subdirectory(src)
