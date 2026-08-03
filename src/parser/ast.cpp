#include "parser/ast.h"

#include <charconv>

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

namespace lox {

namespace {

// Semantic display names of the operator and literal-kind enums, used by the
// Dump switch. Each switch has no default so -Wswitch stays armed: adding an
// enumerator without a case becomes a compile warning.
llvm::StringRef OperatorName(BinaryOp op) {
  switch (op) {
    case BinaryOp::kAdd:
      return "Add";
    case BinaryOp::kSubtract:
      return "Subtract";
    case BinaryOp::kMultiply:
      return "Multiply";
    case BinaryOp::kDivide:
      return "Divide";
    case BinaryOp::kShiftLeft:
      return "ShiftLeft";
    case BinaryOp::kShiftRight:
      return "ShiftRight";
    case BinaryOp::kLess:
      return "Less";
    case BinaryOp::kLessEqual:
      return "LessEqual";
    case BinaryOp::kGreater:
      return "Greater";
    case BinaryOp::kGreaterEqual:
      return "GreaterEqual";
    case BinaryOp::kEqual:
      return "Equal";
    case BinaryOp::kNotEqual:
      return "NotEqual";
    case BinaryOp::kBitAnd:
      return "BitAnd";
    case BinaryOp::kBitOr:
      return "BitOr";
    case BinaryOp::kBitXor:
      return "BitXor";
  }
  llvm_unreachable("unknown BinaryOp");
}

llvm::StringRef OperatorName(LogicalOp op) {
  switch (op) {
    case LogicalOp::kAnd:
      return "And";
    case LogicalOp::kOr:
      return "Or";
  }
  llvm_unreachable("unknown LogicalOp");
}

llvm::StringRef OperatorName(UnaryOp op) {
  switch (op) {
    case UnaryOp::kNot:
      return "Not";
    case UnaryOp::kNegate:
      return "Negate";
    case UnaryOp::kBitNot:
      return "BitNot";
  }
  llvm_unreachable("unknown UnaryOp");
}

llvm::StringRef LiteralKindName(LiteralKind kind) {
  switch (kind) {
    case LiteralKind::kTrue:
      return "True";
    case LiteralKind::kFalse:
      return "False";
    case LiteralKind::kNull:
      return "Null";
    case LiteralKind::kInteger:
      return "Integer";
    case LiteralKind::kFloat:
      return "Float";
    case LiteralKind::kString:
      return "String";
  }
  llvm_unreachable("unknown LiteralKind");
}

// Dump contract: a node prints its header line at the current output position
// (the caller is responsible for the indentation), then each child on its own
// line at depth + 1.

void PrintLocation(llvm::raw_ostream &os, SourceLocation loc) {
  os << loc.line << ':' << loc.column << ' ';
}

void Indent(llvm::raw_ostream &os, unsigned depth) {
  for (unsigned i = 0; i < depth; ++i)
    os << "  ";
}

void DumpExpr(const Expr &e, llvm::raw_ostream &os, unsigned depth);
void DumpStmt(const Stmt &s, llvm::raw_ostream &os, unsigned depth);

// Prints a child node on its own line at `depth`. A non-empty role prefixes
// the line ("callee: ...") when the child's position among its siblings is
// not self-evident from the node kinds alone.
void DumpChildExpr(llvm::raw_ostream &os, unsigned depth, llvm::StringRef role, const Expr &e) {
  Indent(os, depth);
  if (!role.empty())
    os << role << ": ";
  DumpExpr(e, os, depth);
}

void DumpChildStmt(llvm::raw_ostream &os, unsigned depth, llvm::StringRef role, const Stmt &s) {
  Indent(os, depth);
  if (!role.empty())
    os << role << ": ";
  DumpStmt(s, os, depth);
}

void DumpExpr(const Expr &e, llvm::raw_ostream &os, unsigned depth) {
  switch (e.kind) {
    case ExprKind::kLiteral: {
      const auto *literal = llvm::cast<LiteralExpr>(&e);
      PrintLocation(os, literal->loc);
      os << "LiteralExpr " << LiteralKindName(literal->literal_kind);
      if (const auto *integer = std::get_if<std::int64_t>(&literal->value)) {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *integer);
        os << " [" << llvm::StringRef(buffer, result.ptr - buffer) << ']';
      } else if (const auto *floating = std::get_if<double>(&literal->value)) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *floating);
        os << " [" << llvm::StringRef(buffer, result.ptr - buffer) << ']';
      } else if (const auto *text = std::get_if<llvm::StringRef>(&literal->value)) {
        os << ' ';
        llvm::printEscapedString(*text, os);
      }
      os << '\n';
      break;
    }
    case ExprKind::kVar: {
      const auto *var = llvm::cast<VarExpr>(&e);
      PrintLocation(os, var->loc);
      os << "VarExpr name=" << var->name << '\n';
      break;
    }
    case ExprKind::kThis: {
      const auto *this_expr = llvm::cast<ThisExpr>(&e);
      PrintLocation(os, this_expr->loc);
      os << "ThisExpr\n";
      break;
    }
    case ExprKind::kSuper: {
      const auto *super = llvm::cast<SuperExpr>(&e);
      PrintLocation(os, super->loc);
      os << "SuperExpr method=" << super->method << '\n';
      break;
    }
    case ExprKind::kUnary: {
      const auto *unary = llvm::cast<UnaryExpr>(&e);
      PrintLocation(os, unary->loc);
      os << "UnaryExpr " << OperatorName(unary->op) << '\n';
      DumpChildExpr(os, depth + 1, {}, *unary->operand);
      break;
    }
    case ExprKind::kBinary: {
      const auto *binary = llvm::cast<BinaryExpr>(&e);
      PrintLocation(os, binary->loc);
      os << "BinaryExpr " << OperatorName(binary->op) << '\n';
      DumpChildExpr(os, depth + 1, {}, *binary->lhs);
      DumpChildExpr(os, depth + 1, {}, *binary->rhs);
      break;
    }
    case ExprKind::kLogical: {
      const auto *logical = llvm::cast<LogicalExpr>(&e);
      PrintLocation(os, logical->loc);
      os << "LogicalExpr " << OperatorName(logical->op) << '\n';
      DumpChildExpr(os, depth + 1, {}, *logical->lhs);
      DumpChildExpr(os, depth + 1, {}, *logical->rhs);
      break;
    }
    case ExprKind::kCall: {
      const auto *call = llvm::cast<CallExpr>(&e);
      PrintLocation(os, call->loc);
      os << "CallExpr\n";
      DumpChildExpr(os, depth + 1, "callee", *call->callee);
      for (const auto &argument : call->arguments)
        DumpChildExpr(os, depth + 1, "argument", *argument);
      break;
    }
    case ExprKind::kGet: {
      const auto *get = llvm::cast<GetExpr>(&e);
      PrintLocation(os, get->loc);
      os << "GetExpr name=" << get->name << '\n';
      DumpChildExpr(os, depth + 1, {}, *get->object);
      break;
    }
    case ExprKind::kSubscript: {
      const auto *subscript = llvm::cast<SubscriptExpr>(&e);
      PrintLocation(os, subscript->loc);
      os << "SubscriptExpr\n";
      DumpChildExpr(os, depth + 1, "object", *subscript->object);
      DumpChildExpr(os, depth + 1, "index", *subscript->index);
      break;
    }
    case ExprKind::kList: {
      const auto *list = llvm::cast<ListExpr>(&e);
      PrintLocation(os, list->loc);
      os << "ListExpr\n";
      for (const auto &element : list->elements)
        DumpChildExpr(os, depth + 1, "element", *element);
      if (list->size)
        DumpChildExpr(os, depth + 1, "size", *list->size);
      break;
    }
    case ExprKind::kAssign: {
      const auto *assign = llvm::cast<AssignExpr>(&e);
      PrintLocation(os, assign->loc);
      os << "AssignExpr name=" << assign->name << '\n';
      DumpChildExpr(os, depth + 1, {}, *assign->value);
      break;
    }
    case ExprKind::kSet: {
      const auto *set = llvm::cast<SetExpr>(&e);
      PrintLocation(os, set->loc);
      os << "SetExpr name=" << set->name << '\n';
      DumpChildExpr(os, depth + 1, "object", *set->object);
      DumpChildExpr(os, depth + 1, "value", *set->value);
      break;
    }
    case ExprKind::kSetIndex: {
      const auto *set_index = llvm::cast<SetIndexExpr>(&e);
      PrintLocation(os, set_index->loc);
      os << "SetIndexExpr\n";
      DumpChildExpr(os, depth + 1, "object", *set_index->object);
      DumpChildExpr(os, depth + 1, "index", *set_index->index);
      DumpChildExpr(os, depth + 1, "value", *set_index->value);
      break;
    }
  }
}

