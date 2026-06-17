# xRtoCapeOpen 设计文档：把 CAPE-OPEN MINLP DLL 包装为 xOptProblem

> 版本 v3 · 2026-06-15 · 适用模块：`libsrc/xRtoCapeOpen`、`libsrc/xOpt`
>
> v2 变更：采用 **C-ABI vtable** 方案（`xOptModel_createModel` + `xOptProblemFromBlackBoxModel`），
> 取代 v1 的「xOpt 内置 COM/CORBA 适配器」方案。CAPE-OPEN 复杂度全部下沉到 DLL 内部，xOpt 零改动复用
> 既有黑箱通路。索引基统一为 **0-based**。
>
> v3 变更：**同时支持 COM 与 CORBA 两套绑定**，用传输无关抽象 `ICapeMINLPModel` + 后端工厂统一
> （§3.1–3.4）；映射核心 `CapeMINLPProblemCore` 后端共享只写一遍。打包为「共享核心 + 每后端可选 DLL」。
>
> v4 变更：**xOpt 零 CAPE-OPEN 改动**——DLL 即普通黑箱模型，以 `type_name="BlackBox"` 经既有
> `xOptModelBlackBox`/`xOptProblemFromBlackBoxModel` 通路接入；并已**删除** xOpt 里残留的 CAPE-OPEN 特判
> （§4）。代价：DLL 须实现完整 `xOptModelT` vtable（多为平凡桩）。

## 1. 目标（Goal）

把一个支持 CAPE-OPEN MINLP 接口（`ICapeMINLP` / `ICapeMINLPSystem`）的第三方 DLL，包装成
`xOpt` 框架里的 `xOptProblem`，使 `xRto` 能把它当作**黑箱模型**直接求解。

**关键约束（v2 核心）**：`xRtoCapeOpen.dll` 对 xOpt 暴露的接口必须是 **C 风格 vtable**（函数指针表
+ 不透明 handle + `size` 版本字段），以便二进制兼容地后续升级。参照 xOpt 既有约定：

```c
// include/xOpt/xOptModel.h:280
XOPTIF_API int xOptModel_createModel(xOptModelT* model, xOptPlatformT* platform, const char* name);
```

---

## 2. 现状分析（Where we are）

### 2.1 角色澄清（必读）

CAPE-OPEN MINLP 标准里两个接口职责不同：

| 接口 | 职责 | 在本任务中的角色 |
|------|------|------------------|
| `ICapeMINLP` | **模型/问题**：规模、变量/约束界、稀疏结构、函数值与导数。 | **真正被映射成 `xOptProblem` 的接口。** |
| `ICapeMINLPSystem` | **求解器系统**：`Solve` / `Get/SetParameters` / `SetProblemID`。 | 入口句柄；适配器经由它取得背后的 `ICapeMINLP` 实例。 |
| `ICapeMINLPSolverManager` | 工厂：`CreateMINLPSystem(theMINLP, out theSystem)`。 | 本通路一般不需要（那是 DLL 自带求解的场景）。 |

> 一句话：「支持 `ICapeMINLPSystem` 的 DLL」是入口；适配器最终消费的是它背后的 `ICapeMINLP` 模型。
> `xOptProblem` 与 `ICapeMINLP` **天然一一对应**（§5），这是包装可行的根本。

### 2.2 现有 `xRtoCapeOpen` 内容与方向差异

- [`SqpSolver.idl`](../SqpSolver.idl) + [`SqpcSovlerImpl.cpp`](../SqpcSovlerImpl.cpp)：当前实现的是
  **求解器服务端**（本工程当 `ICapeMINLPSystem` servant，经 TAO/ACE 反向回调外部 `ICapeMINLP` 模型）。
  方向与本任务**相反**。IDL 中对 `ICapeMINLP` 的数据/方法定义可作为映射参考，但求解器 servant 代码
  与新方向无关，建议后续迁出为独立示例（见 §8 M4）。

### 2.3 xOpt 既有的两条黑箱通路（决定 v2 方案）

xOpt 里已存在两套「加载 DLL → 变成 xOptProblem」的机制：

| 通路 | 入口符号 | DLL 暴露形态 | xOpt 包装类 | 备注 |
|------|----------|--------------|-------------|------|
| (A) C++ 符号 | `createProblem` / `destroyProblem`（返回 `xOptProblem*`） | **C++ ABI**（虚表跨 DLL） | [`xOptProblemBlackBox`](../../xOpt/src/Problem/xOptProblemBlackBox.cpp) | 当前 `CAPEOPEN*` 类型**错误地**路由到这里 |
| (B) C 符号 | `xOptModel_createModel`（填 `xOptModelT`） | **C ABI vtable**（函数指针 + handle + `size`） | [`xOptProblemFromBlackBoxModel`](../../xOpt/src/Model/xOptModelBlackBox.cpp) | **本方案采用**，满足「C 风格、可升级」 |

