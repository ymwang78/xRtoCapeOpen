# COExamples —— 测试黑箱模型与测试求解器

本目录为平台的 **CORBA 接入**提供参考实现级的测试素材：一个黑箱单元模型 DLL、
一个求解器 DLL、以及"用该求解器求解该模型"的演示程序。CORBA 接入本体由架构师
实施；这两个 DLL 就是未来 `xOptCorbaService` 需要托管/镜像的对象。

示例工程**自包含**：只依赖 `include/xOpt/` 下复制进来的 4 个平台头文件
（`xOptInterface.h`、`xOptModel.h`、`xOptProblem.h`、`xOptSolver.h`，
源自 `zd-cxxproj/include/xOpt/`），不引用平台源码路径、不链接任何平台库。

---

## 1. 组件总览与目录结构

```
COExamples/
├── Readme.md                      # 本文档
├── CMakeLists.txt                 # 3 个目标，产物统一输出到 build/bin/<Config>
├── build.bat                      # 一键配置 + 构建(Release) + 运行
├── include/xOpt/                  # 自包含头文件（平台接口副本）
├── testModel/
│   ├── SplitterModel.cpp          # 黑箱模型：导出 xOptModel_createModel
│   └── Splitter_Model.json        # 黑箱接入描述（部署时与 DLL 同目录）
├── testSolver/
│   ├── PenaltyGradientSolver.h    # 求解器声明（Eigen）
│   ├── PenaltyGradientSolver.cpp  # 罚函数梯度下降算法
│   └── SolverEntry.cpp            # 导出 createSolver / destroySolver
└── demo/
    ├── ProblemFromModelT.h/.cpp   # xOptProblemT → xOptProblem 桥接器
    └── main.cpp                   # 加载两个 DLL、求解、按手算基准对拍
```

与平台黑箱接入链的对应关系：

| 平台侧 | 本目录对应物 |
|--------|--------------|
| `xOptModelBlackBox` 加载模型（`GetProcAddress("xOptModel_createModel")`） | demo 步骤 1 |
| `xOptProblemFromBlackBoxModel`（C 函数表 → C++ 对象） | `demo/ProblemFromModelT` |
| `xOpt.cpp::loadSolver`（`GetProcAddress("createSolver"/"destroySolver")`） | demo 步骤 6 |
| 返回码约定 `<0 失败 / 0 正常 / >0 正常带含义`（宿主按 `ret < 0` 判错） | 全部实现遵守；错误一律负值 |
| 两段式结构查询（输出指针为 `nullptr` 时只填个数） | 模型/问题/求解器各处 |

## 2. 测试模型：Splitter（`testModel/`）

1 进 2 出分流器，纯物料平衡 + 温度/压力直通，**无任何热力学/物性计算**
（`getNumberOfThermoBlock` 返回 0），是满足完整 `xOptModelT` 函数表的最小可行
单元模型；含一个不等式约束供求解器罚函数通道演示。

### 2.1 变量（3N+6 个，N = 组分个数）

| 变量 | 含义 | 边界 | 默认初值 |
|------|------|------|----------|
| `in_T` / `out1_T` / `out2_T` | 温度 [K] | [100, 1000] | 300 |
| `in_P` / `out1_P` / `out2_P` | 压力 [kPa] | [1, 10000] | 101.325 |
| `in_fi_<Ci>` / `out1_fi_<Ci>` / `out2_fi_<Ci>` | 组分流量 | [0, 1e20] | 1.0 |

### 2.2 方程（6+4N 个约束 = 6+3N 等式 + N 不等式，残差/值形式）

| 约束 | 个数 | 类型 | `clow..cupp` |
|------|------|------|--------------|
| `out1_T − in_T = 0`、`out2_T − in_T = 0` | 2 | 等式 | 0..0 |
| `out1_P − in_P = 0`、`out2_P − in_P = 0` | 2 | 等式 | 0..0 |
| `out1_fi_Ci − r·in_fi_Ci = 0` | N | 等式 | 0..0 |
| `out2_fi_Ci − (1−r)·in_fi_Ci = 0` | N | 等式 | 0..0 |
| `in_T = 固定值`、`in_P = 固定值`、`in_fi_Ci = 固定值` | 2+N | 等式 | 0..0 |
| `out1_fi_Ci ≥ min_product_flow`（返回变量值本身） | N | 不等式 | mpf..1e20 |

目标函数恒为 0（可行性问题）；`HAS_DERIVATIVE=1`（手工提供常量雅可比）、
`HAS_LINEAR_A=1`（全部约束线性，`getLinearConstraints` 提供完整 A，
行下标为全局约束下标）。

