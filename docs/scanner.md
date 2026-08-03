# Scanner 模块设计决策

本文档记录 scanner 模块的设计决策，供后续实现（scanner 本体、词法错误类型、关键字表、CMake 接入、测试等）参考。已落地的部分以 `include/scanner/token.h` 为准。

## 1. 模块定位

Scanner 是编译前端第一阶段：读入 lox 源文件字符序列，产出 `Token` 流，同时填充字面量值，供 Parser 消费。Scanner 运行于 VM/GC 存在之前，因此**不依赖 GC 堆**——字符串驻留推迟到 compiler 阶段。

## 2. Token 表示范式

**决策：单一 tagged-union 结构体，值语义，`std::variant` 承载载荷，不使用类继承层次。**

否决的方案：

- **OOP 类层次 + LLVM `isa/cast/dyn_cast` RTTI**：Token 是纯数据、无按类型分派的行为，类层次带来堆分配 + 虚分派 + 指针间接却无收益。LLVM RTTI 适合带行为的多态对象（AST 节点、VM 的 `Class`/`Instance`/`Closure`），不适合扁平数据记录。
- **enum + 类层次 + wrapper 的混合方案**：三种判别机制（enum、虚 kind、wrapper）互相冗余，付出三份代价却只表达“Token 是什么类型”这一条信息。

`Token` 为 plain aggregate，默认成员初始化，值语义，存于 `llvm::SmallVector<Token, 0>`，无逐 Token 堆分配。

## 3. Token 结构

```cpp
namespace lox {

enum class TokenType : std::uint8_t { /* 55 个枚举值，k 前缀 */ };

using TokenValue = std::variant<std::monostate, std::int64_t, double>;

struct Token {
  TokenType       type{};
  std::uint32_t   line{};      // 1-indexed
  std::uint32_t   column{};    // 1-indexed
  llvm::StringRef lexeme{};    // 源码零拷贝切片；.size() 即长度
  TokenValue      value{};     // 仅 kInteger/kFloat 使用
  llvm::StringRef TypeName() const;            // 诊断用
  void            Print(llvm::raw_ostream &os) const;  // 输出流打印
};

}  // namespace lox
```

字段说明：

- `type`：Token 种类，`enum class`，`uint8_t` 底层。
- `line` / `column`：`uint32_t`，1-indexed，用于诊断。
- `lexeme`：`llvm::StringRef`，零拷贝指向源缓冲区；**每个 Token 都携带**，`lexeme.size()` 即源码跨度长度（不另设 length 字段），也供错误信息回显源文本。
- `value`：`TokenValue` 变体，仅 `kInteger`/`kFloat` 填充数值；其余默认 `monostate`。

### 3.1 TokenValue 变体

```cpp
using TokenValue = std::variant<std::monostate, std::int64_t, double>;
```

- `std::monostate` **必须**是首个备选：聚合/默认构造依赖它使非数值 Token 默认为“无值”。重排备选会破坏该不变量（已用注释钉死）。
- 仅 `kInteger`（`int64_t`）/`kFloat`（`double`）使用数值备选；`kString`/`kIdentifier` 的内容经 `lexeme` 承载，变体内无对应条目。

## 4. TokenType 枚举

共 **55** 个枚举值，`enum class TokenType : std::uint8_t`，命名 `k` 前缀 + CamelCase（Google 风格）：

| 类别 | 数量 | 枚举值 |
|---|---|---|
| 关键字 | 21 | `kClass` `kElse` `kFalse` `kFor` `kFun` `kIf` `kNull` `kReturn` `kSuper` `kThis` `kTrue` `kWhile` `kExtends` `kImport` `kTry` `kCatch` `kThrow` `kBreak` `kLet` `kConst` `kAs` |
| 字面量 | 4 | `kInteger` `kFloat` `kString` `kIdentifier` |
| 单字符标点/运算符 | 21 | `kLeftParen` `kRightParen` `kLeftBrace` `kRightBrace` `kLeftBracket` `kRightBracket` `kComma` `kSemicolon` `kDot` `kBang` `kTilde` `kMinus` `kPlus` `kSlash` `kStar` `kPipe` `kCaret` `kAmpersand` `kLess` `kGreater` `kEqual` |
| 双字符运算符 | 8 | `kLessLess` `kGreaterGreater` `kEqualEqual` `kBangEqual` `kGreaterEqual` `kLessEqual` `kPipePipe` `kAmpersandAmpersand` |
| 哨兵 | 1 | `kEof` |