通路 (B) 正是用户要求的形态：DLL 用 `xOptModel_createModel` 填 `xOptModelT`，其 `buildProblem` 再填
`xOptProblemT`（C 风格问题 vtable，见 [`xOptModel.h:148`](../../../include/xOpt/xOptModel.h)）；
`xOptProblemFromBlackBoxModel` 把该 vtable 适配为 `xOptProblemBase` 供 xRto 消费。

---

## 3. 总体架构（v2 / C-ABI）

```
┌───────────────────────────────────────────────────────────────────────┐
│ xRtoCapeOpen.dll   —— 只对外导出一个 C 符号: xOptModel_createModel      │
│                                                                         │
│  xOptModel_createModel(&model, platform, name)                          │
│      └─ 填充 xOptModelT (此场景最小化: handle + buildProblem [+少量])    │
│  model.buildProblem(handle, &problemT)                                  │
│      └─ 填充 xOptProblemT 的 18 个函数指针 (= §5 映射的落点)             │
│                                                                         │
│  ── 以下全部隐藏在 DLL 内部, xOpt 不可见 ──                              │
│  CapeMINLPModel (C++)  ← ICapeMINLP                                      │
│      backend: COM(推荐) / CORBA(复用IDL) / 直连                          │
│      负责: SAFEARRAY/CORBA 编解码、HRESULT/异常→负返回码、结构缓存       │
└──────────────────────────▲────────────────────────────────────────────┘
                           │ C vtable (xOptModelT / xOptProblemT, 含 size 版本字段)
┌──────────────────────────┴────────────────────────────────────────────┐
│ xOpt::createProblem("CapeOpen", path)                                   │
│   1) LoadLibrary(path) + GetProcAddress("xOptModel_createModel")        │
│   2) xOptModel_createModel(&model, nullptr, "CapeOpenModel")            │
│   3) new xOptProblemFromBlackBoxModel(nullptr, &model)   ← 复用既有     │
│   4) initialize() → model.buildProblem(...) 填 problemT                 │
│   → 返回 xOptProblemBasePtr                                             │
└──────────────────────────▲────────────────────────────────────────────┘
                           │ xOptProblem (纯虚)
                    ┌──────┴──────┐
                    │    xRto      │  优化求解循环
                    └─────────────┘
```

**这样设计的好处**
- CAPE-OPEN / COM / CORBA 复杂度**完全封装在 `xRtoCapeOpen.dll` 内**，xOpt 与 xRto 对 CAPE-OPEN 零依赖、零认知。
- C vtable + `size` 字段提供**二进制前后兼容**：DLL 与宿主可独立升级（见 §6.5）。
- xOpt 侧几乎零改动——复用既有 `xOptProblemFromBlackBoxModel`，只需在 `createProblem` 加一条路由分支。

### 3.1 双后端适配架构（COM + CORBA 同时支持）

CAPE-OPEN MINLP 标准有 **COM 映射**与 **CORBA 映射**两套绑定，但二者的**逻辑接口完全相同**
（`GetMINLPSize` / `GetMINLPVariableBounds` / … 方法名、语义一致，只是 marshaling 不同）。因此用
**一个传输无关的 C++ 抽象 `ICapeMINLPModel`** 统一二者，两个后端各自实现，工厂按目标选择：

```
                        ┌──────────────────────────────────────┐
   xOptProblemT 18 指针 │  CapeMINLPProblemCore                 │  ← 与后端无关的映射逻辑
   (DLL 内填充)         │  - §5 映射 / 结构缓存 / setX 状态机    │     (只依赖抽象 ICapeMINLPModel)
                        │  - 异常→负返回码                       │
                        └───────────────▲──────────────────────┘
                                        │ ICapeMINLPModel (纯虚, std::vector 签名)
                          ┌─────────────┴─────────────┐
                          │                           │
              ┌───────────┴───────────┐   ┌───────────┴────────────┐
              │ CapeMINLPModelCom      │   │ CapeMINLPModelCorba     │
              │  ICapeMINLP (COM 指针) │   │  ICapeMINLP_ptr (CORBA) │
              │  SAFEARRAY/BSTR/HRESULT│   │  CORBA::*Seq/CORBA::Exc  │
              └───────────┬───────────┘   └───────────┬────────────┘
                          │                           │
              CoCreateInstance / DllGetClassObject   ORB resolve (IOR / corbaname / NS)
                          │                           │
              ┌───────────┴───────────┐   ┌───────────┴────────────┐
              │ COM CAPE-OPEN 组件     │   │ CORBA CAPE-OPEN servant │
              │ (进程内 DLL)           │   │ (可远程/跨进程)         │
              └───────────────────────┘   └─────────────────────────┘
                          ▲                           ▲
                          └────────── CapeBackendFactory ──────────┘
                                选择 COM / CORBA / Mock
```

