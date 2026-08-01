#include "scanner/scanner.h"

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "scanner/error.h"
#include "scanner/token.h"

namespace lox {

namespace {

// --- character classifiers ------------------------------------------------

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

bool IsHexDigit(char c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

bool IsBaseDigit(char c, unsigned base) {
  switch (base) {
    case 2:
      return c == '0' || c == '1';
    case 8:
      return c >= '0' && c <= '7';
    case 16:
      return IsHexDigit(c);
    default:
      return false;
  }
}

int HexDigitValue(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}

// --- token construction ---------------------------------------------------

Token MakeToken(llvm::StringRef source,
                TokenType type,
                std::size_t start,
                std::size_t pos,
                std::uint32_t line,
                std::uint32_t column) {
  Token token;
  token.type = type;
  token.line = line;
  token.column = column;
  token.lexeme = source.slice(start, pos);
  return token;
}

// --- keyword table ---------------------------------------------------------

// lexeme -> TokenType for the 21 reserved words. Identifier vs keyword
// resolution is an exact map lookup, so "truex" stays an identifier.
const llvm::StringMap<TokenType> &KeywordTable() {
  static const llvm::StringMap<TokenType> table = [] {
    llvm::StringMap<TokenType> table;
    table.try_emplace("class", TokenType::kClass);
    table.try_emplace("else", TokenType::kElse);
    table.try_emplace("false", TokenType::kFalse);
    table.try_emplace("for", TokenType::kFor);
    table.try_emplace("fun", TokenType::kFun);
    table.try_emplace("if", TokenType::kIf);
    table.try_emplace("null", TokenType::kNull);
    table.try_emplace("return", TokenType::kReturn);
    table.try_emplace("super", TokenType::kSuper);
    table.try_emplace("this", TokenType::kThis);
    table.try_emplace("true", TokenType::kTrue);
    table.try_emplace("while", TokenType::kWhile);
    table.try_emplace("extends", TokenType::kExtends);
    table.try_emplace("import", TokenType::kImport);
    table.try_emplace("try", TokenType::kTry);
    table.try_emplace("catch", TokenType::kCatch);
    table.try_emplace("throw", TokenType::kThrow);
    table.try_emplace("break", TokenType::kBreak);
    table.try_emplace("let", TokenType::kLet);
    table.try_emplace("const", TokenType::kConst);
    table.try_emplace("as", TokenType::kAs);
    return table;
  }();
  return table;
}

// --- diagnostics support ---------------------------------------------------

// The full source line containing `pos`, for error display. A copy: errors
// must stay valid even if the source buffer is freed before being consumed.
std::string CurrentSourceLine(llvm::StringRef source, std::size_t pos) {
  std::size_t begin = pos;
  while (begin > 0 && source[begin - 1] != '\n' && source[begin - 1] != '\r') {
    --begin;
  }
  std::size_t end = pos;
  while (end < source.size() && source[end] != '\n' && source[end] != '\r') {
    ++end;
  }
  return source.slice(begin, end).str();
}

}  // namespace

}  // namespace lox

namespace scanner {

char LexicalError::ID = 0;

void LexicalError::log(llvm::raw_ostream &os) const {
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

std::error_code LexicalError::convertToErrorCode() const { return llvm::inconvertibleErrorCode(); }

}  // namespace scanner

namespace lox {

Scanner::Scanner(llvm::StringRef source, llvm::StringRef filename)
    : source_(source), filename_(filename.str()) {}

void Scanner::Reset(llvm::StringRef source) {
  source_ = source;
  pos_ = 0;
  line_ = 1;
  col_ = 1;
  crlf_pending_ = false;
  token_start_ = 0;
  token_start_line_ = 1;
  token_start_col_ = 1;
  tokens_.clear();
  entries_.clear();
}

auto Scanner::Scan() -> llvm::Expected<llvm::SmallVector<Token, 0>> {
  Reset(source_);
  while (pos_ < source_.size()) {
    ScanToken();
  }
  token_start_ = pos_;
  token_start_line_ = line_;
  token_start_col_ = col_;
  EmitToken(TokenType::kEof);
  if (!entries_.empty()) {
    return llvm::make_error<scanner::LexicalError>(filename_, std::move(entries_));
  }
  return std::move(tokens_);
}

char Scanner::Peek(std::size_t ahead) const {
  if (pos_ + ahead >= source_.size())
    return '\0';
  return source_[pos_ + ahead];
}

char Scanner::Advance() {
  if (pos_ >= source_.size())
    return '\0';
  const char c = source_[pos_++];
  if (c == '\n') {
    if (!crlf_pending_)
      ++line_;
    col_ = 1;
    crlf_pending_ = false;
  } else if (c == '\r') {
    ++line_;
    col_ = 1;
    crlf_pending_ = true;
  } else {
    ++col_;
    crlf_pending_ = false;
  }
  return c;
}

bool Scanner::Match(char expected) {
  if (Peek() != expected)
    return false;
  Advance();
  return true;
}

void Scanner::ScanToken() {
  token_start_ = pos_;
  token_start_line_ = line_;
  token_start_col_ = col_;
  const char c = Peek();
  switch (c) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
      SkipWhitespaceAndComments();
      return;
    case '(':
      return EmitSingle(TokenType::kLeftParen);
    case ')':
      return EmitSingle(TokenType::kRightParen);
    case '{':
      return EmitSingle(TokenType::kLeftBrace);
    case '}':
      return EmitSingle(TokenType::kRightBrace);
    case '[':
      return EmitSingle(TokenType::kLeftBracket);
    case ']':
      return EmitSingle(TokenType::kRightBracket);
    case ',':
      return EmitSingle(TokenType::kComma);
    case ';':
      return EmitSingle(TokenType::kSemicolon);
    case '.':
      return EmitSingle(TokenType::kDot);
    case '-':
      return EmitSingle(TokenType::kMinus);
    case '+':
      return EmitSingle(TokenType::kPlus);
    case '/':
      if (Peek(1) == '/') {
        SkipWhitespaceAndComments();
        return;
      }
      return EmitSingle(TokenType::kSlash);
    case '*':
      return EmitSingle(TokenType::kStar);
    case '^':
      return EmitSingle(TokenType::kCaret);
    case '~':
      return EmitSingle(TokenType::kTilde);
    case '!': {
      Advance();
      EmitToken(Match('=') ? TokenType::kBangEqual : TokenType::kBang);
      return;
    }
    case '=': {
      Advance();
      EmitToken(Match('=') ? TokenType::kEqualEqual : TokenType::kEqual);
      return;
    }
    case '<': {
      Advance();
      if (Match('=')) {
        EmitToken(TokenType::kLessEqual);
      } else if (Match('<')) {
        EmitToken(TokenType::kLessLess);
      } else {
        EmitToken(TokenType::kLess);
      }
      return;
    }
    case '>': {
      Advance();
      if (Match('=')) {
        EmitToken(TokenType::kGreaterEqual);
      } else if (Match('>')) {
        EmitToken(TokenType::kGreaterGreater);
      } else {
        EmitToken(TokenType::kGreater);
      }
      return;
    }
    case '|': {
      Advance();
      EmitToken(Match('|') ? TokenType::kPipePipe : TokenType::kPipe);
      return;
    }
    case '&': {
      Advance();
      EmitToken(Match('&') ? TokenType::kAmpersandAmpersand : TokenType::kAmpersand);
      return;
    }
    case '"':
      ScanString();
      return;
    default:
      if (IsDigit(c)) {
        ScanNumber();
        return;
      }
      if (IsAlpha(c)) {
        ScanIdentifier();
        return;
      }
      ReportError(token_start_, 1, "unexpected character '" + std::string(1, c) + "'");
      Advance();
      return;
  }
}

void Scanner::SkipWhitespaceAndComments() {
  for (;;) {
    const char c = Peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      Advance();
      continue;
    }
    if (c == '/' && Peek(1) == '/') {
      while (pos_ < source_.size() && Peek() != '\n' && Peek() != '\r') {
        Advance();
      }
      continue;
    }
    return;
  }
}

void Scanner::ScanIdentifier() {
  while (IsAlpha(Peek()) || IsDigit(Peek())) {
    Advance();
  }
  const llvm::StringMap<TokenType> &keywords = KeywordTable();
  const auto keyword = keywords.find(source_.slice(token_start_, pos_));
  EmitToken(keyword != keywords.end() ? keyword->second : TokenType::kIdentifier);
}

void Scanner::ScanNumber() {
  const char c = Peek();
  if (c == '0' && (Peek(1) == 'x' || Peek(1) == 'X' || Peek(1) == 'o' || Peek(1) == 'O' ||
                   Peek(1) == 'b' || Peek(1) == 'B')) {
    ScanBasedInteger();
    return;
  }
  ScanDecimalNumber();
}

void Scanner::ScanDecimalNumber() {
  bool overflowed = false;
  std::int64_t int_value = 0;
  while (IsDigit(Peek())) {
    if (!overflowed) {
      const std::int64_t digit = Peek() - '0';
      overflowed = __builtin_mul_overflow(int_value, 10, &int_value) ||
                   __builtin_add_overflow(int_value, digit, &int_value);
    }
    Advance();
  }

  bool is_float = false;

  // Fraction: the '.' commits only when a digit follows; '_' right after a
  // '.' in a numeric context is an underscore error (1._5 is not 1 + . + _5).
  if (Peek() == '.') {
    if (IsDigit(Peek(1))) {
      Advance();
      while (IsDigit(Peek())) {
        Advance();
      }
      is_float = true;
    } else if (Peek(1) == '_') {
      ReportNumberError("underscore not allowed in numeric literal");
      return;
    }
  }

  // Exponent: non-committal lookahead. 'e'/'E' is consumed only when followed
  // by an optional sign and at least one digit; otherwise it starts an
  // identifier (1eaten -> 1 + eaten, 1.5e -> 1.5 + e, 1e_5 -> 1 + e_5).
  if (Peek() == 'e' || Peek() == 'E') {
    const char next = Peek(1);
    if (IsDigit(next) || ((next == '+' || next == '-') && IsDigit(Peek(2)))) {
      Advance();
      if (Peek() == '+' || Peek() == '-') {
        Advance();
      }
      while (IsDigit(Peek())) {
        Advance();
      }
      is_float = true;
    }
  }

  if (Peek() == '_') {
    ReportNumberError("underscore not allowed in numeric literal");
    return;
  }

  if (is_float) {
    const llvm::StringRef lexeme = source_.slice(token_start_, pos_);
    double value = 0.0;
    const std::from_chars_result result =
        std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value);
    if (result.ec == std::errc::result_out_of_range) {
      // Distinguish overflow from underflow: a literal that exceeds the
      // double range but fits long double may still be an underflow
      // (accepted as 0.0 / subnormal) or an overflow (rejected).
      long double wide = 0.0L;
      const std::from_chars_result wide_result =
          std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), wide);
      if (wide_result.ec == std::errc::result_out_of_range || wide > DBL_MAX || wide < -DBL_MAX) {
        ReportNumberError("floating-point literal out of range");
        return;
      }
      value = static_cast<double>(wide);
    }
    EmitValueToken(TokenType::kFloat, value);
  } else if (overflowed) {
    ReportNumberError("integer literal out of range");
  } else {
    EmitValueToken(TokenType::kInteger, int_value);
  }
}

