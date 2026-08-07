# xOptMINLPco 设计文档：把 xOptProblem DLL 发布为 CAPE-OPEN MINLP 组件

> 版本 v1 · 2026-06-17 · 适用模块：`libsrc/xRtoCapeOpen/xOptMINLPco`
>
> 这是 [`capeopen_problem_design.md`](capeopen_problem_design.md) 的**反向**工程。

## 1. 目标

把一个符合 **`xOptProblem`** 接口的 DLL，包装成实现 **CAPE-OPEN MINLP（`ICapeMINLP`）** 的组件
DLL（产物名 **`xOptMINLPco.dll`**），使 CAPE-OPEN 的 PME / MINLP 求解器能驱动我们的优化问题。

**先做 C++ ABI 输入**：被包装 DLL 导出 `extern "C" xOptProblem* createProblem();` /
`void destroyProblem(xOptProblem*);`（见 [`xOptProblem.h`](../../../include/xOpt/xOptProblem.h)）。
C-ABI 输入（`xOptModel_createModel`/`xOptProblemT`）留作 N5。

| 子项目 | 方向 | 角色 |
|--------|------|------|
| `capeopen_core` | CAPE-OPEN → xOpt | **client**：消费 CAPE-OPEN MINLP，暴露成 `xOptProblem` |
| **`xOptMINLPco`（本）** | xOpt → CAPE-OPEN | **server**：消费 `xOptProblem`，发布成 CAPE-OPEN MINLP 组件 |

## 2. 架构（最大化复用 capeopen_core）

```
xOptProblem C++ DLL (createProblem/destroyProblem)
  └─ XOptMINLPAdapter : public ICapeMINLPModel        ← 复用 capeopen_core 的抽象，反向填充
        （numVariables/structure/bounds/setX/evaluate* → ICapeMINLPModel 语义；结构缓存；0-based）
            ▲
   COM 前端: CoMINLP : ICapeMINLP(+ICapeIdentification, IDispatch)  → 委托 adapter   (N2)
   CORBA 前端: MINLPServant : POA_SqpSolver::ICapeMINLP            → 委托 adapter   (N4)
```

**关键复用**：`XOptMINLPAdapter` 实现 capeopen_core 已有的 `ICapeMINLPModel` 抽象，于是
- COM/CORBA 前端只把收到的 `ICapeMINLP` 调用转成 `ICapeMINLPModel` 调用；
- 直接复用 `CapeOpenComInterfaces.h`（官方 IID + vtable）、`CapeVariantMarshal`、`SqpSolver` IDL + `CapeCorbaMarshal`；
- **回环测试免费**：`xOptProblem(mock) → xOptMINLPco 的 ICapeMINLP → capeopen_core 的 CapeMINLPModelCom/Corba(注入) → 读回 ICapeMINLPModel`，断言与原始一致——一次验证两个方向。

## 3. 接口映射（capeopen_core §5 的逆）

| `ICapeMINLP`（我们实现） | ← `xOptProblem` 来源 |
|--------------------------|----------------------|
| `GetMINLPSize` | `numVariables`/`numConstraints` + jacobian nnz + objgrad size + 线性约束数 |
| `GetMINLPStructure("Jacobian"/"ObjectiveGradient")` | `getConstraintJacobianStructure` / `getObjectiveGradientStructure`（两段式，缓存） |
| `GetMINLPVariable/ConstraintNames` | `getVariableNames`/`getConstraintNames` |
| `GetMINLPVariable/ConstraintBounds` | `getVariableBounds`/`getConstraintBounds` |
| `GetMINLPVariableValues` | 当前 x（由 `getInitialX` 初始化，`SetMINLPVariableValues` 更新） |
| `SetMINLPVariableValues` | `setX` |
| `GetMINLPNonlinearConstraintValues` | `evaluateConstraints` |
| `GetMINLPConstraintDerivativeValues("Jacobian")` | `evaluateConstraintsJacobianValues` |
| `GetMINLPNonlinearObjectiveFunctionValue` | `evaluateObjective` |
| `GetMINLPObjectiveFunctionDerivativeValues` | `evaluateObjectiveGradient` |
| Hessian / Lagrange / 属性 / 变量类型 | 返回空 / `NO_IMPLEMENT`（首版） |