关键字集合与 `docs/lox-language.typ` 第 49 行的关键字网格一致（21 个，含 `if`）。运算符为 glyph 命名（`kEqualEqual` 而非语义命名），scanner 层只关心字形。

## 5. 载荷与解码时机

| Token | scanner 做什么 | 载荷 | 后续 |
|---|---|---|---|
| `kInteger` | 解析为 `int64_t`；溢出 -> 词法错误 | `value` = `int64_t` | compiler 写入常量池 |
| `kFloat` | 解析为 `double` | `value` = `double` | compiler 写入常量池 |
| `kString` | **仅校验转义语法**（为定位 Token 结尾 + 捕获非法转义）；不解码 | `lexeme`（原始切片，含反斜杠） | **解码 + 驻留推迟到 compiler** |
| `kIdentifier` | 切出词素 | `lexeme` | compiler 驻留名称 |

字符串解码推迟到 compiler 的依据：

1. 语法文档（第 451 行）规定字符串字面量在**编译阶段**驻留；驻留依赖 GC 的 `String` 表，而 GC 在 scan 阶段尚不存在。
2. scanner 为定位 Token 结尾本就必须解析转义语法（`\xNN` 占 4 字符、`\uNNNN` 占 6、非法转义为词法错误），但**解码**（产出字节序列）是可分离的关切，无必要做两遍。
3. 保持变体不含 `std::string`，`Token` 保持平凡可拷贝、统一。

## 6. Token 流容器

**决策：`llvm::SmallVector<Token, 0>`（始终堆分配）。**

- 与项目 LLVM ADT 风格一致。
- 选 `0` 而非非零 inline 容量：当前 scanner 仅按整文件喂入（无 REPL/行级入口），inline 缓冲对小输入无收益。`SmallVector` 的增长机制仍被使用（逐 Token push）。
- 注意：这与 VM 中 `List` 的 `SmallVector<Value, 0>` 含义不同——`List` 是定长不再增长故 `0` 省去 per-object 浪费；Token 流是增长的，`0` 仅为“始终堆 + ADT 统一”。

## 7. 词法错误

**决策：带外 `LexicalError : llvm::ErrorInfo<LexicalError>`，位于独立的 `scanner/error.h`；枚举中无 `kError`。**

- 与项目既有的 `RuntimeError : llvm::ErrorInfo<RuntimeError>`（语法文档第 507-510 行）同构，沿用 `llvm::Error`/`llvm::Expected<T>` 传播。
- `LexicalError` **累积**多条消息：scanner 遇错时跳过坏字符/序列（resync）继续扫描，把所有词法错误收进一个 `LexicalError`，末尾返回（干净则返回 `Expected<SmallVector<Token,0>>` 成功）。
- 枚举无 `kError` 哨兵：`token.h` 只定义 Token 类型；parser 无需跳过/处理错误 Token。
- 这与 Crafting Interpreters 的“错误 Token 入带”不同——本项目错误模型统一为 `llvm::Error`，故采用带外 + 自定义 ErrorInfo。

## 8. 构造方式

**决策：`Token` 为 plain aggregate（无用户自定义构造函数），默认成员初始化（`value{}` -> `monostate`）。**

- 与“简单扁平记录”的范式选择一致。
- 若 scanner 需要防御性构造，辅助函数放 `scanner.cpp` 的匿名命名空间（file-local，依 AGENTS.md），不进 `token.h` 的 API。
- 唯一脚枪：`value{}` 解析为 `monostate` 依赖 monostate 为首备选（见 3.1）。

## 9. 命名空间与头文件保护

- **命名空间**：`TokenType`/`TokenValue`/`Token`（含成员方法 `TypeName`/`Print`）位于 `lox`（跨模块被 parser/JIT 消费）。模块局部 API 放 `scanner`，文件局部放匿名命名空间（依 AGENTS.md）。
- **头文件保护**：`#ifndef ...` 模式，guard 宏以 `LOX_` 开头（AGENTS.md 第 23 行）。`token.h` 用 `LOX_SCANNER_TOKEN_H_`（`LOX_` 前缀 + 路径 + `_H_` 后缀，含尾下划线）。后续 scanner 头文件依此，如 `include/scanner/error.h` -> `LOX_SCANNER_ERROR_H_`。

## 10. 命名风格

