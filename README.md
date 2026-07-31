# Lox-cpp

Lox-cpp is an interpreter of extended lox-language (original lox-language is introduced by [Craft Interpreters](https://craftinginterpreters.com/)). The extended lox-language grammar lies in `docs/lox-language.typ`.

Lox-cpp is still WIP.

## Dependency

+ LLVM 22.1.7

  If you build LLVM from source code, please ensure options shown below is enabled:

  ```
  -DLLVM_ENABLE_PROJECTS="clang;lldb;lld;mlir;clang-tools-extra" \
  -DLLVM_ENABLE_RUNTIMES=compiler-rt
  ```

## Build

Lox-cpp use CMake (version 3.30 or newer) for building, and use clang++(clang) as compiler, lld as linker by default.

Options:

- `LOX_USE_LINKER`：linker used to link Lox-cpp, default to lld.
- `CMAKE_CXX_COMPILER` (and `CMAKE_C_COMPILER`): change default compilers.
- `CMAKE_BUILD_TYPE`: default to Debug.
- `LOX_PARALLEL_LINK_JOBS`: threads used at link stage, default to 4.
- `LOX_CCACHE_BUILD`: whether use ccache to speedup compilation or not, default to ON.
- `LLVM_DIR`: path for CMake finding your LLVM installation, default to `<PROJECT_ROOT>/llvm-build/lib/cmake/llvm`.
