#import "@local/rin-template:0.2.0": conf

#show: conf.with(
  title: "Lox 语法结构与语义",
  course: "lox-cpp — Crafting Interpreters 实现",
  author: "lox-cpp Project",
)

= 引言

Lox 是一种由 Robert Nystrom 在 *Crafting Interpreters* 中设计的小型脚本语言，
兼具 C
风格语法与面向对象编程的特性（类、继承、多态）。本文档对原本的Lox语言进行了扩展使其符合教学需求并兼顾一定的实用性。

本文档从 Lox 的语法形式定义出发，系统阐述其词法结构、语法规则、
语义模型及运行时执行方式。

= 词法结构

Lox 的词法文法（Lexical Grammar）是正则文法，由扫描器（scanner）
将源代码的字符序列转换为标记流。

== 字符集

Lox 的标识符允许 ASCII 字母、数字与下划线，数字不能开头：

```lox-grammar
DIGIT     -> "0"–"9"
ALPHA     -> "a"–"z" | "A"–"Z" | "_"
IDENTIFIER -> ALPHA (ALPHA | DIGIT)*
```

== 字面量

- 数字：十进制整数或浮点数，`INTEGER -> DIGIT^+`、`FLOAT -> DIGIT^+ "." DIGIT^+`
- 字符串：双引号包裹，支持转义。转义序列：`\n`（换行）、`\r`（回车）、`\t`（制表）、`\\`（反斜杠）、`\"`（双引号）、`\0`（空字符）、`\xNN`（单字节十六进制）、`\uNNNN`（4位十六进制
  BMP 码点）、`\u{...}`（变长十六进制全 Unicode
  码点）。`STRING -> "\"" <any char except "\"">* "\""`
- 布尔：`true`、`false`
- 空值：`null`

== 关键字

Lox 保留以下关键字（不可作为标识符）：

#grid(
  columns: (1fr, 1fr, 1fr),
  align: (left, left, left),
  [`class`, `else`, `false`, `for`, `fun`, `if`, `null`, `return`, `super`,
    `this`, `true`, `while`, `extends`, `import`, `try`, `catch`, `throw`,
    `break`, `let`, `const`, `as`],
)

+ 布尔类型：`true`、 `false`
+ 空值：`null`
+ 控制流：`if`、`else`、`while`、`for`、`break`
+ 异常控制流：`try`、`catch`、`throw`
+ 变量声明：`let`、`const`
+ 函数：`fun`、`return`
+ 面向对象编程：`class`、`extends`、`this`
+ 模块系统：`import`、`as`

== 运算符

Lox 的运算符包含算术，逻辑与位运算符。

=== 单目运算符

- `!` — 逻辑非，要求操作数为 `Boolean`，否则抛出运行时异常
- `-` — 算术取反
- `~` — 按位取反，要求操作数为整数

=== 双目运算符

按优先级从低到高：

- 逻辑短路：`||`（LogicalOr）、`&&`（LogicalAnd）
- 位运算（整数专用）：`|`（BitOr）、`^`（Xor）、`&`（BitAnd）
- 等式：`==`（Equal）、`!=`（NotEqual）
- 比较：`>`（Greater）、`>=`（GreaterEqual）、`<`（Less）、`<=`（LessEqual）
- 移位：`<<`（LeftShift）、`>>`（RightShift）
- 算术：`-`（Minus）、`+`（Plus）、`/`（Divide）、`*`（Multiply）

位运算操作数必须为整数类型，否则抛出运行时异常。

= 语法结构

Lox 的语法是上下文无关文法，按照优先级分解为多个层次。
解析器（parser）采用递归下降方式将标记流构造为 AST。

== 声明层（Declarations）

声明语句会绑定新标识符，或作为其他语句的包装：

