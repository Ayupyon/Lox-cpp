#ifndef LOX_PARSER_AST_H_
#define LOX_PARSER_AST_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <variant>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace lox {

// 1-indexed source position of an AST node's leading token, mirroring the
// line/column pair carried by scanner::Token. Used for diagnostics and for
// the Dump location prefix.
struct SourceLocation {
  std::uint32_t line{};
  std::uint32_t column{};
};

class Expr;
class Stmt;

// Owning handles: every AST owns its children through std::unique_ptr, so the
// whole tree frees itself on destruction. Consumers (the bytecode compiler,
// Dump) traverse via const Expr*/const Stmt* and never touch ownership.
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// Payload of a LiteralExpr. std::monostate MUST stay the first alternative so
// that default construction yields "no value". kTrue/kFalse/kNull carry no
// payload; kInteger/kFloat the decoded numbers (copied from the token);
// kString the raw lexeme of the string literal (quotes and escapes included,
// decoding is deferred to the compiler).
using LiteralValue = std::variant<std::monostate, std::int64_t, double, llvm::StringRef>;

// Closed-kind discriminators of the two node hierarchies. Dispatch (Dump now,
// the bytecode compiler later) switches on kind and downcasts with
// llvm::isa/cast/dyn_cast via each subclass's classof.
enum class ExprKind : std::uint8_t {
  kLiteral,
  kVar,
  kThis,
  kSuper,
  kUnary,
  kBinary,
  kLogical,
  kCall,
  kGet,
  kSubscript,
  kList,
  kAssign,
  kSet,
  kSetIndex,
};

enum class StmtKind : std::uint8_t {
  kBlock,
  kVarDecl,
  kConstDecl,
  kFunDecl,
  kClass,
  kImport,
  kExpression,
  kIf,
  kWhile,
  kFor,
  kReturn,
  kThrow,
  kTry,
  kBreak,
};

// Semantic operator kinds. The parser maps TokenType glyphs to these once at
// node construction; the scanner's glyph naming stays scanner-internal.
// Switches over these enums have no default so -Wswitch stays armed.
enum class BinaryOp : std::uint8_t {
  kAdd,
  kSubtract,
  kMultiply,
  kDivide,
  kShiftLeft,
  kShiftRight,
  kLess,
  kLessEqual,
  kGreater,
  kGreaterEqual,
  kEqual,
  kNotEqual,
  kBitAnd,
  kBitOr,
  kBitXor,
};

// Short-circuiting operators, kept out of BinaryOp because they compile to
// jump sequences instead of a single arithmetic instruction.
enum class LogicalOp : std::uint8_t { kAnd, kOr };

enum class UnaryOp : std::uint8_t { kNot, kNegate, kBitNot };

// Sub-kind of a LiteralExpr; semantic categories rather than token glyphs.
enum class LiteralKind : std::uint8_t { kTrue, kFalse, kNull, kInteger, kFloat, kString };

// Base of every expression node. Nodes are plain data: public members, no
// behavior beyond Dump. The virtual destructor exists only so that
// std::unique_ptr<Expr> can delete concrete nodes polymorphically; all
// dispatch is done on kind, never through virtual calls. loc is the position
// of the node's leading token; every StringRef member is a zero-copy view
// into the source buffer, so an AST must not outlive its source.
class Expr {
 public:
  ExprKind kind;
  SourceLocation loc;

  virtual ~Expr() = default;

  // Prints the subtree rooted at this node: one node per line, two-space
  // indentation per depth, "<line>:<col>" prefix, "role:" tags on child lines
  // whose position among siblings is not self-evident.
  void Dump(llvm::raw_ostream &os) const;

 protected:
  Expr(ExprKind kind, SourceLocation loc) : kind(kind), loc(loc) {}
};

// Literal: true/false/null/integer/float/string. The kind discriminates the
// payload interpretation (see LiteralValue).
class LiteralExpr final : public Expr {
 public:
  LiteralKind literal_kind;
  LiteralValue value;

  LiteralExpr(SourceLocation loc, LiteralKind literal_kind, LiteralValue value = {})
      : Expr(ExprKind::kLiteral, loc), literal_kind(literal_kind), value(value) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kLiteral; }
};

// Variable reference.
class VarExpr final : public Expr {
 public:
  llvm::StringRef name;

