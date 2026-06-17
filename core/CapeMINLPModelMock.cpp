// ***************************************************************
//  CapeMINLPModelMock   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "CapeMINLPModelMock.h"

CapeMINLPModelMock::CapeMINLPModelMock(const std::string& target) : target_(target) {}

int CapeMINLPModelMock::fail(const std::string& msg) const {
    last_error_ = msg;
    return -1;
}

int CapeMINLPModelMock::connect() {
    connected_ = true;
    x_.assign(kNumVars, 0.0);  // 初值 (0, 0)
    last_error_.clear();
    return 0;
}

void CapeMINLPModelMock::disconnect() { connected_ = false; }

int CapeMINLPModelMock::getSize(CapeMINLPSize& size_out) {
    if (!connected_) return fail("getSize: not connected");
    size_out = CapeMINLPSize{};
    size_out.num_variables = kNumVars;
    size_out.num_constraints = kNumCons;
    // c0 = x0 + x1 - 3 当作非线性约束处理，整体走 Jacobian 路径：
    size_out.num_nonlinear_jacobian_nz = 2;  // (0,0) (0,1)
    size_out.num_nonlinear_objgrad_nz = 2;   // 列 0, 1
    return 0;
}

int CapeMINLPModelMock::getStructure(const std::string& type, std::vector<int>& row_index,
                                     std::vector<int>& col_index, std::vector<int>& obj_index) {
    if (!connected_) return fail("getStructure: not connected");
    row_index.clear();
    col_index.clear();
    obj_index.clear();
    if (type == cape::kStructJacobian) {
        // d c0 / d x0, d c0 / d x1
        row_index = {0, 0};
        col_index = {0, 1};
        return 0;
    }
    if (type == cape::kStructObjectiveGradient) {
        // d f / d x0, d f / d x1
        obj_index = {0, 1};
        return 0;
    }
    return fail("getStructure: unknown type " + type);
}

int CapeMINLPModelMock::getVariableNames(const std::vector<int>& vids,
                                         std::vector<std::string>& names_out) {
    names_out.clear();
    names_out.reserve(vids.size());
    for (int id : vids) {
        if (id < 0 || id >= kNumVars) return fail("getVariableNames: vid out of range");
        names_out.push_back(id == 0 ? "x0" : "x1");
    }
    return 0;
}

int CapeMINLPModelMock::getVariableBounds(const std::vector<int>& vids, std::vector<double>& lower_out,
                                          std::vector<double>& upper_out) {
    lower_out.assign(vids.size(), -10.0);
    upper_out.assign(vids.size(), 10.0);
    return 0;
}

int CapeMINLPModelMock::getVariableValues(const std::vector<int>& vids,
                                          std::vector<double>& values_out) {
    values_out.clear();
    values_out.reserve(vids.size());
    for (int id : vids) {
        if (id < 0 || id >= kNumVars) return fail("getVariableValues: vid out of range");
        values_out.push_back(x_[id]);
    }
    return 0;
}

int CapeMINLPModelMock::setVariableValues(const std::vector<int>& vids,
                                          const std::vector<double>& values) {
    if (vids.size() != values.size()) return fail("setVariableValues: size mismatch");
    for (size_t i = 0; i < vids.size(); ++i) {
        int id = vids[i];
        if (id < 0 || id >= kNumVars) return fail("setVariableValues: vid out of range");
        x_[id] = values[i];
    }
    return 0;
}

int CapeMINLPModelMock::getConstraintNames(const std::vector<int>& cids,
                                           std::vector<std::string>& names_out) {
    names_out.clear();
    names_out.reserve(cids.size());
    for (int id : cids) {
        if (id < 0 || id >= kNumCons) return fail("getConstraintNames: cid out of range");
        names_out.push_back("c0");
    }
    return 0;
}

int CapeMINLPModelMock::getConstraintBounds(const std::vector<int>& cids, std::vector<double>& lower_out,
                                            std::vector<double>& upper_out) {
    // 等式约束 c0 = 0 -> 上下界都为 0
    lower_out.assign(cids.size(), 0.0);
    upper_out.assign(cids.size(), 0.0);
    return 0;
}

int CapeMINLPModelMock::getNonlinearConstraintValues(const std::vector<int>& cids,
                                                     std::vector<double>& values_out) {
    values_out.clear();
    values_out.reserve(cids.size());
    for (int id : cids) {
        if (id != 0) return fail("getNonlinearConstraintValues: cid out of range");
        values_out.push_back(x_[0] + x_[1] - 3.0);
    }
    return 0;
}

int CapeMINLPModelMock::getConstraintDerivativeValues(const std::string& type,
                                                      const std::vector<int>& cids,
                                                      std::vector<double>& values_out) {
    if (type != cape::kStructJacobian && type != cape::kDerivNonlinear) {
        return fail("getConstraintDerivativeValues: unsupported type " + type);
    }
    // d(x0 + x1 - 3)/dx0 = 1, /dx1 = 1，顺序与 getStructure(Jacobian) 一致
    values_out = {1.0, 1.0};
    return 0;
}

int CapeMINLPModelMock::getObjectiveValue(double& value_out) {
    value_out = x_[0] * x_[0] + x_[1] * x_[1];
    return 0;
}

int CapeMINLPModelMock::getObjectiveDerivativeValues(const std::string& type,
                                                     std::vector<double>& values_out) {
    if (type != cape::kDerivNonlinear && type != cape::kStructObjectiveGradient) {
        return fail("getObjectiveDerivativeValues: unsupported type " + type);
    }
    // d f / d x0 = 2 x0, d f / d x1 = 2 x1，顺序与 getStructure(ObjectiveGradient) 一致
    values_out = {2.0 * x_[0], 2.0 * x_[1]};
    return 0;
}

std::string CapeMINLPModelMock::lastError() const { return last_error_; }
