#include <functional>
#include <iostream>
#include <string>
#include <utility>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "parser/ast.h"
#include "parser/error.h"
#include "parser/parser.h"
#include "scanner/error.h"
#include "scanner/scanner.h"
#include "scanner/token.h"

namespace {

// The pipeline stage to run on the input source. "execute" is the whole
// pipeline (parse -> bytecode -> run) and doubles as the default mode.
enum class Stage { kScanner, kParser, kBytecode, kExecute };

// LLVM convention: cl::opt globals are CamelCase regardless of VariableCase.
// NOLINTNEXTLINE(readability-identifier-naming)
llvm::cl::opt<Stage> RunStage(
    "run-stage",
    llvm::cl::desc("Pipeline stage to run on the input source"),
    llvm::cl::init(Stage::kExecute),
    llvm::cl::values(
        clEnumValN(Stage::kScanner, "scanner", "run the scanner and print the token stream"),
        clEnumValN(Stage::kParser, "parser", "parse and export the AST"),
        clEnumValN(Stage::kBytecode, "bytecode", "emit bytecode"),
        clEnumValN(Stage::kExecute, "execute", "emit bytecode and execute it")));

// NOLINTNEXTLINE(readability-identifier-naming)
llvm::cl::opt<std::string> InputFile(llvm::cl::Positional,
                                     llvm::cl::desc("<input file>"),
                                     llvm::cl::init(""));

// Loads the input source: the file given on the command line, or stdin when
// no file is given. *label receives the name used in diagnostics ("<stdin>"
// for stdin). Returns an error with exit-code 2 semantics on read failure.
llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> LoadInput(llvm::StringRef &label) {
  if (InputFile.empty()) {
    label = "<stdin>";
    return llvm::MemoryBuffer::getSTDIN();
  }
  label = InputFile;
  return llvm::MemoryBuffer::getFile(InputFile);
}

// Scans the input buffer and prints the token stream to stdout. Returns 0 on
// success, 1 if the scanner reported lexical errors (already printed to
// stderr).
int RunScanner(llvm::StringRef source, llvm::StringRef label) {
  lox::Scanner scanner(source, label);
  llvm::Expected<llvm::SmallVector<lox::Token, 0>> tokens = scanner.Scan();
  if (!tokens) {
    llvm::handleAllErrors(std::move(tokens.takeError()),
                          [](const lox::scanner::LexicalError &error) { error.log(llvm::errs()); });
    return 1;
  }

  llvm::raw_ostream &os = llvm::outs();
  for (const lox::Token &token : *tokens) {
    token.Print(os);
  }
  return 0;
}

// Scans the input buffer, parses the token stream, and prints the AST to
// stdout. Returns 0 on success, 1 if the scanner or parser reported errors
// (already printed to stderr).
int RunParser(llvm::StringRef source, llvm::StringRef label) {
  lox::Scanner scanner(source, label);
  llvm::Expected<llvm::SmallVector<lox::Token, 0>> tokens = scanner.Scan();
  if (!tokens) {
    llvm::handleAllErrors(std::move(tokens.takeError()),
                          [](const lox::scanner::LexicalError &error) { error.log(llvm::errs()); });
    return 1;
  }

  lox::Parser parser(source, std::move(*tokens), label);
  llvm::Expected<llvm::SmallVector<lox::StmtPtr, 0>> program = parser.Parse();
  if (!program) {
    llvm::handleAllErrors(std::move(program.takeError()),
                          [](const lox::parser::SyntaxError &error) { error.log(llvm::errs()); });
    return 1;
  }

  lox::Dump(*program, llvm::outs());
  return 0;
}

// Interactive line loop: prints the prompt before each line, reads one line at
// a time from stdin and hands it to handler. A non-zero handler result (e.g.
// lexical errors on that line) does not stop the loop; EOF (Ctrl-D) ends the
// session with exit code 0. Only reached when stdin is a terminal.
int RunInteractiveLoop(const std::function<int(llvm::StringRef)> &handler) {
  std::string line;
  for (;;) {
    llvm::outs() << "(lox-cpp) >> ";
    llvm::outs().flush();
    if (!std::getline(std::cin, line)) {
      return 0;  // EOF (Ctrl-D): normal exit.
    }
    handler(line);
  }
}

// Prints "<stage> stage is not implemented" to stderr and returns 1. Stubs
// never touch the input.
int UnimplementedStage(llvm::StringRef stage) {
  llvm::errs() << stage << " stage is not implemented\n";
  return 1;
}

// Execute mode (with or without an input file) is not implemented yet; with
// no file it is the interactive REPL. Prints a diagnostic to stderr and
// returns 1.
int InterpretMode() {
  llvm::errs() << "interpret mode is not implemented\n";
  return 1;
}

}  // namespace

int main(int argc, char **argv) {
  if (!llvm::cl::ParseCommandLineOptions(argc, argv, "lox-cpp\n", &llvm::errs())) {
    return 1;
  }

  switch (RunStage) {
    case Stage::kExecute:
      return InterpretMode();
    case Stage::kParser:
      // No input file and a terminal behind stdin: interactive line-by-line
      // parsing. A piped/redirected stdin keeps the batch path below.
      if (InputFile.empty() && llvm::sys::Process::StandardInIsUserInput()) {
        return RunInteractiveLoop([](llvm::StringRef line) { return RunParser(line, "<stdin>"); });
      }
      {
        llvm::StringRef label;
        llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = LoadInput(label);
        if (!buffer) {
          if (InputFile.empty()) {
            llvm::errs() << "failed to read stdin: " << buffer.getError().message() << '\n';
          } else {
            llvm::errs() << "failed to open '" << label << "': " << buffer.getError().message()
                         << '\n';
          }
          return 2;
        }
        return RunParser(buffer.get()->getBuffer(), label);
      }
    case Stage::kBytecode:
      return UnimplementedStage("bytecode");
    case Stage::kScanner:
      // No input file and a terminal behind stdin: interactive line-by-line
      // scanning. A piped/redirected stdin keeps the batch path below.
      if (InputFile.empty() && llvm::sys::Process::StandardInIsUserInput()) {
        return RunInteractiveLoop([](llvm::StringRef line) {
          // Each line is scanned independently: line/column restart at 1 and
          // every line's stream ends with its own kEof token.
          return RunScanner(line, "<stdin>");
        });
      }
      {
        llvm::StringRef label;
        llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = LoadInput(label);
        if (!buffer) {
          if (InputFile.empty()) {
            llvm::errs() << "failed to read stdin: " << buffer.getError().message() << '\n';
          } else {
            llvm::errs() << "failed to open '" << label << "': " << buffer.getError().message()
                         << '\n';
          }
          return 2;
        }
        return RunScanner(buffer.get()->getBuffer(), label);
      }
  }
}
