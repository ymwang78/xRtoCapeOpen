// ***************************************************************
//  PenaltyGradientSolver.h
//  -------------------------------------------------------------
//  测试求解器：罚函数梯度下降法（带回溯线搜索）。
//
//  求解   min f(x)  s.t.  clow <= c(x) <= cupp,  xlow <= x <= xupp
//
//  增广目标（等式即 clow==cupp，统一按区间约束处理）：
//    Phi(x) = f(x) + mu * [ sum_i v_i^2
//              + sum_k max(0, xl_k - x_k)^2 + max(0, x_k - xu_k)^2 ]
//  其中 v_i = c_i - clamp(c_i, clow_i, cupp_i) 为约束违背量。
//
//  梯度下降方向 d = -grad(Phi)，Armijo 回溯线搜索确定步长；
//  外层按最大约束违背自适应增大罚参数 mu（x10，上限 1e10）。
//
//  实现平台求解器契约（include/xOpt/xOptSolver.h）：
//  导出 createSolver / destroySolver，拿到的是问题对象的本地
//  xOptProblem* 指针（C++ 纯虚接口）。
// ***************************************************************
#pragma once

// 本 DLL 是导出方：在包含平台头文件之前把 XOPTIF_API 定义为导出，
// 否则 xOptSolver/xOptProblem 类声明是 __declspec(dllimport)，与导出冲突。
#ifndef XOPTIF_API
#define XOPTIF_API __declspec(dllexport)
#endif

#include "xOpt/xOptProblem.h"
#include "xOpt/xOptSolver.h"

#include <Eigen/Core>

#include <string>
#include <vector>

// ***************************************************************
// PenaltyGradientSolver：教学级罚函数梯度下降求解器。
// 异常纪律：solve() 内 try/catch 全包，错误经日志通道上报并以
// 负返回码（SOLVE_RESULT 的负值）返回，不穿透 DLL 边界。
// ***************************************************************
class PenaltyGradientSolver : public xOptSolver {
  public:
    PenaltyGradientSolver(const char* name, xOptProblem* problem, xOptLogFunc log_func);
    ~PenaltyGradientSolver() override = default;

    // ---- xOptSolver 接口 ----
    xOptProblem* getProblem() const override { return problem_; }

    int getTunableParamList(const char* p_name[], OPTION_TYPE p_type[],
                            int& p_size) const override;
    int setTunableParamList(const char* p_name[], int p_size) override;

    int getStringOptions(const char* option_names[], const char* option_values[],
                         int& options_size) const override;
    int setStringOptions(boolean option_results[], const char* option_names[],
                         const char* option_values[], int options_size) override;
    int getIntOptions(const char* option_names[], int option_values[],
                      int& options_size) const override;
    int setIntOptions(boolean option_results[], const char* option_names[],
                      const int option_values[], int options_size) override;
    int getDoubleOptions(const char* option_names[], double option_values[],
                         int& options_size) const override;
    int setDoubleOptions(boolean option_results[], const char* option_names[],
                         const double option_values[], int options_size) override;

    int solve() override;
    int pauseSolve() override { return -1; }    // 不支持暂停
    int continueSolve() override { return -1; } // 不支持继续
    int X(double* x, int x_size) const override;
    int F(double* x, int x_size) const override;
    int Xmul(double* x, int x_size) const override { return -1; } // 无对偶信息
    int Fmul(double* x, int x_size) const override { return -1; }

  private:
    // 计算增广目标函数值（只算值不算梯度，会调用 problem_->setX）
    double evalPenaltyPhi(const Eigen::VectorXd& x) const;
    double phiAt(const Eigen::VectorXd& x) const;
    // 计算增广目标函数值与梯度（解析：目标梯度 + Jᵀw + 边界罚项；
    // 问题未提供雅可比时对 Phi 做中心差分兜底）
    void phiAndGrad(const Eigen::VectorXd& x, double& phi, Eigen::VectorXd& grad) const;
    // 当前点的最大违背量（约束 + 边界）
    double maxViolation(const Eigen::VectorXd& x) const;
    void log(ZLOG_LEVEL level, const char* format, ...) const;

    // ---- 配置 ----
    xOptProblem* problem_;
    std::string name_;
    xOptLogFunc log_func_;
    int print_level_ = 1;

    // ---- 可调参数 ----
    int max_iter_ = 2000;      // 每个罚参数层的最大内层迭代数
    double tol_ = 1e-6;        // 梯度收敛容差（无穷范数）
    double feas_tol_ = 1e-6;   // 可行性容差（最大违背）
    double mu0_ = 100.0;       // 初始罚参数
    double mu_ = 100.0;        // 当前外层罚参数（solve 期间有效）

    // ---- 问题数据缓存（solve 时填充）----
    int n_ = 0;                 // 变量个数
    int m_ = 0;                 // 约束个数
    Eigen::VectorXd xlow_, xupp_, clow_, cupp_;
    std::vector<int> jac_rows_, jac_cols_;  // 雅可比结构（常量）
    std::vector<int> obj_cols_;             // 目标梯度结构（列下标）
    int obj_grad_nnz_ = 0;      // 目标梯度结构大小
    bool has_jacobian_ = false;

    // ---- 结果缓存 ----
    Eigen::VectorXd x_;   // 解
    Eigen::VectorXd f_;   // m+1 个值：f[0]=目标，f[1..m]=约束值
    int result_ = RESULT_UNKNOWN;
};