**抽象接口（统一两套绑定的关键）**
```cpp
// CapeMINLPModel.h —— 镜像 IDL ICapeMINLP，纯 C++ 签名，传输无关
class ICapeMINLPModel {
public:
  virtual ~ICapeMINLPModel() = default;
  virtual int connect() = 0;                 // COM: CoCreateInstance; CORBA: ORB resolve
  virtual void disconnect() = 0;
  // —— 以下与 IDL ICapeMINLP 一一对应（0-based）——
  virtual int getSize(CapeMINLPSize& s) = 0; // nv,niv,...,nnzof
  virtual int getStructure(const std::string& type,
                           std::vector<int>& rowidx, std::vector<int>& colidx,
                           std::vector<int>& objidx) = 0;
  virtual int getVariableNames(const std::vector<int>& vids, std::vector<std::string>& out) = 0;
  virtual int getVariableBounds(const std::vector<int>& vids,
                                std::vector<double>& lb, std::vector<double>& ub) = 0;
  virtual int getVariableValues(const std::vector<int>& vids, std::vector<double>& out) = 0;
  virtual int setVariableValues(const std::vector<int>& vids, const std::vector<double>& v) = 0;
  virtual int getConstraintNames(const std::vector<int>& cids, std::vector<std::string>& out) = 0;
  virtual int getConstraintBounds(const std::vector<int>& cids,
                                  std::vector<double>& lb, std::vector<double>& ub) = 0;
  virtual int getNonlinearConstraintValues(const std::vector<int>& cids, std::vector<double>& v) = 0;
  virtual int getConstraintDerivativeValues(const std::string& type,
                                            const std::vector<int>& cids, std::vector<double>& v) = 0;
  virtual int getObjectiveValue(double& v) = 0;
  virtual int getObjectiveDerivativeValues(const std::string& type, std::vector<double>& v) = 0;
  // 错误信息（供日志）
  virtual std::string lastError() const = 0;
};
```
两个后端唯一的差异是「方法体里如何 marshaling + 如何抛错」；`CapeMINLPProblemCore`（填 `xOptProblemT`
的那层）**只见抽象、不见 COM/CORBA**，因此 §5 映射代码、结构缓存、setX 状态机全部后端共享、只写一遍。

### 3.2 后端选择与连接串约定

`xOpt::createProblem` 传入的 `path` 扩展为**连接串**，由前缀 scheme 决定后端（无前缀按文件后缀推断）：

| 连接串示例 | 后端 | 解析 |
|------------|------|------|
| `com:{CLSID-GUID}` 或 `com:Prog.Id.1` | COM | `CoCreateInstance` / ProgID→CLSID |
| `C:\path\to\unit.dll` | COM | `.dll` 默认走 COM（`DllGetClassObject` 或导出工厂） |
| `corba:IOR:0100...` | CORBA | `ORB::string_to_object(IOR)` |
| `corba:corbaname::host:port/Name` | CORBA | 经 Naming Service 解析 |
| `mock:problemName` | Mock | 内置测试模型 |

`CapeBackendFactory::create(const std::string& conn) -> std::unique_ptr<ICapeMINLPModel>`：
解析 scheme → 实例化对应后端 → `connect()`。失败回退策略可配置（默认不回退，错误显式上报）。

### 3.3 COM vs CORBA 生命周期差异（各自封装在后端内）

| 关注点 | COM 后端 | CORBA 后端 |
|--------|----------|------------|
| 初始化 | `CoInitializeEx`（套间） | `CORBA::ORB_init`（进程级单例，引用计数管理） |
| 取得对象 | `CoCreateInstance(CLSID)` / 加载 DLL `DllGetClassObject` | `string_to_object(IOR)` / NS resolve + `_narrow` |
| 取 `ICapeMINLP` | `QueryInterface` 或经 `ICapeMINLPSystem` | `_narrow` 或经 system 引用 |
| 数组 | `SAFEARRAY(VT_R8/VT_I4/VT_BSTR)` | `CapeArrayDouble`/`CapeArrayLong`/`CapeArrayString`（IDL sequence） |
| 字符串 | `BSTR`(UTF-16) ↔ UTF-8 | `CORBA::String`(UTF-8) ↔ UTF-8 |
| 错误 | `HRESULT` + `ISupportErrorInfo` | `CapeException` / `CORBA::SystemException` |
| 部署位置 | 进程内（同地址空间，最快） | 可本地/远程（ORB 透明） |
| 线程亲和 | 套间约束强（见 §6.6） | ORB 线程策略（POA），相对灵活 |

