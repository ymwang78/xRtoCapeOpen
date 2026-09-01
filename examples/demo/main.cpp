// ***************************************************************
//  demo/main.cpp
//  -------------------------------------------------------------
//  演示程序：用测试求解器求解测试模型的问题，并按手算基准对拍。
//
//  流程（与平台宿主对黑箱模型的调用序列一致）：
//    1. LoadLibrary 模型 DLL → xOptModel_createModel 填 xOptModelT
//    2. getParameters(+setParameters) → setSlate → validateModel
//       → getFixableVariables + generateEstimate 固定进料
//    3. buildProblem 填 xOptProblemT
//    4. ProblemFromModelT 桥接：C 函数表 → xOptProblem C++ 对象
//    5. LoadLibrary 求解器 DLL → createSolver
//    6. options 演示 → solve()
//    7. X() 打印解、F() 打印目标与约束值
//    8. 断言对拍：与手算值一致 ⇒ DEMO PASSED
//
//  手算基准：N=2（C1,C2），固定进料 T=300 K、P=101.325 kPa、
//  fi=1.0/1.0，r=0.5、mpf=0.1
//    ⇒ 三股流 T=300、P=101.325；in_fi=1.0，out1_fi=out2_fi=0.5
// ***************************************************************

#include <windows.h>

// 必须在包含平台头文件之前：demo 不导入平台类（见 ProblemFromModelT.h）
#ifndef XOPTIF_API
#define XOPTIF_API
#endif

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "xOpt/xOptModel.h"
#include "xOpt/xOptProblem.h"
#include "xOpt/xOptSolver.h"

#include "ProblemFromModelT.h"

namespace {

constexpr double kTolerance = 1e-4;  // 对拍容差

// ---- 日志回调：求解器经它输出迭代日志（演示日志通道）----
void demoLogFunc(ZLOG_LEVEL level, const char* format, ...) {
    static const char* level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    const char* tag = (level >= ZLOG_TRACE && level <= ZLOG_FATAL) ? level_names[level] : "LOG";
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    printf("    [%s] %s\n", tag, buf);
}

void stepBanner(int no, const char* title) {
    printf("\n---- 步骤 %d：%s ----\n", no, title);
}

const char* resultName(int result) {
    switch (result) {
        case xOptSolver::RESULT_OPTIMAL: return "RESULT_OPTIMAL(0)";
        case xOptSolver::RESULT_FEASIBLE: return "RESULT_FEASIBLE(1)";
        case xOptSolver::RESULT_UNKNOWN: return "RESULT_UNKNOWN(-1)";
        case xOptSolver::RESULT_INFEASIBLE: return "RESULT_INFEASIBLE(-2)";
        case xOptSolver::RESULT_UNBOUNDED: return "RESULT_UNBOUNDED(-3)";
        case xOptSolver::RESULT_ITER_LIMIT: return "RESULT_ITER_LIMIT(-4)";
        case xOptSolver::RESULT_INVALID_SETTINGS: return "RESULT_INVALID_SETTINGS(-5)";
        case xOptSolver::RESULT_NUMERICAL_ISSUES: return "RESULT_NUMERICAL_ISSUES(-6)";
        case xOptSolver::RESULT_INVALID_PROBLEM: return "RESULT_INVALID_PROBLEM(-7)";
        default: return "UNKNOWN";
    }
}

// 手算基准（变量名 → 期望值）
struct ExpectedVar {
    const char* name;
    double value;
};
const ExpectedVar expected_vars[] = {
    {"in_T", 300.0},       {"in_P", 101.325},   {"in_fi_C1", 1.0},
    {"in_fi_C2", 1.0},     {"out1_T", 300.0},   {"out1_P", 101.325},
    {"out1_fi_C1", 0.5},   {"out1_fi_C2", 0.5}, {"out2_T", 300.0},
    {"out2_P", 101.325},   {"out2_fi_C1", 0.5}, {"out2_fi_C2", 0.5},
};

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("=== xOpt 黑箱模型 + 求解器接入演示 ===\n");

