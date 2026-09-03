// ***************************************************************
//  mock_xoptsolver_dll   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco tests).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  测试夹具：一个**支持暂停/继续**的 xOptSolver DLL（导出 createSolver /
//  destroySolver）。它不做任何优化，只按剧本走：
//
//    solve()          把 x 放到全 1，返回 RESULT_USER_PAUSE
//    continueSolve()  把 x 推到全 2，返回 RESULT_OPTIMAL
//    pauseSolve()     返回 0
//
//  用来钉住"继续求解之后结果码、X/F、写回都要跟着刷新"这条——examples 的
//  罚函数求解器不支持继续，验不到它。F 按 xOpt 约定是 m+1 个：目标在前。
// ***************************************************************
#ifndef XOPTIF_API
#    define XOPTIF_API __declspec(dllexport)
#endif

#include "xOpt/xOptProblem.h"
#include "xOpt/xOptSolver.h"

#include <new>
#include <string>
#include <vector>

namespace {

class PauseMockSolver : public xOptSolver {
  public:
    PauseMockSolver(const char* name, xOptProblem* problem, xOptLogFunc log)
        : problem_(problem), name_(name != nullptr ? name : ""), log_(log) {}

    xOptProblem* getProblem() const override { return problem_; }

    int getTunableParamList(const char*[], OPTION_TYPE[], int& p_size) const override {
        p_size = 0;
        return 0;
    }
    int setTunableParamList(const char*[], int) override { return 0; }
    int getStringOptions(const char*[], const char*[], int&) const override { return -1; }
    int setStringOptions(boolean r[], const char*[], const char*[], int n) override {
        for (int i = 0; i < n; ++i) r[i] = 0;
        return 0;
    }
    int getIntOptions(const char*[], int[], int&) const override { return -1; }
    int setIntOptions(boolean r[], const char*[], const int[], int n) override {
        for (int i = 0; i < n; ++i) r[i] = 0;
        return 0;
    }
    int getDoubleOptions(const char*[], double[], int&) const override { return -1; }
    int setDoubleOptions(boolean r[], const char*[], const double[], int n) override {
        for (int i = 0; i < n; ++i) r[i] = 0;
        return 0;
    }

    int solve() override { return moveTo(1.0, RESULT_USER_PAUSE); }
    int pauseSolve() override { return 0; }
    int continueSolve() override { return moveTo(2.0, RESULT_OPTIMAL); }

    int X(double* x, int x_size) const override {
        if (x_.empty() || x_size < static_cast<int>(x_.size())) return -1;
        for (size_t i = 0; i < x_.size(); ++i) x[i] = x_[i];
        return static_cast<int>(x_.size());
    }
    int F(double* f, int f_size) const override {
        if (f_.empty() || f_size < static_cast<int>(f_.size())) return -1;
        for (size_t i = 0; i < f_.size(); ++i) f[i] = f_[i];
        return static_cast<int>(f_.size());
    }
    int Xmul(double*, int) const override { return -1; }
    int Fmul(double*, int) const override { return -1; }

  private:
    int moveTo(double value, int result) {
        if (problem_ == nullptr) return RESULT_INVALID_PROBLEM;
        const int n = problem_->numVariables();
        const int m = problem_->numConstraints();
        if (n <= 0 || m < 0) return RESULT_INVALID_PROBLEM;
        x_.assign(static_cast<size_t>(n), value);
        if (problem_->setX(x_.data(), n) < 0) return RESULT_NUMERICAL_ISSUES;
        f_.assign(static_cast<size_t>(m) + 1, 0.0);
        if (problem_->evaluateObjective(f_[0]) < 0) return RESULT_NUMERICAL_ISSUES;
        if (m > 0 && problem_->evaluateConstraints(f_.data() + 1, m) < 0) {
            return RESULT_NUMERICAL_ISSUES;
        }
        if (log_ != nullptr) {
            log_(ZLOG_INFOR, "[%s] mock solver moved x to %g (result %d)", name_.c_str(), value,
                 result);
        }
        return result;
    }

    xOptProblem* problem_;
    std::string name_;
    xOptLogFunc log_;
    std::vector<double> x_;
    std::vector<double> f_;
};

}  // namespace

extern "C" {

__declspec(dllexport) xOptSolver* createSolver(const char* name, xOptProblem* problem,
                                               xOptLogFunc logFunc) {
    return new (std::nothrow) PauseMockSolver(name, problem, logFunc);
}

__declspec(dllexport) void destroySolver(xOptSolver* solver) { delete solver; }

}  // extern "C"