void Scanner::ScanBasedInteger() {
  const char prefix = Peek(1);
  unsigned base = 16;
  llvm::StringRef base_name = "hexadecimal";
  if (prefix == 'o' || prefix == 'O') {
    base = 8;
    base_name = "octal";
  } else if (prefix == 'b' || prefix == 'B') {
    base = 2;
    base_name = "binary";
  }
  Advance();  // '0'
  Advance();  // prefix; the prefix is committed even without digits

  bool any_digit = false;
  bool overflowed = false;
  std::int64_t value = 0;
  while (IsBaseDigit(Peek(), base)) {
    if (!overflowed) {
      const std::int64_t digit = HexDigitValue(Peek());
      overflowed = __builtin_mul_overflow(value, static_cast<std::int64_t>(base), &value) ||
                   __builtin_add_overflow(value, digit, &value);
    }
    Advance();
    any_digit = true;
  }

  if (!any_digit) {
    ReportNumberError((base_name + " literal has no digits").str());
    return;
  }
  if (Peek() == '_') {
    ReportNumberError("underscore not allowed in numeric literal");
    return;
  }
  if (overflowed) {
    ReportNumberError("integer literal out of range");
    return;
  }
  EmitValueToken(TokenType::kInteger, value);
}

void Scanner::ScanString() {
  Advance();  // opening quote
  for (;;) {
    if (pos_ >= source_.size()) {
      ReportError(token_start_, pos_ - token_start_, "unterminated string literal");
      return;
    }
    const char c = Peek();
    if (c == '"') {
      Advance();
      EmitToken(TokenType::kString);
      return;
    }
    if (c == '\n' || c == '\r') {
      ReportError(token_start_, pos_ - token_start_, "unterminated string literal");
      return;
    }
    if (c != '\\') {
      Advance();
      continue;
    }

    // Escape sequence: validate the syntax only; decoding and interning are
    // deferred to the compiler. A bad escape dooms the whole token (one
    // error), and scanning fast-forwards to the string terminator.
    const std::size_t escape_start = pos_;
    Advance();  // backslash
    const char e = Peek();
    if (e == '\n' || e == '\r' || pos_ >= source_.size()) {
      ReportError(token_start_, pos_ - token_start_, "unterminated string literal");
      return;
    }
    bool invalid = false;
    switch (e) {
      case 'n':
      case 'r':
      case 't':
      case '\\':
      case '"':
      case '0':
        Advance();
        break;
      case 'x': {
        Advance();
        const std::size_t hex_start = pos_;
        while (IsHexDigit(Peek()) && pos_ - hex_start < 2) {
          Advance();
        }
        if (pos_ >= source_.size() || Peek() == '\n' || Peek() == '\r') {
          ReportError(token_start_, pos_ - token_start_, "unterminated string literal");
          return;
        }
        invalid = pos_ - hex_start != 2;
        break;
      }
      case 'u': {
        Advance();
        if (Peek() == '{') {
          Advance();
          const std::size_t hex_start = pos_;
          while (IsHexDigit(Peek())) {
            Advance();
          }
          if (pos_ >= source_.size() || Peek() == '\n' || Peek() == '\r') {
            ReportError(token_start_, pos_ - token_start_, "unterminated string literal");
            return;
          }
          if (pos_ == hex_start || Peek() != '}') {
            invalid = true;
          } else {
            Advance();  // '}'
          }
        } else {
          const std::size_t hex_start = pos_;
          while (IsHexDigit(Peek()) && pos_ - hex_start < 4) {
            Advance();
          }
          if (pos_ >= source_.size() || Peek() == '\n' || Peek() == '\r') {
            ReportError(token_start_, pos_ - token_start_, "unterminated string literal");
            return;
          }
          invalid = pos_ - hex_start != 4;
        }
        break;
      }
      default:
        Advance();
        invalid = true;
        break;
    }
    if (invalid) {
      ReportError(escape_start, pos_ - escape_start, "invalid escape sequence");
      while (pos_ < source_.size() && Peek() != '"' && Peek() != '\n' && Peek() != '\r') {
        Advance();
      }
      if (pos_ < source_.size() && Peek() == '"') {
        Advance();  // consume the closing quote of the doomed token
      }
      return;
    }
  }
}

