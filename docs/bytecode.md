# Bytecode 模块设计决策

本文档记录 bytecode 模块（AST -> 字节码）的设计决策，供后续实现（compiler 本体、opcode 定义、Chunk/Function/Module 数据结构、CMake 接入、测试）参考。设计基线来自 `docs/lox-language.typ` 的「执行模型」一节；本文档不重新讨论已由语法文档锁定的决策，仅记录 bytecode 模块范围内的开放决策及其结论。

## 1. 模块定位

Bytecode 模块是编译后端：消费 `lox::parser` 产出的 AST，单遍深度优先遍历，为每个函数生成字节码序列（`Chunk`），填充常量池，产出可由 VM 执行的 `Function` / `Module` 对象图。编译器运行时**已具备 GC 堆与字符串驻留表**（语法文档规定字符串字面量在编译阶段驻留），故常量池直接持有已物化的运行时 `Value`，无需序列化层。

## 2. 设计基线（语法文档已锁定）

以下决策由 `docs/lox-language.typ`「执行模型」一节钉死，bytecode 模块遵循之，不重新讨论：

- 栈式字节码 VM。
- `chunk` = 字节码序列 + 常量池 + 行号信息。
- `opcode` 枚举。
- 操作数栈 + 调用帧（call frame）。
- 单遍 AST -> 字节码编译器。
- 运行时错误走 `llvm::Error` / `llvm::Expected<T>`，`RuntimeError : llvm::ErrorInfo`。
- 值/引用类型拆分：值类型（Null/Boolean/Integer i64/Float f64）栈上；引用类型经 `GcPtr<T>` 引用。
- 闭包经 `Upvalue` 引用捕获。
- try-catch：catch 子句（偏移、捕获类型）记入调用帧元数据。
- 字符串在编译期驻留。
- `Function` = 字节码 + 常量池 + 形参数量 + 函数名。
- `Closure` = `GcPtr<Function>` + upvalue 数组；函数字面量求值时新建。

## 3. 编译单元结构

**决策：逐函数 chunk，嵌套函数经父级常量池引用；顶层为 `<script>` Function；`Module` 包装 script + 模块元数据。**

每个源文件编译为一个 `Module`，其顶层代码是一个名为 `<script>`、arity 0 的 `Function`；函数字面量编译成子 `Function`，作为常量存入**父函数**的常量池。运行时由 `CLOSURE` 指令取出 Function 常量、捕获 upvalue、生成 `Closure`。每个 Function 独立拥有自己的 code / 常量池 / 行号 / upvalue 描述 / 异常表 / 最大栈深。

否决方案：

- **单体 Module（共享 code + 共享常量池）**：偏离语法文档 `Function` 定义（纯代码对象），异常表/upvalue 描述/调用帧均 per-function，共享 code 需额外切函数边界；全局常量池拖累 JIT 逐函数翻译。
- **顶层独立 `Script` 类型**：`<script>` 与普通函数仅差 arity=0、name=`<script>`，第三种类型徒增分派分支。

依据：语法文档 `Function` 定义已是「字节码 + 常量池 + 形参数量 + 函数名」，逐函数 chunk 直接复用；`Closure` 语义要求 Function 是可被常量池持有、可被 `CLOSURE` 引用的值；异常表/upvalue 描述天然 per-function；JIT 以 Function 为编译单元，per-function chunk 边界清晰。

## 4. Value 表示

**决策：tagged union `struct Value { ValueType tag; union {...} as; }`，值类型各占独立槽，引用类型共享 `GcPtr<GcObject>` 槽。**

```cpp
enum class ValueType : std::uint8_t {
  kNull, kBoolean, kInteger, kFloat,                  // 值类型（栈上）
  kString, kFunction, kClosure, kClass, kInstance,    // 引用类型（GC 堆）
  kNative, kUpvalue, kBoundMethod, kList, kModule,
};
struct Value {
  ValueType tag;
  union {
    bool            boolean;
    std::int64_t    integer;
    double          floating;
    GcPtr<GcObject> obj;   // 所有引用类型共享，按 tag 向下 cast
  } as;
};
```

- `std::monostate` 无需：`kNull` 由 tag 表达，联合体不参与。
- 所有引用类型共享 `GcPtr<GcObject>` 一个联合体槽（GC 对象共享基类 header），避免联合体膨胀，GC 根扫描统一取 `obj`。
- 值类型 `i64`/`f64` 免装箱。
- `switch(tag)` 无 `default`，保持 `-Wswitch` 武装（与 AST/token 风格一致）。