> `vids`/`cids`：按 0-based 索引子集挑选；空表示「全部」。

## 4. 里程碑

- **N1 — provider 核心（无传输）**  ← 本次开始
  `XOptMINLPAdapter`（实现 `ICapeMINLPModel`，背靠 `xOptProblem`；C++ ABI 加载器 + 注入构造）
  + `MockXOptProblem`（二次问题）+ gtest（adapter 直接对拍）。零外部依赖、可立即测。
- **N2 — COM 前端**  ✅ 已落地（2026-06）
  - [x] `CoMINLP` 实现官方 IID 的 `ICapeMINLP`（前 23 方法）委托 `ICapeMINLPModel`（生产 ctor 读
    `XRTO_XOPT_PROBLEM_DLL` 建 `XOptMINLPAdapter`；注入 ctor 供测试）。复用 capeopen_core 的
    `CapeOpenComInterfaces.h` + `CapeVariantMarshal` + 官方 IID。
  - [x] `xOptMINLPcoServer.cpp`：类厂 + `DllGetClassObject`/`DllCanUnloadNow`/`DllRegisterServer`/
    `DllUnregisterServer`，自铸 CLSID `{7B2C9E10-5A3D-4C8E-9F21-0A1B2C3D4E5F}` + ProgID `xOpt.MINLP.1`；
    产物 **`xOptMINLPco.dll`**（导出 4 个 COM 入口已核对）。
  - [x] **回环测试**（`tests/test_xoptminlpco_com.cpp`）：mock → adapter → `CoMINLP`(打包 VARIANT)
    → capeopen_core `CapeMINLPModelCom`(注入, 解包) → 对拍。**1/1 通过**（adapter 4/4，共 5/5）。
  - [ ] 待办：`ICapeIdentification`（名称/描述）+ MINLP CATID 注册（移交 N3）。
  - 构建注记：DLL 导入库改名 `xOptMINLPco_import.lib` 避免与静态库 `xoptminlpco.lib` 大小写冲突
    （LNK1149）；`<olectl.h>` 提供 `SELFREG_E_CLASS`；本机构建用 `-DVCPKG_APPLOCAL_DEPS=OFF`（applocal 缺 dumpbin）。
- **N3 — COM 注册 + 激活冒烟**  ✅ 已落地（2026-06）
  - [x] `ICapeIdentification`（官方 IID `{678C0990-…}`，名称/描述）加到 `CoMINLP`（多继承
    `ICapeMINLP`+`ICapeIdentification`，单份 IUnknown/IDispatch 覆盖两个基类）。
  - [x] `DllRegisterServer`/`DllUnregisterServer` 注册到 **`HKCU\Software\Classes`**（免管理员）。
    标识 `xOptMINLPcoClsid.h` 共享。
  - [x] 测试夹具 `tests/mock_xoptproblem_dll.cpp` → `mock_xoptproblem.dll`（导出 createProblem/
    destroyProblem 返回 MockXOptProblem）。
  - [x] `tests/test_xoptminlpco_register.cpp`：注册 → `CoCreateInstance(CLSID)` → 类厂 → 生产
    `CoMINLP()`（经 `XRTO_XOPT_PROBLEM_DLL` 加载 mock DLL）→ 驱动 `ICapeMINLP`(obj=25) +
    `ICapeIdentification` → 注销。**1/1 通过**（注册失败则 GTEST_SKIP）。验证注入未覆盖的整条
    激活/类厂/生产 ctor/跨 DLL 加载路径。
  - [ ] 待办：真正独立进程的客户端 exe；MINLP CATID 注册（PME 发现）。
