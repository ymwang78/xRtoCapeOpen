// ***************************************************************
//  test_xoptminlpco_adapter   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  N1：XOptMINLPAdapter 把一个 xOptProblem(Mock) 暴露成 ICapeMINLPModel，
//  对拍解析值（design xOptMINLPco_design.md §4 N1）。Mock 问题：
//      min x0^2 + x1^2  s.t. x0 + x1 - 3 = 0, -10<=xi<=10
// ***************************************************************
// MockXOptProblem 派生自 xOptProblem 并就地实例化；xOptProblem 类带 XOPTIF_API。
// 定义 XOPTINTERFACE_EXPORTS 使其解析为 dllexport，从而本 TU 就地生成隐式
// 构造/析构定义（否则按 dllimport 需链接 xOptInterface DLL）。须在包含 xOpt 头之前。
#ifdef _WIN32
#    ifndef XOPTINTERFACE_EXPORTS
#        define XOPTINTERFACE_EXPORTS
#    endif
#endif

#include <gtest/gtest.h>

#include <vector>

#include "CapeMINLPModel.h"
#include "XOptMINLPAdapter.h"
#include "xOpt/xOptProblem.h"

namespace {

// 直接实现纯虚 xOptProblem 接口（不经 zce），承载二次问题。
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

class XOptMINLPcoAdapterTest : public ::testing::Test {
  protected:
    MockXOptProblem mock_;
    XOptMINLPAdapter adapter_{&mock_};

    void SetUp() override { ASSERT_EQ(adapter_.connect(), 0) << adapter_.lastError(); }
};

TEST_F(XOptMINLPcoAdapterTest, Size) {
    CapeMINLPSize s;
    ASSERT_EQ(adapter_.getSize(s), 0);
    EXPECT_EQ(s.num_variables, 2);
    EXPECT_EQ(s.num_constraints, 1);
    EXPECT_EQ(s.num_nonlinear_jacobian_nz, 2);
    EXPECT_EQ(s.num_nonlinear_objgrad_nz, 2);
    EXPECT_EQ(s.num_linear_jacobian_nz, 0);
}

TEST_F(XOptMINLPcoAdapterTest, NamesAndBounds) {
    std::vector<std::string> vn;
    ASSERT_EQ(adapter_.getVariableNames({0, 1}, vn), 0);
    EXPECT_EQ(vn, (std::vector<std::string>{"x0", "x1"}));

    std::vector<std::string> cn;
    ASSERT_EQ(adapter_.getConstraintNames({0}, cn), 0);
    EXPECT_EQ(cn, (std::vector<std::string>{"c0"}));

    std::vector<double> lb, ub;
    ASSERT_EQ(adapter_.getVariableBounds({0, 1}, lb, ub), 0);
    EXPECT_DOUBLE_EQ(lb[0], -10.0);
    EXPECT_DOUBLE_EQ(ub[1], 10.0);

    std::vector<double> clb, cub;
    ASSERT_EQ(adapter_.getConstraintBounds({0}, clb, cub), 0);
    EXPECT_DOUBLE_EQ(clb[0], 0.0);
    EXPECT_DOUBLE_EQ(cub[0], 0.0);
}

TEST_F(XOptMINLPcoAdapterTest, Structure_ZeroBased) {
    std::vector<int> r, c, o;
    ASSERT_EQ(adapter_.getStructure(cape::kStructJacobian, r, c, o), 0);
    EXPECT_EQ(r, (std::vector<int>{0, 0}));
    EXPECT_EQ(c, (std::vector<int>{0, 1}));
    EXPECT_TRUE(o.empty());

    r.clear(); c.clear(); o.clear();
    ASSERT_EQ(adapter_.getStructure(cape::kStructObjectiveGradient, r, c, o), 0);
    EXPECT_EQ(o, (std::vector<int>{0, 1}));
    EXPECT_TRUE(r.empty());
}

TEST_F(XOptMINLPcoAdapterTest, Evaluate_AgainstAnalytic) {
    // 初值为 0：先验证初值，再 setX
    std::vector<double> x0;
    ASSERT_EQ(adapter_.getVariableValues({0, 1}, x0), 0);
    EXPECT_DOUBLE_EQ(x0[0], 0.0);

    ASSERT_EQ(adapter_.setVariableValues({0, 1}, {3.0, 4.0}), 0);

    double obj = 0;
    ASSERT_EQ(adapter_.getObjectiveValue(obj), 0);
    EXPECT_DOUBLE_EQ(obj, 25.0);

    std::vector<double> cons;
    ASSERT_EQ(adapter_.getNonlinearConstraintValues({0}, cons), 0);
    EXPECT_DOUBLE_EQ(cons[0], 4.0);

    std::vector<double> grad;
    ASSERT_EQ(adapter_.getObjectiveDerivativeValues("Nonlinear", grad), 0);
    EXPECT_EQ(grad, (std::vector<double>{6.0, 8.0}));

    std::vector<double> jac;
    ASSERT_EQ(adapter_.getConstraintDerivativeValues("Jacobian", {0}, jac), 0);
    EXPECT_EQ(jac, (std::vector<double>{1.0, 1.0}));

    // getVariableValues 反映 setX 后的当前点
    std::vector<double> xc;
    ASSERT_EQ(adapter_.getVariableValues({}, xc), 0);  // 空 = 全部
    EXPECT_EQ(xc, (std::vector<double>{3.0, 4.0}));
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