否决方案：

- **NaN-boxing**：8B 紧凑但位操作易错、调试困难、GC 根扫描需 unbox；WIP 阶段正确性优先。
- **低位 tag 指针**：`double` 需堆装箱，对算术/位运算密集语言不可接受。

依据：语法文档「VM 操作数栈上统一存储 `Value`，按 tag 区分类型」最直接的读法是显式 tag 字段；项目 WIP，可调试性优先；GC 三色标记从操作数栈/常量池/upvalue 扫根，裸 `GcPtr` 最简；JIT 翻译为 LLVM IR 时 tagged union 映射为简单 `{i8, union}`。约 16B 的代价对教学解释器可忽略，将来 profiling 指明瓶颈可降级为 NaN-boxing（Value 是孤立叶子类型，局部改动）。

## 5. 常量池

**决策：单一内存 `Value` 池，常量种类 = `{Integer, Float, String(驻留), Function}`；标识符名称与字面量共用同一池；Integer/Float/String 按值去重，Function 不去重；布尔/null 走 opcode。**

```cpp
class Chunk {
  llvm::SmallVector<Value, 0> constants_;          // 常量池
  llvm::SmallVector<std::uint8_t, 0> code_;        // 字节码
  llvm::SmallVector<LineEntry, 0> lines_;          // 行号（见 §11）
  std::uint16_t AddConstant(Value v);              // 去重后返回索引
};
```

| 常量种类 | Value tag | 去重 | 用途 |
|---|---|---|---|
| Integer | kInteger | 按值 | 整数字面量 |
| Float | kFloat | 按值 | 浮点字面量 |
| String | kString | 指针（驻留） | 字符串字面量 + 所有标识符名称 |
| Function | kFunction | 不去重 | 嵌套函数字面量（供 `CLOSURE` 引用） |

- 名称与字面量共用池 = 单一索引空间，需要名称的指令（`DEFINE_GLOBAL`/`GET_PROPERTY`/`METHOD` 等）与需要字面量的指令（`CONSTANT`）用同一种「常量池索引」操作数。
- 去重几乎免费：驻留字符串内容唯一 -> 指针比较即去重；i64/f64 按值比较；Function 每个字面量语义独立不去重。
- 布尔/null 高频，单字节 `PUSH_TRUE`/`PUSH_FALSE`/`PUSH_NULL` 比「CONSTANT + 池索引」更短更快且不占池容量。

否决方案：

- **可序列化描述符池**：与 §4 tagged union `Value`（带 `GcPtr`，不可平坦序列化）不一致，需额外 schema + 反序列化器；语法文档未要求 bytecode 落盘，`import` 命中失败时从源码重编。
- **分池（按类型独立索引空间）**：opcode 多一维「哪个池」，复杂度高、省一个 tag 字节对教学语言无意义。

## 6. 指令编码

**决策：1B opcode + 按类别定宽操作数 + 相对跳转 + 无 long 变体。**

- opcode：1B（`uint8` 枚举，最多 256 条）。
- 局部槽 / 参数 / upvalue 索引：1B（u8，≤256）。
- 常量池索引：2B（u16，≤65535）。
- 跳转偏移：2B（int16 有符号，±32767），**相对寻址**（基准 = 跳转指令下一条的 PC）。
- 操作数宽度由 opcode 决定，译码固定；无 per-instance 长度前缀；无 `*_LONG` 变体。

| 形态 | 字节数 | 示例 |
|---|---|---|
| `[op]` | 1 | `ADD` `POP` `PUSH_NULL` `RETURN` `CLOSE_UPVALUE` |
| `[op][u8]` | 2 | `LOAD_LOCAL slot` `STORE_LOCAL slot` `GET_UPVALUE idx` `CALL arity` |
| `[op][u16]` | 3 | `CONSTANT idx` `GET_GLOBAL idx` `DEFINE_GLOBAL idx` `JUMP off` `LOOP off` |
| `[op][u16][u8]` | 4 | `SUPER_INVOKE name arity` |

- 跳转基准：偏移相对「跳转指令下一条指令的 PC」。前向 `JUMP +N` 跳过 N 字节；后向 `LOOP -N` 回到循环头。
- 字节序：内存对象不落盘，native 字节序，封装 `ReadU16`/`WriteU16` 助手隔离。
- 变长尾随：`CLOSURE`（func_idx u16）的 upvalue 捕获描述存 Function 元数据（见 §8），不内联 code，故 CLOSURE 保持统一 `[u16]`。

否决方案：