- **N4 — CORBA 前端**  ✅ 已落地（2026-06）
  - [x] `xOptMINLPco/MINLPServant.{h,cpp}`：POA 实现 `SqpSolver::ICapeMINLP` 委托 `ICapeMINLPModel`
    （生产 ctor 读 `XRTO_XOPT_PROBLEM_DLL`；注入 ctor 供测试）。复用 `SqpSolver*` stub + `CapeCorbaMarshal`。
  - [x] **回环测试**（`tests/test_xoptminlpco_corba.cpp`）：mock → adapter → `MINLPServant`(collocated
    POA) → capeopen_core `CapeMINLPModelCorba`(注入) → 对拍。**1/1 通过**（vcpkg `ace[tao]` static-md）。
  - 构建：`-DWITH_XOPTMINLPCO_CORBA=ON -DTAO_TRIPLET_DIR=<…/x64-windows-static-md>`；CMake 经
    `add_subdirectory(core)` + `WITH_CAPEOPEN_CORBA=ON` 拉起 `capeopen_corba`（含 tao_idl 重生成）。
  - [x] **独立进程 CORBA server**（2026-08）：`xOptMINLPco/MINLPCorbaServer.cpp` →
    `xOptMINLPcoCorbaServer.exe`。`ORB_init`(消费 `-ORB*`) → RootPOA → `MINLPServant`(生产 ctor)
    → `activate_object` → `object_to_string` → IOR 打 stdout + 原子写 `--ior-file`(先 `.tmp` 再
    改名，避免读者读到半截) → `orb->run()`；SIGINT/SIGTERM → `orb->shutdown()`。
    **为什么必须是 exe**：CORBA 没有 COM 的进程内激活（注册表 + `DllGetClassObject` 把组件
    加载进客户端进程），外部 ORB 只能经 IIOP 连到一个真在跑 ORB 的进程。
  - [x] **跨进程冒烟**（`tests/test_xoptminlpco_corba_ipc.cpp`）：起 server → 轮询 IOR（server
    提前退出则带退出码立即失败，不干等超时）→ `CapeBackendFactory("corba:IOR:…")` →
    `string_to_object`/`_narrow` → 驱动对拍。**1/1 通过**（xOptMINLPco 共 6 测试）。
    同时验掉 capeopen_core M4 挂着的 production 连接串路径——collocated 用例走注入构造，
    从不碰 `string_to_object`/`_narrow`，也就验不到 IIOP。
  - 已验证的证物：`tao_catior -f <ior>` 解出 `The Type Id: "IDL:SqpSolver/ICapeMINLP:1.0"`，
    IIOP 1.2、TAO ORB type。**注意这个 Repository ID 不是 CAPE-OPEN 官方的**，见 §6。
  - 构建注记：IPC 测试里 ACE/TAO 头必须排在 `<windows.h>` 之前——ACE 拉 `winsock2.h`，
    `windows.h` 默认拉 `winsock.h`(v1)，顺序反了会炸出 `IPPROTO_IPV6`/`timeval` 一片重定义。
  - server 的控制台输出一律 ASCII：源码是 UTF-8 而 Windows 控制台默认 GBK，中文会乱码——
    这个 exe 正是拿去给第三方演示的。
  - [ ] 待办：Naming Service 绑定（`corbaname:`）——需另跑 naming 进程才有意义、也才测得了，
    故与本次分开。IOR 这条路不依赖任何外部服务。
- **N5 — C-ABI 输入 + 打包**  ✅ 已落地（2026-06）
  - [x] `XOptMINLPAdapter` 重构为内部 `IXOptProblemView` 抽象 + 两实现：`CppProblemView`(xOptProblem*)
    / `CapiProblemView`(xOptProblemT vtable，int&↔int* 转换)。`connect()` 自动探测 DLL 导出
    （优先 `xOptModel_createModel`→C ABI；否则 `createProblem`→C++ ABI）；另加 `xOptProblemT` 注入 ctor。
  - [x] `tests/test_xoptminlpco_capi.cpp`：in-proc 填 `xOptProblemT`（二次问题）注入 adapter，对拍。**1/1**。
    既有 C++ ABI 路径（adapter/com/register/corba）重构后全绿（xOptMINLPco 共 8 测试）。
  - 打包：COM 产物 `xOptMINLPco.dll`（N3 可注册激活）；CORBA `MINLPServant`（lib，N4 collocated 验证）。
  - [x] VS 工程 `xOptMINLPco.vcxproj`(+`.filters`)：直接生成 COM `xOptMINLPco.dll`（v145、x64/Win32、
    `NotUsing` PCH、`SDLCheck=false`、`/DEF:xOptMINLPco.def`、链接 ole32/oleaut32/uuid）。源含本项目
    3 文件 + capeopen_core 的 `CapeMINLPModelCom.cpp`/`CapeVariantMarshal.cpp`（官方 IID + VARIANT marshaling）。
    msbuild Release|x64 验证导出 4 个 COM 入口。CORBA 前端因需环境相关的 TAO 路径，仍由 CMake 构建。
  - [ ] 待办：独立进程 CORBA server（IOR）；MINLP CATID 注册；正式 install 规则。