```
program    -> declaration* EOF ;

declaration -> classDecl
            | funDecl
            | varDecl
            | constDecl
            | importDecl
            | statement ;

classDecl -> class IDENTIFIER (extends IDENTIFIER)? "{" function* "}"

funDecl -> fun IDENTIFIER "(" parameters? ")" block

varDecl -> let IDENTIFIER "=" expression ";"

constDecl -> const IDENTIFIER "=" expression ";"

importDecl -> import STRING (as IDENTIFIER)? ";"
```

- 变量和常量声明必须附带对应的定义
- 类声明通过extends关键字实现单继承

== 语句层（Statements）

语句执行副作用，不产生值：

```
statement -> exprStmt | forStmt | ifStmt
          | returnStmt | whileStmt | tryStmt | breakStmt | block ;

exprStmt -> expression ";"

forStmt    -> "for" "(" ( varDecl | exprStmt | ";" )
                        expression? ";" expression? ")" block ;
- `forStmt`：初始化器可以是变量声明（`let`）、表达式语句或空（直接写 `;`），条件表达式和递增表达式均可省略

ifStmt     -> "if" "(" expression ")" block
             ("else" block)? ;

returnStmt -> "return" expression? ";" ;

whileStmt  -> "while" "(" expression ")" block ;

tryStmt -> "try" block ("catch" IDENTIFIER IDENTIFIER? block)+ ;

breakStmt -> "break" ";" ;

block      -> "{" declaration* "}" ;
```

- `exprStmt`：表达式后加分号，用于其副作用
- `block`：创建新的作用域，所有变量和常量均只在作用域内有效，在内层作用域定义的函数和类方法可以捕获外层的变量（闭包语义）
- 控制流语句体均要求使用block包裹内层语句
- try-catch语句指定捕获的类型（可选捕获对应的对象），多个catch子句按声明顺序依次匹配，第一个类型匹配的子句生效
- `breakStmt`：跳出最近一层循环（`while` 或 `for`），在循环体外使用为编译时错误
- `returnStmt`：从函数返回值，默认返回 `null`

== 表达式层（Expressions）

表达式产生值，按优先级从低到高分层。每层规则引用下一层， 避免文法二义性：

```
expression -> assignment ;

assignment -> IDENTIFIER "=" assignment
           | call "." IDENTIFIER "=" assignment
           | call "[" expression "]" "=" assignment
           | logic_or ;

logic_or   -> logic_and ("||" logic_and)* ;
logic_and  -> bitwise_or ("&&" bitwise_or)* ;
bitwise_or -> bitwise_xor ("|" bitwise_xor)* ;
bitwise_xor -> bitwise_and ("^" bitwise_and)* ;
bitwise_and -> equality ("&" equality)* ;
equality   -> comparison (("!=" | "==") comparison)* ;

comparison -> shift ((">" | ">=" | "<" | "<=") shift)* ;

term       -> factor (("-" | "+") factor)* ;
shift      -> term (("<<" | ">>") term)* ;
factor     -> unary (("/" | "*") unary)* ;

unary      -> ("!" | "-" | "~") unary | call ;
call       -> primary ("(" arguments? ")" | "." IDENTIFIER | "[" expression "]")* ;
primary    -> "true" | "false" | "null" | "this"
           | INTEGER | FLOAT | STRING | IDENTIFIER | "(" expression ")"
           | "super" "." IDENTIFIER
           | listLiteral ;
```

优先级汇总（从高到低）：

#table(
  columns: (auto, auto, auto),
  [*层级*], [*运算符*], [*结合性*],
  [`primary`], [字面量、变量、分组、列表、`this`、`super.method`], [—],
  [`call`], [`()` `.` `[]`], [左结合],
  [`unary`], [`!` `-` `~`], [右结合],
  [`factor`], [`/` `*`], [左结合],
  [`term`], [`-` `+`], [左结合],
  [`shift`], [`<<` `>>`], [左结合],
  [`comparison`], [`>` `>=` `<` `<=`], [左结合],
  [`equality`], [`==` `!=`], [左结合],
  [`bitwise_and`], [`&`], [左结合],
  [`bitwise_xor`], [`^`], [左结合],
  [`bitwise_or`], [`\|`], [左结合],
  [`logic_and`], [`&&`], [左结合],
  [`logic_or`], [`||`], [左结合],
  [`assignment`], [`=`], [右结合],
)