  VarExpr(SourceLocation loc, llvm::StringRef name) : Expr(ExprKind::kVar, loc), name(name) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kVar; }
};

// "this".
class ThisExpr final : public Expr {
 public:
  explicit ThisExpr(SourceLocation loc) : Expr(ExprKind::kThis, loc) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kThis; }
};

// "super.<method>": the object is implicit (the superclass chain of the
// current instance), only the method name is carried.
class SuperExpr final : public Expr {
 public:
  llvm::StringRef method;

  SuperExpr(SourceLocation loc, llvm::StringRef method)
      : Expr(ExprKind::kSuper, loc), method(method) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kSuper; }
};

// Prefix "!" / "-" / "~". op_loc points at the operator token so that runtime
// errors (e.g. bit-not on a non-integer) report the operator's line.
class UnaryExpr final : public Expr {
 public:
  UnaryOp op;
  SourceLocation op_loc;
  ExprPtr operand;

  UnaryExpr(SourceLocation loc, UnaryOp op, SourceLocation op_loc, ExprPtr operand)
      : Expr(ExprKind::kUnary, loc), op(op), op_loc(op_loc), operand(std::move(operand)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kUnary; }
};

// Arithmetic, bitwise, comparison and shift operators; the single BinaryOp
// kind spans every precedence layer (layering is a parse-time concern).
// op_loc points at the operator token for precise runtime error lines.
class BinaryExpr final : public Expr {
 public:
  ExprPtr lhs;
  BinaryOp op;
  SourceLocation op_loc;
  ExprPtr rhs;

  BinaryExpr(SourceLocation loc, ExprPtr lhs, BinaryOp op, SourceLocation op_loc, ExprPtr rhs)
      : Expr(ExprKind::kBinary, loc),
        lhs(std::move(lhs)),
        op(op),
        op_loc(op_loc),
        rhs(std::move(rhs)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kBinary; }
};

// Short-circuiting "&&" / "||".
class LogicalExpr final : public Expr {
 public:
  ExprPtr lhs;
  LogicalOp op;
  SourceLocation op_loc;
  ExprPtr rhs;

  LogicalExpr(SourceLocation loc, ExprPtr lhs, LogicalOp op, SourceLocation op_loc, ExprPtr rhs)
      : Expr(ExprKind::kLogical, loc),
        lhs(std::move(lhs)),
        op(op),
        op_loc(op_loc),
        rhs(std::move(rhs)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kLogical; }
};

// Function/constructor call.
class CallExpr final : public Expr {
 public:
  ExprPtr callee;
  llvm::SmallVector<ExprPtr, 4> arguments;

  CallExpr(SourceLocation loc, ExprPtr callee, llvm::SmallVector<ExprPtr, 4> arguments)
      : Expr(ExprKind::kCall, loc), callee(std::move(callee)), arguments(std::move(arguments)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kCall; }
};

// Property access "object.name".
class GetExpr final : public Expr {
 public:
  ExprPtr object;
  llvm::StringRef name;

  GetExpr(SourceLocation loc, ExprPtr object, llvm::StringRef name)
      : Expr(ExprKind::kGet, loc), object(std::move(object)), name(name) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kGet; }
};

// Subscript access "object[index]".
class SubscriptExpr final : public Expr {
 public:
  ExprPtr object;
  ExprPtr index;

  SubscriptExpr(SourceLocation loc, ExprPtr object, ExprPtr index)
      : Expr(ExprKind::kSubscript, loc), object(std::move(object)), index(std::move(index)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kSubscript; }
};

// List literal "[e1, e2; N]" / "[; N]" / "[]". size is null when there is no
// "; N" clause.
class ListExpr final : public Expr {
 public:
  llvm::SmallVector<ExprPtr, 4> elements;
  ExprPtr size;

  ListExpr(SourceLocation loc, llvm::SmallVector<ExprPtr, 4> elements, ExprPtr size)
      : Expr(ExprKind::kList, loc), elements(std::move(elements)), size(std::move(size)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kList; }
};

// Simple assignment "x = v"; the left-hand side is a plain name (the parser
// rejects other shapes at parse time).
class AssignExpr final : public Expr {
 public:
  llvm::StringRef name;
  ExprPtr value;

  AssignExpr(SourceLocation loc, llvm::StringRef name, ExprPtr value)
      : Expr(ExprKind::kAssign, loc), name(name), value(std::move(value)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kAssign; }
};