- **clox 式（long 变体 + 绝对跳转）**：常量 1B + `CONSTANT_LONG` 变体，超 256 常量需偏移跟踪与跳转修补；绝对跳转不利 JIT 重定位。
- **全 2B 操作数**：无操作数指令之外都胖 1B，体积无谓膨胀。

依据：u16 常量索引（65535）远超任何 lox 函数需求，省 long 变体的编译器复杂度；相对跳转直接映射为 LLVM IR 相对分支，代码块可重定位，为 JIT/OSR 铺路；u8 局部槽（256）对 lox 绰绰有余。

## 7. 变量解析

**决策：三层模型--局部（函数+块作用域）= 编译期栈槽；闭包 = upvalue 描述 `{is_local, index}`；模块级变量 = 命名字符串表运行期 late-bind；`const` = 编译期 per-binding 标记；`this` = 方法函数 slot 0。**

| 标识符出现位置 | 编译期动作 | 生成指令 |
|---|---|---|
| 函数/块内声明的 `let`/`const`/参数 | 分配栈槽，记 const 位 | `LOAD_LOCAL`/`STORE_LOCAL`（u8 槽） |
| 引用外层**函数**作用域的局部 | 记 upvalue `{is_local, idx}` | `GET_UPVALUE`/`SET_UPVALUE`（u8） |
| 引用模块顶层变量 | 不解析槽位，按名 | `GET_GLOBAL`/`SET_GLOBAL`（u16 名索引） |
| `this`（方法体内） | 即 slot 0 | `LOAD_LOCAL 0` |
| 块结束 | 释放该块槽位，弹出作用域 | `POP` N 次 |

- **作用域栈**：编译器维护词法作用域栈，每层记录 `{name -> (slot, is_const)}`；遮蔽 = 内层同名新槽；块退出弹栈并 `POP` 释放。
- **模块级表 = 模块导出表**：主脚本与被导入模块各有自己的 `StringMap<Value>` 顶层表；`DEFINE_GLOBAL` 写入当前模块表。`import` 绑定整个模块对象、`bar.x` 读其表（见 §13）。
- **upvalue 透传**：内层函数引用的变量若不在直接外层局部、而在更外层，则沿 upvalue 链 `is_local=false` 逐层透传，运行时 `CLOSURE` 按描述串联 `Upvalue`。
- **const 编译期标记 = 零运行时开销**：`STORE_LOCAL` 到 const 槽在编译期即报错、永不发射。「const 仅保护绑定本身、不保护复合内部状态」自动满足--只拦截对绑定槽的 STORE，不拦截经其引用的变异。

否决方案：

- **全槽位模型（模块顶层也编译期槽位）**：单遍编译下破坏闭包前向引用模式（函数内引用后文声明的模块级变量）。
- **全命名模型（含局部按名查表）**：局部哈希查找不可接受。

依据：lox 一等函数 + 闭包要求 `fun useA() { print a; } let a = "hi"; useA();` 成立--单遍编译下 `useA` 体内引用后文声明的 `a` 编译期无法解析槽位，全局 late-bind 让此模式成立；局部走栈槽 = O(1) 索引；upvalue `{is_local,index}` 精确映射 Upvalue 栈槽/堆搬移语义。

## 8. 函数 / 闭包调用约定

**决策：`CLOSURE u16 func_idx` 统一宽度，upvalue 捕获描述存 `Function.upvalue_descs`；`CALL u8 arity` 运行期校验 arity；`RETURN` + `CLOSE_UPVALUE`；函数尾隐式 `PUSH_NULL; RETURN`。**

```cpp
struct UpvalueDesc { bool is_local; std::uint8_t index; };
```

- **`CLOSURE u16 func_idx`**：取 func 常量，遍历 `func.upvalue_descs` 建链：`is_local=true` -> 在当前帧 `slot_base+index` 处取/建 Upvalue（指向栈槽）；`is_local=false` -> 复用调用者帧已建的 `Upvalue[index]`（透传，嵌套闭包）。封装进新 `Closure` 压栈。
- **`CALL u8 arity`**：栈布局 `[callee, arg0..argN-1]`；resolve callee（Closure/Native/BoundMethod，否则 RuntimeError）；Closure 校验 arity==func.arity（不符 -> RuntimeError）；新建帧。BoundMethod：压接收者到 callee 之上，递归以 arity+1 调用其 Closure（接收者成 slot 0）。Native：直接调用函数指针、压回结果、不建帧。
- **帧结构**：`CallFrame { Closure* closure; const std::uint8_t* ip; Value* slot_base; }`。`slot_base` 指向首个参数（slot 0）；方法调用接收者为隐式首参占 slot 0（`this`）。
- **`RETURN`**：弹返回值、关闭本函数 upvalue、恢复调用者帧、压返回值。
- **`CLOSE_UPVALUE`**：被捕获局部出作用域时把值搬堆（语法文档「外层返回时 Upvalue 将值搬移到自身堆存储」）。
- **隐式返回 null**：编译器在每个函数体末尾补 `PUSH_NULL; RETURN`（若无显式 return）；init 方法补 `LOAD_LOCAL 0; RETURN`（见 §9）。

