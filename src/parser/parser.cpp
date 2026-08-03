#include "parser/parser.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "parser/ast.h"
#include "parser/error.h"
#include "scanner/token.h"

namespace lox {

namespace {

// Maps a binary operator token to its BinaryOp enumerator. Returns
// std::nullopt for non-binary-operator tokens.
std::optional<BinaryOp> BinaryOpFromToken(TokenType type) {
  switch (type) {
    case TokenType::kPlus:
      return BinaryOp::kAdd;
    case TokenType::kMinus:
      return BinaryOp::kSubtract;
    case TokenType::kStar:
      return BinaryOp::kMultiply;
    case TokenType::kSlash:
      return BinaryOp::kDivide;
    case TokenType::kLessLess:
      return BinaryOp::kShiftLeft;
    case TokenType::kGreaterGreater:
      return BinaryOp::kShiftRight;
    case TokenType::kLess:
      return BinaryOp::kLess;
    case TokenType::kLessEqual:
      return BinaryOp::kLessEqual;
    case TokenType::kGreater:
      return BinaryOp::kGreater;
    case TokenType::kGreaterEqual:
      return BinaryOp::kGreaterEqual;
    case TokenType::kEqualEqual:
      return BinaryOp::kEqual;
    case TokenType::kBangEqual:
      return BinaryOp::kNotEqual;
    case TokenType::kAmpersand:
      return BinaryOp::kBitAnd;
    case TokenType::kPipe:
      return BinaryOp::kBitOr;
    case TokenType::kCaret:
      return BinaryOp::kBitXor;
    default:
      return std::nullopt;
  }
}

// Maps a logical operator token to its LogicalOp enumerator.
std::optional<LogicalOp> LogicalOpFromToken(TokenType type) {
  switch (type) {
    case TokenType::kPipePipe:
      return LogicalOp::kOr;
    case TokenType::kAmpersandAmpersand:
      return LogicalOp::kAnd;
    default:
      return std::nullopt;
  }
}

// Maps a unary operator token to its UnaryOp enumerator.
std::optional<UnaryOp> UnaryOpFromToken(TokenType type) {
  switch (type) {
    case TokenType::kBang:
      return UnaryOp::kNot;
    case TokenType::kMinus:
      return UnaryOp::kNegate;
    case TokenType::kTilde:
      return UnaryOp::kBitNot;
    default:
      return std::nullopt;
  }
}

}  // namespace

// --- Construction & top-level parse ------------------------------------------

Parser::Parser(llvm::StringRef source, llvm::SmallVector<Token, 0> tokens, llvm::StringRef filename)
    : source_(source), filename_(filename), tokens_(std::move(tokens)) {
  assert(!tokens_.empty() && tokens_.back().type == TokenType::kEof &&
         "token stream must be EOF-terminated");
}

auto Parser::Parse() -> llvm::Expected<llvm::SmallVector<StmtPtr, 0>> {
  llvm::SmallVector<StmtPtr, 0> statements;
  while (!IsAtEnd()) {
    auto stmt = ParseDeclaration();
    if (stmt) {
      statements.push_back(std::move(*stmt));
    } else {
      llvm::consumeError(stmt.takeError());
      Synchronize();
    }
  }
  if (!errors_.empty()) {
    return llvm::make_error<parser::SyntaxError>(filename_, std::move(errors_));
  }
  return statements;
}

// --- Token cursor ------------------------------------------------------------

const Token &Parser::Peek(std::size_t ahead) const {
  // current_ <= last always holds (Advance halts at EOF), so last - current_
  // cannot underflow. Clamp ahead so we never read past the EOF terminator.
  const std::size_t last = tokens_.size() - 1;
  const std::size_t idx = (ahead <= last - current_) ? current_ + ahead : last;
  return tokens_[idx];
}

const Token &Parser::Previous() const {
  assert(current_ > 0);
  return tokens_[current_ - 1];
}

bool Parser::IsAtEnd() const { return Peek().type == TokenType::kEof; }

const Token &Parser::Advance() {
  if (!IsAtEnd()) {
    ++current_;
  }
  return tokens_[current_ - 1];
}

bool Parser::Check(TokenType type) const { return Peek().type == type; }

bool Parser::Match(TokenType type) {
  if (Check(type)) {
    Advance();
    return true;
  }
  return false;
}

llvm::Error Parser::Expect(TokenType type, llvm::StringRef msg) {
  if (Check(type)) {
    Advance();
    return llvm::Error::success();
  }
  return ErrorAt(Peek(), msg);
}

// --- Error handling ----------------------------------------------------------

llvm::Error Parser::ErrorAt(const Token &tok, llvm::StringRef msg) {
  errors_.push_back(parser::SyntaxError::Entry{tok.line,
                                               tok.column,
                                               static_cast<std::uint32_t>(tok.lexeme.size()),
                                               msg.str(),
                                               CurrentSourceLine(tok.line)});
  return llvm::make_error<parser::ParseAbort>();
}

void Parser::Synchronize() {
  while (!IsAtEnd()) {
    if (Previous().type == TokenType::kSemicolon) {
      return;
    }
    switch (Peek().type) {
      case TokenType::kRightBrace:
      case TokenType::kClass:
      case TokenType::kFun:
      case TokenType::kLet:
      case TokenType::kConst:
      case TokenType::kImport:
      case TokenType::kIf:
      case TokenType::kWhile:
      case TokenType::kFor:
      case TokenType::kTry:
      case TokenType::kThrow:
      case TokenType::kReturn:
      case TokenType::kBreak:
        return;
      default:
        Advance();
        break;
    }
  }
}

std::string Parser::CurrentSourceLine(std::uint32_t target_line) const {
  std::uint32_t line = 1;
  std::size_t start = 0;
  for (std::size_t i = 0; i < source_.size() && line < target_line; ++i) {
    if (source_[i] == '\n') {
      ++line;
      start = i + 1;
    } else if (source_[i] == '\r') {
      ++line;
      if (i + 1 < source_.size() && source_[i + 1] == '\n') {
        ++i;
      }
      start = i + 1;
    }
  }
  if (line != target_line) {
    return {};
  }
  std::size_t end = start;
  while (end < source_.size() && source_[end] != '\n' && source_[end] != '\r') {
    ++end;
  }
  return source_.slice(start, end).str();
}

// --- Declarations ------------------------------------------------------------

auto Parser::ParseDeclaration() -> llvm::Expected<StmtPtr> {
  switch (Peek().type) {
    case TokenType::kClass:
      return ParseClassDecl();
    case TokenType::kFun: {
      const Token &fun_tok = Advance();  // consume 'fun'
      auto fun = ParseFunction("function");
      if (!fun) {
        return fun.takeError();
      }
      // Override loc: ParseFunction uses the name token's position (correct
      // for methods), but a top-level function's leading token is 'fun'.
      (*fun)->loc = {fun_tok.line, fun_tok.column};
      return StmtPtr(std::move(*fun));
    }
    case TokenType::kLet:
      return ParseVarDecl();
    case TokenType::kConst:
      return ParseConstDecl();
    case TokenType::kImport:
      return ParseImportDecl();
    default:
      return ParseStatement();
  }
}

auto Parser::ParseClassDecl() -> llvm::Expected<StmtPtr> {
  const Token &class_tok = Advance();  // consume 'class'
  SourceLocation loc = {class_tok.line, class_tok.column};

  if (auto err = Expect(TokenType::kIdentifier, "expected class name")) {
    return std::move(err);
  }
  const Token &name = Previous();

  llvm::StringRef superclass;
  if (Match(TokenType::kExtends)) {
    if (auto err = Expect(TokenType::kIdentifier, "expected superclass name")) {
      return std::move(err);
    }
    superclass = Previous().lexeme;
  }

  if (auto err = Expect(TokenType::kLeftBrace, "expected '{' before class body")) {
    return std::move(err);
  }

  llvm::SmallVector<std::unique_ptr<FunDeclStmt>, 4> methods;
  while (!Check(TokenType::kRightBrace) && !IsAtEnd()) {
    auto method = ParseFunction("method");
    if (method) {
      methods.push_back(std::move(*method));
    } else {
      // On error, skip to the closing brace: method names are plain
      // identifiers, not synchronization anchors, so we cannot reliably
      // resume at the next method.
      llvm::consumeError(method.takeError());
      while (!Check(TokenType::kRightBrace) && !IsAtEnd()) {
        Advance();
      }
    }
  }

  if (auto err = Expect(TokenType::kRightBrace, "expected '}' after class body")) {
    return std::move(err);
  }

  return std::make_unique<ClassStmt>(loc, name.lexeme, superclass, std::move(methods));
}

auto Parser::ParseFunction(llvm::StringRef kind) -> llvm::Expected<std::unique_ptr<FunDeclStmt>> {
  std::string name_msg = (llvm::Twine("expected ") + kind + " name").str();
  if (auto err = Expect(TokenType::kIdentifier, name_msg)) {
    return std::move(err);
  }
  const Token &name = Previous();
  SourceLocation loc = {name.line, name.column};

  std::string paren_msg = (llvm::Twine("expected '(' after ") + kind + " name").str();
  if (auto err = Expect(TokenType::kLeftParen, paren_msg)) {
    return std::move(err);
  }

  auto params = ParseParameters();
  if (!params) {
    return params.takeError();
  }

  auto body = ParseBlock();
  if (!body) {
    return body.takeError();
  }

  return std::make_unique<FunDeclStmt>(loc, name.lexeme, std::move(*params), std::move(*body));
}

auto Parser::ParseVarDecl() -> llvm::Expected<StmtPtr> {
  const Token &let_tok = Advance();  // consume 'let'
  SourceLocation loc = {let_tok.line, let_tok.column};

  if (auto err = Expect(TokenType::kIdentifier, "expected variable name")) {
    return std::move(err);
  }
  const Token &name = Previous();

  if (auto err = Expect(TokenType::kEqual, "expected '=' after variable name")) {
    return std::move(err);
  }

  auto expr = ParseExpression();
  if (!expr) {
    return expr.takeError();
  }

  if (auto err = Expect(TokenType::kSemicolon, "expected ';' after variable declaration")) {
    return std::move(err);
  }

  return std::make_unique<VarDeclStmt>(loc, name.lexeme, std::move(*expr));
}

auto Parser::ParseConstDecl() -> llvm::Expected<StmtPtr> {
  const Token &const_tok = Advance();  // consume 'const'
  SourceLocation loc = {const_tok.line, const_tok.column};

  if (auto err = Expect(TokenType::kIdentifier, "expected constant name")) {
    return std::move(err);
  }
  const Token &name = Previous();

  if (auto err = Expect(TokenType::kEqual, "expected '=' after constant name")) {
    return std::move(err);
  }

  auto expr = ParseExpression();
  if (!expr) {
    return expr.takeError();
  }

  if (auto err = Expect(TokenType::kSemicolon, "expected ';' after constant declaration")) {
    return std::move(err);
  }

  return std::make_unique<ConstDeclStmt>(loc, name.lexeme, std::move(*expr));
}

auto Parser::ParseImportDecl() -> llvm::Expected<StmtPtr> {
  const Token &import_tok = Advance();  // consume 'import'
  SourceLocation loc = {import_tok.line, import_tok.column};

  if (auto err = Expect(TokenType::kString, "expected string path after 'import'")) {
    return std::move(err);
  }
  const Token &path = Previous();

  llvm::StringRef alias;
  if (Match(TokenType::kAs)) {
    if (auto err = Expect(TokenType::kIdentifier, "expected alias name after 'as'")) {
      return std::move(err);
    }
    alias = Previous().lexeme;
  }

  if (auto err = Expect(TokenType::kSemicolon, "expected ';' after import")) {
    return std::move(err);
  }

  return std::make_unique<ImportStmt>(loc, path.lexeme, alias);
}

// --- Statements --------------------------------------------------------------

auto Parser::ParseStatement() -> llvm::Expected<StmtPtr> {
  switch (Peek().type) {
    case TokenType::kLeftBrace: {
      auto block = ParseBlock();
      if (!block) {
        return block.takeError();
      }
      return StmtPtr(std::move(*block));
    }
    case TokenType::kIf:
      return ParseIfStmt();
    case TokenType::kWhile:
      return ParseWhileStmt();
    case TokenType::kFor:
      return ParseForStmt();
    case TokenType::kReturn:
      return ParseReturnStmt();
    case TokenType::kThrow:
      return ParseThrowStmt();
    case TokenType::kTry:
      return ParseTryStmt();
    case TokenType::kBreak:
      return ParseBreakStmt();
    default:
      return ParseExpressionStmt();
  }
}

auto Parser::ParseBlock() -> llvm::Expected<std::unique_ptr<BlockStmt>> {
  if (auto err = Expect(TokenType::kLeftBrace, "expected '{' before block")) {
    return std::move(err);
  }
  const Token &brace = Previous();  // the consumed '{'
  SourceLocation loc = {brace.line, brace.column};

  llvm::SmallVector<StmtPtr, 4> statements;
  while (!Check(TokenType::kRightBrace) && !IsAtEnd()) {
    auto stmt = ParseDeclaration();
    if (stmt) {
      statements.push_back(std::move(*stmt));
    } else {
      llvm::consumeError(stmt.takeError());
      Synchronize();
    }
  }

  if (auto err = Expect(TokenType::kRightBrace, "expected '}' after block")) {
    return std::move(err);
  }

  return std::make_unique<BlockStmt>(loc, std::move(statements));
}

auto Parser::ParseIfStmt() -> llvm::Expected<StmtPtr> {
  const Token &if_tok = Advance();  // consume 'if'
  SourceLocation loc = {if_tok.line, if_tok.column};

  if (auto err = Expect(TokenType::kLeftParen, "expected '(' after 'if'")) {
    return std::move(err);
  }
  auto condition = ParseExpression();
  if (!condition) {
    return condition.takeError();
  }
  if (auto err = Expect(TokenType::kRightParen, "expected ')' after if condition")) {
    return std::move(err);
  }

  auto then_branch = ParseBlock();
  if (!then_branch) {
    return then_branch.takeError();
  }

  std::unique_ptr<BlockStmt> else_branch;
  if (Match(TokenType::kElse)) {
    auto else_block = ParseBlock();
    if (!else_block) {
      return else_block.takeError();
    }
    else_branch = std::move(*else_block);
  }

  return std::make_unique<IfStmt>(
      loc, std::move(*condition), std::move(*then_branch), std::move(else_branch));
}

auto Parser::ParseWhileStmt() -> llvm::Expected<StmtPtr> {
  const Token &while_tok = Advance();  // consume 'while'
  SourceLocation loc = {while_tok.line, while_tok.column};

  if (auto err = Expect(TokenType::kLeftParen, "expected '(' after 'while'")) {
    return std::move(err);
  }
  auto condition = ParseExpression();
  if (!condition) {
    return condition.takeError();
  }
  if (auto err = Expect(TokenType::kRightParen, "expected ')' after while condition")) {
    return std::move(err);
  }

  auto body = ParseBlock();
  if (!body) {
    return body.takeError();
  }

  return std::make_unique<WhileStmt>(loc, std::move(*condition), std::move(*body));
}

auto Parser::ParseForStmt() -> llvm::Expected<StmtPtr> {
  const Token &for_tok = Advance();  // consume 'for'
  SourceLocation loc = {for_tok.line, for_tok.column};

  if (auto err = Expect(TokenType::kLeftParen, "expected '(' after 'for'")) {
    return std::move(err);
  }

  // Initializer: varDecl, expression statement, or empty.
  StmtPtr initializer;
  if (Match(TokenType::kSemicolon)) {
    // Empty initializer.
  } else if (Check(TokenType::kLet)) {
    auto init = ParseVarDecl();
    if (!init) {
      return init.takeError();
    }
    initializer = std::move(*init);
  } else {
    auto expr = ParseExpression();
    if (!expr) {
      return expr.takeError();
    }
    SourceLocation expr_loc = (*expr)->loc;
    if (auto err = Expect(TokenType::kSemicolon, "expected ';' after for-loop initializer")) {
      return std::move(err);
    }
    initializer = std::make_unique<ExpressionStmt>(expr_loc, std::move(*expr));
  }

  // Condition (optional).
  ExprPtr condition;
  if (!Check(TokenType::kSemicolon)) {
    auto cond = ParseExpression();
    if (!cond) {
      return cond.takeError();
    }
    condition = std::move(*cond);
  }
  if (auto err = Expect(TokenType::kSemicolon, "expected ';' after for-loop condition")) {
    return std::move(err);
  }

  // Increment (optional).
  ExprPtr increment;
  if (!Check(TokenType::kRightParen)) {
    auto inc = ParseExpression();
    if (!inc) {
      return inc.takeError();
    }
    increment = std::move(*inc);
  }
  if (auto err = Expect(TokenType::kRightParen, "expected ')' after for-loop clauses")) {
    return std::move(err);
  }

  auto body = ParseBlock();
  if (!body) {
    return body.takeError();
  }

  return std::make_unique<ForStmt>(
      loc, std::move(initializer), std::move(condition), std::move(increment), std::move(*body));
}

auto Parser::ParseReturnStmt() -> llvm::Expected<StmtPtr> {
  const Token &return_tok = Advance();  // consume 'return'
  SourceLocation loc = {return_tok.line, return_tok.column};

  ExprPtr value;
  if (!Check(TokenType::kSemicolon)) {
    auto expr = ParseExpression();
    if (!expr) {
      return expr.takeError();
    }
    value = std::move(*expr);
  }

  if (auto err = Expect(TokenType::kSemicolon, "expected ';' after return value")) {
    return std::move(err);
  }

  return std::make_unique<ReturnStmt>(loc, std::move(value));
}

auto Parser::ParseThrowStmt() -> llvm::Expected<StmtPtr> {
  const Token &throw_tok = Advance();  // consume 'throw'
  SourceLocation loc = {throw_tok.line, throw_tok.column};

  auto expr = ParseExpression();
  if (!expr) {
    return expr.takeError();
  }

  if (auto err = Expect(TokenType::kSemicolon, "expected ';' after throw value")) {
    return std::move(err);
  }

  return std::make_unique<ThrowStmt>(loc, std::move(*expr));
}

auto Parser::ParseTryStmt() -> llvm::Expected<StmtPtr> {
  const Token &try_tok = Advance();  // consume 'try'
  SourceLocation loc = {try_tok.line, try_tok.column};

  auto body = ParseBlock();
  if (!body) {
    return body.takeError();
  }

  // At least one catch clause is required.
  if (!Check(TokenType::kCatch)) {
    return ErrorAt(Peek(), "expected 'catch' after try block");
  }

  llvm::SmallVector<CatchClause, 2> catches;
  while (Check(TokenType::kCatch)) {
    Advance();  // consume 'catch'

    if (auto err = Expect(TokenType::kIdentifier, "expected catch type")) {
      return std::move(err);
    }
    const Token &type_tok = Previous();

    // Optional binding: the second IDENTIFIER (the first is the type).
    llvm::StringRef binding;
    if (Check(TokenType::kIdentifier)) {
      binding = Advance().lexeme;
    }

    auto catch_body = ParseBlock();
    if (!catch_body) {
      return catch_body.takeError();
    }

    catches.push_back(CatchClause{type_tok.lexeme, binding, std::move(*catch_body)});
  }

  return std::make_unique<TryStmt>(loc, std::move(*body), std::move(catches));
}

auto Parser::ParseBreakStmt() -> llvm::Expected<StmtPtr> {
  const Token &break_tok = Advance();  // consume 'break'
  SourceLocation loc = {break_tok.line, break_tok.column};

  if (auto err = Expect(TokenType::kSemicolon, "expected ';' after 'break'")) {
    return std::move(err);
  }

  return std::make_unique<BreakStmt>(loc);
}

auto Parser::ParseExpressionStmt() -> llvm::Expected<StmtPtr> {
  auto expr = ParseExpression();
  if (!expr) {
    return expr.takeError();
  }
  SourceLocation loc = (*expr)->loc;
  if (auto err = Expect(TokenType::kSemicolon, "expected ';' after expression")) {
    return std::move(err);
  }
  return std::make_unique<ExpressionStmt>(loc, std::move(*expr));
}

// --- Expressions -------------------------------------------------------------

auto Parser::ParseExpression() -> llvm::Expected<ExprPtr> { return ParseAssignment(); }

auto Parser::ParseAssignment() -> llvm::Expected<ExprPtr> {
  auto expr = ParseLogicOr();
  if (!expr) {
    return expr.takeError();
  }

  if (Match(TokenType::kEqual)) {
    const Token &equals = Previous();
    auto value = ParseAssignment();  // right-associative
    if (!value) {
      return value.takeError();
    }

    ExprPtr &lhs = *expr;
    if (lhs->kind == ExprKind::kVar) {
      auto *var = llvm::cast<VarExpr>(lhs.get());
      SourceLocation loc = var->loc;
      llvm::StringRef name = var->name;
      return std::make_unique<AssignExpr>(loc, name, std::move(*value));
    }
    if (lhs->kind == ExprKind::kGet) {
      auto *get = llvm::cast<GetExpr>(lhs.get());
      SourceLocation loc = get->loc;
      llvm::StringRef name = get->name;
      ExprPtr object = std::move(get->object);
      return std::make_unique<SetExpr>(loc, std::move(object), name, std::move(*value));
    }
    if (lhs->kind == ExprKind::kSubscript) {
      auto *sub = llvm::cast<SubscriptExpr>(lhs.get());
      SourceLocation loc = sub->loc;
      ExprPtr object = std::move(sub->object);
      ExprPtr index = std::move(sub->index);
      return std::make_unique<SetIndexExpr>(
          loc, std::move(object), std::move(index), std::move(*value));
    }
    return ErrorAt(equals, "invalid assignment target");
  }

  return expr;
}

auto Parser::ParseLogicOr() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseLogicAnd();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kPipePipe)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseLogicAnd();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<LogicalExpr>(loc,
                                         std::move(left),
                                         LogicalOp::kOr,
                                         SourceLocation{op_tok.line, op_tok.column},
                                         std::move(*right));
  }
  return left;
}