// Property assignment "object.name = v".
class SetExpr final : public Expr {
 public:
  ExprPtr object;
  llvm::StringRef name;
  ExprPtr value;

  SetExpr(SourceLocation loc, ExprPtr object, llvm::StringRef name, ExprPtr value)
      : Expr(ExprKind::kSet, loc), object(std::move(object)), name(name), value(std::move(value)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kSet; }
};

// Subscript assignment "object[index] = v".
class SetIndexExpr final : public Expr {
 public:
  ExprPtr object;
  ExprPtr index;
  ExprPtr value;

  SetIndexExpr(SourceLocation loc, ExprPtr object, ExprPtr index, ExprPtr value)
      : Expr(ExprKind::kSetIndex, loc),
        object(std::move(object)),
        index(std::move(index)),
        value(std::move(value)) {}

  static bool classof(const Expr *e) { return e->kind == ExprKind::kSetIndex; }
};

// Base of every statement node; same design as Expr.
class Stmt {
 public:
  StmtKind kind;
  SourceLocation loc;

  virtual ~Stmt() = default;

  void Dump(llvm::raw_ostream &os) const;

 protected:
  Stmt(StmtKind kind, SourceLocation loc) : kind(kind), loc(loc) {}
};

// "{ ... }": creates a scope; also the body type of every control-flow
// statement (the grammar requires block bodies).
class BlockStmt final : public Stmt {
 public:
  llvm::SmallVector<StmtPtr, 4> statements;

  explicit BlockStmt(SourceLocation loc, llvm::SmallVector<StmtPtr, 4> statements = {})
      : Stmt(StmtKind::kBlock, loc), statements(std::move(statements)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kBlock; }
};

// "let name = initializer;". The grammar requires an initializer.
class VarDeclStmt final : public Stmt {
 public:
  llvm::StringRef name;
  ExprPtr initializer;

  VarDeclStmt(SourceLocation loc, llvm::StringRef name, ExprPtr initializer)
      : Stmt(StmtKind::kVarDecl, loc), name(name), initializer(std::move(initializer)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kVarDecl; }
};

// "const name = initializer;". Reassignment to a const binding is a
// compile-time error detected by the compiler.
class ConstDeclStmt final : public Stmt {
 public:
  llvm::StringRef name;
  ExprPtr initializer;

  ConstDeclStmt(SourceLocation loc, llvm::StringRef name, ExprPtr initializer)
      : Stmt(StmtKind::kConstDecl, loc), name(name), initializer(std::move(initializer)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kConstDecl; }
};

// Function declaration; also the node type of class methods (same shape:
// name, parameters, block body).
class FunDeclStmt final : public Stmt {
 public:
  llvm::StringRef name;
  llvm::SmallVector<llvm::StringRef, 4> params;
  std::unique_ptr<BlockStmt> body;

  FunDeclStmt(SourceLocation loc,
              llvm::StringRef name,
              llvm::SmallVector<llvm::StringRef, 4> params,
              std::unique_ptr<BlockStmt> body)
      : Stmt(StmtKind::kFunDecl, loc),
        name(name),
        params(std::move(params)),
        body(std::move(body)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kFunDecl; }
};

// Class declaration; single inheritance via "extends IDENTIFIER".
class ClassStmt final : public Stmt {
 public:
  llvm::StringRef name;
  llvm::StringRef superclass;  // empty when there is no extends clause
  llvm::SmallVector<std::unique_ptr<FunDeclStmt>, 4> methods;

  ClassStmt(SourceLocation loc,
            llvm::StringRef name,
            llvm::StringRef superclass,
            llvm::SmallVector<std::unique_ptr<FunDeclStmt>, 4> methods)
      : Stmt(StmtKind::kClass, loc),
        name(name),
        superclass(superclass),
        methods(std::move(methods)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kClass; }
};

// "import \"path\" as alias;". path is the raw string lexeme (quotes and
// escapes included, decoding and module resolution are compiler concerns);
// alias is empty when there is no "as" clause.
class ImportStmt final : public Stmt {
 public:
  llvm::StringRef path;
  llvm::StringRef alias;  // empty when there is no "as" clause

  ImportStmt(SourceLocation loc, llvm::StringRef path, llvm::StringRef alias = {})
      : Stmt(StmtKind::kImport, loc), path(path), alias(alias) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kImport; }
};

