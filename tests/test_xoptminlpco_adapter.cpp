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
#include "MockXOptProblem.h"  // 首个包含：定义 XOPTINTERFACE_EXPORTS + xOptProblem

#include <gtest/gtest.h>

#include <vector>

#include "CapeMINLPModel.h"
#include "XOptMINLPAdapter.h"

namespace {

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

// 规范：GetMINLPConstraintDerivativeValues 取 cids 指定的**子集**。
// mock 只有一个约束，覆盖不到真正的过滤，这里用一个多约束问题补上——
// 原实现整个忽略 cids、永远返回整表 nnz，子集请求会静默拿到错误的长度和含义。
namespace {

// 3 变量 2 约束：c0 = x0 + x1，c1 = x1 + x2；Jacobian 行 {0,0,1,1}
class TwoConstraintProblem : public MockXOptProblem {
  public:
    int numVariables() const override { return 3; }
    int numConstraints() const override { return 2; }
    int getConstraintNames(const char* names[], int n) const override {
        if (n > 0) names[0] = "c0";
        if (n > 1) names[1] = "c1";
        return n;
    }
    int getConstraintBounds(double* lo, double* hi, int n) const override {
        for (int i = 0; i < n; ++i) { lo[i] = 0.0; hi[i] = 10.0; }
        return n;
    }
    int getVariableNames(const char* names[], int n) const override {
        static const char* kN[] = {"x0", "x1", "x2"};
        for (int i = 0; i < n && i < 3; ++i) names[i] = kN[i];
        return n;
    }
    int getVariableBounds(double* lo, double* hi, int n) const override {
        for (int i = 0; i < n; ++i) { lo[i] = -10.0; hi[i] = 10.0; }
        return n;
    }
    int getInitialX(double* x0, int n) const override {
        for (int i = 0; i < n; ++i) x0[i] = 0.0;
        return n;
    }
    int getConstraintJacobianStructure(int* row, int* col, int& nnz) const override {
        if (row == nullptr || col == nullptr) { nnz = 4; return 0; }
        const int r[] = {0, 0, 1, 1}, c[] = {0, 1, 1, 2};
        for (int i = 0; i < 4; ++i) { row[i] = r[i]; col[i] = c[i]; }
        nnz = 4;
        return 0;
    }
    // 取可区分的值，才能看出返回的是哪几个
    int evaluateConstraintsJacobianValues(double* v, int n) const override {
        const double all[] = {11.0, 12.0, 21.0, 22.0};
        for (int i = 0; i < n && i < 4; ++i) v[i] = all[i];
        return 0;
    }
    int evaluateConstraints(double* cons, int n) const override {
        for (int i = 0; i < n; ++i) cons[i] = 0.0;
        return 0;
    }
};

TEST(XOptMINLPcoAdapterSubsetTest, ConstraintDerivativesFilterByCids) {
    TwoConstraintProblem problem;
    XOptMINLPAdapter adapter(&problem);
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();

    std::vector<double> only_c0;
    ASSERT_EQ(adapter.getConstraintDerivativeValues("Jacobian", {0}, only_c0), 0)
        << adapter.lastError();
    EXPECT_EQ(only_c0, (std::vector<double>{11.0, 12.0})) << "只要 c0 却拿到了别的约束";

    std::vector<double> only_c1;
    ASSERT_EQ(adapter.getConstraintDerivativeValues("Jacobian", {1}, only_c1), 0)
        << adapter.lastError();
    EXPECT_EQ(only_c1, (std::vector<double>{21.0, 22.0}));

    std::vector<double> both;
    ASSERT_EQ(adapter.getConstraintDerivativeValues("Jacobian", {}, both), 0);
    EXPECT_EQ(both, (std::vector<double>{11.0, 12.0, 21.0, 22.0})) << "空 cids 应表示全部";

    std::vector<double> bad;
    EXPECT_LT(adapter.getConstraintDerivativeValues("Jacobian", {5}, bad), 0)
        << "越界 cid 应报错而不是静默返回";
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
