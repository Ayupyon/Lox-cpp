#include "scanner/token.h"

#include <charconv>
#include <cstdint>

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace lox {

llvm::StringRef Token::TypeName() const {
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

void Token::Print(llvm::raw_ostream &os) const {
  os << line << ':' << column << ' ' << TypeName();
  if (!lexeme.empty()) {
    os << ' ';
    llvm::printEscapedString(lexeme, os);
  }
  if (const auto *integer = std::get_if<std::int64_t>(&value)) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *integer);
    os << " [" << llvm::StringRef(buffer, result.ptr - buffer) << ']';
  } else if (const auto *floating = std::get_if<double>(&value)) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *floating);
    os << " [" << llvm::StringRef(buffer, result.ptr - buffer) << ']';
  }
  os << '\n';
}

}  // namespace lox
