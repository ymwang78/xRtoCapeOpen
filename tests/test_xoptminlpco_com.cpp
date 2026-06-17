// ***************************************************************
//  test_xoptminlpco_com   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  N2 回环测试（同时验证两个方向）：
//    xOptProblem(Mock) → XOptMINLPAdapter(ICapeMINLPModel)
//      → CoMINLP(ICapeMINLP, 打包 VARIANT)
//        → capeopen_core 的 CapeMINLPModelCom(注入, 解包 VARIANT, ICapeMINLPModel)
//          → 读回，与解析值对拍。
//  仅 Windows。自带 main。
// ***************************************************************
#ifdef _WIN32

#include "MockXOptProblem.h"  // 首个包含：XOPTINTERFACE_EXPORTS + xOptProblem

#include <gtest/gtest.h>

#include <vector>

#include "CapeMINLPModel.h"
#include "CoMINLP.h"
#include "XOptMINLPAdapter.h"
#include "backend/com/CapeMINLPModelCom.h"

namespace {

TEST(XOptMINLPcoComLoopback, RoundTrip) {
    MockXOptProblem mock;
    XOptMINLPAdapter adapter(&mock);
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();

    // 生产者：把 adapter 发布为 ICapeMINLP COM 组件
    CoMINLP* co = new CoMINLP(&adapter);  // ref=1

    // 消费者：capeopen_core 的 COM 后端，注入上面的 ICapeMINLP
    CapeMINLPModelCom consumer(co);  // AddRef -> 2
    co->Release();                   // -> 1，归 consumer
    ASSERT_EQ(consumer.connect(), 0) << consumer.lastError();

    CapeMINLPSize s;
    ASSERT_EQ(consumer.getSize(s), 0) << consumer.lastError();
    EXPECT_EQ(s.num_variables, 2);
    EXPECT_EQ(s.num_constraints, 1);
    EXPECT_EQ(s.num_nonlinear_jacobian_nz, 2);

    std::vector<std::string> names;
    ASSERT_EQ(consumer.getVariableNames({0, 1}, names), 0);
    EXPECT_EQ(names, (std::vector<std::string>{"x0", "x1"}));

    std::vector<double> lb, ub;
    ASSERT_EQ(consumer.getVariableBounds({0, 1}, lb, ub), 0);
    EXPECT_DOUBLE_EQ(lb[0], -10.0);
    EXPECT_DOUBLE_EQ(ub[1], 10.0);

    std::vector<int> r, c, o;
    ASSERT_EQ(consumer.getStructure("Jacobian", r, c, o), 0);
    EXPECT_EQ(r, (std::vector<int>{0, 0}));
    EXPECT_EQ(c, (std::vector<int>{0, 1}));

    ASSERT_EQ(consumer.setVariableValues({0, 1}, {3.0, 4.0}), 0);
    double obj = 0;
    ASSERT_EQ(consumer.getObjectiveValue(obj), 0);
    EXPECT_DOUBLE_EQ(obj, 25.0);

    std::vector<double> cons;
    ASSERT_EQ(consumer.getNonlinearConstraintValues({0}, cons), 0);
    EXPECT_DOUBLE_EQ(cons[0], 4.0);

    std::vector<double> grad;
    ASSERT_EQ(consumer.getObjectiveDerivativeValues("Nonlinear", grad), 0);
    EXPECT_EQ(grad, (std::vector<double>{6.0, 8.0}));

    std::vector<double> jac;
    ASSERT_EQ(consumer.getConstraintDerivativeValues("Jacobian", {0}, jac), 0);
    EXPECT_EQ(jac, (std::vector<double>{1.0, 1.0}));
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#endif  // _WIN32