    // ============================================================
    // 步骤 1：加载模型 DLL，调唯一导出 xOptModel_createModel
    // ============================================================
    stepBanner(1, "加载模型 DLL（test_splitter_model.dll）");
    HMODULE model_lib = LoadLibraryA("test_splitter_model.dll");
    if (model_lib == nullptr) {
        printf("加载模型 DLL 失败（错误码 %lu）\n", GetLastError());
        return 1;
    }
    auto create_model = reinterpret_cast<int (*)(xOptModelT*, xOptPlatformT*, const char*)>(
        GetProcAddress(model_lib, "xOptModel_createModel"));
    if (create_model == nullptr) {
        printf("找不到导出函数 xOptModel_createModel\n");
        return 1;
    }
    // 宿主分配 xOptModelT 并填 size，模型填 handle 与函数表
    xOptModelT model = {sizeof(xOptModelT)};
    if (create_model(&model, nullptr, "TestSplitter") < 0) {
        printf("createModel 失败\n");
        return 1;
    }
    printf("createModel 成功；模型版本：%s\n",
           model.getVersion != nullptr ? model.getVersion(model.handle) : "v1.0.0");

    // ============================================================
    // 步骤 2：参数、slate、校验、固定进料（宿主调用序列）
    // ============================================================
    stepBanner(2, "getParameters / setParameters（数值参数通道）");
    int param_count = 0;
    if (model.getParameters(model.handle, nullptr, nullptr, param_count) < 0 ||
        param_count <= 0) {
        printf("getParameters 查询失败\n");
        return 1;
    }
    std::vector<const char*> param_names(param_count);
    std::vector<double> param_values(param_count, 0.0);
    if (model.getParameters(model.handle, param_names.data(), param_values.data(),
                            param_count) < 0) {
        printf("getParameters 失败\n");
        return 1;
    }
    for (int i = 0; i < param_count; ++i) {
        printf("    参数 %s 默认值 = %g", param_names[i], param_values[i]);
        // demo 设定：r=0.5、mpf=0.1
        if (std::strcmp(param_names[i], "split_ratio") == 0) {
            param_values[i] = 0.5;
            printf(" -> 设定为 0.5");
        } else if (std::strcmp(param_names[i], "min_product_flow") == 0) {
            param_values[i] = 0.1;
            printf(" -> 设定为 0.1");
        }
        printf("\n");
    }
    // 宿主在 getParameters 后立即回送 setParameters，这里照做
    if (model.setParameters(model.handle, param_names.data(), param_values.data(),
                            param_count) < 0) {
        printf("setParameters 失败\n");
        return 1;
    }

    stepBanner(3, "setSlate（组分表 C1、C2）+ validateModel");
    xOptSlate slate = {};
    slate.name = "StreamPort";
    slate.thermo_method = nullptr;  // 无热力学
    slate.components[0] = "C1";
    slate.components[1] = "C2";
    slate.components[2] = nullptr;
    if (model.setSlate(model.handle, 0, &slate) < 0) {
        printf("setSlate 失败\n");
        return 1;
    }
    printf("setSlate 成功：2 个组分，1 进 2 出（进 %d 口，出 %d 口）\n",
           model.getInPortNum(model.handle), model.getOutPortNum(model.handle));
    if (model.validateModel(model.handle) < 0) {
        printf("validateModel 失败\n");
        return 1;
    }
    printf("validateModel 通过（0 < split_ratio < 1）\n");