### 2.3 参数（数值通道）

| 参数 | 默认值 | 约束 |
|------|--------|------|
| `split_ratio`（r） | 0.5 | 0 < r < 1（`validateModel` 校验） |
| `min_product_flow`（mpf） | 0.0 | 产生不等式约束 |

`getParameters` 返回 ≥1 个参数（宿主查询后会立即回送 `setParameters`）；
JSON 参数通道（`setParametersJson`/`getParametersJson`）成对置 `NULL`，
走数值通道，同时验证宿主的判空回退路径。

### 2.4 端口映射与 fixable 机制

- `getNumberOfSlate`=1，三端口共用同一 slate；`setSlate` 动态接受任意组分表。
- 端口映射（两段式查询，字符串由模型成员持有）：
  进料口 `T→in_T, P→in_P, fi_<Ci>→in_fi_<Ci>`；两个出料口同款（`out1_*`/`out2_*`）。
- **fixable 机制**：`getFixableVariables` 返回 `in_T`（默认 300）、`in_P`
  （默认 101.325）、`in_fi_*`（默认 1.0）——进料状态由上游/用户固定。
  `generateEstimate` 按宿主打包方式（固定变量名以 `'\0'` 分隔拼在连续缓冲里、
  值数组一一对应）解析固定值并缓存，随后按其填初值。
  `buildProblem` 把进料固定**作为等式约束追加进问题**
  （`in_T = 固定值` 等，等价于宿主 `xOptModelFixVars` 的效果），
  保证示例问题独立适定。

### 2.5 手算基准（demo 断言）

N=2（C1, C2），固定进料 T=300 K、P=101.325 kPa、fi=1.0/1.0，r=0.5、mpf=0.1：

```
三股流 T = 300、P = 101.325
in_fi_C1 = in_fi_C2 = 1.0
out1_fi_C1 = out1_fi_C2 = 0.5
out2_fi_C1 = out2_fi_C2 = 0.5
不等式 0.5 >= 0.1 满足
```

## 3. 测试求解器：罚函数梯度下降（`testSolver/`）

求解 `min f(x)  s.t. clow ≤ c(x) ≤ cupp, xlow ≤ x ≤ xupp`，
底层向量运算用 Eigen（`Eigen3::Eigen`，vcpkg `eigen3:x64-windows-static-md`）。

### 3.1 算法

**增广目标**（等式即 `clow==cupp`，统一按区间约束处理）：

\[
\Phi(x) = f(x) + \mu\Big[\textstyle\sum_i v_i^2
+ \sum_k \max(0,\, xl_k - x_k)^2 + \max(0,\, x_k - xu_k)^2\Big],
\quad v_i = c_i - \mathrm{clamp}(c_i,\, clow_i,\, cupp_i)
\]

**梯度**（解析优先）：
- `∇f`：`evaluateObjectiveGradient`（结构大小为 0 视为零）；
- `Jᵀw`：`evaluateConstraintsJacobianStructure` + `...JacobianValues`，
  `w_i = 2μ·v_i`；问题未提供雅可比时对 Φ 做中心差分兜底；
- 边界罚项梯度解析。

**线搜索**：方向 `d = −∇Φ`，回溯 Armijo（c=1e-4，收缩 ρ=0.5，步长从 1 起）。

**罚参数自适应**：外层 μ 从 `mu0`（默认 100）起步，内层做 `max_iter` 次
梯度下降；若最大约束违背 > `feas_tol` 则 μ×10（上限 1e10）再来一轮。

**收敛判据**：`‖∇Φ‖∞ < tol` 且最大违背（约束+边界）`< feas_tol` ⇒
可行性问题返回 `RESULT_FEASIBLE`、带目标问题返回 `RESULT_OPTIMAL`；
超限 `RESULT_ITER_LIMIT`；问题非法（MAGIC 非 `'X'`、空指针）⇒
`RESULT_INVALID_PROBLEM`。异常在 `solve()` 内全包，经日志上报 + 负返回码，
不穿透 DLL 边界。

### 3.2 可调参数与 options