否决方案：

- **CLOSURE 变长内联（clox 式）**：`CLOSURE u16` 后跟 N×2B（is_local+index）铺在 code，破坏 §6 统一宽度不变量；clox 内联为可序列化，内存模型无此需求。
- **CALL 不带 arity**：非常规调用约定，无收益。

依据：upvalue 描述是 Function 的属性（它捕获什么），存 Function 元数据语义正确；编译期无法静态得知 callee 形参数（动态分派），arity 比较须运行期。

## 9. 类 / 方法 / super / this

**决策：忠于文档组合--`GET_PROPERTY`+`CALL` 方法调用；`SUPER_GET`/`SUPER_INVOKE`；Class 只存自身方法 + 链式查找；init 返回 this。**

| opcode | 操作数 | 语义 |
|---|---|---|
| `CLASS` | u16 name | 弹 superclass（有 extends 时）/无；建 `Class{name, superclass}`，压栈 |
| `METHOD` | u16 name | 弹 Closure + Class，加入 Class 自身方法表 |
| `GET_PROPERTY` | u16 name | 弹 obj；Instance -> 字段值或 BoundMethod；Module -> globals 表项；否则 RuntimeError |
| `SET_PROPERTY` | u16 name | 弹 value + obj；Instance -> 设字段；压回 value |
| `SUPER_GET` | u16 name | 弹 superclass local；沿其链查方法，`BoundMethod{this, found}` |
| `SUPER_INVOKE` | u16 name, u8 arity | 栈 `[this, args..., superclass]`；沿 superclass 链查方法并调用 |

- **方法调用 = `GET_PROPERTY` + `CALL`**：严格匹配语法文档「`obj.method` 求值时创建 `BoundMethod{this, method}`」。把求值点（bind）与调用点（call）分离。
- **Class 只存自身方法 + 链式查找**：`CLASS` 仅设 superclass 指针，`METHOD` 仅写自身表，VM 查找时沿 `superclass` 链上溯（语法文档「沿 class.superclass 链向上查找」）。
- **`SUPER_GET` + `SUPER_INVOKE`**：`super.IDENTIFIER` 是 primary，既可调用（`()`）也可取值，故需两条指令。superclass 作隐式 local 存于方法帧（`this`=slot0 之后的固定槽），编译期可知槽位。
- **init 返回 this**：编译器标记当前函数是否为 init；其隐式末尾与裸 `return;` 发射 `LOAD_LOCAL 0; RETURN`（返回 slot0=this），而非普通函数的 `PUSH_NULL; RETURN`。显式 `return expr;` 仍返回 expr。
- **this = slot 0**：方法调用接收者作隐式首参占 slot 0，`this` 表达式 = `LOAD_LOCAL 0`，贯通 §7/§8。

`class Foo extends Bar { m(){...} }` 编译骨架：

```
GET_GLOBAL "Bar"          ; 父类（extends 时）
CLASS "Foo"               ; 建 Class{Foo, superclass=Bar}
<编译 m 为 Function, CLOSURE> ; 方法闭包压栈
METHOD "m"                ; 装入 Foo 方法表
... (其余方法)
DEFINE_GLOBAL "Foo"       ; 绑定类名
```

否决方案：

- **引入 `INVOKE name arity`**：合并 get+call 跳过 BoundMethod 分配，是常见优化但偏离文档「求值即创建 BoundMethod」；WIP 阶段先忠于文档，INVOKE 留作未来纯加法优化。
- **CLASS 创建时复制父类方法表**：偏离文档链式查找描述，CLASS 变重。

## 10. 列表与下标

**决策：`NEW_LIST`（静态 count）+ `LIST_WITH_SIZE`（运行时 N）+ `GET_INDEX`/`SET_INDEX`（无操作数）；列表定长，无 append。**