auto Parser::ParseLogicAnd() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseBitwiseOr();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kAmpersandAmpersand)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseBitwiseOr();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<LogicalExpr>(loc,
                                         std::move(left),
                                         LogicalOp::kAnd,
                                         SourceLocation{op_tok.line, op_tok.column},
                                         std::move(*right));
  }
  return left;
}

auto Parser::ParseBitwiseOr() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseBitwiseXor();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kPipe)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseBitwiseXor();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<BinaryExpr>(loc,
                                        std::move(left),
                                        BinaryOp::kBitOr,
                                        SourceLocation{op_tok.line, op_tok.column},
                                        std::move(*right));
  }
  return left;
}

auto Parser::ParseBitwiseXor() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseBitwiseAnd();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kCaret)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseBitwiseAnd();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<BinaryExpr>(loc,
                                        std::move(left),
                                        BinaryOp::kBitXor,
                                        SourceLocation{op_tok.line, op_tok.column},
                                        std::move(*right));
  }
  return left;
}

auto Parser::ParseBitwiseAnd() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseEquality();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kAmpersand)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseEquality();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<BinaryExpr>(loc,
                                        std::move(left),
                                        BinaryOp::kBitAnd,
                                        SourceLocation{op_tok.line, op_tok.column},
                                        std::move(*right));
  }
  return left;
}