## 5. 风险

| 风险 | 对策 |
|------|------|
| `xOptProblem` C++ ABI 跨 DLL（须同编译器/同 CRT） | 与 `xOptProblemBlackBox` 同约束；文档声明；注入路径单测不受影响 |
| `ICapeMINLP` 是 dual 接口，消费者可能后期绑定（`Invoke`） | 首版实现 vtable（早绑定，主流）；如需配 typelib + `IDispatchImpl`（N2 标注） |
| 组件配置（被包装 DLL 路径）传递（`CoCreateInstance` 无参） | 环境变量 `XRTO_XOPT_PROBLEM_DLL` / sidecar（同 capeopen_core §4.3） |
| 自铸 CLSID/ProgID + CAPE-OPEN 类别注册 | N2 处理；CLSID 写入仓库常量 |
| 真实求解器调用未实现的方法（Hessian/属性等） | 返回空 / `NO_IMPLEMENT`；按真实消费者反馈补 |

## 6. CORBA 侧的 CAPE-OPEN 合规性（**未完成**，阻塞项）

COM 侧已经是合规的：用官方 IID `{678C09CC-7D66-11D2-A67D-00105A42887F}`
（`core/backend/com/CapeOpenComInterfaces.h`），`QueryInterface(IID_ICapeMINLP)` 成功本身就是证明。

**CORBA 侧不是。** CORBA 里接口的身份是 Repository ID，地位等同 COM 的 IID：

| | 我们现在 | CAPE-OPEN 官方 |
|---|---|---|
| IDL 模块 | `module SqpSolver`（`SqpSolver.idl`） | `CAPEOPEN100::Business::Numeric::Minlp` |
| Repository ID | `IDL:SqpSolver/ICapeMINLP:1.0` | `IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLP:1.0` |

### 6.1 官方模块路径（已确证）

`docs/` 里**没有** `CAPE-OPENv1-0-0.idl` 本体（全目录只有 PDF + 两个 `07_CO_Sequential_Modular_
Specific_Tools.zip`；后者里的 3 个 `.idl` 是 SMST 规范的占位文件，无 module、无 pragma，与 MINLP 无关）。

但模块骨架在文档里**被完整内联**了：`Methods&Tools_Integrated_Guidelines.pdf` §「IMPLEMENTATION
SPECIFICATION FOR CORBA PLATFORM」把 `CAPE-OPENv1-0-0.idl` 的 module 结构原样列了出来：

```
module CAPEOPEN100 {
  module Common { Types(完整列出) | Error | Identification | Collection | Utilities | Parameter | Persistence }
  module Cose   { SContext }
  module Business {
    module PhyProp { Thrm{Cose,ThermoSystem,CalculationRoutine,EquilibriumServer} | Reactions | Ppdb }
    module Numeric { Solvers{Eso,PdaEso,Model,Solver} | Minlp | Pedr }   ← ICapeMINLP 在 Minlp
    module UnitOp  { Unit }
    module Other   { Smst | Psp }
  }
}
```

同文并明确：「The OMG directive `#pragma version` … **It isn't used here**」，全文再无 `#pragma prefix`。
于是 RID 就是模块路径直推、版本恒为 `1.0`：

| | 我们现在 | 官方 |
|---|---|---|
| RID | `IDL:SqpSolver/ICapeMINLP:1.0` | `IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLP:1.0` |
| 标识接口 | 无 | `IDL:CAPEOPEN100/Common/Identification/ICapeIdentification:1.0` |