| opcode | 操作数 | 栈效果 |
|---|---|---|
| `NEW_LIST` | u8 count | `[e1..ek] -> [List(k)]` |
| `LIST_WITH_SIZE` | u8 k | `[e1..ek, N] -> [List(N, 前min(k,N)填元素)]` |
| `GET_INDEX` | - | `[obj, idx] -> [elem]` |
| `SET_INDEX` | - | `[obj, idx, v] -> [v]` |

- `NEW_LIST u8 count`：弹 count 个值建定长列表（覆盖 `[e1,e2,e3]`/`[]`）。
- `LIST_WITH_SIZE u8 k`：栈 `[e1..ek, N]`；弹运行时 N（校验 Integer>=0，否则 RuntimeError），弹 k 个元素，建长度 N 的列表，前 min(k,N) 填元素、余填 null（覆盖 `[e1,e2;N]`/`[;N]`，k=0 即 `[;N]`）。
- `GET_INDEX`：弹 `[object, index]`；List -> 越界/类型检查后取元素（无负索引）；Instance -> 查 `__getitem__` 调用；否则 RuntimeError。
- `SET_INDEX`：弹 `[value, index, object]`；List -> 设元素；Instance -> `__setitem__` 调用；否则 RuntimeError；压回 value（赋值值=右值）。
- 求值顺序：`a[i]=v` 左到右求值 a、i、v，栈深到浅 `[a, i, v]`，`SET_INDEX` 弹 v、i、a 后压回 v。

`[e1, e2; N]` 编译：`eval e1; eval e2; eval N; LIST_WITH_SIZE 2`
`[; N]` 编译：`eval N; LIST_WITH_SIZE 0`
`[]` 编译：`NEW_LIST 0`

否决方案：

- **统一 `NEW_LIST` + 条件跳转编 `;N` 截断/填充**：编译器复杂、运行时多次判跳。
- **全运行时建表**：静态常见路径变慢。

## 11. 行号信息

**决策：游程编码 `SmallVector<LineEntry{u16 offset, u32 line}>`，行变化时追加，二分查找；仅存行号。**

```cpp
struct LineEntry { std::uint16_t offset; std::uint32_t line; };
// Function.lines : SmallVector<LineEntry, 0>
// 发射指令时：若 line != 上一条记录的 line，追加 {code_.size(), line}
// 查询：二分 lines，返回最大 offset <= pc 的 entry.line
```

- 紧凑：lox 源码典型一行对应多条指令，RLE 仅在行变化时记一条。
- 行号足够：错误信息含栈轨迹（函数名+行号），column 不入 bytecode（AST 仍有）。
- 每 Function 独立，与 §3 per-function chunk 一致。

否决方案：每指令一行号（内存重）、每字节一行号（内存最重）。

## 12. 异常处理

**决策：每函数 handler 表 + `ErrorKind` 类型化 VM 错误 + 全错误可捕获（标准库注册有名类后）+ 类型 lazy 解析 + 内层优先。**

```cpp
enum class ErrorKind : std::uint8_t {
  kTypeMismatch, kDivideByZero, kStackOverflow, kIndexOutOfBounds,
  kInvalidSubscript, kArityMismatch, kNoSuchMethod,
  kInvalidPropertyTarget, kInvalidCallTarget,  // 可扩展
};
struct HandlerEntry {
  std::uint16_t try_start, try_end, catch_offset, type_idx;  // type_idx = 常量池名索引
};
// Function.handlers : SmallVector<HandlerEntry, 0>，内层优先序
// RuntimeError 载荷：variant<lox::Value /*throw*/, struct{ErrorKind; std::string message;} /*VM*/>
```

- **`THROW`**：无操作数；弹值 -> `RuntimeError`（携该 lox Value）-> 执行循环返回 `llvm::Error`。
- **VM 错误类型化**：除零/类型不匹配/越界等生成携 `ErrorKind` + message 的 `RuntimeError`，使可匹配。
- **匹配流程**：执行循环遇 Error -> 查当前帧 `handlers`，按内层优先序找首个 `IP∈[try_start,try_end)` 且类型匹配者：
  - 抛出 Instance：`value.class` 沿继承链 instanceof `catch_class`。
  - 抛出原语（`throw 42`）：查标准库该原语 tag 的有名类，instanceof `catch_class`。
  - VM 错误：查标准库该 `ErrorKind` 的有名错误类，instanceof `catch_class`（支持 `Error` 基类 + 子类层级，`catch Error` 捕获全部）。