> 设计要点：把这些差异**全部关进各自后端类**，抽象层和映射核心对它们无感知。新增第三种绑定（如未来
> gRPC 版）只需再加一个 `ICapeMINLPModel` 实现 + 一个 scheme，不动其余代码。

### 3.4 打包：共享核心 + 每后端可选

TAO/ACE 是重依赖，不应强加给只用 COM 的部署。推荐**共享静态核心 + 后端分离**：

| 产物 | 内容 | 依赖 |
|------|------|------|
| `capeopen_core`（静态库） | `ICapeMINLPModel` 抽象、`CapeMINLPProblemCore`、Mock 后端、`xOptModel_createModel` 框架、工厂骨架 | 仅 xOpt C 头 |
| `xRtoCapeOpenCom.dll` | core + `CapeMINLPModelCom` | COM（Windows 自带） |
| `xRtoCapeOpenCorba.dll` | core + `CapeMINLPModelCorba` | TAO + ACE |

- 两个 DLL 都导出 `xOptModel_createModel`，各自工厂只注册自己支持的 scheme（`xRtoCapeOpenCom` 拒绝 `corba:`，反之亦然），或——
- **可选合并**：`option(CAPEOPEN_SINGLE_DLL)` 把两后端编进一个 `xRtoCapeOpen.dll`，同时支持全部 scheme（适合既要 COM 又要 CORBA 的站点，代价是该 DLL 拖入 TAO）。
- xOpt 侧无需感知：以 `type_name="BlackBox"`、`model_path=<哪个DLL>` 接入（连接目标见 §4.3）。

> CMake：`option(WITH_CAPEOPEN_COM ON)`、`option(WITH_CAPEOPEN_CORBA OFF)`、`option(CAPEOPEN_SINGLE_DLL OFF)`
> 三个开关组合产出上述形态；`capeopen_core` 始终编译并可被 gtest 直接链接（走 Mock 后端）。

---

## 4. 与 xOpt 的对接（xOpt 零 CAPE-OPEN 改动）

**核心决策（v4）**：xRtoCapeOpen.dll 就是一个**普通黑箱模型 DLL**，导出 `xOptModel_createModel`。
xOpt **不需要、也不应该**知道它是 CAPE-OPEN。直接复用 xOpt 既有的 `"BlackBox"` 模型通路即可：

```cpp
// xOpt.cpp createModel(): 既有分支，无需新增
} else if (type_name == "BlackBox") {
    model = new xOptModelBlackBox(context->model_path);   // model_path = 我们的 DLL
}
```

`xOptModelBlackBox` → `xOptModel_createModel` 填 `xOptModelT` → `buildProblem` 填 `xOptProblemT` →
[`xOptProblemFromBlackBoxModel`](../../xOpt/src/Model/xOptModelBlackBox.cpp:43) 包装为 `xOptProblemBase`。
全程 xOpt 已有实现，**不改一行**。

### 4.1 已完成的 xOpt 清理（去除 CAPE-OPEN 特判）
既然不区分，反而要把 xOpt 里残留的 CAPE-OPEN 特判**删掉**（已于本次实施）：
- `xOptIsBlackBoxProblemType` 移除 `CAPEOPEN`/`CAPEOPENPROBLEM`/`CAPEOPENBLACKBOX` 三个分支。
- 删除不再需要的 `xOptIsCapeOpenModelType` 函数。
- 删除 `createModel` 里 `else if (xOptIsCapeOpenModelType(...))` 分支（原先路由到 `createProblem("CapeOpen",…)`
  → `xOptProblemBlackBox` 的 C++-符号通路，属遗留死路）。

结果：CAPE-OPEN DLL 以 `type_name="BlackBox"`、`model_path=<dll>` 接入，xOpt 对 CAPE-OPEN 完全无感知。

### 4.2 由此带来的 DLL 侧要求：实现完整 `xOptModelT` vtable
走 `xOptModelBlackBox` 通路（而非更轻的 problem-only），DLL 的 `xOptModelT` 不能只填 `buildProblem`，
还须实现 `xOptModelBlackBox` 会调用的方法（多数可为平凡桩）：
- `initializeModel()` 要求 `getParameters`（**必须返回 ≥1**）+ `setParameters`
  （[xOptModelBlackBox.cpp:304](../../xOpt/src/Model/xOptModelBlackBox.cpp)）。
- `prepareRuntime()` 断言非空 `validateModel`、`getInPortNum`、`getInPortVariableMap`、`getOutPortNum`、
  `buildProblem`、`setProblemType`。
- 纯优化黑箱：`getInPortNum/getOutPortNum`→0，`validateModel`→0，`getParameters`→至少 1 个参数
  （可为 CAPE-OPEN 求解器参数或占位），其余按需。

