// ***************************************************************
//  test_capeopen_problem   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  capeopen_core 单元测试：经 C 风格 vtable（xOptProblemT）驱动，
//  对拍 Mock 后端的解析值。Mock 问题：
//      min x0^2 + x1^2  s.t. x0 + x1 - 3 = 0, -10<=xi<=10
//  详见 docs/capeopen_problem_design.md §9。
// ***************************************************************
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "CapeBackendFactory.h"
#include "CapeMINLPModelMock.h"
#include "CapeMINLPProblemCore.h"
#include "xOpt/xOptModel.h"
#include "xOpt/xOptProblem.h"

namespace {

// 经 vtable 驱动，镜像生产用法（core 堆分配，destroyProblem 释放）。
class CapeOpenProblemTest : public ::testing::Test {
  protected:
    xOptProblemT pt_{sizeof(xOptProblemT)};

    void SetUp() override {
        auto* core = new CapeMINLPProblemCore(std::make_unique<CapeMINLPModelMock>(""));
        ASSERT_GE(core->initialize(), 0);
        core->fillVtable(&pt_);
        ASSERT_NE(pt_.handle, nullptr);
    }

    void TearDown() override {
        if (pt_.handle) pt_.destroyProblem(pt_.handle);
    }
};

TEST_F(CapeOpenProblemTest, Size_MatchesMock) {
    EXPECT_EQ(pt_.numVariables(pt_.handle), 2);
    EXPECT_EQ(pt_.numConstraints(pt_.handle), 1);
}

TEST_F(CapeOpenProblemTest, Names) {
    const char* vnames[2] = {nullptr, nullptr};
    EXPECT_EQ(pt_.getVariableNames(pt_.handle, vnames, 2), 2);
    EXPECT_STREQ(vnames[0], "x0");
    EXPECT_STREQ(vnames[1], "x1");

    const char* cnames[1] = {nullptr};
    EXPECT_EQ(pt_.getConstraintNames(pt_.handle, cnames, 1), 1);
    EXPECT_STREQ(cnames[0], "c0");
}

TEST_F(CapeOpenProblemTest, Bounds_And_InitialX) {
    double xlow[2] = {0, 0}, xupp[2] = {0, 0};
    EXPECT_EQ(pt_.getVariableBounds(pt_.handle, xlow, xupp, 2), 2);
    EXPECT_DOUBLE_EQ(xlow[0], -10.0);
    EXPECT_DOUBLE_EQ(xupp[1], 10.0);

    double clow[1] = {9}, cupp[1] = {9};
    EXPECT_EQ(pt_.getConstraintBounds(pt_.handle, clow, cupp, 1), 1);
    EXPECT_DOUBLE_EQ(clow[0], 0.0);  // 等式约束
    EXPECT_DOUBLE_EQ(cupp[0], 0.0);

    double x0[2] = {9, 9};
    EXPECT_EQ(pt_.getInitialX(pt_.handle, x0, 2), 2);
    EXPECT_DOUBLE_EQ(x0[0], 0.0);
    EXPECT_DOUBLE_EQ(x0[1], 0.0);
}

TEST_F(CapeOpenProblemTest, Options) {
    double options[xOptProblem::OPTIONS_LIMIT] = {0};
    EXPECT_EQ(pt_.getOptions(pt_.handle, options, xOptProblem::OPTIONS_LIMIT), 0);
    EXPECT_EQ(options[xOptProblem::OPTIONS_MAGIC], 'X');
    EXPECT_EQ(options[xOptProblem::HAS_DERIVATIVE], 1);
    EXPECT_EQ(options[xOptProblem::IS_SIMULATION], 0);
}

TEST_F(CapeOpenProblemTest, JacobianStructure_TwoPassQuery_ZeroBased) {
    int nnz = -1;
    // 第一段：传 null 查长度
    EXPECT_GE(pt_.getConstraintJacobianStructure(pt_.handle, nullptr, nullptr, &nnz), 0);
    EXPECT_EQ(nnz, 2);

    // 第二段：填值
    std::vector<int> rows(nnz, -1), cols(nnz, -1);
    EXPECT_EQ(pt_.getConstraintJacobianStructure(pt_.handle, rows.data(), cols.data(), &nnz), 2);
    EXPECT_EQ(rows[0], 0);
    EXPECT_EQ(rows[1], 0);
    EXPECT_EQ(cols[0], 0);  // 0-based
    EXPECT_EQ(cols[1], 1);
}

TEST_F(CapeOpenProblemTest, ObjectiveGradientStructure_TwoPassQuery) {
    int sz = -1;
    EXPECT_GE(pt_.getObjectiveGradientStructure(pt_.handle, nullptr, &sz), 0);
    EXPECT_EQ(sz, 2);

    std::vector<int> cols(sz, -1);
    EXPECT_EQ(pt_.getObjectiveGradientStructure(pt_.handle, cols.data(), &sz), 2);
    EXPECT_EQ(cols[0], 0);
    EXPECT_EQ(cols[1], 1);
}

TEST_F(CapeOpenProblemTest, LinearConstraints_None) {
    int lsize = -1;
    EXPECT_EQ(pt_.getLinearConstraints(pt_.handle, nullptr, nullptr, nullptr, &lsize), 0);
    EXPECT_EQ(lsize, 0);
}

TEST_F(CapeOpenProblemTest, Evaluate_AgainstAnalytic) {
    const double x[2] = {3.0, 4.0};
    ASSERT_EQ(pt_.setX(pt_.handle, x, 2), 2);

    double obj = 0;
    EXPECT_EQ(pt_.evaluateObjective(pt_.handle, &obj), 0);
    EXPECT_DOUBLE_EQ(obj, 25.0);  // 9 + 16

    double cons[1] = {0};
    EXPECT_EQ(pt_.evaluateConstraints(pt_.handle, cons, 1), 1);
    EXPECT_DOUBLE_EQ(cons[0], 4.0);  // 3 + 4 - 3

    double grad[2] = {0, 0};
    EXPECT_EQ(pt_.evaluateObjectiveGradient(pt_.handle, grad, 2), 2);
    EXPECT_DOUBLE_EQ(grad[0], 6.0);  // 2*3
    EXPECT_DOUBLE_EQ(grad[1], 8.0);  // 2*4

    double jac[2] = {0, 0};
    EXPECT_EQ(pt_.evaluateConstraintsJacobianValues(pt_.handle, jac, 2), 2);
    EXPECT_DOUBLE_EQ(jac[0], 1.0);
    EXPECT_DOUBLE_EQ(jac[1], 1.0);
}

TEST_F(CapeOpenProblemTest, SetX_ChangesObjective) {
    double obj_a = 0, obj_b = 0;
    const double xa[2] = {1.0, 1.0};
    const double xb[2] = {2.0, 0.0};
    ASSERT_EQ(pt_.setX(pt_.handle, xa, 2), 2);
    ASSERT_EQ(pt_.evaluateObjective(pt_.handle, &obj_a), 0);
    ASSERT_EQ(pt_.setX(pt_.handle, xb, 2), 2);
    ASSERT_EQ(pt_.evaluateObjective(pt_.handle, &obj_b), 0);
    EXPECT_DOUBLE_EQ(obj_a, 2.0);  // 1 + 1
    EXPECT_DOUBLE_EQ(obj_b, 4.0);  // 4 + 0
}

// —— 工厂 ——

TEST(CapeBackendFactoryTest, Parse) {
    EXPECT_EQ(CapeBackendFactory::parse("mock:q").first, "mock");
    EXPECT_EQ(CapeBackendFactory::parse("corba:IOR:01").first, "corba");
    EXPECT_EQ(CapeBackendFactory::parse("com:Some.ProgId").first, "com");
    EXPECT_EQ(CapeBackendFactory::parse("C:/path/unit.DLL").first, "com");  // .dll 后缀 -> com
    EXPECT_EQ(CapeBackendFactory::parse("garbage").first, "");              // 盘符不误伤
    EXPECT_EQ(CapeBackendFactory::parse("C:/path/unit.DLL").second, "C:/path/unit.DLL");
}

TEST(CapeBackendFactoryTest, CreateMock) {
    std::string err;
    auto m = CapeBackendFactory::instance().create("mock:quadratic", err);
    ASSERT_NE(m, nullptr) << err;
    ASSERT_EQ(m->connect(), 0);
    CapeMINLPSize size;
    ASSERT_EQ(m->getSize(size), 0);
    EXPECT_EQ(size.num_variables, 2);
    EXPECT_EQ(size.num_constraints, 1);
}

TEST(CapeBackendFactoryTest, UnregisteredScheme_ReturnsNullNotThrow) {
    std::string err;
    auto m = CapeBackendFactory::instance().create("corba:IOR:01", err);  // 无 CORBA DLL 注册
    EXPECT_EQ(m, nullptr);
    EXPECT_FALSE(err.empty());
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