项目采用 Google 风格（`.clang-format` `BasedOnStyle: Google`，`PointerAlignment: Right`；`.clang-tidy` 从 LLVM 配置迁移为 Google）。命名规则：

| 类别 | 风格 | 示例 |
|---|---|---|
| 类型（class/struct/enum/union/type-alias） | CamelCase | `Token` `TokenType` `TokenValue` |
| 函数（含成员方法） | CamelCase | `TypeName` `Print` |
| 变量/参数/成员 | lower_case | `type` `line` `lexeme` |
| class private/protected 成员 | lower_case + `_` 后缀 | `count_` |
| struct/public 成员 | lower_case（无后缀） | `type` |
| enum 常量 | `k` + CamelCase | `kLeftParen` |
| `constexpr` 常量 | `k` + CamelCase | `kMaxTokenLength` |
| `const` 变量（含局部） | lower_case | `max_token_length` |
| namespace | lower_case | `lox` |

## 11. clang-tidy 配置要点

- 禁用 `llvm-header-guard`（LLVM 子项目专用 guard 命名，与 Google `_H_` 尾下划线冲突）。
- 禁用 `llvm-include-order`（LLVM include 顺序与 Google 不同）。
- 禁用 `llvm-prefer-static-over-anonymous-namespace`（与 AGENTS.md「file-local API 放匿名命名空间」约定冲突）。
- `ConstantCase`（`lower_case`）与 `ConstexprVariableCase`（`CamelCase` + `k` 前缀）分工：`const` 变量（含局部）为 lower_case，`constexpr` 常量为 `k` + CamelCase（见第 10 节）。未配置的细分选项（`LocalConstexprVariableCase`/`GlobalConstexprVariableCase`/`StaticConstexprVariableCase`/`LocalConstantCase` 等）按检查的回退语义归入对应通用类别，故仅配置通用两项即可。注意：`-dump-config` 不列出未配置的选项，不能据此判断选项是否存在。
- 其余 `llvm-*`（`llvm-qualified-auto`、`llvm-else-after-return`、`llvm-prefer-isa-or-dyn-cast-in-conditionals` 等）保留——项目大量使用 LLVM ADT/RTTI，这些检查有用且不会误报。
- `Token::TypeName` 用 `switch` 且**无 default**，使 `-Wswitch` 保持武装：新增 `TokenType` 而漏 case 即编译告警（实现位于 `src/scanner/token.cpp`）。
- `Token::TypeName` 用 `switch` 而非 map：键稠密（`uint8_t` 0–54）+ 冷路径（仅诊断用）+ `-Wswitch` 完备性检查，map（`DenseMap`/`StringMap`）对稠密整型键无优势且无完备性保证。反向的关键字查找（`StringRef -> TokenType`）才是 map 的用武之地（见第 12 节）。

## 12. 后续工作

- **`scanner/error.h` + `LexicalError`**：实现 `LexicalError : llvm::ErrorInfo<LexicalError>`，累积多条消息，提供格式化输出。guard `LOX_SCANNER_ERROR_H_`。
- **关键字表**：`llvm::StringMap<TokenType>`（lexeme -> `TokenType`），位于 scanner（非 `token.h`），用于标识符 vs 关键字识别。
- **注释语法**：语法文档未规定注释（`//` 和/或 `/* */`）。注释不产生 Token，不影响 `token.h`，但 scanner 实现前需定。待与语法文档一并对齐。
- **CMake 接入**：scanner 模块尚未接入 CMake（`src/CMakeLists.txt` 仅有 `lox-cpp` 可执行文件 + `main.cpp`）。需加 scanner 库 target，使 `run-clang-tidy`/`compile_commands.json` 覆盖之。
- **`scanner.cpp`**：词法分析主体——字符驱动状态机、关键字识别、数值/字符串/标识符切分、转义校验、resync、行号列号维护、构造辅助（匿名命名空间）。
- **测试**：`tests/scanner/` 单元测试，覆盖各类 Token、边界值、转义、溢出、多错误累积。

## 13. 已落地

- `include/scanner/token.h`：`TokenType`（55）、`TokenValue`、`Token`（成员方法 `TypeName`/`Print`，实现在 `src/scanner/token.cpp`，已加入 `lox_scanner` 目标）。通过 clang-tidy（Google 配置）、clang-format、`-Wall -Wextra -Werror` 编译。
- `.clang-tidy`：从 LLVM 配置迁移为 Google 风格（见第 10、11 节）。
- guard：`LOX_SCANNER_TOKEN_H_`（依 AGENTS.md `LOX_` 前缀约束）。