void DumpCatchClause(llvm::raw_ostream &os, unsigned depth, const CatchClause &clause) {
  Indent(os, depth);
  os << "catch: CatchClause type=" << clause.type;
  if (!clause.binding.empty())
    os << " binding=" << clause.binding;
  os << '\n';
  DumpChildStmt(os, depth + 1, {}, *clause.body);
}

void DumpStmt(const Stmt &s, llvm::raw_ostream &os, unsigned depth) {
  switch (s.kind) {
    case StmtKind::kBlock: {
      const auto *block = llvm::cast<BlockStmt>(&s);
      PrintLocation(os, block->loc);
      os << "BlockStmt\n";
      for (const auto &statement : block->statements)
        DumpChildStmt(os, depth + 1, {}, *statement);
      break;
    }
    case StmtKind::kVarDecl: {
      const auto *var_decl = llvm::cast<VarDeclStmt>(&s);
      PrintLocation(os, var_decl->loc);
      os << "VarDeclStmt name=" << var_decl->name << '\n';
      DumpChildExpr(os, depth + 1, {}, *var_decl->initializer);
      break;
    }
    case StmtKind::kConstDecl: {
      const auto *const_decl = llvm::cast<ConstDeclStmt>(&s);
      PrintLocation(os, const_decl->loc);
      os << "ConstDeclStmt name=" << const_decl->name << '\n';
      DumpChildExpr(os, depth + 1, {}, *const_decl->initializer);
      break;
    }
    case StmtKind::kFunDecl: {
      const auto *fun = llvm::cast<FunDeclStmt>(&s);
      PrintLocation(os, fun->loc);
      os << "FunDeclStmt name=" << fun->name << " params=[";
      bool first = true;
      for (llvm::StringRef param : fun->params) {
        if (!first)
          os << ", ";
        first = false;
        os << param;
      }
      os << "]\n";
      DumpChildStmt(os, depth + 1, {}, *fun->body);
      break;
    }
    case StmtKind::kClass: {
      const auto *cls = llvm::cast<ClassStmt>(&s);
      PrintLocation(os, cls->loc);
      os << "ClassStmt name=" << cls->name;
      if (!cls->superclass.empty())
        os << " superclass=" << cls->superclass;
      os << '\n';
      for (const auto &method : cls->methods)
        DumpChildStmt(os, depth + 1, {}, *method);
      break;
    }
    case StmtKind::kImport: {
      const auto *import = llvm::cast<ImportStmt>(&s);
      PrintLocation(os, import->loc);
      os << "ImportStmt path=";
      llvm::printEscapedString(import->path, os);
      if (!import->alias.empty())
        os << " alias=" << import->alias;
      os << '\n';
      break;
    }
    case StmtKind::kExpression: {
      const auto *expression_stmt = llvm::cast<ExpressionStmt>(&s);
      PrintLocation(os, expression_stmt->loc);
      os << "ExpressionStmt\n";
      DumpChildExpr(os, depth + 1, {}, *expression_stmt->expression);
      break;
    }
    case StmtKind::kIf: {
      const auto *if_stmt = llvm::cast<IfStmt>(&s);
      PrintLocation(os, if_stmt->loc);
      os << "IfStmt\n";
      DumpChildExpr(os, depth + 1, "condition", *if_stmt->condition);
      DumpChildStmt(os, depth + 1, "then", *if_stmt->then_branch);
      if (if_stmt->else_branch)
        DumpChildStmt(os, depth + 1, "else", *if_stmt->else_branch);
      break;
    }
    case StmtKind::kWhile: {
      const auto *while_stmt = llvm::cast<WhileStmt>(&s);
      PrintLocation(os, while_stmt->loc);
      os << "WhileStmt\n";
      DumpChildExpr(os, depth + 1, "condition", *while_stmt->condition);
      DumpChildStmt(os, depth + 1, "body", *while_stmt->body);
      break;
    }
    case StmtKind::kFor: {
      const auto *for_stmt = llvm::cast<ForStmt>(&s);
      PrintLocation(os, for_stmt->loc);
      os << "ForStmt\n";
      if (for_stmt->initializer)
        DumpChildStmt(os, depth + 1, "initializer", *for_stmt->initializer);
      if (for_stmt->condition)
        DumpChildExpr(os, depth + 1, "condition", *for_stmt->condition);
      if (for_stmt->increment)
        DumpChildExpr(os, depth + 1, "increment", *for_stmt->increment);
      DumpChildStmt(os, depth + 1, "body", *for_stmt->body);
      break;
    }
    case StmtKind::kReturn: {
      const auto *return_stmt = llvm::cast<ReturnStmt>(&s);
      PrintLocation(os, return_stmt->loc);
      os << "ReturnStmt\n";
      if (return_stmt->value)
        DumpChildExpr(os, depth + 1, {}, *return_stmt->value);
      break;
    }
    case StmtKind::kThrow: {
      const auto *throw_stmt = llvm::cast<ThrowStmt>(&s);
      PrintLocation(os, throw_stmt->loc);
      os << "ThrowStmt\n";
      DumpChildExpr(os, depth + 1, {}, *throw_stmt->value);
      break;
    }
    case StmtKind::kTry: {
      const auto *try_stmt = llvm::cast<TryStmt>(&s);
      PrintLocation(os, try_stmt->loc);
      os << "TryStmt\n";
      DumpChildStmt(os, depth + 1, {}, *try_stmt->body);
      for (const auto &clause : try_stmt->catches)
        DumpCatchClause(os, depth + 1, clause);
      break;
    }
    case StmtKind::kBreak: {
      const auto *break_stmt = llvm::cast<BreakStmt>(&s);
      PrintLocation(os, break_stmt->loc);
      os << "BreakStmt\n";
      break;
    }
  }
}

}  // namespace

void Expr::Dump(llvm::raw_ostream &os) const { DumpExpr(*this, os, 0); }

void Stmt::Dump(llvm::raw_ostream &os) const { DumpStmt(*this, os, 0); }

void Dump(const llvm::SmallVectorImpl<StmtPtr> &program, llvm::raw_ostream &os) {
  for (const auto &statement : program)
    statement->Dump(os);
}

}  // namespace lox
