#include "parser/error.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "llvm/Support/raw_ostream.h"

namespace lox::parser {

char SyntaxError::ID = 0;
char ParseAbort::ID = 0;

void SyntaxError::log(llvm::raw_ostream &os) const {
  for (const Entry &entry : entries_) {
    if (!filename_.empty())
      os << filename_ << ':';
    os << entry.line << ':' << entry.column << ": ";
    if (os.is_displayed())
      os.changeColor(llvm::raw_ostream::RED, true);
    os << "error: ";
    if (os.is_displayed())
      os.resetColor();
    os << entry.message << '\n';
    os << entry.source_line << '\n';
    std::size_t pad = entry.column > 0 ? entry.column - 1 : 0;
    if (pad > entry.source_line.size())
      pad = entry.source_line.size();
    const std::size_t run = std::min<std::size_t>(entry.length, entry.source_line.size() - pad);
    os << std::string(pad, ' ');
    if (os.is_displayed())
      os.changeColor(llvm::raw_ostream::GREEN, true);
    os << '^';
    if (run > 1)
      os << std::string(run - 1, '~');
    if (os.is_displayed())
      os.resetColor();
    os << '\n';
  }
}

std::error_code SyntaxError::convertToErrorCode() const { return llvm::inconvertibleErrorCode(); }

}  // namespace lox::parser
