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

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