- **命中**：清错、`IP=catch_offset`、压绑定值（throw 直接绑该值；VM 错误构造关联错误类的 `Instance` 携 message）。
- **无命中**：弹帧向上传播；顶层无匹配 -> 输出错误终止。
- **内层优先**：handler 表按 try 体**完成**时登记（内层先完成 -> 先入表），「首个 range 命中且类型匹配」即内层。
- **类型 lazy 解析**：`type_idx` 存常量池名索引，匹配时经当前模块 globals + 标准库注册表解析为 `Class`。

**前向兼容与文档约定**：

- 标准库注册前：VM 错误有 `ErrorKind` 但注册表无对应类 -> 不可匹配 -> 传播到顶（行为同「仅 throw 可捕获」，向后兼容）。
- 标准库注册后：注册 `Integer`/`Float`/`String`/`Boolean`/`Null` 有名类 + `Error` 基类及各 `ErrorKind` 子类 -> 全错误与原语 throw 均可经类名 catch。
- 文档注明：标准库负责将内建类型与 `ErrorKind` 关联为有名 `Class`，使 `catch <TypeName>` 可匹配对应错误/原语；未注册前这些错误不可捕获。

`try { BODY } catch E e { CATCH }` 编译骨架：

```
try_start:
  <BODY>                    ; try 体
  JUMP L_end                ; 正常完成跳过 catch
try_end:                    ; handler 区间 = [try_start, try_end)
catch_offset:
  <定义 e 为局部, 压入抛出值>  ; VM 命中后设 IP=catch_offset 并压抛出值
  <CATCH>
L_end:
```

否决方案：

- **handler 信息内联 code（region 标记 opcode）**：破坏 §6 统一宽度。
- **仅 throw 可捕获**：与设计目标（全错误可捕获经标准库）相悖。

## 13. 模块与 import

**决策：import 采用点号命名空间语法；合并 `Module`（统一 globals 承载导出与子模块）；`__init__.lox` 包模型；`IMPORT`/`IMPORT_NS` opcode。**

> 本节对应的语法修改见 `docs/lox-language.typ` 的 import 语法规则与模块系统/加载语义（已同步更新）。

### 13.1 语法

```
importDecl  -> import dottedName (as IDENTIFIER)? ";" ;
dottedName  -> IDENTIFIER ("." IDENTIFIER)* ;
```

- Scanner 无需改动（`.` 与 IDENTIFIER 已是 token）。
- Parser：`import` 后解析 `dottedName`（IDENTIFIER 链）而非 STRING。
- AST：`ImportStmt.path: StringRef` -> `ImportStmt.segments: SmallVector<StringRef>`。

### 13.2 绑定语义

- `import lib;`（单段）-> `lib = Module`（直接绑定）。`lib.foo` 取导出。
- `import lib.utils;`（多段）-> ensure `lib` 为 `Module`（占位符），`lib.utils = Module`。`lib.utils.foo` 链式取导出。
- `import lib.utils as u;` -> `u = Module`（`as` 绕过命名空间）。
- 前缀合并：`import lib.utils; import lib.helper;` 共享同一 `lib` Module。

### 13.3 模块名与解析

模块名 = 点号形式（`lib.utils`），兼作缓存键与诊断名。文件解析：每段累积路径 `p`，按序尝试 `p.lox`（文件模块）-> `p/__init__.lox`（目录包模块）。命中 -> 该段为 Module（加载其 .lox，执行顶层）；都不命中：末段 -> 解析错误，中间段 -> 占位符（`script=null`）。`import a.b.c` 从左到右解析 a、a.b、a.b.c；有文件的段加载（含 `__init__.lox` 顶层执行），无文件的中间段为占位符；末段必须命中。命名空间链：父 `globals[子名] = 子 Module`。

### 13.4 合并 Module 类型

```cpp
class Module : public GcObject {
  GcPtr<String> name;              // 点号全名 "lib.utils"
  GcPtr<Function> script;          // nullable：占位符为 null；__init__.lox/文件模块非空
  llvm::StringMap<Value> globals;  // 统一：导出 + 子模块
};
```

- 文件模块 / `__init__.lox` 包模块：`script` 非空，`globals` 含导出（+ 子模块）。
- 占位符（无文件的中间段）：`script=null`，`globals` 仅含子模块。
- `GET_PROPERTY` 对 Module 统一读 `globals`（导出/子模块不区分）。
- 共存（allow-merge）：`import a` 与 `import a.b` 可共存--`a` 为真实模块，`a.globals["b"]` 加子模块条目；占位符遇后续 `import a` 升级（加载并合并）。

### 13.5 opcode