## 14. 实现决策汇总（grilling 结论）

本节汇总 grilling 环节确认的实现决策，supersede 第 12 节中对应的待定项。命名风格遵循第 10 节（函数 CamelCase）；以下决策待实现时落地，代码实现暂缓。

### 14.1 错误返回模型（累积）

确认采用第 7 节的**累积模型**：scanner 遇词法错误时 resync（跳过坏字符/序列到下一个有效边界）继续扫描，把所有词法错误收进**一个** `LexicalError`，末尾返回--干净则返回 `Expected<SmallVector<Token,0>>` 成功，否则返回该 `LexicalError`（部分 token 流丢弃）。"短路"（遇第一个错误即返回）已被否决：`LexicalError` 的"累积多条消息"设计仅在累积模型下有意义。Parser 仅在干净流上运行。

### 14.2 注释

仅 `//` 行注释（至行尾），无块注释 `/* */`。注释不产生 Token。匹配 `tests/lox-source/pass/comments.lox`。

### 14.3 字符串字面量与转义

- scanner **仅校验转义语法、不解码**（解码/interning 延后至 compiler）。
- 支持的转义：`\n \r \t \\ \" \0 \xNN \uNNNN \u{...}`（花括号内可变长度码点）。
- 坏转义 -> 整个字符串 token 丢弃 + 记一条词法错误。
- 字符串内裸换行（未闭合 `"` 前遇到 `\n`/`\r`）-> 词法错误（未终止字符串）。

### 14.4 错误 token 模型

丢弃坏 token，token 流保持良构；错误累积进 `LexicalError`。`token.h` 无 `kError` 哨兵，parser 无需跳过/处理错误 token（与 Crafting Interpreters 的"错误 token 入带"不同）。

### 14.5 数值字面量（扩展语法）

扩展当前语法文档（见第 14.13 节）的数值规则，均映射到既有 `kInteger`/`kFloat`，无新 token 类型：

- 十进制：`DIGIT^+`
- 十六进制：`("0x"|"0X") HEX_DIGIT^+`；八进制：`("0o"|"0O") OCTAL_DIGIT^+`；二进制：`("0b"|"0B") BIN_DIGIT^+`
- 浮点：`DECIMAL "." DECIMAL EXPONENT?` 或 `DECIMAL EXPONENT`
- 指数：`("e"|"E") ("+"|"-")? DECIMAL`--**总产生 `kFloat`**（含 `1e10` 无小数点）
- 指数 `e`/`E` **非提交**（前瞻）：仅当后随可选 `+`/`-` 再 ≥1 数字才消费；否则 `e` 起标识符。例：`1eaten` -> `1` kInteger + `eaten` kIdentifier；`1e10` -> kFloat；`1.5e` -> `1.5` kFloat + `e` kIdentifier；`1e_5` -> `1` kInteger + `e_5` kIdentifier（下划线不参与指数前瞻，`e` 未被提交）。
- 数字字面量中**不允许下划线**：任何位置（前导、尾随、数字之间、进制前缀后、指数内）出现 `_` 均为词法错误——整个数字 token 丢弃并累积一条错误，不拆分为"数字 + 标识符"（`1_000` 不是 `1` + `_000`）。`1_000`、`100_`、`1__0`、`0x_FF`、`1._5`、`1.5e10_` 均错误。
- `0x`/`0o`/`0b` 后零个基数字 = 词法错误（提交性前缀）。
- int 溢出（int64）-> 词法错误；float 溢出 -> 词法错误；float 下溢 -> 接受 `0.0`。
- 符号**非**字面量一部分（一元运算，parser 职责）。
- 匹配测试：`tests/lox-source/pass/number_literals.lox`；词法错误样例见 `tests/lox-source/compile_error/`（`underscore_decimal`/`underscore_hex`/`underscore_after_dot`/`underscore_exponent`/`underscore_trailing`/`underscore_inner`、`hex_no_digits`/`octal_no_digits`/`binary_no_digits`、`integer_overflow`/`hex_overflow`/`float_overflow`）。

### 14.6 LexicalError