| 名称 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_iter` | INT | 2000 | 每个罚参数层的最大内层迭代数 |
| `tol` | REAL | 1e-6 | 梯度收敛容差（无穷范数） |
| `feas_tol` | REAL | 1e-6 | 可行性容差（最大违背） |
| `mu0` | REAL | 100 | 初始罚参数 |

经 `getTunableParamList`/`setTunableParamList`（两段式）暴露；
值的设置走 options 通道：`setIntOptions`（`print_level`、`max_iter`）、
`setDoubleOptions`（`tol`/`feas_tol`/`mu0`）、
`getStringOptions` 只读暴露 `solver_name`。
`pauseSolve`/`continueSolve`/`Xmul`/`Fmul` 返回负值（不支持，无对偶信息）。

日志经创建时传入的 `xOptLogFunc` 输出（demo 里打印到控制台），
`print_level`：0 静默、1 外层摘要、≥2 内层每 200 次迭代。

## 4. 构建与运行

前置条件：

- Visual Studio（x64 MSVC；本机为 VS2026，`build.bat` 优先使用 VS 自带
  CMake 4.x——独立的旧版 cmake 不认识新 VS 生成器）；
- `F:\vcpkg`，已 `vcpkg install eigen3:x64-windows-static-md`
  （triplet 为 /MD 动态 CRT，与平台项目一致）。

一键复现：

```
build.bat
```

等价手工步骤：

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=F:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
build\bin\Release\xopt_demo.exe
```

三个目标 `test_splitter_model`（SHARED）、`test_penalty_solver`（SHARED）、
`xopt_demo`（EXE）统一输出到 `build/bin/<Config>`，DLL 与 exe 同目录
（`LoadLibrary` 相对名加载）；`Splitter_Model.json` 随产物复制。

### 真实运行输出摘录

```
=== xOpt 黑箱模型 + 求解器接入演示 ===

---- 步骤 4：getFixableVariables + generateEstimate（固定进料状态） ----
    可固定变量 4 个（默认值即 demo 的固定值）：
      in_T = 300
      in_P = 101.325
      in_fi_C1 = 1
      in_fi_C2 = 1
generateEstimate 成功：初值向量 12 维（进料已被固定）

---- 步骤 5：buildProblem + ProblemFromModelT 桥接 ----
问题构造成功：变量 12 个，约束 14 个

---- 步骤 8：solve() ----
    [INFO] [SplitterDemo] problem loaded: n=12, m=14, jacobian nnz=22, mu0=100.0
    [INFO] [SplitterDemo] outer round 1: mu = 1.000e+02
    [INFO] [SplitterDemo] converged at inner iter 36: Phi=2.366366e-15, |grad|inf=4.701e-07, max_viol=2.350e-09
    [INFO] [SplitterDemo] solve finished: result=1, objective=0.000000e+00, max_viol=2.350e-09
solve 返回：RESULT_FEASIBLE(1)（>=0 表示成功）

---- 步骤 9：解与约束值 ----
    变量解（X）：
      in_T         =   300.000000
      in_P         =   101.325000
      in_fi_C1     =     1.000000
      ...
      out1_fi_C1   =     0.500000
      out1_fi_C2   =     0.500000
      out2_fi_C1   =     0.500000
      out2_fi_C2   =     0.500000
    目标值 f = 0.000000
      out1_fi >= mpf [0]           =     0.500000    in [0.1, 1e+20]
      out1_fi >= mpf [1]           =     0.500000    in [0.1, 1e+20]

=====================================
DEMO PASSED
=====================================
```

（μ=100 一层即收敛：本问题是相容线性方程组，罚函数最小点恰为精确可行解。）

## 5. demo 调用序列逐步解读

`demo/main.cpp` 的每一步都打印，就是上面输出对应的脚本：

1. **加载模型**：`LoadLibrary("test_splitter_model.dll")` →
   `GetProcAddress("xOptModel_createModel")`；宿主分配 `xOptModelT` 并填
   `size = sizeof(xOptModelT)`，模型填 `handle` 与函数表（尾部扩展字段按
   `size` 覆盖范围写入）。
2. **参数**：`getParameters` 两段式查询 → 覆盖为 `split_ratio=0.5`、
   `min_product_flow=0.1` → `setParameters` 回送（宿主同样在查询后立即回送）。
3. **slate/校验**：`setSlate`（C1、C2）→ `validateModel`（0 < r < 1）。
4. **固定进料**：`getFixableVariables` 两段式 → 把固定值按
   `'\0'` 分隔打包 → `generateEstimate` 两段式（先报个数再填初值）。
5. **构造问题**：`buildProblem` 填 `xOptProblemT`（模型分配问题对象，
   `handle` 指向它，析构时经 `destroyProblem` 归还）。
6. **桥接**：`ProblemFromModelT` 把 C 函数表适配为 `xOptProblem` C++ 对象
   ——这是平台 `xOptProblemFromBlackBoxModel` 的精简镜像：模型契约只能填
   函数表，求解器消费的是纯虚接口，两者之间必须有这层适配。注意结构查询的
   `size` 参数在这里做 `int& ↔ int*` 转换。
