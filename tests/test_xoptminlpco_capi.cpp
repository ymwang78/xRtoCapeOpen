// ***************************************************************
//  test_xoptminlpco_capi   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  N5：XOptMINLPAdapter 经 C-ABI（xOptProblemT vtable）输入路径，对拍解析值。
//  in-proc 填一个 xOptProblemT（二次问题），注入 adapter（CapiProblemView 路径）。
//      min x0^2 + x1^2  s.t. x0 + x1 - 3 = 0, -10<=xi<=10
// ***************************************************************
#include <gtest/gtest.h>

#include <vector>

#include "CapeMINLPModel.h"
#include "XOptMINLPAdapter.h"
#include "xOpt/xOptModel.h"  // xOptProblemT / xOptProblemHandle

namespace {

struct QHandle {
    double x[2] = {0.0, 0.0};
};
QHandle* H(xOptProblemHandle h) { return reinterpret_cast<QHandle*>(h); }

int q_numVariables(xOptProblemHandle) { return 2; }
int q_numConstraints(xOptProblemHandle) { return 1; }
int q_getVariableNames(xOptProblemHandle, const char* names[], int n) {
    if (n >= 1) names[0] = "x0";
    if (n >= 2) names[1] = "x1";
    return 2;
}
int q_getConstraintNames(xOptProblemHandle, const char* names[], int n) {
    if (n >= 1) names[0] = "c0";
    return 1;
}
int q_getVariableBounds(xOptProblemHandle, double* lo, double* hi, int n) {
    for (int i = 0; i < n; ++i) { lo[i] = -10.0; hi[i] = 10.0; }
    return n;
}
int q_getConstraintBounds(xOptProblemHandle, double* lo, double* hi, int n) {
    if (n >= 1) { lo[0] = 0.0; hi[0] = 0.0; }
    return n;
}
int q_getInitialX(xOptProblemHandle, double* x, int n) {
    for (int i = 0; i < n; ++i) x[i] = 0.0;
    return n;
}
int q_getLinearConstraints(xOptProblemHandle, int*, int*, double*, int* size) {
    *size = 0;
    return 0;
}
int q_getObjectiveGradientStructure(xOptProblemHandle, int* col, int* size) {
    if (col == nullptr) { *size = 2; return 0; }
    col[0] = 0; col[1] = 1; *size = 2;
    return 0;
}
int q_getConstraintJacobianStructure(xOptProblemHandle, int* row, int* col, int* nnz) {
    if (row == nullptr || col == nullptr) { *nnz = 2; return 0; }
    row[0] = 0; row[1] = 0; col[0] = 0; col[1] = 1; *nnz = 2;
    return 0;
}
int q_setX(xOptProblemHandle h, const double* x, int n) {
    if (n >= 1) H(h)->x[0] = x[0];
    if (n >= 2) H(h)->x[1] = x[1];
    return n;
}
int q_evaluateObjective(xOptProblemHandle h, double* obj) {
    *obj = H(h)->x[0] * H(h)->x[0] + H(h)->x[1] * H(h)->x[1];
    return 0;
}
int q_evaluateConstraints(xOptProblemHandle h, double* cons, int n) {
    if (n >= 1) cons[0] = H(h)->x[0] + H(h)->x[1] - 3.0;
    return n;
}
int q_evaluateObjectiveGradient(xOptProblemHandle h, double* g, int n) {
    if (n >= 1) g[0] = 2.0 * H(h)->x[0];
    if (n >= 2) g[1] = 2.0 * H(h)->x[1];
    return n;
}
int q_evaluateConstraintsJacobianValues(xOptProblemHandle, double* v, int n) {
    if (n >= 1) v[0] = 1.0;
    if (n >= 2) v[1] = 1.0;
    return n;
}

xOptProblemT makeQuadraticVtable(QHandle* h) {
    xOptProblemT pt = {sizeof(xOptProblemT)};
    pt.handle = reinterpret_cast<xOptProblemHandle>(h);
    pt.numVariables = &q_numVariables;
    pt.numConstraints = &q_numConstraints;
    pt.getVariableNames = &q_getVariableNames;
    pt.getConstraintNames = &q_getConstraintNames;
    pt.getVariableBounds = &q_getVariableBounds;
    pt.getConstraintBounds = &q_getConstraintBounds;
    pt.getInitialX = &q_getInitialX;
    pt.getLinearConstraints = &q_getLinearConstraints;
    pt.getObjectiveGradientStructure = &q_getObjectiveGradientStructure;
    pt.getConstraintJacobianStructure = &q_getConstraintJacobianStructure;
    pt.setX = &q_setX;
    pt.evaluateObjective = &q_evaluateObjective;
    pt.evaluateConstraints = &q_evaluateConstraints;
    pt.evaluateObjectiveGradient = &q_evaluateObjectiveGradient;
    pt.evaluateConstraintsJacobianValues = &q_evaluateConstraintsJacobianValues;
    return pt;
}

TEST(XOptMINLPcoCapi, AdapterOverCApiVtable) {
    QHandle qh;
    xOptProblemT pt = makeQuadraticVtable(&qh);
    XOptMINLPAdapter adapter(pt);  // C-ABI 注入（不拥有）
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();

    CapeMINLPSize s;
    ASSERT_EQ(adapter.getSize(s), 0);
    EXPECT_EQ(s.num_variables, 2);
    EXPECT_EQ(s.num_constraints, 1);
    EXPECT_EQ(s.num_nonlinear_jacobian_nz, 2);
    EXPECT_EQ(s.num_nonlinear_objgrad_nz, 2);

    std::vector<std::string> names;
    ASSERT_EQ(adapter.getVariableNames({0, 1}, names), 0);
    EXPECT_EQ(names, (std::vector<std::string>{"x0", "x1"}));

    std::vector<int> r, c, o;
    ASSERT_EQ(adapter.getStructure(cape::kStructJacobian, r, c, o), 0);
    EXPECT_EQ(r, (std::vector<int>{0, 0}));
    EXPECT_EQ(c, (std::vector<int>{0, 1}));

    ASSERT_EQ(adapter.setVariableValues({0, 1}, {3.0, 4.0}), 0);
    double obj = 0;
    ASSERT_EQ(adapter.getObjectiveValue(obj), 0);
    EXPECT_DOUBLE_EQ(obj, 25.0);

    std::vector<double> cons;
    ASSERT_EQ(adapter.getNonlinearConstraintValues({0}, cons), 0);
    EXPECT_DOUBLE_EQ(cons[0], 4.0);

    std::vector<double> grad;
    ASSERT_EQ(adapter.getObjectiveDerivativeValues("Nonlinear", grad), 0);
    EXPECT_EQ(grad, (std::vector<double>{6.0, 8.0}));

    std::vector<double> jac;
    ASSERT_EQ(adapter.getConstraintDerivativeValues("Jacobian", {0}, jac), 0);
    EXPECT_EQ(jac, (std::vector<double>{1.0, 1.0}));
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