- 位于 `scanner` 命名空间（模块局部），与未来 `vm::RuntimeError` 同构。
- `class LexicalError : public llvm::ErrorInfo<LexicalError>`，guard `LOX_SCANNER_ERROR_H_`。
- 累积 `Entry` 列表：每条含 `line`/`column`/`length`/`message`/`source_line`（struct 公开成员，lower_case 无后缀）。
- `log()` 每条输出 `<filename>:<line>:<column>: error: <message>`，附源码行 + caret（`^`/`~`，长度按 `length`），颜色经 `raw_ostream`（`is_displayed()` 时启用）。无文件名时由驱动前置（如 `<stdin>`）。
- `convertToErrorCode()` 返回 `llvm::inconvertibleErrorCode()`。
- LLVM `ErrorInfo` 约定的方法（用户编写的 `log`/`convertToErrorCode` 与 `static char ID`）保留 LLVM 命名（强制），与函数 CamelCase 规则的冲突必要时以 `// NOLINT(readability-identifier-naming)` 处理。

### 14.7 行号/列号

- `\n`、`\r\n`、孤 `\r` 各结束一行（col -> 1）；tab 推进 col 1。
- token col = 首字符 col；1-indexed。
- EOF 总在末尾发射；空源 -> `[kEof]` at 1:1。

### 14.8 Scanner API

- `class lox::Scanner`，构造取 `llvm::StringRef source` + 可选 `llvm::StringRef filename = ""`。
- `[[nodiscard]] auto Scan() -> llvm::Expected<llvm::SmallVector<Token, 0>>;`
- `void Reset(llvm::StringRef source);`（重置源；`Scan` 内部重置扫描状态，故 `Scan` 幂等可重入）
- 可移动、不可拷贝（持有 `StringRef` 视图与累积状态）。
- `lexeme` 为零拷贝 `llvm::StringRef`，指向调用方源缓冲--调用方须 outlive tokens。

### 14.9 整数/浮点解析

- 整数：经 `__builtin_mul_overflow`/`__builtin_add_overflow` 累积，int64 溢出 -> 词法错误。
- 浮点：`std::from_chars` 直接作用于 lexeme（字面量不含下划线，无需副本）。

### 14.10 关键字表

- `llvm::StringMap<TokenType>`（lexeme -> `TokenType`），位于 `scanner.cpp`，静态局部缓存。
- 21 个关键字：`class else false for fun if null return super this true while extends import try catch throw break let const as`。

### 14.11 构造辅助

- Token 构造辅助（`MakeToken` 等）置于 `scanner.cpp` 匿名命名空间（文件局部），不污染 `token.h`（`Token` 保持 aggregate）。
- 字符分类器（`IsDigit`/`IsAlpha`/`IsHexDigit` 等）亦在匿名命名空间，CamelCase 命名。

### 14.12 CMake 接入

- `src/CMakeLists.txt` 加 `lox_scanner` 静态库 target（源 `scanner/scanner.cpp`），链入 `lox-cpp`，使 `run-clang-tidy`/`compile_commands.json` 覆盖之。

### 14.13 语法文档同步

- 已更新 `docs/lox-language.typ` 数值文法（第 35 行起）：以第 14.5 节的扩展 INTEGER/FLOAT/EXPONENT/基数字规则（不含下划线）替换原十进制文法，并声明"数字字面量中不允许下划线——任何数字与下划线的组合均为词法错误"。

## 15. Scanner 实现落地

本节记录 scanner 模块实现完成的状态（2026-07-31）：12 节的待定项除测试外均已实现（决策见 14 节与本节），实现期新增决策亦记录于此。

### 15.1 已落地

- `include/scanner/error.h`：`lox::scanner::LexicalError`（guard `LOX_SCANNER_ERROR_H_`），累积 `Entry`（line/column/length/message/source_line）。`log()` 输出 `<filename>:<line>:<column>: error: <message>` + 源码行 + caret（`^` + `~`，run 长度按 `length`，超行尾截断），`is_displayed()` 时 `error:` 红色、caret 绿色；`convertToErrorCode()` 返回 `inconvertibleErrorCode()`。
- `include/scanner/scanner.h`：`lox::Scanner`（API 依 14.8，可移动不可拷贝）。
- `src/scanner/scanner.cpp`：字符驱动状态机、关键字表（`llvm::StringMap<TokenType>` 静态局部，21 关键字）、构造辅助与字符分类器位于 `namespace lox` 内匿名命名空间（依 AGENTS.md；引用 `Token`/`TokenType` 需在 `lox` 作用域内）。
- CMake：`src/CMakeLists.txt` 加 `lox_scanner` 静态库（链 `${llvm_libs}`），链接进 `lox-cpp`。
- 顶层 `CMakeLists.txt`：全局 `add_compile_options(-fno-rtti)`——LLVM 22.1.7 以 `LLVM_ENABLE_RTTI=OFF` 构建，项目代码不匹配时 `llvm::ErrorInfo` 子类链接缺 typeinfo（scanner 是首个使用 `llvm::Error` 的模块，链接时暴露）。
- `.clang-tidy` 调整（见 11 节）：`ConstantCase` 改 `lower_case`（`const` 变量，原配置误伤 const 局部变量）；新增 `ConstexprVariableCase` `k` + CamelCase（`constexpr` 常量，恢复第 10 节原约定）；禁用 `llvm-prefer-static-over-anonymous-namespace`。