### 4.3 待定：CAPE-OPEN 目标（CLSID/IOR）如何传入 DLL
`xOptModelBlackBox` 调 `xOptModel_createModel(&model, nullptr, "BlackBoxModel")`——`name` 被**硬编码**为
`"BlackBoxModel"`，且模型参数是 `double`（无法携带字符串连接串）。故连接目标不能走既有契约，候选（均为 DLL 内部，
不改 xOpt）：环境变量（如 `XRTO_CAPEOPEN_TARGET`，最简）/ DLL 同目录 sidecar 配置 / 一组件一 DLL（CLSID 固定）。
M2 的 mock 不涉及此问题（硬编码）；M3 真接 COM/CORBA 时再定。**当前默认取环境变量。**

---

## 5. 接口映射表（核心）

`xOptProblemT` 的每个函数指针（DLL 内填充），对应 `ICapeMINLP` 的一次调用。这是适配的规格说明。

| `xOptProblemT` 函数指针 | `ICapeMINLP` 来源 | 说明（0-based 索引） |
|-------------------------|-------------------|----------------------|
| （`xOptModelT::buildProblem`） | `ICapeMINLPSystem::SetProblemID` + 取 `ICapeMINLP` + `GetMINLPSize` | 绑定模型、读规模、预取并缓存稀疏结构 |
| `numVariables` | `GetMINLPSize → nv` | |
| `numConstraints` | `GetMINLPSize → nc` | |
| `getVariableNames` | `GetMINLPVariableNames(vids=[0..nv))` | |
| `getVariableDescriptions` | （CAPE-OPEN 无对应）→ 空串 | 与 BlackBox 行为一致 |
| `getConstraintNames` | `GetMINLPConstraintNames(cids=[0..nc))` | |
| `getVariableBounds` | `GetMINLPVariableBounds(vids, lb, ub)` | |
| `getConstraintBounds` | `GetMINLPConstraintBounds(cids, lb, ub)` | |
| `getInitialX` | `GetMINLPVariableValues(vids)` | 当前值作初值 |
| `getOptions` | `GetMINLPSize` 派生 | `MAGIC='X'`；`HAS_DERIVATIVE=1`；`HAS_LINEAR_A` 视 `nlc/nlz`；`IS_SIMULATION=0` |
| `getObjectiveGradientStructure` | `GetMINLPStructure("ObjectiveGradient", …, objindex)` + `nnzof/nlzof` | 传 null 查长度，再填值 |
| `getConstraintJacobianStructure` | `GetMINLPStructure("Jacobian", rowindex, columnindex, …)` + `nnz/nlz` | 同上两段式；**0-based** 行列索引 |
| `getLinearConstraints` | `GetMINLPStructure` 线性部分（`nlc,nlz`）+ `GetMINLPConstraintDerivativeValues("Linear", …)` | 不区分线性/非线性时置 `lcons_size=0` |
| `setX` | `SetMINLPVariableValues(vids, x)` | 缓存 x；evaluate 前必须先调 |
| `runTimeCheck` | 恒 1（或校验 bounds） | |
| `evaluateObjective` | `GetMINLPNonlinearObjectiveFunctionValue` | |
| `evaluateConstraints` | `GetMINLPNonlinearConstraintValues(cids)` | |
| `evaluateObjectiveGradient` | `GetMINLPObjectiveFunctionDerivativeValues("Nonlinear", v)` | 顺序同 `getObjectiveGradientStructure` |
| `evaluateConstraintsJacobianValues` | `GetMINLPConstraintDerivativeValues("Jacobian", cids, vals)` | 顺序同 `getConstraintJacobianStructure` |
| `destroyProblem` | 释放 `ICapeMINLP`/system | RAII；DLL 内回收 |

> 拉格朗日乘子（`Get/SetMINLPLagrangeMultipliers`）黑箱用法通常不需要；若 xRto 需对偶热启动，可扩展。

### 5.1 落地前需与 DLL 提供方对齐的语义
1. **索引基 = 0**（已确认）。若某些 CAPE-OPEN 组件返回 1-based，在 DLL 后端内部转换，对 xOpt 始终 0-based。
2. **结构类型字符串**：`GetMINLPStructure`/`GetMINLP*DerivativeValues` 的 `structuretype`/`stype` 取值
   （`"Jacobian"`/`"ObjectiveGradient"`/`"Linear"`/`"Nonlinear"` 等）抽成常量表，以组件文档为准。
3. **变量/约束属性名**：`GetMINLPVariableDoubleAttribute(attrib)` 的合法取值（`"Value"`/`"LowerBound"`…）。
4. **整数变量**：`GetMINLPSize` 的 `niv/nliv`。首版**按连续松弛处理**并显式声明该限制。
5. **求值前置**：是否每次 evaluate 前都需 `SetMINLPVariableValues`？DLL 后端在 `setX` 缓存后，evaluate
   时若发现未下发则补发（防御式）。