auto Parser::ParseEquality() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseComparison();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kEqualEqual) || Check(TokenType::kBangEqual)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseComparison();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<BinaryExpr>(loc,
                                        std::move(left),
                                        *BinaryOpFromToken(op_tok.type),
                                        SourceLocation{op_tok.line, op_tok.column},
                                        std::move(*right));
  }
  return left;
}

auto Parser::ParseComparison() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseShift();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kGreater) || Check(TokenType::kGreaterEqual) || Check(TokenType::kLess) ||
         Check(TokenType::kLessEqual)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseShift();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<BinaryExpr>(loc,
                                        std::move(left),
                                        *BinaryOpFromToken(op_tok.type),
                                        SourceLocation{op_tok.line, op_tok.column},
                                        std::move(*right));
  }
  return left;
}

auto Parser::ParseShift() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseTerm();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kLessLess) || Check(TokenType::kGreaterGreater)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseTerm();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<BinaryExpr>(loc,
                                        std::move(left),
                                        *BinaryOpFromToken(op_tok.type),
                                        SourceLocation{op_tok.line, op_tok.column},
                                        std::move(*right));
  }
  return left;
}

auto Parser::ParseTerm() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseFactor();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kMinus) || Check(TokenType::kPlus)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseFactor();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<BinaryExpr>(loc,
                                        std::move(left),
                                        *BinaryOpFromToken(op_tok.type),
                                        SourceLocation{op_tok.line, op_tok.column},
                                        std::move(*right));
  }
  return left;
}

