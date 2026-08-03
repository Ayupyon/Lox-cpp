#ifndef LOX_PARSER_ERROR_H_
#define LOX_PARSER_ERROR_H_

#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace lox::parser {

// Accumulated syntax diagnostics, propagated out-of-band through llvm::Error.
// The parser resynchronizes after each error and collects every message into
// one SyntaxError, which is returned instead of the AST.
class SyntaxError : public llvm::ErrorInfo<SyntaxError> {
 public:
  // One syntax diagnostic at a source span. Mirrors lox::scanner::LexicalError::Entry.
  // source_line is a copy so the error stays self-contained even if the source
  // buffer is freed before it is consumed.
  struct Entry {
    std::uint32_t line{};
    std::uint32_t column{};
    std::uint32_t length{};  // token lexeme length, for the ^~~~ caret
    std::string message;
    std::string source_line;
  };

  // NOLINTNEXTLINE(readability-identifier-naming)
  static char ID;

  SyntaxError(std::string filename, std::vector<Entry> entries)
      : filename_(std::move(filename)), entries_(std::move(entries)) {}

  // NOLINTNEXTLINE(readability-identifier-naming)
  void log(llvm::raw_ostream &os) const override;
  std::error_code convertToErrorCode() const override;

  llvm::StringRef GetFilename() const { return filename_; }
  const std::vector<Entry> &GetEntries() const { return entries_; }

 private:
  std::string filename_;
  std::vector<Entry> entries_;
};

// Zero-allocation sentinel used internally by the Parser to unwind a failed
// parse subtree back to the synchronization point. Carries no data: the actual
// diagnostics are accumulated in Parser::errors_. Recovery points consume it
// with llvm::consumeError; it never reaches the caller of Parse().
class ParseAbort : public llvm::ErrorInfo<ParseAbort> {
 public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  static char ID;

  // NOLINTNEXTLINE(readability-identifier-naming)
  void log(llvm::raw_ostream &os) const override { os << "parse abort"; }
  std::error_code convertToErrorCode() const override { return llvm::inconvertibleErrorCode(); }
};

}  // namespace lox::parser

#endif  // LOX_PARSER_ERROR_H_