---

## 6. 关键设计细节（实现在 DLL 内）

### 6.1 两段式 size 查询
`xOptProblemT` 的结构类 API（`getLinearConstraints`/`get*Structure`）约定「size 指针传 null 查长度，
再填值」。CAPE-OPEN 一次性返回整段，故在 `buildProblem` 阶段**一次拉取并缓存**所有结构（nnz、行列索引），
两段式查询都从缓存返回，避免重复跨界调用。

### 6.2 setX 状态机
evaluate 必须在 setX 之后。DLL 后端保存「最近下发的 x」与 `dirty` 标志；`setX` 置脏并
`SetMINLPVariableValues`；evaluate 读取 DLL 当前点（CAPE-OPEN 对象有状态）。

### 6.3 错误/异常转码（绝不穿透）
COM `HRESULT` / IDL `CapeException` / CORBA `CORBA::Exception` 全部在 DLL 内 `try/catch` 转成
**返回值 < 0 + 日志**，与 [`xOptProblemBlackBox`](../../xOpt/src/Problem/xOptProblemBlackBox.cpp) 每方法
`try/catch + errorLog` 风格一致。任何异常不得跨越 C-ABI 边界进入 xRto 求解循环（C ABI 不传播 C++ 异常）。

### 6.4 数据编解码（COM 后端）
`CapeArrayDouble`↔`SAFEARRAY(VT_R8)`；`CapeArrayString`↔`SAFEARRAY(VT_BSTR)`（UTF-16↔UTF-8，走
`zce::CharacterConvertor`）；`CapeArrayLong`↔`SAFEARRAY(VT_I4)`。字符串生命周期：vtable 返回的
`const char*[]` 必须指向 DLL 内稳定缓存（`std::vector<std::string>`），与 BlackBox 同。

### 6.5 ABI 版本兼容（`size` 字段——「可升级」的关键）
- `xOptModelT`/`xOptProblemT` 首字段 `size_t size`。DLL 在构造时填 `sizeof(自己编译期的结构)`。
- 升级规则：**只在结构尾部追加函数指针**，永不重排/删除既有字段；新增可选指针。
- 宿主调用任一**新增/可选**函数指针前，应校验 `model.size >= offsetof(struct, newfield) + sizeof(ptr)`
  且指针非空，再调用。
- ⚠️ 现状：宿主 `xOptProblemFromBlackBoxModel` 直接调用各指针、未校验 `size`/null。本设计要求新工厂路径
  对**可选**指针加 size+null 守卫；核心 18 个 `xOptProblemT` 指针视为 v1 必备、可不守卫。把该约定写入 DLL 开发规范。

### 6.6 线程 / COM 套间
COM 需 `CoInitializeEx`（套间类型依组件而定）。注意近期提交 `Scheduler performExclusive` 严格绑定
worker：**创建 COM 对象的线程须与调用 evaluate 的线程一致**，否则需跨套间代理。此约束写入 xRto 集成说明。

---

## 7. 代码落地清单

### 7.1 `libsrc/xRtoCapeOpen`（DLL 实现，CAPE-OPEN 复杂度都在这）

**`capeopen_core`（静态库，后端无关）**
| 文件 | 内容 |
|------|------|
| `core/CapeMINLPModel.h` | 抽象接口 `ICapeMINLPModel`（§3.1）+ `CapeMINLPSize` 等数据结构 |
| `core/CapeMINLPProblemCore.{h,cpp}` | §5 映射 / 结构缓存 / setX 状态机 / 异常→负返回码；只依赖抽象 |
| `core/CapeBackendFactory.{h,cpp}` | 连接串 scheme 解析 + 后端注册表（§3.2） |
| `core/xOptModelCapeOpen.cpp` | 导出 `XOPTIF_API int xOptModel_createModel(...)`；填 `xOptModelT`（最小：handle+buildProblem）；`buildProblem` 经工厂建后端→`CapeMINLPProblemCore`→填 `xOptProblemT` 18 指针 |
| `core/CapeMINLPModelMock.{h,cpp}` | Mock 后端（已知解析解小 NLP），随 core 编译，供 gtest |