auto Parser::ParseFactor() -> llvm::Expected<ExprPtr> {
  auto left_result = ParseUnary();
  if (!left_result) {
    return left_result.takeError();
  }
  ExprPtr left = std::move(*left_result);
  while (Check(TokenType::kSlash) || Check(TokenType::kStar)) {
    SourceLocation loc = left->loc;
    const Token &op_tok = Advance();
    auto right = ParseUnary();
    if (!right) {
      return right.takeError();
    }
    left = std::make_unique<BinaryExpr>(loc,
                                        std::move(left),
                                        *BinaryOpFromToken(op_tok.type),
                                        SourceLocation{op_tok.line, op_tok.column},
                                        std::move(*right));
  }
  return left;
}

auto Parser::ParseUnary() -> llvm::Expected<ExprPtr> {
  if (Check(TokenType::kBang) || Check(TokenType::kMinus) || Check(TokenType::kTilde)) {
    const Token &op_tok = Advance();
    SourceLocation loc = {op_tok.line, op_tok.column};
    auto operand = ParseUnary();  // recursive: --x, !!flag
    if (!operand) {
      return operand.takeError();
    }
    return std::make_unique<UnaryExpr>(
        loc, *UnaryOpFromToken(op_tok.type), loc, std::move(*operand));
  }
  return ParseCall();
}