    stepBanner(4, "getFixableVariables + generateEstimate（固定进料状态）");
    int fix_count = 0;
    if (model.getFixableVariables(model.handle, nullptr, nullptr, fix_count) < 0 ||
        fix_count <= 0) {
        printf("getFixableVariables 查询失败\n");
        return 1;
    }
    std::vector<const char*> fix_names(fix_count);
    std::vector<double> fix_values(fix_count, 0.0);
    if (model.getFixableVariables(model.handle, fix_names.data(), fix_values.data(),
                                  fix_count) < 0) {
        printf("getFixableVariables 失败\n");
        return 1;
    }
    printf("    可固定变量 %d 个（默认值即 demo 的固定值）：\n", fix_count);
    for (int i = 0; i < fix_count; ++i) {
        printf("      %s = %g\n", fix_names[i], fix_values[i]);
    }
    // 按宿主打包方式：变量名以 '\0' 分隔拼在连续缓冲里，值数组一一对应
    std::vector<char> packed_names;
    std::vector<double> packed_values;
    for (int i = 0; i < fix_count; ++i) {
        const char* name = fix_names[i];
        packed_names.insert(packed_names.end(), name, name + std::strlen(name));
        packed_names.push_back('\0');
        packed_values.push_back(fix_values[i]);
    }
    // generateEstimate 两段式：先报个数，再填初值
    int est_size = 0;
    if (model.generateEstimate(model.handle, nullptr, est_size, packed_names.data(),
                               packed_values.data(), (int)packed_values.size()) < 0 ||
        est_size <= 0) {
        printf("generateEstimate 查询失败\n");
        return 1;
    }
    std::vector<double> init_x(est_size, 0.0);
    if (model.generateEstimate(model.handle, init_x.data(), est_size, packed_names.data(),
                               packed_values.data(), (int)packed_values.size()) < 0) {
        printf("generateEstimate 失败\n");
        return 1;
    }
    printf("generateEstimate 成功：初值向量 %d 维（进料已被固定）\n", est_size);

    // ============================================================
    // 步骤 3/4：buildProblem + 桥接为 xOptProblem C++ 对象
    // ============================================================
    stepBanner(5, "buildProblem + ProblemFromModelT 桥接");
    std::unique_ptr<ProblemFromModelT> wrapper(new ProblemFromModelT(&model));
    if (wrapper->initialize() < 0) {
        printf("buildProblem 失败\n");
        return 1;
    }
    printf("问题构造成功：变量 %d 个，约束 %d 个（桥接器把 C 函数表适配为 "
           "xOptProblem 接口，与平台 xOptProblemFromBlackBoxModel 同款）\n",
           wrapper->numVariables(), wrapper->numConstraints());

    // ============================================================
    // 步骤 5/6：加载求解器 DLL → createSolver → options → solve
    // ============================================================
    stepBanner(6, "加载求解器 DLL（test_penalty_solver.dll）并创建求解器");
    HMODULE solver_lib = LoadLibraryA("test_penalty_solver.dll");
    if (solver_lib == nullptr) {
        printf("加载求解器 DLL 失败（错误码 %lu）\n", GetLastError());
        return 1;
    }
    auto create_solver =
        reinterpret_cast<CreateSolverFunc>(GetProcAddress(solver_lib, "createSolver"));
    auto destroy_solver =
        reinterpret_cast<DestroySolverFunc>(GetProcAddress(solver_lib, "destroySolver"));
    if (create_solver == nullptr || destroy_solver == nullptr) {
        printf("找不到导出函数 createSolver/destroySolver\n");
        return 1;
    }
    xOptSolver* solver = create_solver("SplitterDemo", wrapper.get(), demoLogFunc);
    if (solver == nullptr) {
        printf("createSolver 失败\n");
        return 1;
    }
    printf("createSolver 成功（实例名 SplitterDemo）\n");

    stepBanner(7, "可调参数与 options 演示");
    int tun_count = 0;
    solver->getTunableParamList(nullptr, nullptr, tun_count);  // 两段式：先报个数
    std::vector<const char*> tun_names(tun_count);
    std::vector<xOptSolver::OPTION_TYPE> tun_types(tun_count);
    solver->getTunableParamList(tun_names.data(), tun_types.data(), tun_count);
    printf("    可调参数 %d 个：", tun_count);
    for (int i = 0; i < tun_count; ++i) {
        printf("%s(%s)%s", tun_names[i],
               tun_types[i] == xOptSolver::OPTION_INT ? "INT" : "REAL",
               i + 1 < tun_count ? ", " : "");
    }
    printf("\n");
    const char* selected_params[] = {"max_iter", "tol", "feas_tol", "mu0"};
    solver->setTunableParamList(selected_params, 4);
    // 经 options 通道设置：print_level（int）、mu0（double）
    const char* int_opt_names[] = {"print_level"};
    const int int_opt_values[] = {1};
    xOptSolver::boolean opt_results[1] = {0};
    solver->setIntOptions(opt_results, int_opt_names, int_opt_values, 1);
    printf("    setIntOptions(print_level=1) 结果：%s\n", opt_results[0] ? "接受" : "拒绝");
    const char* dbl_opt_names[] = {"mu0"};
    const double dbl_opt_values[] = {100.0};
    solver->setDoubleOptions(opt_results, dbl_opt_names, dbl_opt_values, 1);
    printf("    setDoubleOptions(mu0=100) 结果：%s\n", opt_results[0] ? "接受" : "拒绝");

