#ifndef LOX_SCANNER_TOKEN_H_
#define LOX_SCANNER_TOKEN_H_

#include <cstdint>
#include <variant>

#include "llvm/ADT/StringRef.h"

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
};

// Stable display name for a token kind, for diagnostics such as "expected X,
// found Y". Deliberately a switch without a default so -Wswitch stays armed:
// adding a TokenType without a case becomes a compile warning.
inline llvm::StringRef TokenTypeName(TokenType type) {
  switch (type) {
    case TokenType::kClass:
      return "class";
    case TokenType::kElse:
      return "else";
    case TokenType::kFalse:
      return "false";
    case TokenType::kFor:
      return "for";
    case TokenType::kFun:
      return "fun";
    case TokenType::kIf:
      return "if";
    case TokenType::kNull:
      return "null";
    case TokenType::kReturn:
      return "return";
    case TokenType::kSuper:
      return "super";
    case TokenType::kThis:
      return "this";
    case TokenType::kTrue:
      return "true";
    case TokenType::kWhile:
      return "while";
    case TokenType::kExtends:
      return "extends";
    case TokenType::kImport:
      return "import";
    case TokenType::kTry:
      return "try";
    case TokenType::kCatch:
      return "catch";
    case TokenType::kThrow:
      return "throw";
    case TokenType::kBreak:
      return "break";
    case TokenType::kLet:
      return "let";
    case TokenType::kConst:
      return "const";
    case TokenType::kAs:
      return "as";
    case TokenType::kInteger:
      return "integer";
    case TokenType::kFloat:
      return "float";
    case TokenType::kString:
      return "string";
    case TokenType::kIdentifier:
      return "identifier";
    case TokenType::kLeftParen:
      return "(";
    case TokenType::kRightParen:
      return ")";
    case TokenType::kLeftBrace:
      return "{";
    case TokenType::kRightBrace:
      return "}";
    case TokenType::kLeftBracket:
      return "[";
    case TokenType::kRightBracket:
      return "]";
    case TokenType::kComma:
      return ",";
    case TokenType::kSemicolon:
      return ";";
    case TokenType::kDot:
      return ".";
    case TokenType::kBang:
      return "!";
    case TokenType::kTilde:
      return "~";
    case TokenType::kMinus:
      return "-";
    case TokenType::kPlus:
      return "+";
    case TokenType::kSlash:
      return "/";
    case TokenType::kStar:
      return "*";
    case TokenType::kPipe:
      return "|";
    case TokenType::kCaret:
      return "^";
    case TokenType::kAmpersand:
      return "&";
    case TokenType::kLess:
      return "<";
    case TokenType::kGreater:
      return ">";
    case TokenType::kEqual:
      return "=";
    case TokenType::kLessLess:
      return "<<";
    case TokenType::kGreaterGreater:
      return ">>";
    case TokenType::kEqualEqual:
      return "==";
    case TokenType::kBangEqual:
      return "!=";
    case TokenType::kGreaterEqual:
      return ">=";
    case TokenType::kLessEqual:
      return "<=";
    case TokenType::kPipePipe:
      return "||";
    case TokenType::kAmpersandAmpersand:
      return "&&";
    case TokenType::kEof:
      return "eof";
  }
}

}  // namespace lox

#endif  // LOX_SCANNER_TOKEN_H_