| opcode | 操作数 | 语义 |
|---|---|---|
| `IMPORT` | u16 name | 解析+加载/缓存+压 Module（供 `as` 后续 `DEFINE_GLOBAL`/`STORE_LOCAL` 绑定） |
| `IMPORT_NS` | u16 name | 解析+加载+按段 ensure 占位符链+末段设 Module+绑定首段（无别名） |

```
import lib;            -> IMPORT "lib"; DEFINE_GLOBAL "lib"
import lib.utils;      -> IMPORT_NS "lib.utils"
import lib.utils as u; -> IMPORT "lib.utils"; DEFINE_GLOBAL "u"
```

解析（文件 vs `__init__.lox`、中间段加载、循环导入部分缓存）全在 VM 内，bytecode 契约不变。模块缓存 `StringMap<GcPtr<Module>>` 为 GC 根。

否决方案：

- **保留 `import STRING` + 自助 `/`->`.` 转义**：转义规约不透明；点号语法直接对应访问语法，更清晰。
- **ModuleNamespace 与 ModuleObject 分离**：两类型 + 「读子段 vs 读导出」分派，更繁复；合并后统一 map 天然支持。
- **逐符号拷贝导入**：偏离「绑定整个 Module」语义，无法支持循环导入部分缓存。

## 14. 完整 opcode 清单

共 53 条（≤256），按类别：

| 类别 | opcode | 操作数 |
|---|---|---|
| 字面量 | `PUSH_NULL` `PUSH_TRUE` `PUSH_FALSE` `CONSTANT` | - / u16 |
| 栈 | `POP` | - |
| 算术 | `ADD` `SUBTRACT` `MULTIPLY` `DIVIDE` | - |
| 移位 | `SHIFT_LEFT` `SHIFT_RIGHT` | - |
| 位运算 | `BIT_AND` `BIT_OR` `BIT_XOR` | - |
| 比较 | `LESS` `LESS_EQUAL` `GREATER` `GREATER_EQUAL` `EQUAL` `NOT_EQUAL` | - |
| 单目 | `NOT` `NEGATE` `BIT_NOT` | - |
| 局部 | `LOAD_LOCAL` `STORE_LOCAL` | u8 |
| upvalue | `GET_UPVALUE` `SET_UPVALUE` | u8 |
| 全局 | `DEFINE_GLOBAL` `GET_GLOBAL` `SET_GLOBAL` | u16 |
| 闭包 | `CLOSURE` `CLOSE_UPVALUE` | u16 / - |
| 调用 | `CALL` `RETURN` | u8 / - |
| 控制流 | `JUMP` `LOOP` `JUMP_IF_FALSE` `JUMP_IF_TRUE` | int16 |
| OO | `CLASS` `METHOD` `GET_PROPERTY` `SET_PROPERTY` `SUPER_GET` `SUPER_INVOKE` | u16 / u16 / u16 / u16 / u16 / u16+u8 |
| 列表 | `NEW_LIST` `LIST_WITH_SIZE` `GET_INDEX` `SET_INDEX` | u8 / u8 / - / - |
| 异常 | `THROW` | - |
| 模块 | `IMPORT` `IMPORT_NS` | u16 |

- 算术/比较统一 opcode + 运行期类型分派（int+int=int、任一 float->float、位运算 int-only、`==`/`!=` 原语按值/对象按引用）；JIT 后续可经类型反馈特化。
- `JUMP_IF_FALSE`/`JUMP_IF_TRUE` pop 条件并校验 Boolean（非 Boolean -> RuntimeError），类型检查与求值耦合：短路跳过求值即跳过检查（符合短路语义）。

## 15. 控制流编码要点

- `JUMP`（前向无条件）/ `LOOP`（后向，显式循环回边，供 profiler/OSR 识别热点）/ `JUMP_IF_FALSE`/`JUMP_IF_TRUE`（pop 条件 + Boolean 类型检查，int16 相对）。
- `&&`/`||` 编为条件跳转序列（pop 式，短路）。`a && b`：

  ```
  eval a
  JUMP_IF_FALSE L_false   ; ① a 必须 Boolean（pop a）；a=false 跳 L_false，b 不求值不检查
  eval b
  JUMP_IF_FALSE L_false   ; ② b 必须 Boolean（pop b）；仅 a=true（未短路）才执行
  PUSH_TRUE
  JUMP L_end
  L_false: PUSH_FALSE
  L_end:
  ```

  短路时 b 不求值、不检查类型 = 短路语义本质（`false && (1/0==0)` 不崩溃）。