auto Parser::ParseCall() -> llvm::Expected<ExprPtr> {
  auto expr_result = ParsePrimary();
  if (!expr_result) {
    return expr_result.takeError();
  }
  ExprPtr expr = std::move(*expr_result);
  for (;;) {
    if (Check(TokenType::kLeftParen)) {
      SourceLocation loc = expr->loc;
      auto call = FinishCall(loc, std::move(expr));
      if (!call) {
        return call.takeError();
      }
      expr = std::move(*call);
    } else if (Check(TokenType::kDot)) {
      Advance();  // consume '.'
      if (auto err = Expect(TokenType::kIdentifier, "expected property name after '.'")) {
        return std::move(err);
      }
      const Token &name = Previous();
      SourceLocation loc = expr->loc;
      expr = std::make_unique<GetExpr>(loc, std::move(expr), name.lexeme);
    } else if (Check(TokenType::kLeftBracket)) {
      Advance();  // consume '['
      auto index = ParseExpression();
      if (!index) {
        return index.takeError();
      }
      if (auto err = Expect(TokenType::kRightBracket, "expected ']' after subscript index")) {
        return std::move(err);
      }
      SourceLocation loc = expr->loc;
      expr = std::make_unique<SubscriptExpr>(loc, std::move(expr), std::move(*index));
    } else {
      break;
    }
  }
  return expr;
}