**后端（各自一个 DLL，或合并）**
| 文件 / 产物 | 内容 | 依赖 |
|-------------|------|------|
| `backend/com/CapeMINLPModelCom.{h,cpp}` → `xRtoCapeOpenCom.dll` | COM 后端：`CoCreateInstance`/`QI`、`SAFEARRAY`/`BSTR`/`HRESULT` 编解码 | COM |
| `backend/corba/CapeMINLPModelCorba.{h,cpp}` → `xRtoCapeOpenCorba.dll` | CORBA 后端：`ORB_init`/`_narrow`、复用 `SqpSolver*` stub、`CapeException`/`CORBA::Exception` 转码 | TAO+ACE |
| `marshal/CapeMarshalCom.{h,cpp}` | `SAFEARRAY`/`BSTR` ↔ `std::vector`/UTF-8 工具 | COM |
| `marshal/CapeMarshalCorba.{h,cpp}` | CORBA sequence ↔ `std::vector`/UTF-8 工具 | TAO |
| `docs/capeopen_problem_design.md` | 本文档 |

> 每个后端 DLL 在其工厂里只注册自己支持的 scheme；`CAPEOPEN_SINGLE_DLL` 开关时两后端合编为单一
> `xRtoCapeOpen.dll` 并注册全部 scheme（§3.4）。

### 7.2 `libsrc/xOpt`（仅清理，不新增接入代码）
| 文件 | 改动 |
|------|------|
| `src/xOpt.cpp` | **已做**：`xOptIsBlackBoxProblemType` 删除 CAPEOPEN 三分支；删 `xOptIsCapeOpenModelType` 函数；删 `createModel` 的 CapeOpen 分支。CAPE-OPEN DLL 以 `type_name="BlackBox"` 接入（§4），无需新增任何接入代码。|

> v4 不再需要 v3 设想的 `xOptProblemFromBlackBoxModel.h` 提升 / `xOptCreateProblemFromCApiDll` 工厂——
> 因为直接复用既有 `xOptModelBlackBox` 通路。代价转移到 DLL 侧实现完整 `xOptModelT`（§4.2）。

### 7.3 `libsrc/xRtoCapeOpen/CMakeLists.txt`
由「生成 CORBA solver `.so`」改为生成导出 `xOptModel_createModel` 的 DLL；COM 后端默认编译，CORBA 后端
`option` 控制；移除对 xOpt 的反向依赖（DLL 仅依赖 xOpt 的 C 头 `xOptModel.h`/`xOptInterface.h`）。

---

## 8. 开发计划（里程碑）

**M1 — xOpt 清理（去 CAPE-OPEN 特判）**  ✅ 已完成（2026-06）
- [x] `xOpt.cpp`：`xOptIsBlackBoxProblemType` 删 CAPEOPEN 三分支；删 `xOptIsCapeOpenModelType`；删
  `createModel` 的 CapeOpen 分支。CAPE-OPEN DLL 走既有 `type_name="BlackBox"` 通路，xOpt 零感知（§4）。
- 验收：xOpt 编译通过、无 CAPE-OPEN 残留引用（grep 干净）。

**M2 — `capeopen_core` + 抽象 + Mock 后端 + 端到端求解**  ✅ 核心已落地（2026-06）
- [x] 定义 `ICapeMINLPModel` 抽象 + `CapeBackendFactory`（内置 `mock:`，com/corba 留注册位）。
- [x] `CapeMINLPModelMock`：已知解析解小 NLP `min x0²+x1² s.t. x0+x1=3`（解 (1.5,1.5)，f*=4.5）。
- [x] `CapeMINLPProblemCore` 实现 §5 全部映射 + 结构缓存 + setX 状态机 + 异常转码（后端无关，写一遍）。
- [x] `xOptModelCapeOpen.cpp` 导出 `xOptModel_createModel`，`buildProblem` 经工厂→core→填 `xOptProblemT`。
- [x] gtest（`tests/test_capeopen_problem.cpp`）经 C vtable 驱动：size/names/bounds/initial、结构两段式
      （0-based）、obj/cons/grad/jac 对拍解析值、工厂解析/错误路径。**12/12 通过**（VS2026 + vcpkg gtest）。
- [x] `xOptModelCapeOpen.cpp` 补全为**完整 `xOptModelT` vtable**（§4.2：`getParameters≥1`、`setParameters`、
      `validateModel`、`getInPortNum/getOutPortNum`=0、`setProblemType`、slate/report/thermo 安全桩等），
      连接目标按 `XRTO_CAPEOPEN_TARGET` 环境变量 > name-with-scheme > `mock:default` 解析（§4.3）。
- [x] gtest（`tests/test_capeopen_model.cpp`）模拟 `xOptModelBlackBox` 全过程：`xOptModel_createModel`→走
      `getParameters/setParameters/validateModel/ports`→`buildProblem`→驱动 `xOptProblemT`。**15/15 通过**。
- [ ] 待办：在真实 xRto/xOpt 进程内以 `type_name="BlackBox"`、`model_path=<本 DLL>` 加载并跑通 mock 收敛
      （需链接完整 xOpt/zce，超出 core 单测范围）。