== 辅助规则

```
function     -> IDENTIFIER "(" parameters? ")" block ;
parameters   -> IDENTIFIER ("," IDENTIFIER)* ;
arguments    -> expression ("," expression)* ;
listLiteral  -> "[" (listElements)? (";" expression)? "]" ;
listElements -> expression ("," expression)* (",")? ;
```

= 语义模型

== 类型系统

Lox 是动态类型语言，值在运行时携带类型标签。

- `Null` — 空值
- `Boolean` — 布尔值
- `Integer` — 64位整数类型
- `Float` — 双精度浮点数
- `Object(String | Function | Closure | Class | Instance | Native | Upvalue | BoundMethod | List)` /
  `GcPtr<T>`

对象类型通过引用技术和垃圾回收器两层进行内存管理。

== 类与继承

Lox 的面向对象模型：

- 类声明定义构造器和实例方法
- `this` 关键字引用当前实例
- `init()` 是构造器方法，调用时自动返回实例
- 单继承通过 `extends` 操作符指定父类
- `super` 关键字用于调用父类方法

```
class Animal { speak() { return "..."; } }
class Dog extends Animal {
  speak() {
    super.speak();
    return "Woof!";
  }
}
```

== 运行时语义

=== 作用域与绑定

程序执行过程中，标识符的可见性由词法作用域（lexical scoping）决定：

- 每个 `block`（`{ }`）创建新的嵌套作用域，内层可访问外层定义的标识符
- 内层可用同名标识符遮蔽（shadow）外层，遮蔽持续到该作用域结束
- `let` 声明的变量可在同一作用域内重新赋值，`const` 声明的常量不可重新赋值
- 对 `const` 绑定的复合类型（对象、数组），其内部状态仍可修改——`const`
  仅保护绑定本身
- 未声明直接赋值的标识符为编译时错误

=== 求值模型

Lox 采用严格求值（strict
evaluation）：表达式从左到右求值，所有子表达式在进入外层运算前计算完毕。

逻辑运算符 `||`、`&&` 与 `!` 要求操作数为 `Boolean`
类型，否则抛出运行时异常。`||` 和 `&&` 执行短路求值：

- `a || b`：先求值 `a`（必须为 `Boolean`），若为 `true` 则短路返回
  `true`，否则求值 `b`（必须为 `Boolean`）并返回其结果
- `a && b`：先求值 `a`（必须为 `Boolean`），若为 `false` 则短路返回
  `false`，否则求值 `b`（必须为 `Boolean`）并返回其结果

条件表达式（`if`、`while`、`for` 的条件部分）必须求值为
`Boolean`，否则抛出运行时异常。不进行隐式真值推断。

算术类型规则：

- 两个 `Integer` 运算产生 `Integer`（整除 `/` 也产生 `Integer`）
- 任一操作数为 `Float` 则将对方提升为 `Float`，结果为 `Float`
- 位运算符仅接受 `Integer`，否则抛出运行时异常

`==` 和 `!=` 对基本类型比较值相等性（value
equality），对对象类型比较引用相等性（reference equality）。

=== 赋值

`assignment` 将右操作数的值绑定到左操作：

- 简单赋值 `x = v` 在当前作用域向上查找 `x`
  的声明并写入——若所有外层均无声明则为编译时错误
- 属性赋值 `a.b = v`：若 `a` 为类实例则设置字段，否则运行时异常
- 下标赋值 `a[i] = v`：见列表与下标节
- 赋值表达式的值等于右操作数的值，可链式赋值：`a = b = c`

=== 列表与下标

列表是定长、元素可变的原生复合类型。列表字面量 `[ ... ]` 在求值时创建新的 `List`
对象。

创建语义：