auto Parser::FinishCall(SourceLocation loc, ExprPtr callee) -> llvm::Expected<ExprPtr> {
  Advance();  // consume '('
  llvm::SmallVector<ExprPtr, 4> arguments;
  if (!Check(TokenType::kRightParen)) {
    do {
      auto arg = ParseExpression();
      if (!arg) {
        return arg.takeError();
      }
      arguments.push_back(std::move(*arg));
    } while (Match(TokenType::kComma));
    // A trailing comma (e.g. f(1, 2,)) is rejected naturally: Match consumes
    // the comma, then ParseExpression fails on ')'.
  }
  if (auto err = Expect(TokenType::kRightParen, "expected ')' after arguments")) {
    return std::move(err);
  }
  return std::make_unique<CallExpr>(loc, std::move(callee), std::move(arguments));
}

auto Parser::ParsePrimary() -> llvm::Expected<ExprPtr> {
  const Token &tok = Peek();
  SourceLocation loc = {tok.line, tok.column};

  switch (tok.type) {
    case TokenType::kTrue:
      Advance();
      return std::make_unique<LiteralExpr>(loc, LiteralKind::kTrue);
    case TokenType::kFalse:
      Advance();
      return std::make_unique<LiteralExpr>(loc, LiteralKind::kFalse);
    case TokenType::kNull:
      Advance();
      return std::make_unique<LiteralExpr>(loc, LiteralKind::kNull);
    case TokenType::kThis:
      Advance();
      return std::make_unique<ThisExpr>(loc);
    case TokenType::kInteger: {
      Advance();
      return std::make_unique<LiteralExpr>(
          loc, LiteralKind::kInteger, std::get<std::int64_t>(tok.value));
    }
    case TokenType::kFloat: {
      Advance();
      return std::make_unique<LiteralExpr>(loc, LiteralKind::kFloat, std::get<double>(tok.value));
    }
    case TokenType::kString:
      Advance();
      return std::make_unique<LiteralExpr>(loc, LiteralKind::kString, tok.lexeme);
    case TokenType::kIdentifier:
      Advance();
      return std::make_unique<VarExpr>(loc, tok.lexeme);
    case TokenType::kLeftParen: {
      Advance();  // consume '('
      auto inner = ParseExpression();
      if (!inner) {
        return inner.takeError();
      }
      if (auto err = Expect(TokenType::kRightParen, "expected ')' after expression")) {
        return std::move(err);
      }
      return std::move(*inner);
    }
    case TokenType::kSuper: {
      Advance();  // consume 'super'
      if (auto err = Expect(TokenType::kDot, "expected '.' after 'super'")) {
        return std::move(err);
      }
      if (auto err = Expect(TokenType::kIdentifier, "expected method name after 'super.'")) {
        return std::move(err);
      }
      const Token &method = Previous();
      return std::make_unique<SuperExpr>(loc, method.lexeme);
    }
    case TokenType::kLeftBracket:
      return ParseListLiteral();
    default:
      return ErrorAt(tok, "expected expression");
  }
}

