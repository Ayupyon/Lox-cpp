#ifndef LOX_SCANNER_ERROR_H_
#define LOX_SCANNER_ERROR_H_

#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace lox::scanner {

// Accumulated lexical diagnostics, propagated out-of-band through llvm::Error.
// The scanner does not stop at the first problem: it resynchronizes at the
// next valid token boundary and collects every message into one LexicalError,
// which is returned instead of the token stream. Parser only ever runs on a
// clean stream.
class LexicalError : public llvm::ErrorInfo<LexicalError> {
 public:
  // One problem at a source span. source_line is a copy so the error stays
  // self-contained even if the source buffer is freed before it is consumed.
  struct Entry {
    std::uint32_t line{};
    std::uint32_t column{};
    std::uint32_t length{};
    std::string message;
    std::string source_line;
  };

  // NOLINTNEXTLINE(readability-identifier-naming)
  static char ID;

  LexicalError(std::string filename, std::vector<Entry> entries)
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

}  // namespace lox::scanner

#endif  // LOX_SCANNER_ERROR_H_