- `[e1, e2, e3]`：元素从左到右依次求值，列表长度 = 元素个数
- `[e1, e2; N]`：先求值 `e1, e2`，再求值 `N`。若 `N < 2` 则截断为前 `N`
  个元素，若 `N > 2` 则末尾填充 `null`
- `[; N]`：长度为 `N`，全部元素初始化为 `null`
- `[]`：空列表，长度 0
- `N` 求值必须为 `Integer` 且 `>= 0`，`N < 0` 时抛运行时异常

下标访问 `a[i]`：

- 若 `a` 为 `List`：`i` 必须为
  `Integer`，`0 <= i < len(a)`，否则运行时异常；返回元素值
- 若 `a` 为类实例且实现了 `__getitem__` 方法：调用 `a.__getitem__(i)`
- 其它：运行时异常

下标赋值 `a[i] = v`：

- 若 `a` 为 `List`：`i` 必须为
  `Integer`，`0 <= i < len(a)`，否则运行时异常；写入 `v`
- 若 `a` 为类实例且实现了 `__setitem__` 方法：调用 `a.__setitem__(i, v)`
- 其它：运行时异常

下标不支持负索引（`a[-1]` 抛运行时异常）。列表创建后长度不变，不允许 `a[i]`
追加新元素。

=== 控制流

- `if`：条件表达式必须求值为 `Boolean`，为 `true` 时执行 `then`
  分支，否则执行可选的 `else` 分支
- `while`：条件表达式必须求值为 `Boolean`，为 `true`
  时循环执行体，每次迭代前重新求值
- `for`：等价于 `{ 初始化器; while (条件) { 体; 递增表达式; } }`，初始化器中用
  `let` 声明的变量作用域限于循环体
- `break`：立即退出最近一层 `while` 或 `for` 循环，在循环体外使用为编译时错误

=== 函数与调用

- 函数（`fun`）是一等值：可赋值给变量、作为参数传递、作为返回值
- 调用机制为传值调用（call by value）：所有实参从左到右求值，值拷贝后传入函数体
- 参数数量与形参数量不符为运行时错误
- `return` 立即退出当前函数，`return` 无表达式时返回
  `null`；函数执行到末尾隐式返回 `null`
- 函数捕获其定义时所在作用域的变量（闭包语义），捕获为引用语义——外部赋值对闭包可见

=== 异常处理

- `throw` 可以抛出任意类型的值（不限于异常对象）
- `try` 块中的异常沿调用栈向上传播，直到被匹配的 `catch` 子句捕获
- 多个 `catch`
  子句按声明顺序依次匹配异常值的运行时类型，第一个类型匹配的子句生效
- 未捕获的异常传播到顶层，导致程序终止并输出错误信息

=== 模块系统

- `import` 按入口脚本的相对路径解析模块文件
- 每个模块在首次导入时编译并执行，后续导入直接返回缓存的模块对象
- `as` 为导入的模块指定本地别名：`import "foo" as bar` 将模块绑定到 `bar`

= 执行模型

== 编译流水线

lox-cpp 的编译流水线分为两个阶段：

*第一阶段（前端）*：源码文本到 AST

- `Scanner` 将源码字符序列转换为 `Token` 流（词法分析），同时填充字面量值
- `Parser` 采用递归下降算法，将 `Token` 流构造为
  AST（语法分析），同时报告语法错误

*第二阶段（后端）*：AST 到字节码

- `Compiler` 对 AST 进行单次深度优先遍历，为每个节点生成对应的字节码序列
- 编译过程中填充常量池：字面量去重、标识符驻留
- 每条字节码指令由 `opcode` +
  可选操作数组成，操作数通常为常量池索引或局部变量槽位

编译错误（语法错误、未声明变量、break
在循环外等）在对应阶段报告并阻止后续阶段执行。

== 对象表示

=== 运行时值

Lox 的值分为两类：