    stepBanner(8, "solve()");
    const int solve_result = solver->solve();
    printf("solve 返回：%s（>=0 表示成功）\n", resultName(solve_result));
    if (solve_result < 0) {
        printf("求解失败\n");
        destroy_solver(solver);
        return 1;
    }

    // ============================================================
    // 步骤 7：X() 打印解、F() 打印目标与约束值
    // ============================================================
    stepBanner(9, "解与约束值");
    const int n = wrapper->numVariables();
    const int m = wrapper->numConstraints();
    std::vector<const char*> var_names(n);
    wrapper->getVariableNames(var_names.data(), n);
    std::vector<double> x(n, 0.0);
    solver->X(x.data(), n);
    printf("    变量解（X）：\n");
    for (int i = 0; i < n; ++i) printf("      %-12s = %12.6f\n", var_names[i], x[i]);

    std::vector<const char*> cons_names(m);
    wrapper->getConstraintNames(cons_names.data(), m);
    std::vector<double> clow(m, 0.0), cupp(m, 0.0);
    wrapper->getConstraintBounds(clow.data(), cupp.data(), m);
    std::vector<double> fvals(m + 1, 0.0);
    solver->F(fvals.data(), m + 1);  // 平台约定：m+1 个值，f[0]=目标
    printf("    目标值 f = %.6f\n", fvals[0]);
    printf("    约束值（F，共 %d 个）：\n", m);
    for (int i = 0; i < m; ++i) {
        printf("      %-28s = %12.6f    in [%g, %g]\n", cons_names[i], fvals[1 + i],
               clow[i], cupp[i]);
    }

    // ============================================================
    // 步骤 8：断言对拍（手算基准）
    // ============================================================
    stepBanner(10, "断言对拍（|解 - 手算值| < 1e-4 且全部约束满足）");
    bool pass = true;
    for (const auto& expected : expected_vars) {
        int idx = -1;
        for (int i = 0; i < n; ++i) {
            if (std::strcmp(var_names[i], expected.name) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            printf("    [失败] 变量 %s 不存在\n", expected.name);
            pass = false;
            continue;
        }
        const double diff = std::fabs(x[idx] - expected.value);
        if (diff >= kTolerance) {
            printf("    [失败] %s = %.6f，期望 %.6f（偏差 %.3e）\n", expected.name,
                   x[idx], expected.value, diff);
            pass = false;
        }
    }
    for (int i = 0; i < m; ++i) {
        const double c = fvals[1 + i];
        if (c < clow[i] - 1e-6 || c > cupp[i] + 1e-6) {
            printf("    [失败] 约束 %s 不满足：%g not in [%g, %g]\n", cons_names[i], c,
                   clow[i], cupp[i]);
            pass = false;
        }
    }

    // ---- 清理（注意销毁顺序：求解器→求解器DLL→问题→模型→模型DLL）----
    destroy_solver(solver);
    FreeLibrary(solver_lib);
    wrapper.reset();  // 析构时调 destroyProblem，归还模型分配的问题对象
    model.destroyModel(model.handle);
    FreeLibrary(model_lib);

    printf("\n=====================================\n");
    if (pass) {
        printf("DEMO PASSED\n");
        printf("=====================================\n");
        return 0;
    }
    printf("DEMO FAILED\n");
    printf("=====================================\n");
    return 1;
}
