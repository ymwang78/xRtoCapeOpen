// ***************************************************************
//  test_capeopen_com   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  COM 后端测试：CapeMINLPModelCom 经注入的参考 COM 组件 RefCapeMINLP
//  做 in-proc 全链路（VARIANT/SAFEARRAY/BSTR/HRESULT）对拍（design §5.3）。
//  仅 Windows。本文件不含 main，复用同 exe 内的 main。
// ***************************************************************
#ifdef _WIN32

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "backend/com/CapeMINLPModelCom.h"
#include "backend/com/RefCapeMINLP.h"
#include "CapeMINLPProblemCore.h"
#include "xOpt/xOptModel.h"

namespace {

TEST(CapeComBackendTest, RoundTripAgainstReferenceComponent) {
    RefCapeMINLP* ref = new RefCapeMINLP();  // ref count = 1
    CapeMINLPModelCom com(ref);              // AddRef -> 2
    ref->Release();                          // -> 1, 归 com 所有

    ASSERT_EQ(com.connect(), 0) << com.lastError();

    CapeMINLPSize s;
    ASSERT_EQ(com.getSize(s), 0) << com.lastError();
    EXPECT_EQ(s.num_variables, 2);
    EXPECT_EQ(s.num_constraints, 1);
    EXPECT_EQ(s.num_nonlinear_jacobian_nz, 2);

    std::vector<std::string> names;
    ASSERT_EQ(com.getVariableNames({0, 1}, names), 0) << com.lastError();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x0");
    EXPECT_EQ(names[1], "x1");

    std::vector<double> lb, ub;
    ASSERT_EQ(com.getVariableBounds({0, 1}, lb, ub), 0) << com.lastError();
    ASSERT_EQ(lb.size(), 2u);
    EXPECT_DOUBLE_EQ(lb[0], -10.0);
    EXPECT_DOUBLE_EQ(ub[1], 10.0);

    std::vector<int> rows, cols, obj;
    ASSERT_EQ(com.getStructure("Jacobian", rows, cols, obj), 0) << com.lastError();
    EXPECT_EQ(rows, (std::vector<int>{0, 0}));
    EXPECT_EQ(cols, (std::vector<int>{0, 1}));

    ASSERT_EQ(com.setVariableValues({0, 1}, {3.0, 4.0}), 0) << com.lastError();

    double objv = 0;
    ASSERT_EQ(com.getObjectiveValue(objv), 0) << com.lastError();
    EXPECT_DOUBLE_EQ(objv, 25.0);

    std::vector<double> cons;
    ASSERT_EQ(com.getNonlinearConstraintValues({0}, cons), 0) << com.lastError();
    ASSERT_EQ(cons.size(), 1u);
    EXPECT_DOUBLE_EQ(cons[0], 4.0);

    std::vector<double> grad;
    ASSERT_EQ(com.getObjectiveDerivativeValues("Nonlinear", grad), 0) << com.lastError();
    EXPECT_EQ(grad, (std::vector<double>{6.0, 8.0}));

    std::vector<double> jac;
    ASSERT_EQ(com.getConstraintDerivativeValues("Jacobian", {0}, jac), 0) << com.lastError();
    EXPECT_EQ(jac, (std::vector<double>{1.0, 1.0}));
}

// 全栈：COM 后端 -> CapeMINLPProblemCore -> xOptProblemT
TEST(CapeComBackendTest, FullStackThroughProblemCore) {
    RefCapeMINLP* ref = new RefCapeMINLP();
    auto backend = std::make_unique<CapeMINLPModelCom>(ref);
    ref->Release();  // 归 backend 所有

    auto* core = new CapeMINLPProblemCore(std::move(backend));
    ASSERT_GE(core->initialize(), 0);

    xOptProblemT pt{sizeof(xOptProblemT)};
    core->fillVtable(&pt);
    EXPECT_EQ(pt.numVariables(pt.handle), 2);
    EXPECT_EQ(pt.numConstraints(pt.handle), 1);

    const double x[2] = {3.0, 4.0};
    ASSERT_EQ(pt.setX(pt.handle, x, 2), 2);
    double obj = 0;
    ASSERT_EQ(pt.evaluateObjective(pt.handle, &obj), 0);
    EXPECT_DOUBLE_EQ(obj, 25.0);

    double jac[2] = {0, 0};
    ASSERT_EQ(pt.evaluateConstraintsJacobianValues(pt.handle, jac, 2), 2);
    EXPECT_DOUBLE_EQ(jac[0], 1.0);
    EXPECT_DOUBLE_EQ(jac[1], 1.0);

    pt.destroyProblem(pt.handle);
}

}  // namespace

#endif  // _WIN32