- *值类型*（栈上，按值传递）：`Null`、`Boolean`、`Integer`（i64）、`Float`（f64）
- *引用类型*（GC 堆分配，通过 `GcPtr<T>`
  引用）：`String`、`Function`、`Closure`、`Class`、`Instance`、`Native`、`Upvalue`、`BoundMethod`

VM 操作数栈上统一存储 `Value`，按 tag 区分类型。

=== 各变体的数据组成

- `String`：驻留字符串的指针（见字符串驻留节）
- `Function`：编译产出的纯代码对象——字节码序列、常量池、形参数量、函数名
- `Closure`：`GcPtr<Function>` +
  `llvm::SmallVector<GcPtr<Upvalue>, kUpvalueInlineSize>`。每次函数字面量求值时创建新的
  `Closure`
- `Upvalue`：对外层变量槽位的间接引用。内层函数与外层栈帧通过同一个 `Upvalue`
  共享变量 ——外层仍在执行时 `Upvalue` 指向栈槽；外层返回后 `Upvalue`
  将值搬移到自身的堆存储中
- `Class`：类名、`GcPtr<Class>`
  父类（可选）、`llvm::SmallDenseMap<GcPtr<String>, GcPtr<Closure>, kMethodTableInlineSize>`
  方法表
- `Instance`：`GcPtr<Class>` 所属类 +
  `llvm::SmallDenseMap<GcPtr<String>, Value, kFieldTableInlineSize>` 字段表
- `BoundMethod`：`GcPtr<Instance>` 接收者 + `GcPtr<Closure>` 方法。`obj.method`
  求值时创建
- `Native`：函数指针 +
  函数名。由外部库注册的内建函数载体；核心语言不预置任何内建函数
- `List`：`llvm::SmallVector<Value, 0>` 定长元素数组 +
  长度。创建后长度不可变，元素可读写

上述 `llvm::SmallVector` 与 `llvm::SmallDenseMap` 的内联容量参数为实现期可调的 `constexpr`
常量，当前暂定值如下：

#table(
  columns: (auto, auto, auto),
  [*常量名*], [*暂定值*], [*用途*],
  [`kUpvalueInlineSize`], [`4`], [`Closure` upvalue 内联容量],
  [`kMethodTableInlineSize`], [`8`], [`Class` 方法表内联容量],
  [`kFieldTableInlineSize`], [`4`], [`Instance` 字段表内联容量],
)

=== 引用关系图

Closure ──→ Function（代码） └──→ Upvalue[]（捕获的变量）

Instance ──→ Class └──→ fields（字段值表）

Class ──-> superclass（父类） └──-> methods: llvm::SmallDenseMap\<GcPtr\<String\>,
GcPtr\<Closure\>, kMethodTableInlineSize\>

BoundMethod ──→ Instance (this) └──→ Closure (method)

List ──-> elements: llvm::SmallVector\<Value, 0\>（定长，创建后不可变）

== 字符串驻留


所有运行时创建的字符串写入全局驻留表（`llvm::StringMap<GcPtr<String>>`）。相同内容的
字符串共享同一份 `GcPtr<String>` 存储：

- 字符串字面量在编译阶段驻留
- 运行时字符串拼接、`toString()` 等产生的新字符串也经驻留表去重
- 驻留使 `==` / `!=` 对字符串的比较退化为指针比较
- 方法名、字段名作为驻留字符串存储，`Class` 方法表的 `llvm::SmallDenseMap`
  键比较退化为 指针比较
- 驻留表是 GC 根集的一部分，驻留字符串永不被回收

== 方法分发

每个 `Class` 对象持有一个
`llvm::SmallDenseMap<GcPtr<String>, GcPtr<Closure>, kMethodTableInlineSize>` 方法表。
方法调用 `obj.method(args)` 的执行路径：

. 从 `obj` 取出 `Instance.class` . 以 `"method"`（驻留字符串）为键查找方法表 .
命中：创建 `BoundMethod { this: obj, method: found_closure }` 并调用 .
未命中：沿 `class.superclass` 链向上查找，直到找到或到达 `null`（根类） .
仍未找到：抛出运行时异常