7. **求解器**：`LoadLibrary("test_penalty_solver.dll")` →
   `createSolver("SplitterDemo", &wrapped, logger)`；options 演示后 `solve()`。
8. **对拍**：`X()` 打印解、`F()` 打印目标+约束值（平台约定 `m+1` 个值：
   `f[0]`=目标、`f[1..m]`=约束值）；逐变量与手算基准比对（容差 1e-4）、
   逐约束检查 `[clow, cupp]`，全部通过 ⇒ `DEMO PASSED`。
9. **清理**：`destroySolver` → `FreeLibrary`（求解器）→ 桥接器析构
   （`destroyProblem`）→ `destroyModel` → `FreeLibrary`（模型）。
   顺序不能乱：谁持有的对象谁先销毁，DLL 最后卸载。

## 6. 平台接入说明

### 6.1 描述 JSON（`testModel/Splitter_Model.json`）

格式仿 `apps/xRto/bin/x64/UnitModel/RCC/RCC_Model.json`，四个顶层键：

- `parameters`：参数默认值对象；
- `fixable_variables`：可固定变量名数组（本模型 = `in_T`/`in_P`/`in_fi_C1`/`in_fi_C2`）；
- `inports` / `outports`：对象数组，每个对象是
  `"<模型内部变量名>": "<流股变量名>"` 的映射（流股变量名 `T`、`P`、`fi_<组分>`）。
  它告知平台如何把上下游流股接到本模型的内部变量上；实际部署时组分条目随
  slate 调整。

该 JSON 服务于平台的 `xOptModelProblemWithDesc` 通道：走描述 JSON 时，
fixable 变量与端口映射来自 JSON 而非 DLL 函数。**demo 走 DLL 函数表通道**
（两条通道的信息必须一致）；JSON 是附带的部署参考。

### 6.2 部署形态

- 模型：`UnitModel/<名称>/` 目录下 `DLL + <名称>_Model.json`；
- 求解器：`Solver/Solver.json` 注册，条目形如
  `{"Name": "...", "SolverPath": "xxx.dll", "Parameters": {...}}`，
  宿主经 `loadSolver`（`LoadLibrary` + `GetProcAddress("createSolver")`）加载。

### 6.3 契约要点速查

- 返回码：`<0 失败`；宿主判定一律 `ret < 0`。`XOPTF_ERROR_*` 枚举是**正数**，
  返回它们会被当成成功——错误必须返回负值。
- 两段式查询：输出指针为 `nullptr` 只填个数；第二次调用才填数据。
- `xOptModelT` 尾部扩展：新字段只追加在尾部且可选，按 `model->size`
  覆盖范围（`offsetof`）写字段，不要求 `size` 恰好等于自己的 `sizeof`。
- `generateEstimate`：固定变量名以 `'\0'` 分隔打包；宿主校验返回后 `size` 未变。
- `setParametersJson`/`getParametersJson` 必须成对实现或成对 `NULL`。
- 端口映射返回的 `const char*` 必须活得够久（存成员容器，别返回临时 `string`）。
- `XOPTIF_API`：导出方在包含头文件前 `#define XOPTIF_API __declspec(dllexport)`；
  纯消费方（如 demo）定义为空，避免 `dllimport` 修饰造成链接问题。

## 7. 给架构师的 CORBA 提示

- 这两个 DLL 即未来 `xOptCorbaService.exe` 宿主的对象：
  **模型**经 `xOptModelT` 函数表镜像为 IDL 接口（每个函数指针一个远端方法，
  两段式查询原样保留——跨进程时"先报个数再传数据"正好省一次序列化往返）；
  **求解器**经 `createSolver` + `xOptProblem` 回调镜像（问题的 `evaluate*`
  需要反向回调模型侧，方向与模型查询相反，IDL 设计时注意双向引用与生命周期）。
- `demo/ProblemFromModelT` 就是 CORBA 服务端需要的同款适配：
  把远端函数表（等价于 `xOptProblemT`）适配成本地 `xOptProblem` 对象交给求解器。
- 返回码与 `'\0'` 打包、`size` 覆盖等约定跨语言/跨进程时逐条保留，
  可直接拿本目录的 demo 输出做端到端对拍基准（手算值见第 2.5 节）。

---

## 附：已知环境事项

- `VCPKG_ROOT = D:\GitHub\vcpkg`（非 `vcpkg_requirements.bat` 注释里的旧路径）。
- 编码规范与平台仓库一致：源文件 UTF-8 BOM、类 PascalCase、函数 camelCase、
  变量 snake_case、成员尾下划线。
- 构建产物在 `build/`（不入库）；重新构建前如遇生成器缓存冲突，删掉 `build/` 重来。