- `for` 糖化为 `init + while(cond){body; incr}`，无 `FOR` opcode。
- `break` = 编译期跳转表 + `POP` 释放块局部；捕获 upvalue 的局部退出补 `CLOSE_UPVALUE`。
- `if/else`：`cond; JUMP_IF_FALSE L_else; <then>; JUMP L_end; L_else: <else>; L_end:`
- `while`：`L_head: cond; JUMP_IF_FALSE L_end; <body>; LOOP L_head; L_end:`

## 16. 关键数据结构汇总

```cpp
// 值（§4）
enum class ValueType : std::uint8_t { /* 见 §4 */ };
struct Value { ValueType tag; union { bool boolean; std::int64_t integer; double floating; GcPtr<GcObject> obj; } as; };

// 代码容器（§3/§5/§11）
struct LineEntry { std::uint16_t offset; std::uint32_t line; };
class Chunk {
  llvm::SmallVector<Value, 0> constants_;
  llvm::SmallVector<std::uint8_t, 0> code_;
  llvm::SmallVector<LineEntry, 0> lines_;
};

// 函数（§3/§7/§8/§9/§12）
struct UpvalueDesc { bool is_local; std::uint8_t index; };
struct HandlerEntry { std::uint16_t try_start, try_end, catch_offset, type_idx; };
class Function : public GcObject {
  GcPtr<String> name;
  std::uint8_t arity;
  Chunk chunk;
  llvm::SmallVector<UpvalueDesc, 0> upvalue_descs;
  llvm::SmallVector<HandlerEntry, 0> handlers;   // 内层优先序
  std::uint16_t max_stack;
  bool is_init;
};

// 模块（§13）
class Module : public GcObject {
  GcPtr<String> name;
  GcPtr<Function> script;             // nullable
  llvm::StringMap<Value> globals;     // 导出 + 子模块
};

// 异常（§12）
enum class ErrorKind : std::uint8_t { /* 见 §12 */ };
class RuntimeError : public llvm::ErrorInfo<RuntimeError> {
  // 载荷 variant<lox::Value /*throw*/, struct{ErrorKind; std::string message;} /*VM*/>
};

// 调用帧（§8）
struct CallFrame { Closure* closure; const std::uint8_t* ip; Value* slot_base; };
```

## 17. 决策汇总

| # | 决策 | 选择 |
|---|---|---|
| 1 | 编译单元 | 逐函数 chunk，嵌套入父常量池，顶层 `<script>` Function |
| 2 | Value 表示 | tagged union（值类型独立槽/引用共享 GcPtr） |
| 3 | 常量池 | 单一内存 Value 池，名称与字面量共用，Bool/Null 走 opcode |
| 4 | 指令编码 | 统一宽度 + 相对跳转，无 long 变体 |
| 5 | 变量解析 | 局部栈槽 + upvalue + 全局命名表 late-bind，const 编译期，this=slot0 |
| 6 | 控制流 | 显式 LOOP 回边 + pop 式条件跳转，类型检查与求值耦合 |
| 7 | 函数/闭包 | CLOSURE 统一宽度，描述入 Function 元数据，CALL u8 运行期校验 |
| 8 | 类/方法 | GET_PROPERTY+CALL，SUPER_GET/SUPER_INVOKE，Class 只存自身方法+链式查找，init 返回 this |
| 9 | 列表/下标 | NEW_LIST/LIST_WITH_SIZE + GET_INDEX/SET_INDEX，定长 |
| 10 | 异常 | 每函数 handler 表 + ErrorKind 类型化 + 全错误可捕获（标准库注册后） |
| 11 | 行号 | 游程编码 offset+line，二分查 |
| 12 | 模块/import | 点号命名空间语法，合并 Module + `__init__.lox` 包，文件唯一解析 |

## 18. 跨模块前置改动

实现 bytecode 模块前须完成：

1. **import 语法修改**（§13）：`docs/lox-language.typ` import 语法规则与模块系统/加载语义（已同步）；`ImportStmt.path: StringRef` -> `segments: SmallVector<StringRef>`；`parser.cpp` 解析 dottedName。
2. **编译器访问 GC 堆 + 字符串驻留表**：bytecode 编译器依赖 vm 模块的 `String` 分配与驻留表，模块间依赖需在 CMake 接入（`lox_bytecode` link `lox_vm` 或抽出 value/gc 子库）。
3. **`__init__.lox`、标准库错误/原语有名类**（§12/§13）：标准库注册表为后续任务，注册前 VM 错误不可捕获（向后兼容）。
