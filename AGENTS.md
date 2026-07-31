# Lox-cpp

Lox-cpp is an interpreter of extended lox-language (original lox-language is introduced by [Craft Interpreters](https://craftinginterpreters.com/)). The extended lox-language grammar lies in `docs/lox-language.typ`.

Lox-cpp is using C++20 standard.

## Project Structure

+ `include`, `src`, `tests` holds header file, implementation code and unit tests for each submodules, respectively. Submodules are divided into:

  - `scanner`: Read lox source file and produce token sequence.
  - `parser`: Parse token sequence and produce AST.
  - `bytecode`: Parse AST and emit bytecode of it.
  - `vm`: Execute bytecode or binary code produced by jit, hold class hierarchy information and run GC.
  - `jit`: Do jit compilation from bytecode to binary code with different optimization level and optional profile information.

+ `llvm-build`: default LLVM dependency installation path.

## Constraints

+ All module apis can be referenced by other module should be placed in `lox` namespace，for module local apis, place them in `<module>` namespace, for file local apis, place them in an anonymous namespace.

+ All header files *MUST* be protected by `#ifndef ...` pattern.

## Build

+ Generate cmake configuration files to `build` directory (If all `CMakeLists.txt`s are not modified and `build` exists, skip this step)：

  ```
  cmake . -G Ninja -B build
  ```

  Extra configs can be found in `README.md`.

  TIPS: When re-config cmake， make sure the soft link to `build/compile_commands.json` is valid.

+ Build with ninja:

  ```
  ninja -C build
  ```

### Dependency

Lox-cpp use LLVM 22.1.7 as external build dependency, one must have an LLVM 22.1.7 install before building.

## Check

Every cpp source file must be checked after implementation:

+ Clang-tidy check:

  ```
  run-clang-tidy
  ```

+ Clang-format code-format:

  ```
  find . -type f -regex '.*\.\(cpp\|hpp\|h\|cc\|cxx\)' | xargs -P $(nprocs) -I {} clang-format -style=file -i {}
  ```

Every git commit *MUST* after check accomplished.
