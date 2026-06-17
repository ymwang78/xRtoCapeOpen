#pragma once
// ***************************************************************
//  MockXOptProblem   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco tests).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  测试用 xOptProblem 实现（二次问题）：
//      min x0^2 + x1^2  s.t. x0 + x1 - 3 = 0, -10<=xi<=10
//  解析最优 (1.5,1.5)，本测试用 (3,4) 对拍 obj=25。
//
//  注意：xOptProblem 类带 XOPTIF_API。就地实例化派生类需基类隐式构造/析构
//  「就地生成」而非按 dllimport 链接 xOptInterface DLL —— 故本头在包含
//  xOptProblem.h 之前定义 XOPTINTERFACE_EXPORTS。应作为 TU 的首个相关包含。
// ***************************************************************
#ifdef _WIN32
#    ifndef XOPTINTERFACE_EXPORTS
#        define XOPTINTERFACE_EXPORTS
#    endif
#endif

#include "xOpt/xOptProblem.h"

class MockXOptProblem : public xOptProblem {
  public:
    int initialize() override { return 0; }
    int numVariables() const override { return 2; }
    int numConstraints() const override { return 1; }

    int getVariableNames(const char* names[], int names_size) const override {
        if (names_size >= 1) names[0] = "x0";
        if (names_size >= 2) names[1] = "x1";
        return 2;
    }
    int getVariableDescriptions(const char* descriptions[], int n) const override {
        for (int i = 0; i < n; ++i) descriptions[i] = "";
        return n;
    }
    int getConstraintNames(const char* names[], int names_size) const override {
        if (names_size >= 1) names[0] = "c0";
        return 1;
    }
    int getOptions(double* options, int options_size) const override {
        if (options_size < OPTIONS_LIMIT) return -1;
        options[OPTIONS_MAGIC] = 'X';
        options[HAS_DERIVATIVE] = 1;
        options[HAS_LINEAR_A] = 0;
        options[IS_SIMULATION] = 0;
        return 0;
    }
    int getVariableBounds(double* xlow, double* xupp, int x_size) const override {
        for (int i = 0; i < x_size; ++i) {
            xlow[i] = -10.0;
            xupp[i] = 10.0;
        }
        return x_size;
    }
    int getConstraintBounds(double* clow, double* cupp, int c_size) const override {
        if (c_size >= 1) {
            clow[0] = 0.0;
            cupp[0] = 0.0;
        }
        return c_size;
    }
    int getInitialX(double* x0, int x0_size) const override {
        for (int i = 0; i < x0_size; ++i) x0[i] = 0.0;
        return x0_size;
    }
    int getLinearConstraints(int*, int*, double*, int& lcons_size) const override {
        lcons_size = 0;
        return 0;
    }
    int getObjectiveGradientStructure(int* obj_colidx, int& obj_colidx_size) const override {
        if (obj_colidx == nullptr) {
            obj_colidx_size = 2;
            return 0;
        }
        obj_colidx[0] = 0;
        obj_colidx[1] = 1;
        obj_colidx_size = 2;
        return 0;
    }
    int getConstraintJacobianStructure(int* rowidx, int* colidx, int& nnz) const override {
        if (rowidx == nullptr || colidx == nullptr) {
            nnz = 2;
            return 0;
        }
        rowidx[0] = 0;
        rowidx[1] = 0;
        colidx[0] = 0;
        colidx[1] = 1;
        nnz = 2;
        return 0;
    }
    int setX(const double* x, int x_size) override {
        if (x_size >= 1) x_[0] = x[0];
        if (x_size >= 2) x_[1] = x[1];
        return x_size;
    }
    int runTimeCheck() const override { return 1; }
    int evaluateObjective(double& obj) const override {
        obj = x_[0] * x_[0] + x_[1] * x_[1];
        return 0;
    }
    int evaluateConstraints(double* cons, int cons_size) const override {
        if (cons_size >= 1) cons[0] = x_[0] + x_[1] - 3.0;
        return cons_size;
    }
    int evaluateObjectiveGradient(double* grad, int grad_size) const override {
        if (grad_size >= 1) grad[0] = 2.0 * x_[0];
        if (grad_size >= 2) grad[1] = 2.0 * x_[1];
        return grad_size;
    }
    int evaluateConstraintsJacobianValues(double* values, int values_size) const override {
        if (values_size >= 1) values[0] = 1.0;
        if (values_size >= 2) values[1] = 1.0;
        return values_size;
    }

  private:
    double x_[2] = {0.0, 0.0};
};