`super.method(args)` 从当前类的父类开始查找，跳过自身。

== 模块加载

`import "path" as alias;` 的执行流程：

. 按入口脚本的相对路径解析 `"path"` → 文件系统路径（尝试 `.lox` 后缀和目录） .
查全局模块缓存 `llvm::StringMap<ModuleObject>`：
- 命中 → 跳到步骤 6
- 缓存中标记为 *加载中* → 循环导入，返回当前部分缓存（防止无限递归）
. 未命中：创建模块缓存条目（标记为加载中），启动模块编译流水线 . 创建独立的 VM
调用帧（共享 GC 堆），执行模块的顶层语句 . 将模块顶层声明的标识符打包为
`ModuleObject`，写入缓存 . 如果有 `as alias`，在当前作用域将 `ModuleObject`
绑定到 `alias`

模块缓存由 GC 根集引用，确保已加载模块不被回收。

== 字节码虚拟机

lox-cpp 的 VM 是基于栈的字节码解释器：

- `chunk`：字节码序列 + 常量池 + 行号信息
- `opcode`：所有指令的枚举定义
- 执行循环依次读取并译码指令（fetch-decode-execute）
- 操作数栈存放中间计算结果
- 调用栈（call frame）管理函数调用和返回

编译器（compiler）进行单次 AST 遍历，为每个表达式/语句生成对应的字节码序列。

== 运行时错误

lox-cpp 的运行时错误采用 `llvm::Expected<T>` / `llvm::Error`
传播机制，不使用异常：

- VM 执行循环中每条指令返回 `llvm::Error`
- 任何操作失败（类型不匹配、栈溢出、除零等）立即生成 `RuntimeError` 并返回
- 错误沿调用栈向上传播：每层调用帧检测到错误后立即退出，将错误传递给上一层

`RuntimeError` 继承自 `llvm::ErrorInfo<RuntimeError>`，通过
`llvm::make_error<RuntimeError>(...)` 构造，封装于 `llvm::Error` / `llvm::Expected<T>`
中传播。`llvm::Error` 为类型擦除的错误载体，可持有任意 `llvm::ErrorInfo` 子类；VM 中仅产生
`RuntimeError`，可通过 `isA<RuntimeError>()` 进行类型检查。

`throw` 语句映射为 `llvm::make_error<RuntimeError>(...)`，其中包含抛出时栈上的实际值。

`try-catch` 的实现：

- 编译时在调用帧元数据中记录每个 `catch` 子句的字节码偏移和捕获类型
- 运行时遇错时沿调用栈回退，在每个帧检查是否有匹配的 `catch` handler
- 匹配规则：遍历当前帧的所有 handler，比较捕获类型与错误的运行时类型
  ——第一个类型匹配的 handler 生效，跳转到对应字节码偏移
- 若当前帧无匹配 handler，继续向上层传播
- 错误传播到顶层（主脚本调用帧）且无 handler 匹配时，输出错误信息并终止程序

错误信息包含：异常值（或错误描述）、栈轨迹（函数名 + 行号列表）。

== 垃圾回收

lox-cpp 采用三色标记-清除（tri-color mark-sweep）GC：

- `heap`：内存分配与 GC header 管理
- `collector`：从根集（栈 + 全局变量 + 调用帧 + upvalues + 模块缓存 +
  字符串驻留表） 出发进行根扫描和可达性追踪
- 每次 GC 周期标记所有可达对象，然后回收不可达对象的存储

== JIT 编译

lox-cpp 支持 JIT：

- `profiler`：运行时收集热点代码和类型反馈
- `codegen`：将解释器字节码翻译为 LLVM IR，可多级优化（O0–O3）
- `osr`（On-Stack Replacement）：对热点循环进行栈上替换， 无缝从解释器迁移到 JIT
  编译代码
- 去优化（deoptimization / bailout）：在类型假设失效时回退到解释器
