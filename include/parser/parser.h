#ifndef LOX_PARSER_PARSER_H_
#define LOX_PARSER_PARSER_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "parser/ast.h"
#include "parser/error.h"
#include "scanner/token.h"

namespace lox {

// Recursive-descent parser: turns a clean Token stream (terminated by kEof)
// into an AST. Mirrors the Scanner's contract: takes the source view (for
// diagnostic source-line extraction) and the token vector; returns the program
// or a lox::parser::SyntaxError accumulating every syntax problem found.
//
// source and the token lexemes must outlive this parser and every node it
// produces (every StringRef in the AST is a zero-copy view). filename is used
// only for error messages.
class Parser {
 public:
  Parser(llvm::StringRef source, llvm::SmallVector<Token, 0> tokens, llvm::StringRef filename = {});

  // Parses the whole token stream and returns the top-level statement list,
  // or a lox::parser::SyntaxError if any syntax errors were found. The parser
  // resynchronizes after each error and continues, so the error may carry
  // multiple diagnostics.
  [[nodiscard]] auto Parse() -> llvm::Expected<llvm::SmallVector<StmtPtr, 0>>;

 private:
  // --- Token cursor ---
  const Token &Peek(std::size_t ahead = 0) const;
  const Token &Previous() const;
  bool IsAtEnd() const;
  const Token &Advance();
  bool Check(TokenType type) const;
  bool Match(TokenType type);
  // If the current token matches, advance and return success; otherwise record
  // an error at the current token and return ParseAbort. The consumed token is
  // accessible via Previous().
  llvm::Error Expect(TokenType type, llvm::StringRef msg);

  // --- Error handling ---
  // Records a diagnostic in errors_ and returns ParseAbort for propagation.
  llvm::Error ErrorAt(const Token &tok, llvm::StringRef msg);
  // Discards tokens until a statement boundary (;, }, or a declaration
  // keyword) so parsing can resume after an error.
  void Synchronize();
  // The 1-indexed source line, for error display. A copy (self-contained).
  std::string CurrentSourceLine(std::uint32_t line) const;

  // --- Declarations ---
  auto ParseDeclaration() -> llvm::Expected<StmtPtr>;
  auto ParseClassDecl() -> llvm::Expected<StmtPtr>;
  // Shared by fun declarations (after consuming 'fun') and class methods.
  // kind is "function" or "method" for error messages.
  auto ParseFunction(llvm::StringRef kind) -> llvm::Expected<std::unique_ptr<FunDeclStmt>>;
  auto ParseVarDecl() -> llvm::Expected<StmtPtr>;
  auto ParseConstDecl() -> llvm::Expected<StmtPtr>;
  auto ParseImportDecl() -> llvm::Expected<StmtPtr>;

  // --- Statements ---
  auto ParseStatement() -> llvm::Expected<StmtPtr>;
  // Always produces a BlockStmt; callers needing StmtPtr convert explicitly.
  auto ParseBlock() -> llvm::Expected<std::unique_ptr<BlockStmt>>;
  auto ParseIfStmt() -> llvm::Expected<StmtPtr>;
  auto ParseWhileStmt() -> llvm::Expected<StmtPtr>;
  auto ParseForStmt() -> llvm::Expected<StmtPtr>;
  auto ParseReturnStmt() -> llvm::Expected<StmtPtr>;
  auto ParseThrowStmt() -> llvm::Expected<StmtPtr>;
  auto ParseTryStmt() -> llvm::Expected<StmtPtr>;
  auto ParseBreakStmt() -> llvm::Expected<StmtPtr>;
  auto ParseExpressionStmt() -> llvm::Expected<StmtPtr>;

  // --- Expressions (one function per grammar precedence layer) ---
  auto ParseExpression() -> llvm::Expected<ExprPtr>;
  auto ParseAssignment() -> llvm::Expected<ExprPtr>;
  auto ParseLogicOr() -> llvm::Expected<ExprPtr>;
  auto ParseLogicAnd() -> llvm::Expected<ExprPtr>;
  auto ParseBitwiseOr() -> llvm::Expected<ExprPtr>;
  auto ParseBitwiseXor() -> llvm::Expected<ExprPtr>;
  auto ParseBitwiseAnd() -> llvm::Expected<ExprPtr>;
  auto ParseEquality() -> llvm::Expected<ExprPtr>;
  auto ParseComparison() -> llvm::Expected<ExprPtr>;
  auto ParseShift() -> llvm::Expected<ExprPtr>;
  auto ParseTerm() -> llvm::Expected<ExprPtr>;
  auto ParseFactor() -> llvm::Expected<ExprPtr>;
  auto ParseUnary() -> llvm::Expected<ExprPtr>;
  auto ParseCall() -> llvm::Expected<ExprPtr>;
  // '(' already consumed by the caller; loc is the callee's leading position.
  auto FinishCall(SourceLocation loc, ExprPtr callee) -> llvm::Expected<ExprPtr>;
  auto ParsePrimary() -> llvm::Expected<ExprPtr>;
  auto ParseListLiteral() -> llvm::Expected<ExprPtr>;

  // --- Helpers ---
  auto ParseParameters() -> llvm::Expected<llvm::SmallVector<llvm::StringRef, 4>>;

  // --- State ---
  llvm::StringRef source_;
  std::string filename_;
  llvm::SmallVector<Token, 0> tokens_;
  std::size_t current_ = 0;
  std::vector<parser::SyntaxError::Entry> errors_;
};

}  // namespace lox

#endif  // LOX_PARSER_PARSER_H_