### 15.2 实现期决策（14 节未覆盖的实现细节）

- 坏数值 token 跨度 = 自数字起始至「数字/字母/下划线/点」的最大游程，整体丢弃（`1_000x` 为一个错误 token；`1.5e10_+2` 的 `+2` 不被吞）。
- 意外字符（`@` `%` `?` `:` 等）为长度 1 的词法错误，resync 前进 1 字符；源内裸 `\0` 字节按意外字符处理。
- `Entry::source_line` 存 `std::string` 拷贝：错误自包含，不依赖 source 缓冲区生命周期。
- 错误消息英文："unexpected character 'x'" / "unterminated string literal" / "invalid escape sequence" / "integer literal out of range" / "floating-point literal out of range" / "underscore not allowed in numeric literal" / "<base> literal has no digits"（base = hexadecimal/octal/binary）。
- 各进制整数按有符号幅度解析，`> INT64_MAX` 即溢出错误（含 `0x8000000000000000`）；`__builtin_mul_overflow`/`__builtin_add_overflow` 累积。
- float 溢出/下溢判别：`std::from_chars` 报 `result_out_of_range` 时以 `long double` 重解析——超出 `DBL_MAX` 为溢出错误，否则接受（下溢得 `0.0`/次正规）。
- `\u{...}`/`\uNNNN`/`\xNN` 的码点**范围**合法性（surrogate、`>0x10FFFF`）留待 compiler 解码期；scanner 仅校验转义语法（14.3）。
- 转义错误 caret 指向转义序列（列 = token 起始列 + 序列内偏移）；未终止字符串 caret 指向开引号、span 至换行/EOF。
- 字符串内坏转义后整串丢弃并记一条错误，快进至终止符（引号/换行/EOF）；中途再遇坏转义不重复报告。

### 15.3 验证与测试

- `run-clang-tidy` 0 告警（Google 配置）；`clang-format -style=file` 通过；`ninja` 构建无告警。
- C++ 单元测试已落地（2026-07-31，见 §16）：`tests/scanner_golden_test`（gtest + FetchContent）对 `tests/lox-source/**/*.lox` 逐文件运行真实 `lox-cpp --run-stage scanner`，比对 stdout/stderr/exit code 与 golden（`tests/lox-source-out/scanner/` 镜像布局）；`--update` 重新生成 golden。此前以临时驱动（`/tmp`，未入库）对全场景 smoke test：关键字/标识符、各进制数值、指数非提交、下划线错误、int64 溢出、float 溢出/下溢、转义校验（合法/非法/未终止）、注释、CRLF 行号、多错误累积、空源、API 契约（可移动不可拷贝/可重入/Reset/错误恢复）。

### 16. CLI 与 golden 测试落地（2026-07-31）

