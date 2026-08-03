#ifndef LOX_SCANNER_SCANNER_H_
#define LOX_SCANNER_SCANNER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "scanner/error.h"
#include "scanner/token.h"

namespace lox {

// Single-pass lexical analyzer: turns a source buffer into a flat Token stream
// with zero-copy lexemes and pre-parsed numeric values, per docs/scanner.md.
//
// source must outlive this scanner and every token it produces (Token::lexeme
// is a view into it). filename is used only for error messages.
class Scanner {
 public:
  Scanner(llvm::StringRef source, llvm::StringRef filename = {});
  ~Scanner() = default;

  Scanner(const Scanner &) = delete;
  Scanner &operator=(const Scanner &) = delete;
  Scanner(Scanner &&) = default;
  Scanner &operator=(Scanner &&) = default;

  // Scans the whole source and returns the token stream terminated by kEof,
  // or a lox::scanner::LexicalError accumulating every lexical problem found (the
  // partial stream is discarded). Resets all scanning state first, so it is
  // idempotent and re-entrant.
  [[nodiscard]] auto Scan() -> llvm::Expected<llvm::SmallVector<Token, 0>>;

  // Points the scanner at new source and resets scanning state. The filename
  // given at construction is unchanged.
  void Reset(llvm::StringRef source);

 private:
  // Character-level primitives. Peek returns '\0' past the end of input; a
  // real '\0' byte inside the source is treated as an unexpected character by
  // the dispatcher in ScanToken.
  char Peek(std::size_t ahead = 0) const;
  char Advance();
  bool Match(char expected);

  // Token-level scanning steps.
  void ScanToken();
  void SkipWhitespaceAndComments();
  void ScanIdentifier();
  void ScanNumber();
  void ScanDecimalNumber();
  void ScanBasedInteger();
  void ScanString();

  void EmitSingle(TokenType type);
  void EmitToken(TokenType type);
  void EmitValueToken(TokenType type, TokenValue value);
  void ReportError(std::size_t start, std::size_t length, std::string message);
  void ReportNumberError(std::string message);

  llvm::StringRef source_;
  std::string filename_;
  std::size_t pos_ = 0;
  std::uint32_t line_ = 1;
  std::uint32_t col_ = 1;
  // True right after a consumed '\r' whose '\n' partner must not re-count the
  // line (a "\r\n" pair is a single line ending).
  bool crlf_pending_ = false;
  std::size_t token_start_ = 0;
  std::uint32_t token_start_line_ = 1;
  std::uint32_t token_start_col_ = 1;
  llvm::SmallVector<Token, 0> tokens_;
  std::vector<scanner::LexicalError::Entry> entries_;
};

}  // namespace lox

#endif  // LOX_SCANNER_SCANNER_H_