// Expression statement: evaluate for side effects, discard the value.
class ExpressionStmt final : public Stmt {
 public:
  ExprPtr expression;

  explicit ExpressionStmt(SourceLocation loc, ExprPtr expression)
      : Stmt(StmtKind::kExpression, loc), expression(std::move(expression)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kExpression; }
};

// "if (condition) { then } else { else }". Both branches are blocks per the
// grammar; else_branch is null without an else clause.
class IfStmt final : public Stmt {
 public:
  ExprPtr condition;
  std::unique_ptr<BlockStmt> then_branch;
  std::unique_ptr<BlockStmt> else_branch;  // null when there is no else clause

  IfStmt(SourceLocation loc,
         ExprPtr condition,
         std::unique_ptr<BlockStmt> then_branch,
         std::unique_ptr<BlockStmt> else_branch)
      : Stmt(StmtKind::kIf, loc),
        condition(std::move(condition)),
        then_branch(std::move(then_branch)),
        else_branch(std::move(else_branch)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kIf; }
};

class WhileStmt final : public Stmt {
 public:
  ExprPtr condition;
  std::unique_ptr<BlockStmt> body;

  WhileStmt(SourceLocation loc, ExprPtr condition, std::unique_ptr<BlockStmt> body)
      : Stmt(StmtKind::kWhile, loc), condition(std::move(condition)), body(std::move(body)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kWhile; }
};

// "for (initializer; condition; increment) { body }". initializer is a
// VarDeclStmt, an ExpressionStmt, or null; condition and increment are null
// when omitted; all are optional.
class ForStmt final : public Stmt {
 public:
  StmtPtr initializer;  // VarDeclStmt, ExpressionStmt, or null
  ExprPtr condition;    // null when omitted
  ExprPtr increment;    // null when omitted
  std::unique_ptr<BlockStmt> body;

  ForStmt(SourceLocation loc,
          StmtPtr initializer,
          ExprPtr condition,
          ExprPtr increment,
          std::unique_ptr<BlockStmt> body)
      : Stmt(StmtKind::kFor, loc),
        initializer(std::move(initializer)),
        condition(std::move(condition)),
        increment(std::move(increment)),
        body(std::move(body)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kFor; }
};

// "return value;" or bare "return;" (value null, returns null at runtime).
class ReturnStmt final : public Stmt {
 public:
  ExprPtr value;  // null for bare "return;"

  explicit ReturnStmt(SourceLocation loc, ExprPtr value = {})
      : Stmt(StmtKind::kReturn, loc), value(std::move(value)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kReturn; }
};

// "throw value;".
class ThrowStmt final : public Stmt {
 public:
  ExprPtr value;

  explicit ThrowStmt(SourceLocation loc, ExprPtr value)
      : Stmt(StmtKind::kThrow, loc), value(std::move(value)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kThrow; }
};

// One "catch <Type> <binding>? { ... }" clause of a TryStmt. Not itself a
// statement: type is matched against the thrown value's runtime type (first
// match wins); binding receives the thrown value and is empty when no binding
// is declared.
struct CatchClause {
  llvm::StringRef type;
  llvm::StringRef binding;  // empty when no binding is declared
  std::unique_ptr<BlockStmt> body;
};

// "try { body } catch ...". The body is a block; catches are tried in order.
class TryStmt final : public Stmt {
 public:
  std::unique_ptr<BlockStmt> body;
  llvm::SmallVector<CatchClause, 2> catches;

  TryStmt(SourceLocation loc,
          std::unique_ptr<BlockStmt> body,
          llvm::SmallVector<CatchClause, 2> catches)
      : Stmt(StmtKind::kTry, loc), body(std::move(body)), catches(std::move(catches)) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kTry; }
};

// "break;": exits the innermost enclosing loop; using it outside a loop is a
// compile-time error detected by the compiler.
class BreakStmt final : public Stmt {
 public:
  explicit BreakStmt(SourceLocation loc) : Stmt(StmtKind::kBreak, loc) {}

  static bool classof(const Stmt *s) { return s->kind == StmtKind::kBreak; }
};

// Dumps a whole program (the top-level statement list) in order.
void Dump(const llvm::SmallVectorImpl<StmtPtr> &program, llvm::raw_ostream &os);

}  // namespace lox

#endif  // LOX_PARSER_AST_H_