与 `Optimisation_Interface_Specification.pdf` §4.2（p.57）、`Identification Common Interface.pdf` §4.2
互相印证。**之前「可能有 pragma prefix」的疑点到此撤销。**

我们这边的实测值可复现：`tao_catior -f <ior>` → `The Type Id: "IDL:SqpSolver/ICapeMINLP:1.0"`，
或直接看生成的 stub `SqpSolverS.cpp:2032`。

### 6.2 三个合规缺口

**缺口 1 —— Repository ID（致命）**。第三方客户端做 `Minlp::ICapeMINLP::_narrow(obj)`，`_narrow`
内部是 `_is_a("IDL:CAPEOPEN100/...")`，我们的 servant 答 false，narrow 返回 nil。**方法名和签名
对得上毫无用处，`_narrow` 只认 RID。** 换模块之前，跨进程跑通只证明「这是一个能用的 CORBA 对象」，
不证明「这是一个 CAPE-OPEN 组件」。

**缺口 2 —— 索引基（致命，且回环测试永远抓不到）**。规范全篇 20 处明写
「Constraints and variables in the MINLP are numbered starting from 1」、`vids` 合法范围 `1,...,nv`。
**我们全线 0-based**：`CapeMINLPProblemCore::allVariableIds()` 生成 `0..nv-1` 直接当 `vids` 发出去；
`XOptMINLPAdapter::pick()` 按 0-based 取；`GetMINLPStructure` 的 rowindex/columnindex 直接透传
xOptProblem 的 0-based 下标。全程无转换。

后果双向都错：
- 作为 provider：真实求解器发 `vids=1..nv`，我们按 0-based 取 → 整体错位一格，且 `vid=nv` 越界，
  `pick()` 静默返回 `T{}`（**最后一个变量变成 0，不报错**）。
- 作为 consumer：我们发 `vids=0..nv-1` 给真实组件，0 越界、nv 漏掉。

之所以一直没暴露：每个回环测试的两端都是我们自己，双方一致地错，自洽。这正是回环测不出来的那类缺陷。

**缺口 3 —— 方法覆盖**。规范 §3.6.1 完整定义了 **32** 个 `ICapeMINLP` 方法，
`SqpSolver.idl` 只声明了 **17** 个（缺 `GetMINLPVariableTypes`、`GetMINLPConstraintLinearity`、
`GetMINLPObjectiveFunctionType`、Boolean/Integer/String 属性族、Hessian 三件套等 15 个）。

### 6.3 修法

**步骤 1（IDL 重建）✅ 已落地（2026-08）**

`CAPEOPEN100_Minlp.idl` —— 官方模块路径的 IDL 子集，由
`tools/gen_capeopen_minlp_idl.py` 从 docs/ 的规范 PDF **解析生成**（不是手抄：32 个操作 x 平均
3 个参数 + raises 子句，手抄必错）。生成器在任一参数类型/返回值无法解析时直接拒绝出文件。
逐节出处与「已知偏差」写在 .idl 文件头。

- `tao_idl` 编译通过；生成的 `Minlp` 模块下**只有**官方该有的 6 个 RID
  （enum + 2 个自有异常 + 3 个接口）——签名里的类型一律全限定，本地 typedef 会在 `Minlp` 下
  造出官方没有的类型 RID。
- 实测 RID：`IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLP:1.0`、
  `IDL:CAPEOPEN100/Common/Identification/ICapeIdentification:1.0`。
- CMake：`capeopen100_minlp` 静态库（编译检查，防腐烂）+
  `tests/test_capeopen100_rid.cpp` 把 RID 显式钉死——RID 由 module 嵌套隐式推出，
  改错了编译照过、测试照绿，只在对接真实 PME 时才炸。已用反向验证确认该断言非空转。
- 构建注记：**.idl 必须纯 ASCII（含注释）**。tao_idl 用 cl.exe 预处理，CP936 下 UTF-8 全角字符
  （如 `）`= `EF BC 89`）的尾字节会和行尾 `0A` 凑成双字节字符**把换行吃掉**，导致 `//` 注释
  吞掉下一行——实测让 `module Types {` 整行消失、报出 64 个假的 lookup 错误。生成器已断言 ASCII。