**M3 — COM 后端（`xRtoCapeOpenCom.dll`，真实价值落地）**
- `CapeMarshalCom` + `CapeMINLPModelCom`：套间初始化、`CoCreateInstance`/`QI`、`SAFEARRAY`/`BSTR`
  编解码、`HRESULT` 转码；工厂注册 `com:` / `.dll` scheme。
- 用真实/参考 COM CAPE-OPEN 组件集成测试，校准索引基、结构类型字符串、属性名。

**M4 — CORBA 后端（`xRtoCapeOpenCorba.dll`，与 COM 平行）**
- `CapeMarshalCorba` + `CapeMINLPModelCorba`：`ORB_init`/`string_to_object`/`_narrow`，复用既有
  `SqpSolver*` stub；工厂注册 `corba:` scheme。
- 跨后端一致性测试：**同一逻辑问题分别经 COM 与 CORBA 加载，断言 `xOptProblem` 输出数值一致**
  （验证抽象层等价性）。
- `option(CAPEOPEN_SINGLE_DLL)` 合编验证。

**M5 — 收尾**
- 文档补充整数松弛限制、线程套间约束、`size` 升级规范；把旧 `SqpcSovlerImpl.cpp` 求解器 servant
  迁出为独立示例或归档。

> 关键路径在 **M2**（零外部依赖即可验证整条黑箱通路 + 后端无关核心）。**M3/M4** 是两套绑定的真实落地，
> 因共享 `CapeMINLPProblemCore`，第二个后端的增量成本主要是 marshaling + 连接生命周期。

---

## 9. 测试方案

遵循仓库规范（GTest，`test_*.cpp`，`#ifndef USE_GTEST_MAIN` 包裹 main，放 `tests/`）：

- `tests/test_capeopen_problem.cpp`：
  - `CapeOpenProblem_Size_MatchesMock`
  - `CapeOpenProblem_Bounds_RoundTrip`
  - `CapeOpenProblem_JacobianStructure_TwoPassQuery_ZeroBased`
  - `CapeOpenProblem_EvaluateObjective_AgainstAnalytic`
  - `CapeOpenProblem_EvaluateJacobian_AgainstFiniteDiff`
  - `CapeOpenProblem_SetXBeforeEvaluate_Enforced`
  - `CapeOpenProblem_BackendError_ReturnsNegativeNotThrow`（注入错误，确认不跨 ABI 穿透）
  - `CapeOpenProblem_VtableSize_Compat`（旧宿主 + 新 DLL：`size` 守卫生效）
- 端到端：`xOpt::createProblem("CapeOpen", mock_dll)` + 求解器跑到收敛。

---

## 10. 风险与对策

| 风险 | 对策 |
|------|------|
| 各厂商结构类型字符串/属性名/索引基不一致 | 抽常量表；DLL 后端内部归一为 0-based；自检（结构 nnz 与 size 一致性断言） |
| C++ 异常跨 C-ABI 边界 = UB | DLL 内每个 vtable 函数 `try/catch` 全包，转负返回码（§6.3） |
| COM 套间/线程亲和与 xRto worker 绑定冲突 | §6.6 明确同线程约束；必要时加跨套间代理 |
| 整数变量被当连续 | 首版声明限制；后续 MINLP 由 xRto 外层分支定界 |
| `size`/null 未守卫导致升级崩溃 | 可选指针调用前查 `size`+null（§6.5）；写入 DLL 开发规范 |
| TAO/ACE 重依赖污染纯 COM 部署 | 共享核心 + 后端分离 DLL（§3.4）；COM-only 站点不装 CORBA DLL |
| COM 与 CORBA 行为/数值不一致（如索引基、舍入） | 两后端在抽象层归一；M4 跨后端一致性测试断言输出相等 |
| 旧求解器 servant 代码方向相反易混淆 | §2 明确角色；M5 迁出/归档 |

---

## 附：相关源码索引
- C 风格 vtable 定义：[`include/xOpt/xOptModel.h`](../../../include/xOpt/xOptModel.h)（`xOptModelT` / `xOptProblemT` / `xOptModel_createModel`）
- 复用的包装类：[`xOptProblemFromBlackBoxModel`](../../xOpt/src/Model/xOptModelBlackBox.cpp:43)
- 纯虚问题接口：[`include/xOpt/xOptProblem.h`](../../../include/xOpt/xOptProblem.h)
- 路由入口：[`xOpt::createProblem`](../../xOpt/src/xOpt.cpp:296)（`xOptIsCapeOpenModelType` 已存在于 :289）
- CAPE-OPEN 接口参考：[`SqpSolver.idl`](../SqpSolver.idl)（`ICapeMINLP` / `ICapeMINLPSystem`）
