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
- **N2 — COM 前端**：`CoMINLP` 实现官方 IID 的 `ICapeMINLP`(+`ICapeIdentification`) 委托 adapter；
  类厂 + DLL 导出（`DllGetClassObject`/`DllRegisterServer`…）+ 自铸 CLSID/ProgID + MINLP CATID 注册。
  测试：注入式驱动 vtable + **回环经 capeopen_core 的 COM 后端**。被包装 DLL 路径经 `XRTO_XOPT_PROBLEM_DLL`。
- **N3 — COM 注册 + 跨客户端冒烟**：regsvr32 + 独立客户端 `CoCreateInstance` 验证。
- **N4 — CORBA 前端**：`MINLPServant : POA_SqpSolver::ICapeMINLP` 委托 adapter，导出 IOR；
  测试：collocated 回环经 capeopen_core 的 CORBA 后端。
- **N5 — 扩展输入 + 打包/文档**：再支持 C-ABI 输入（`xOptModel_createModel`/`xOptProblemT`）；
  打包 COM DLL + CORBA server。

## 5. 风险

| 风险 | 对策 |
|------|------|
| `xOptProblem` C++ ABI 跨 DLL（须同编译器/同 CRT） | 与 `xOptProblemBlackBox` 同约束；文档声明；注入路径单测不受影响 |
| `ICapeMINLP` 是 dual 接口，消费者可能后期绑定（`Invoke`） | 首版实现 vtable（早绑定，主流）；如需配 typelib + `IDispatchImpl`（N2 标注） |
| 组件配置（被包装 DLL 路径）传递（`CoCreateInstance` 无参） | 环境变量 `XRTO_XOPT_PROBLEM_DLL` / sidecar（同 capeopen_core §4.3） |
| 自铸 CLSID/ProgID + CAPE-OPEN 类别注册 | N2 处理；CLSID 写入仓库常量 |
| 真实求解器调用未实现的方法（Hessian/属性等） | 返回空 / `NO_IMPLEMENT`；按真实消费者反馈补 |

## 附：N1 落地清单
- `xOptMINLPco/XOptMINLPAdapter.{h,cpp}`：加载器 + adapter（实现 `ICapeMINLPModel`）。
- `tests/test_xoptminlpco_adapter.cpp`：`MockXOptProblem` + adapter 对拍（size/names/bounds/结构/求值）。
- `xOptMINLPco/CMakeLists.txt`：静态库 `xoptminlpco` + 测试（Release）。