**步骤 2（servant 迁移 + 1-based）✅ 已落地（2026-08）**

两件事一起做，因为动的是同一批方法，且单改索引基会让回环测试全红（两端得同步改）。

- `MINLPServant` 改继承 `POA_CAPEOPEN100::Business::Numeric::Minlp::ICapeMINLP`，32 个方法
  全部实现（缺口 3 关闭）。ICapeMINLPModel 覆盖不到的（属性族、变量类型、约束线性性、
  Lagrange）抛 `CORBA::NO_IMPLEMENT`——系统异常，不需要出现在 raises 子句里，且语义准确；
  Hessian 三件套抛规范专设的 `ECapeHessianInfoNotAvailable`。**不返回空值冒充真答案。**
- 消费端 `CapeMINLPModelCorba` 同步迁移：用自造模块的 stub 去 `_narrow` 一个真实 CO 组件
  必然返回 nil，消费端不迁就永远连不上真实组件。
- **索引基（缺口 2 关闭）**：约定为「`ICapeMINLPModel` 及其以内保持 0-based，转换只发生在
  CAPE-OPEN 线上边界」，即消费端与生产端两处，每个方向各一个转换点。
  换基函数与普通 long 序列的转换函数**分开命名**（`indicesToWire`/`indicesFromWire` vs
  `toLongSeq`/`fromLongSeq`）：ICapeMINLP 里既有要换基的量也有不换基的量
  （`GetMINLPVariableIntegerAttribute` 的 values 就是普通整数属性），共用一个函数时误用无征兆。
- 越界 id 抛 `CORBA::BAD_PARAM` 而非静默放过。v1 的 `pick()` 对越界 id 返回 `T{}`，
  于是「求解器发了个非法 id」会变成「悄悄返回 0」。
- `RefCapeMINLPServant`（capeopen_core 里扮演第三方 CO 组件的参考实现）同步迁移，并**刻意
  不复用** `indicesToWire/FromWire`，自己写一份加减一。它存在的意义就是给消费端换基做独立
  校验；共用 helper 的话，helper 里的错会让两端一致地偏、测试照样全绿。
- 新增 `tests/test_xoptminlpco_onebased.cpp`：**非回环**校验，直接对着 CORBA stub 手工构造
  1-based 入参、手工写死 1-based 期望值。已反向验证——把 servant 改回 0-based 时 4 条断言
  如实失败。
- 实测证物：`tao_catior` 现在解出
  `The Type Id: "IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLP:1.0"`。
- 测试：capeopen_core 3/3，xOptMINLPco 7/7。

**步骤 3（ICapeIdentification）✅ 已落地（2026-08）**

先前一版文档说「Optimisation 规范未提 Identification，等官方 .idl 再定」——那是只查了 Optimisation
一份文档就下的结论，不成立。翻 Common Interface 那几份后，规范其实把两件事都说清楚了：

- **必须提供**。`Methods&Tools_Integrated_Guidelines.pdf` §9.2.2：
  「All PMC objects (primary and secondary) have to provide Common Interfaces services to any PME
  such as **an identification process** and an error handling strategy」，用例图里
  "Identifying PMC Object" 对 primary/secondary 均标 mandatory。
- **CORBA 下靠 IDL 继承**。同文 §6.1.2：「the interface diagram can map the inheritance
  relationships directly into the CIDL」，与 §6.1.1 COM 的「write the MIDL assuming an
  implementation that is **not** done with custom interface inheritance」（即 QueryInterface）
  形成明确对照。同文另附 CIDL 实例
  `interface ICapeThermoSystem : Cape::ICapeIdentification`。
- 旁证：`CO_Unit_Operations_v6.25.pdf`「Unit Operation exposes at least the interfaces ICapeUnit,
  ICapeUtilities, ICapeIdentification」——用词是 exposes，对两种绑定中立。
- `Identification Common Interface.pdf` §5.2「CORBA Specifications」是空的（只有标题），
  所以整合方式的依据只能是上面的 Methods&Tools。

