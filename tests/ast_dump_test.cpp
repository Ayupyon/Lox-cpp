#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "parser/ast.h"

namespace {

using namespace lox;

ExprPtr MakeVar(llvm::StringRef name, SourceLocation loc = {1, 1}) {
  return std::make_unique<VarExpr>(loc, name);
}

ExprPtr MakeInt(std::int64_t value, SourceLocation loc = {1, 1}) {
  return std::make_unique<LiteralExpr>(loc, LiteralKind::kInteger, value);
}

std::string DumpExprToString(const Expr &e) {
  llvm::SmallString<256> buffer;
  llvm::raw_svector_ostream os(buffer);
  e.Dump(os);
  return buffer.str().str();
}

std::string DumpStmtToString(const Stmt &s) {
  llvm::SmallString<256> buffer;
  llvm::raw_svector_ostream os(buffer);
  s.Dump(os);
  return buffer.str().str();
}

TEST(AstDumpTest, LiteralExpr) {
  EXPECT_EQ("1:1 LiteralExpr True\n", DumpExprToString(LiteralExpr({1, 1}, LiteralKind::kTrue)));
  EXPECT_EQ("1:3 LiteralExpr False\n", DumpExprToString(LiteralExpr({1, 3}, LiteralKind::kFalse)));
  EXPECT_EQ("1:5 LiteralExpr Null\n", DumpExprToString(LiteralExpr({1, 5}, LiteralKind::kNull)));
  EXPECT_EQ("1:7 LiteralExpr Integer [42]\n",
            DumpExprToString(LiteralExpr({1, 7}, LiteralKind::kInteger, std::int64_t(42))));
  EXPECT_EQ("1:9 LiteralExpr Integer [-7]\n",
            DumpExprToString(LiteralExpr({1, 9}, LiteralKind::kInteger, std::int64_t(-7))));
  EXPECT_EQ("1:11 LiteralExpr Float [2.5]\n",
            DumpExprToString(LiteralExpr({1, 11}, LiteralKind::kFloat, 2.5)));
  // Integral doubles print without a fractional part (shortest round-trip).
  EXPECT_EQ("1:13 LiteralExpr Float [2]\n",
            DumpExprToString(LiteralExpr({1, 13}, LiteralKind::kFloat, 2.0)));
  EXPECT_EQ("1:15 LiteralExpr Float [1e+10]\n",
            DumpExprToString(LiteralExpr({1, 15}, LiteralKind::kFloat, 1e10)));
  // Strings keep the raw lexeme (quotes and escapes included), escaped for
  // display exactly like scanner token lines.
  EXPECT_EQ("1:1 LiteralExpr String \\22hi\\22\n",
            DumpExprToString(LiteralExpr({1, 1}, LiteralKind::kString, llvm::StringRef("\"hi\""))));
  EXPECT_EQ(
      "1:1 LiteralExpr String \\22a\\\\b\\22\n",
      DumpExprToString(LiteralExpr({1, 1}, LiteralKind::kString, llvm::StringRef("\"a\\b\""))));
}

TEST(AstDumpTest, VarThisSuper) {
  EXPECT_EQ("1:1 VarExpr name=count\n", DumpExprToString(VarExpr({1, 1}, "count")));
  EXPECT_EQ("2:3 ThisExpr\n", DumpExprToString(ThisExpr({2, 3})));
  EXPECT_EQ("3:1 SuperExpr method=speak\n", DumpExprToString(SuperExpr({3, 1}, "speak")));
}

TEST(AstDumpTest, UnaryExpr) {
  EXPECT_EQ("1:1 UnaryExpr Not\n  1:3 VarExpr name=flag\n",
            DumpExprToString(UnaryExpr({1, 1}, UnaryOp::kNot, {1, 2}, MakeVar("flag", {1, 3}))));
  EXPECT_EQ("1:1 UnaryExpr Negate\n  1:3 LiteralExpr Integer [5]\n",
            DumpExprToString(UnaryExpr({1, 1}, UnaryOp::kNegate, {1, 2}, MakeInt(5, {1, 3}))));
  EXPECT_EQ("1:1 UnaryExpr BitNot\n  1:3 VarExpr name=mask\n",
            DumpExprToString(UnaryExpr({1, 1}, UnaryOp::kBitNot, {1, 2}, MakeVar("mask", {1, 3}))));
}

TEST(AstDumpTest, BinaryExpr) {
  const std::pair<BinaryOp, llvm::StringRef> k_ops[] = {
      {BinaryOp::kAdd, "Add"},
      {BinaryOp::kSubtract, "Subtract"},
      {BinaryOp::kMultiply, "Multiply"},
      {BinaryOp::kDivide, "Divide"},
      {BinaryOp::kShiftLeft, "ShiftLeft"},
      {BinaryOp::kShiftRight, "ShiftRight"},
      {BinaryOp::kLess, "Less"},
      {BinaryOp::kLessEqual, "LessEqual"},
      {BinaryOp::kGreater, "Greater"},
      {BinaryOp::kGreaterEqual, "GreaterEqual"},
      {BinaryOp::kEqual, "Equal"},
      {BinaryOp::kNotEqual, "NotEqual"},
      {BinaryOp::kBitAnd, "BitAnd"},
      {BinaryOp::kBitOr, "BitOr"},
      {BinaryOp::kBitXor, "BitXor"},
  };
  for (const auto &[op, name] : k_ops) {
    BinaryExpr expr({1, 1}, MakeVar("a", {1, 2}), op, {1, 4}, MakeVar("b", {1, 6}));
    const std::string expected =
        "1:1 BinaryExpr " + name.str() + "\n  1:2 VarExpr name=a\n  1:6 VarExpr name=b\n";
    EXPECT_EQ(expected, DumpExprToString(expr)) << "operator name: " << name.str();
  }
}

TEST(AstDumpTest, LogicalExpr) {
  EXPECT_EQ("1:1 LogicalExpr And\n  1:6 VarExpr name=a\n  1:11 VarExpr name=b\n",
            DumpExprToString(LogicalExpr(
                {1, 1}, MakeVar("a", {1, 6}), LogicalOp::kAnd, {1, 8}, MakeVar("b", {1, 11}))));
  EXPECT_EQ("1:1 LogicalExpr Or\n  1:6 VarExpr name=a\n  1:11 VarExpr name=b\n",
            DumpExprToString(LogicalExpr(
                {1, 1}, MakeVar("a", {1, 6}), LogicalOp::kOr, {1, 8}, MakeVar("b", {1, 11}))));
}

TEST(AstDumpTest, CallExpr) {
  llvm::SmallVector<ExprPtr, 4> args;
  args.push_back(MakeInt(1, {1, 10}));
  args.push_back(MakeInt(2, {1, 13}));
  CallExpr call({1, 1},
                std::make_unique<GetExpr>(SourceLocation{1, 3}, MakeVar("p", {1, 5}), "getX"),
                std::move(args));
  EXPECT_EQ(
      "1:1 CallExpr\n"
      "  callee: 1:3 GetExpr name=getX\n"
      "    1:5 VarExpr name=p\n"
      "  argument: 1:10 LiteralExpr Integer [1]\n"
      "  argument: 1:13 LiteralExpr Integer [2]\n",
      DumpExprToString(call));
}

TEST(AstDumpTest, SubscriptExpr) {
  SubscriptExpr expr({1, 1}, MakeVar("a", {1, 2}), MakeVar("i", {1, 4}));
  EXPECT_EQ(
      "1:1 SubscriptExpr\n"
      "  object: 1:2 VarExpr name=a\n"
      "  index: 1:4 VarExpr name=i\n",
      DumpExprToString(expr));
}

TEST(AstDumpTest, ListExpr) {
  // [10, 20; 3]
  llvm::SmallVector<ExprPtr, 4> elements;
  elements.push_back(MakeInt(10, {1, 2}));
  elements.push_back(MakeInt(20, {1, 5}));
  ListExpr sized({1, 1}, std::move(elements), MakeInt(3, {1, 8}));
  EXPECT_EQ(
      "1:1 ListExpr\n"
      "  element: 1:2 LiteralExpr Integer [10]\n"
      "  element: 1:5 LiteralExpr Integer [20]\n"
      "  size: 1:8 LiteralExpr Integer [3]\n",
      DumpExprToString(sized));

  // [; 3] — no elements, only a size clause.
  ListExpr empty_elements({1, 1}, {}, MakeInt(3, {1, 4}));
  EXPECT_EQ(
      "1:1 ListExpr\n"
      "  size: 1:4 LiteralExpr Integer [3]\n",
      DumpExprToString(empty_elements));

  // [1, 2] — no size clause at all.
  ListExpr no_size({1, 1}, {}, nullptr);
  EXPECT_EQ("1:1 ListExpr\n", DumpExprToString(no_size));
}

TEST(AstDumpTest, AssignmentExpr) {
  EXPECT_EQ("1:1 AssignExpr name=x\n  1:5 LiteralExpr Integer [5]\n",
            DumpExprToString(AssignExpr({1, 1}, "x", MakeInt(5, {1, 5}))));
  EXPECT_EQ(
      "1:1 SetExpr name=b\n"
      "  object: 1:3 VarExpr name=a\n"
      "  value: 1:7 VarExpr name=v\n",
      DumpExprToString(SetExpr({1, 1}, MakeVar("a", {1, 3}), "b", MakeVar("v", {1, 7}))));
  EXPECT_EQ(
      "1:1 SetIndexExpr\n"
      "  object: 1:3 VarExpr name=a\n"
      "  index: 1:5 VarExpr name=i\n"
      "  value: 1:9 VarExpr name=v\n",
      DumpExprToString(
          SetIndexExpr({1, 1}, MakeVar("a", {1, 3}), MakeVar("i", {1, 5}), MakeVar("v", {1, 9}))));
}

TEST(AstDumpTest, LoxRtti) {
  // The kind-enum + classof machinery backs llvm::isa/cast/dyn_cast without
  // C++ RTTI (-fno-rtti).
  VarExpr var({1, 1}, "x");
  const Expr &expr = var;
  EXPECT_TRUE(llvm::isa<VarExpr>(expr));
  EXPECT_FALSE(llvm::isa<LiteralExpr>(expr));
  EXPECT_EQ(&var, llvm::dyn_cast<VarExpr>(&expr));
  EXPECT_EQ("x", llvm::cast<VarExpr>(expr).name);
}

TEST(AstDumpTest, BasicStmts) {
  llvm::SmallVector<StmtPtr, 4> inner;
  inner.push_back(std::make_unique<BreakStmt>(SourceLocation{2, 3}));
  BlockStmt block({1, 1}, std::move(inner));
  EXPECT_EQ(
      "1:1 BlockStmt\n"
      "  2:3 BreakStmt\n",
      DumpStmtToString(block));

  EXPECT_EQ("1:1 VarDeclStmt name=x\n  1:9 LiteralExpr Integer [5]\n",
            DumpStmtToString(VarDeclStmt({1, 1}, "x", MakeInt(5, {1, 9}))));
  EXPECT_EQ("1:1 ConstDeclStmt name=PI\n  1:13 LiteralExpr Float [3.14]\n",
            DumpStmtToString(ConstDeclStmt(
                {1, 1},
                "PI",
                std::make_unique<LiteralExpr>(SourceLocation{1, 13}, LiteralKind::kFloat, 3.14))));
  EXPECT_EQ("1:1 ExpressionStmt\n  1:1 VarExpr name=x\n",
            DumpStmtToString(ExpressionStmt({1, 1}, MakeVar("x", {1, 1}))));
  EXPECT_EQ("1:1 ReturnStmt\n", DumpStmtToString(ReturnStmt({1, 1})));
  EXPECT_EQ("1:1 ReturnStmt\n  1:8 LiteralExpr Integer [42]\n",
            DumpStmtToString(ReturnStmt({1, 1}, MakeInt(42, {1, 8}))));
  EXPECT_EQ(
      "1:1 ThrowStmt\n"
      "  1:7 CallExpr\n"
      "    callee: 1:7 VarExpr name=E\n",
      DumpStmtToString(ThrowStmt(
          {1, 1},
          std::make_unique<CallExpr>(
              SourceLocation{1, 7}, MakeVar("E", {1, 7}), llvm::SmallVector<ExprPtr, 4>{}))));
  EXPECT_EQ("1:1 ImportStmt path=\\22foo.lox\\22\n",
            DumpStmtToString(ImportStmt({1, 1}, "\"foo.lox\"")));
  EXPECT_EQ("1:1 ImportStmt path=\\22foo.lox\\22 alias=bar\n",
            DumpStmtToString(ImportStmt({1, 1}, "\"foo.lox\"", "bar")));
}

TEST(AstDumpTest, FunctionAndClass) {
  // fun add(a, b) { return a + b; }
  llvm::SmallVector<StmtPtr, 4> body_stmts;
  body_stmts.push_back(
      std::make_unique<ReturnStmt>(SourceLocation{1, 19},
                                   std::make_unique<BinaryExpr>(SourceLocation{1, 26},
                                                                MakeVar("a", {1, 27}),
                                                                BinaryOp::kAdd,
                                                                SourceLocation{1, 29},
                                                                MakeVar("b", {1, 31}))));
  FunDeclStmt fun({1, 1},
                  "add",
                  {"a", "b"},
                  std::make_unique<BlockStmt>(SourceLocation{1, 17}, std::move(body_stmts)));
  EXPECT_EQ(
      "1:1 FunDeclStmt name=add params=[a, b]\n"
      "  1:17 BlockStmt\n"
      "    1:19 ReturnStmt\n"
      "      1:26 BinaryExpr Add\n"
      "        1:27 VarExpr name=a\n"
      "        1:31 VarExpr name=b\n",
      DumpStmtToString(fun));

  // class Dog extends Animal { speak() { return "Woof!"; } }
  llvm::SmallVector<std::unique_ptr<FunDeclStmt>, 4> methods;
  llvm::SmallVector<StmtPtr, 4> speak_body;
  speak_body.push_back(std::make_unique<ReturnStmt>(
      SourceLocation{2, 5},
      std::make_unique<LiteralExpr>(
          SourceLocation{2, 12}, LiteralKind::kString, llvm::StringRef("\"Woof!\""))));
  methods.push_back(std::make_unique<FunDeclStmt>(
      SourceLocation{1, 21},
      "speak",
      llvm::SmallVector<llvm::StringRef, 4>{},
      std::make_unique<BlockStmt>(SourceLocation{1, 29}, std::move(speak_body))));
  ClassStmt cls({1, 1}, "Dog", "Animal", std::move(methods));
  EXPECT_EQ(
      "1:1 ClassStmt name=Dog superclass=Animal\n"
      "  1:21 FunDeclStmt name=speak params=[]\n"
      "    1:29 BlockStmt\n"
      "      2:5 ReturnStmt\n"
      "        2:12 LiteralExpr String \\22Woof!\\22\n",
      DumpStmtToString(cls));

  // A class without a superclass omits the superclass field.
  ClassStmt base({1, 1}, "Animal", "", {});
  EXPECT_EQ("1:1 ClassStmt name=Animal\n", DumpStmtToString(base));
}

TEST(AstDumpTest, ControlFlow) {
  IfStmt if_stmt({1, 1},
                 MakeVar("flag", {1, 5}),
                 std::make_unique<BlockStmt>(SourceLocation{1, 12}),
                 std::make_unique<BlockStmt>(SourceLocation{1, 21}));
  EXPECT_EQ(
      "1:1 IfStmt\n"
      "  condition: 1:5 VarExpr name=flag\n"
      "  then: 1:12 BlockStmt\n"
      "  else: 1:21 BlockStmt\n",
      DumpStmtToString(if_stmt));

  IfStmt no_else(
      {1, 1}, MakeVar("flag", {1, 5}), std::make_unique<BlockStmt>(SourceLocation{1, 12}), nullptr);
  EXPECT_EQ(
      "1:1 IfStmt\n"
      "  condition: 1:5 VarExpr name=flag\n"
      "  then: 1:12 BlockStmt\n",
      DumpStmtToString(no_else));

  WhileStmt while_stmt({1, 1},
                       std::make_unique<BinaryExpr>(SourceLocation{1, 8},
                                                    MakeVar("i", {1, 8}),
                                                    BinaryOp::kLess,
                                                    SourceLocation{1, 10},
                                                    MakeInt(3, {1, 12})),
                       std::make_unique<BlockStmt>(SourceLocation{1, 15}));
  EXPECT_EQ(
      "1:1 WhileStmt\n"
      "  condition: 1:8 BinaryExpr Less\n"
      "    1:8 VarExpr name=i\n"
      "    1:12 LiteralExpr Integer [3]\n"
      "  body: 1:15 BlockStmt\n",
      DumpStmtToString(while_stmt));

  // for (let i = 0; i < 3; i = i + 1) { }
  ForStmt for_stmt({1, 1},
                   std::make_unique<VarDeclStmt>(SourceLocation{1, 6}, "i", MakeInt(0, {1, 10})),
                   std::make_unique<BinaryExpr>(SourceLocation{1, 13},
                                                MakeVar("i", {1, 13}),
                                                BinaryOp::kLess,
                                                SourceLocation{1, 15},
                                                MakeInt(3, {1, 17})),
                   std::make_unique<AssignExpr>(SourceLocation{1, 20},
                                                "i",
                                                std::make_unique<BinaryExpr>(SourceLocation{1, 24},
                                                                             MakeVar("i", {1, 24}),
                                                                             BinaryOp::kAdd,
                                                                             SourceLocation{1, 26},
                                                                             MakeInt(1, {1, 28}))),
                   std::make_unique<BlockStmt>(SourceLocation{1, 31}));
  EXPECT_EQ(
      "1:1 ForStmt\n"
      "  initializer: 1:6 VarDeclStmt name=i\n"
      "    1:10 LiteralExpr Integer [0]\n"
      "  condition: 1:13 BinaryExpr Less\n"
      "    1:13 VarExpr name=i\n"
      "    1:17 LiteralExpr Integer [3]\n"
      "  increment: 1:20 AssignExpr name=i\n"
      "    1:24 BinaryExpr Add\n"
      "      1:24 VarExpr name=i\n"
      "      1:28 LiteralExpr Integer [1]\n"
      "  body: 1:31 BlockStmt\n",
      DumpStmtToString(for_stmt));

  // for (;;) { } — every optional slot omitted.
  ForStmt bare(
      {1, 1}, nullptr, nullptr, nullptr, std::make_unique<BlockStmt>(SourceLocation{1, 7}));
  EXPECT_EQ("1:1 ForStmt\n  body: 1:7 BlockStmt\n", DumpStmtToString(bare));
}

TEST(AstDumpTest, TryCatch) {
  llvm::SmallVector<CatchClause, 2> catches;
  catches.push_back(CatchClause{"E", "e", std::make_unique<BlockStmt>(SourceLocation{1, 19})});
  catches.push_back(CatchClause{"Integer", "", std::make_unique<BlockStmt>(SourceLocation{2, 5})});
  TryStmt try_stmt({1, 1}, std::make_unique<BlockStmt>(SourceLocation{1, 5}), std::move(catches));
  EXPECT_EQ(
      "1:1 TryStmt\n"
      "  1:5 BlockStmt\n"
      "  catch: CatchClause type=E binding=e\n"
      "    1:19 BlockStmt\n"
      "  catch: CatchClause type=Integer\n"
      "    2:5 BlockStmt\n",
      DumpStmtToString(try_stmt));
}

TEST(AstDumpTest, Program) {
  llvm::SmallVector<StmtPtr, 4> program;
  program.push_back(std::make_unique<VarDeclStmt>(
      SourceLocation{1, 1},
      "px",
      std::make_unique<CallExpr>(
          SourceLocation{1, 9},
          std::make_unique<GetExpr>(SourceLocation{1, 11}, MakeVar("p", {1, 13}), "getX"),
          llvm::SmallVector<ExprPtr, 4>{})));
  program.push_back(std::make_unique<BreakStmt>(SourceLocation{2, 1}));

  llvm::SmallString<256> buffer;
  llvm::raw_svector_ostream os(buffer);
  Dump(program, os);
  EXPECT_EQ(
      "1:1 VarDeclStmt name=px\n"
      "  1:9 CallExpr\n"
      "    callee: 1:11 GetExpr name=getX\n"
      "      1:13 VarExpr name=p\n"
      "2:1 BreakStmt\n",
      std::string(buffer));
}

}  // namespace
