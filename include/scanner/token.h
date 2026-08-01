#ifndef LOX_SCANNER_TOKEN_H_
#define LOX_SCANNER_TOKEN_H_

#include <cstdint>
#include <variant>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace lox {

// Every legal token kind of the lox language. The 55 enumerators cover the
// keywords, literals, punctuators/operators and the end-of-input sentinel.
// Scoped so enumerator names do not pollute the lox namespace.
enum class TokenType : std::uint8_t {
  // Keywords.
  kClass,
  kElse,
  kFalse,
  kFor,
  kFun,
  kIf,
  kNull,
  kReturn,
  kSuper,
  kThis,
  kTrue,
  kWhile,
  kExtends,
  kImport,
  kTry,
  kCatch,
  kThrow,
  kBreak,
  kLet,
  kConst,
  kAs,
  // Literals.
  kInteger,
  kFloat,
  kString,
  kIdentifier,
  // Single-char punctuators and operators.
  kLeftParen,
  kRightParen,
  kLeftBrace,
  kRightBrace,
  kLeftBracket,
  kRightBracket,
  kComma,
  kSemicolon,
  kDot,
  kBang,
  kTilde,
  kMinus,
  kPlus,
  kSlash,
  kStar,
  kPipe,
  kCaret,
  kAmpersand,
  kLess,
  kGreater,
  kEqual,
  // Two-char operators.
  kLessLess,
  kGreaterGreater,
  kEqualEqual,
  kBangEqual,
  kGreaterEqual,
  kLessEqual,
  kPipePipe,
  kAmpersandAmpersand,
  // Sentinel marking end of input.
  kEof,
};

// Payload carried by value-bearing tokens. std::monostate MUST stay the first
// alternative: aggregate/default construction relies on it so that tokens
// without a numeric value default to "no value". Only kInteger/kFloat populate
// the numeric alternatives; kString and kIdentifier carry their content via
// Token::lexeme (escape decoding and interning are deferred to the compiler).
using TokenValue = std::variant<std::monostate, std::int64_t, double>;

// A single lexeme produced by the scanner. Value semantics: tokens live in a
// flat vector (llvm::SmallVector<Token, 0>) and own no heap storage. lexeme is
// a zero-copy view into the source buffer; line/column are 1-indexed.
struct Token {
  TokenType type{};
  std::uint32_t line{};
  std::uint32_t column{};
  llvm::StringRef lexeme{};
  TokenValue value{};

  // Stable display name of this token's kind, for diagnostics such as
  // "expected X, found Y". Implemented in token.cpp as a switch without a
  // default so -Wswitch stays armed: adding a TokenType without a case
  // becomes a compile warning.
  llvm::StringRef TypeName() const;

  // Prints this token as "<line>:<col> <TypeName> <lexeme>", with the
  // decoded numeric value appended in brackets for kInteger/kFloat. The
  // lexeme is escaped so control characters inside string literals stay on
  // one line.
  void Print(llvm::raw_ostream &os) const;
};

}  // namespace lox

#endif  // LOX_SCANNER_TOKEN_H_