于是三个接口都加上了 `: ::CAPEOPEN100::Common::Identification::ICapeIdentification`。
**这一条是按通用规则推的，不是 Optimisation 规范的明文**（该规范全文未提 Identification，
Figure 2 也没画这层继承）。之所以仍然加：风险不对称——官方 IDL 若有而我们没有，PME 拿不到
标识，违反强制要求；官方若没有而我们加了，代价只是多 4 个方法、`_is_a` 多答一个 true。
继承**不改变** `ICapeMINLP` 自身的 RID（已实测：IOR 的 Type Id 不变）。

附带好处：原先担心的「一个引用要同时应答两个 RID、需多继承两个 POA 骨架并处理
`_is_a`/`_primary_interface` 歧义」不复存在——IDL 继承后 TAO 生成单一骨架。

实现：`MINLPServant` 与 `RefCapeMINLPServant` 各补 4 个方法；名称/描述与 COM 侧 `CoMINLP`
用同一对字符串（`"xOpt MINLP"` / `"xOpt problem published as CAPE-OPEN MINLP"`），两个绑定
对外身份一致。测试断言的是**从 ICapeMINLP 引用 `_narrow` 到 ICapeIdentification**——那才是
PME 实际走的路径，也是 `_is_a` 真正被考的地方。

### 6.4 当前能力画像：**NLP-only profile**（对第三方须明示）

我们实现的是 `ICapeMINLP` 的全部 32 个操作，但**能力上不是完整的 MINLP**。对接第三方 PME
前必须讲清楚，免得对方以为拿到的是完整实现：

| 能力 | 状态 | 说明 |
|------|------|------|
| 连续变量、非线性约束、目标与一阶导数 | ✅ 完整 | 这是 `xOptProblem` 的能力范围 |
| 变量类型 / 约束线性性 | ⚠️ 推导 | `GetMINLPSize` 上报 `niv=0`/`nlc=0`，故一律回 false。**这不是编造**——是从我们自己上报的规模推出来的。若哪天 `niv>0` 而又说不出是哪几个，则抛 `NO_IMPLEMENT` |
| Boolean/Integer/String 属性族 | ❌ `NO_IMPLEMENT` | `ICapeMINLPModel` 无此概念 |
| Hessian | ❌ `ECapeHessianInfoNotAvailable` | 规范为此专设的异常 |
| Lagrange 乘子 | ❌ `NO_IMPLEMENT` | |

一律不返回空值冒充真答案：求解器分不出「空梯度」和「我没实现」。

### 6.5 已知缺口：**COM 绑定仍是 0-based**

缺口 2 这一轮只修了 CORBA 侧。规范的「variables and constraints are numbered starting
from 1」写在接口规范里，**与绑定无关**，所以 COM 侧同样适用；而 `CoMINLP`（生产端）与
`CapeMINLPModelCom`（消费端）目前仍是 0-based 直通。

于是两个绑定的线上语义现在是**分裂**的：CORBA 合规、COM 不合规。COM 侧的回环测试同样
测不出来（两端都是我们自己，一起偏）。修法与 CORBA 侧同构：在 `CoMINLP` /
`CapeMINLPModelCom` 这两个边界换基，并补一条非回环测试。

未在本轮一并修，是因为它需要自己的测试改造，且会把这个已经很大的 PR 再撑一圈。
**在修掉之前，不要对外宣称 COM 绑定符合 CAPE-OPEN 索引约定。**

**步骤 4 证明链**：`tao_catior` 看 Type Id → 第三方 ORB（omniORB/JacORB）**用官方 IDL 自己
生成 stub** 写客户端、`_narrow` 成功并驱动 → Wireshark GIOP 解析看 `operation` 字段。
最后一条最强：整条链路里没有我们的任何东西。

## 附：N1 落地清单
- `xOptMINLPco/XOptMINLPAdapter.{h,cpp}`：加载器 + adapter（实现 `ICapeMINLPModel`）。
- `tests/test_xoptminlpco_adapter.cpp`：`MockXOptProblem` + adapter 对拍（size/names/bounds/结构/求值）。
- `xOptMINLPco/CMakeLists.txt`：静态库 `xoptminlpco` + 测试（Release）。