- `src/main.cpp`：`llvm::cl` CLI（`--run-scanner-only` 布尔开关 + 位置参数 `<input file>`，`ParseCommandLineOptions` 传 `errs()`，错误/`--help` 处理见 LLVM 惯例）。模式分派：无位置参数 → interpret（REPL，未实现：打印 `interpret mode is not implemented` + `assert(false)`）；有文件无 flag → interpret 文件模式（同上 stub）；`--run-scanner-only <file>` → scanner 模式：`MemoryBuffer::getFile`（失败 stderr + exit 2）→ `Scanner::Scan()` 成功逐 token 打印 stdout + exit 0，`LexicalError` 经 `log(errs())` + exit 1。
- token 行格式：`<line>:<col> <TypeName> <lexeme>`（经 `Token::Print`），kInteger/kFloat 追加 ` [<value>]`（`std::to_chars` 最短回程；double 如 `1e10` → `1e+10`、`2.0` → `2`）；lexeme 经 `llvm::printEscapedString`（`"` → `\22` 等）；EOF 行也打印。
- **注释 bug 修复**：`ScanToken` 的 `case '/'` 原本直接发 `kSlash`，`//` 注释处理仅在 `SkipWhitespaceAndComments`（空白分支可达），注释从未被识别。修复为 `case '/'` 内 `Peek(1) == '/'` 时转 `SkipWhitespaceAndComments`，恢复 §14.2 契约（`//` 行注释不产生 token）。golden 审查（`comments.lox`）发现，经用户确认修复。
- CMake：顶层 `enable_testing()` + `add_subdirectory(tests)`；`tests/CMakeLists.txt` FetchContent googletest（v1.15.2 固定 zip）+ `gtest_discover_tests`；`scanner_golden_test` 经 `target_compile_definitions` 注入二进制/仓库/源/golden 根路径（discovery 阶段无 argv，路径须编译期已知）。
- 测试驱动：静态初始化遍历 `tests/lox-source` 收集 `.lox`（守卫测试防空遍历静默通过）；`ExecuteAndWait` + 临时文件重定向捕获子进程两流；期望 exit code 由 golden `.err` 非空与否推导（空 → 0，非空 → 1，文件读取失败 exit 2 不出现于既有测试）。

### 17. CLI 重构：--run-stage 分派（2026-08-01）

- `--run-scanner-only` 布尔开关替换为 `--run-stage` 枚举选项（`llvm::cl::opt<Stage>` + `clEnumValN`，枚举常量 `kScanner`/`kParser`/`kBytecode`/`kExecute`，匿名 namespace）：取值 `scanner`（输出 Token 流）/ `parser`（导出 AST）/ `bytecode`（输出字节码）/ `execute`（产生字节码并执行），默认 `execute`；非法值由 `ParseCommandLineOptions` 拒绝（stderr + exit 1），`--help` 列出四值（LLVM 惯例）。
- 分派矩阵：
  - `kExecute`（默认，覆盖无 flag 情形）→ interpret 模式（无文件为 REPL）：stderr `interpret mode is not implemented` + exit 1。execute ≡ 整条流水线（parse → bytecode → run）≡ interpret，消息统一；**删除原 interpret stub 的 `assert(false)`**。有文件无 flag 同走此路径（行为不变）。
  - `kParser` / `kBytecode` → stderr `<stage> stage is not implemented` + exit 1；**stub 不碰输入**（不读文件也不读 stdin）。
  - `kScanner` → 惰性加载输入：共享 helper `LoadInput`（`InputFile.empty() ? MemoryBuffer::getSTDIN() : MemoryBuffer::getFile`），仅 scanner 分支调用；文件读取失败 stderr `failed to open '<file>'` + exit 2，stdin 读取失败 `failed to read stdin` + exit 2；stdin 诊断标签 `<stdin>`（错误形如 `<stdin>:1:5: error: ...`）。`RunScanner` 签名改为 `(source, label)`。
- `--run-stage` 与位置参数 `<input file>` 相互独立：无文件时 scanner/parser/bytecode 读 stdin（parser/bytecode 未实现），execute 进 interpret 模式。
- 测试：`scanner_golden_test` 驱动改 `--run-stage scanner`（args 追加 `"scanner"`），golden 文件不变（token 输出与旧 flag 一致）。

### 18. 交互式输入：scanner REPL（2026-08-01）

- `--run-stage scanner` 无文件且 stdin 为终端（`llvm::sys::Process::StandardInIsUserInput()`）时，从批量 `getSTDIN` 改为交互式逐行扫描：每行前 `llvm::outs()` 打印 prompt `(lox-cpp) >> `（尾随空格）并 flush，`std::getline` 读一行，逐行独立 `Scan` 并输出该行完整 token 流（含 kEof）。EOF（Ctrl-D）正常退出（exit 0）。
- 行内词法错误：stderr 报错（标签 `<stdin>`，行号恒 1）后继续循环，错误不影响最终退出码。
- stdin 为管道/重定向时保持批量路径不变（整读一次扫描）；有文件模式不变。交互循环为通用 `RunInteractiveLoop(handler)`（prompt + 读行 + 逐行分派），未来 interpret/parser 阶段可复用，handler 仅提供"每行处理"。