void Scanner::EmitSingle(TokenType type) {
  Advance();
  EmitToken(type);
}

void Scanner::EmitToken(TokenType type) {
  tokens_.push_back(
      MakeToken(source_, type, token_start_, pos_, token_start_line_, token_start_col_));
}

void Scanner::EmitValueToken(TokenType type, TokenValue value) {
  Token token = MakeToken(source_, type, token_start_, pos_, token_start_line_, token_start_col_);
  token.value = value;
  tokens_.push_back(std::move(token));
}

void Scanner::ReportError(std::size_t start, std::size_t length, std::string message) {
  // `start` always sits on the same line as the token start: escape-sequence
  // errors never cross a line ending (a bare newline short-circuits to the
  // unterminated-string error), and every other error starts at the token
  // start itself.
  const std::uint32_t column =
      static_cast<std::uint32_t>(token_start_col_ + (start - token_start_));
  entries_.push_back(scanner::LexicalError::Entry{token_start_line_,
                                                  column,
                                                  static_cast<std::uint32_t>(length),
                                                  std::move(message),
                                                  CurrentSourceLine(source_, start)});
}

void Scanner::ReportNumberError(std::string message) {
  // Drop the whole offending literal: consume the maximal run of characters
  // that could belong to a numeric token, then report once (1_000 is not
  // 1 + _000; 1.5e10_ is not 1.5e10 + _).
  while (IsDigit(Peek()) || IsAlpha(Peek()) || Peek() == '.' || Peek() == '_') {
    Advance();
  }
  ReportError(token_start_, pos_ - token_start_, std::move(message));
}

}  // namespace lox