auto Parser::ParseListLiteral() -> llvm::Expected<ExprPtr> {
  const Token &bracket = Advance();  // consume '['
  SourceLocation loc = {bracket.line, bracket.column};

  llvm::SmallVector<ExprPtr, 4> elements;
  ExprPtr size;

  if (!Check(TokenType::kRightBracket) && !Check(TokenType::kSemicolon)) {
    // Parse comma-separated elements with optional trailing comma.
    for (;;) {
      auto elem = ParseExpression();
      if (!elem) {
        return elem.takeError();
      }
      elements.push_back(std::move(*elem));
      if (!Match(TokenType::kComma)) {
        break;
      }
      // Trailing comma: the next token is ']' or ';' (size clause).
      if (Check(TokenType::kRightBracket) || Check(TokenType::kSemicolon)) {
        break;
      }
    }
  }

  if (Match(TokenType::kSemicolon)) {
    auto size_expr = ParseExpression();
    if (!size_expr) {
      return size_expr.takeError();
    }
    size = std::move(*size_expr);
  }

  if (auto err = Expect(TokenType::kRightBracket, "expected ']' after list literal")) {
    return std::move(err);
  }

  return std::make_unique<ListExpr>(loc, std::move(elements), std::move(size));
}

// --- Helpers -----------------------------------------------------------------

auto Parser::ParseParameters() -> llvm::Expected<llvm::SmallVector<llvm::StringRef, 4>> {
  llvm::SmallVector<llvm::StringRef, 4> params;
  if (!Check(TokenType::kRightParen)) {
    do {
      if (auto err = Expect(TokenType::kIdentifier, "expected parameter name")) {
        return std::move(err);
      }
      params.push_back(Previous().lexeme);
    } while (Match(TokenType::kComma));
    // Trailing comma is rejected: Match consumes it, then Expect fails on ')'.
  }
  if (auto err = Expect(TokenType::kRightParen, "expected ')' after parameters")) {
    return std::move(err);
  }
  return params;
}

}  // namespace lox
