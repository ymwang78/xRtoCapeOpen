// ***************************************************************
//  XOptMINLPAdapter   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "XOptMINLPAdapter.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

#ifdef _WIN32
void* loadLib(const std::string& path) {
    return reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
}
void* getSym(void* mod, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(mod), name));
}
void closeLib(void* mod) { FreeLibrary(reinterpret_cast<HMODULE>(mod)); }
#else
void* loadLib(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* getSym(void* mod, const char* name) { return dlsym(mod, name); }
void closeLib(void* mod) { dlclose(mod); }
#endif

// 按 ids 子集挑选；ids 为空表示「全部」。
template <class T>
void pick(const std::vector<T>& all, const std::vector<int>& ids, std::vector<T>& out) {
    if (ids.empty()) {
        out = all;
        return;
    }
    out.clear();
    out.reserve(ids.size());
    for (int id : ids) {
        if (id >= 0 && id < static_cast<int>(all.size())) out.push_back(all[id]);
        else out.push_back(T{});
    }
}

}  // namespace

XOptMINLPAdapter::XOptMINLPAdapter(const std::string& dll_path) : dll_path_(dll_path) {}

XOptMINLPAdapter::XOptMINLPAdapter(xOptProblem* injected) : problem_(injected) {}

XOptMINLPAdapter::~XOptMINLPAdapter() { disconnect(); }

int XOptMINLPAdapter::fail(const std::string& msg) const {
    last_error_ = msg;
    return -1;
}

int XOptMINLPAdapter::connect() {
    // 1) 取得 xOptProblem*（注入或从 DLL 创建）
    if (problem_ == nullptr) {
        if (dll_path_.empty()) return fail("connect: no dll path and no injected problem");
        module_ = loadLib(dll_path_);
        if (module_ == nullptr) return fail("connect: failed to load '" + dll_path_ + "'");
        auto create = reinterpret_cast<createProblemFunc>(getSym(module_, "createProblem"));
        destroy_func_ = reinterpret_cast<destroyProblemFunc>(getSym(module_, "destroyProblem"));
        if (create == nullptr || destroy_func_ == nullptr) {
            return fail("connect: createProblem/destroyProblem not exported by '" + dll_path_ + "'");
        }
        problem_ = create();
        owns_problem_ = true;
        if (problem_ == nullptr) return fail("connect: createProblem returned null");
    }

    if (problem_->initialize() < 0) return fail("connect: xOptProblem::initialize failed");

    size_ = CapeMINLPSize{};
    const int nv = problem_->numVariables();
    const int nc = problem_->numConstraints();
    if (nv < 0 || nc < 0) return fail("connect: numVariables/numConstraints failed");
    size_.num_variables = nv;
    size_.num_constraints = nc;

    // 名称
    {
        std::vector<const char*> tmp(nv > 0 ? nv : 0, nullptr);
        if (nv > 0 && problem_->getVariableNames(tmp.data(), nv) < 0)
            return fail("connect: getVariableNames failed");
        var_names_.resize(nv);
        for (int i = 0; i < nv; ++i) var_names_[i] = tmp[i] ? tmp[i] : "";
    }
    {
        std::vector<const char*> tmp(nc > 0 ? nc : 0, nullptr);
        if (nc > 0 && problem_->getConstraintNames(tmp.data(), nc) < 0)
            return fail("connect: getConstraintNames failed");
        con_names_.resize(nc);
        for (int i = 0; i < nc; ++i) con_names_[i] = tmp[i] ? tmp[i] : "";
    }

    // 界
    var_lower_.assign(nv, 0.0);
    var_upper_.assign(nv, 0.0);
    if (nv > 0 && problem_->getVariableBounds(var_lower_.data(), var_upper_.data(), nv) < 0)
        return fail("connect: getVariableBounds failed");
    con_lower_.assign(nc, 0.0);
    con_upper_.assign(nc, 0.0);
    if (nc > 0 && problem_->getConstraintBounds(con_lower_.data(), con_upper_.data(), nc) < 0)
        return fail("connect: getConstraintBounds failed");

    // 初值
    initial_x_.assign(nv, 0.0);
    if (nv > 0 && problem_->getInitialX(initial_x_.data(), nv) < 0)
        return fail("connect: getInitialX failed");

    // 约束 Jacobian 结构（两段式：nnz 经引用返回）
    {
        int nnz = 0;
        if (problem_->getConstraintJacobianStructure(nullptr, nullptr, nnz) < 0)
            return fail("connect: getConstraintJacobianStructure(size) failed");
        jac_rowidx_.assign(nnz, 0);
        jac_colidx_.assign(nnz, 0);
        if (nnz > 0 &&
            problem_->getConstraintJacobianStructure(jac_rowidx_.data(), jac_colidx_.data(), nnz) < 0)
            return fail("connect: getConstraintJacobianStructure(fill) failed");
        size_.num_nonlinear_jacobian_nz = static_cast<int>(jac_rowidx_.size());
    }
    // 目标梯度结构
    {
        int gsz = 0;
        if (problem_->getObjectiveGradientStructure(nullptr, gsz) < 0)
            return fail("connect: getObjectiveGradientStructure(size) failed");
        objgrad_colidx_.assign(gsz, 0);
        if (gsz > 0 && problem_->getObjectiveGradientStructure(objgrad_colidx_.data(), gsz) < 0)
            return fail("connect: getObjectiveGradientStructure(fill) failed");
        size_.num_nonlinear_objgrad_nz = static_cast<int>(objgrad_colidx_.size());
    }
    // 线性约束非零数（仅取规模）
    {
        int lsz = 0;
        if (problem_->getLinearConstraints(nullptr, nullptr, nullptr, lsz) >= 0)
            size_.num_linear_jacobian_nz = lsz;
    }

    // 下发初值，保证后续 evaluate 有效
    current_x_ = initial_x_;
    if (nv > 0 && problem_->setX(current_x_.data(), nv) < 0)
        return fail("connect: setX(initial) failed");

    initialized_ = true;
    return 0;
}

void XOptMINLPAdapter::disconnect() {
    if (problem_ && owns_problem_ && destroy_func_) {
        destroy_func_(problem_);
    }
    problem_ = nullptr;
    owns_problem_ = false;
    if (module_) {
        closeLib(module_);
        module_ = nullptr;
    }
}

int XOptMINLPAdapter::getSize(CapeMINLPSize& size_out) {
    if (!initialized_) return fail("getSize: not connected");
    size_out = size_;
    return 0;
}

int XOptMINLPAdapter::getStructure(const std::string& type, std::vector<int>& row_index,
                                   std::vector<int>& col_index, std::vector<int>& obj_index) {
    if (!initialized_) return fail("getStructure: not connected");
    row_index.clear();
    col_index.clear();
    obj_index.clear();
    if (type == cape::kStructJacobian) {
        row_index = jac_rowidx_;
        col_index = jac_colidx_;
        return 0;
    }
    if (type == cape::kStructObjectiveGradient) {
        obj_index = objgrad_colidx_;
        return 0;
    }
    return fail("getStructure: unknown type " + type);
}

int XOptMINLPAdapter::getVariableNames(const std::vector<int>& vids,
                                       std::vector<std::string>& names_out) {
    if (!initialized_) return fail("getVariableNames: not connected");
    pick(var_names_, vids, names_out);
    return 0;
}

int XOptMINLPAdapter::getVariableBounds(const std::vector<int>& vids, std::vector<double>& lower_out,
                                        std::vector<double>& upper_out) {
    if (!initialized_) return fail("getVariableBounds: not connected");
    pick(var_lower_, vids, lower_out);
    pick(var_upper_, vids, upper_out);
    return 0;
}

int XOptMINLPAdapter::getVariableValues(const std::vector<int>& vids,
                                        std::vector<double>& values_out) {
    if (!initialized_) return fail("getVariableValues: not connected");
    pick(current_x_, vids, values_out);
    return 0;
}

int XOptMINLPAdapter::setVariableValues(const std::vector<int>& vids,
                                        const std::vector<double>& values) {
    if (!initialized_) return fail("setVariableValues: not connected");
    if (vids.empty()) {
        if (values.size() != current_x_.size()) return fail("setVariableValues: size mismatch");
        current_x_ = values;
    } else {
        if (vids.size() != values.size()) return fail("setVariableValues: vids/values mismatch");
        for (size_t i = 0; i < vids.size(); ++i) {
            int id = vids[i];
            if (id < 0 || id >= static_cast<int>(current_x_.size()))
                return fail("setVariableValues: vid out of range");
            current_x_[id] = values[i];
        }
    }
    if (problem_->setX(current_x_.data(), static_cast<int>(current_x_.size())) < 0)
        return fail("setVariableValues: xOptProblem::setX failed");
    return 0;
}

int XOptMINLPAdapter::getConstraintNames(const std::vector<int>& cids,
                                         std::vector<std::string>& names_out) {
    if (!initialized_) return fail("getConstraintNames: not connected");
    pick(con_names_, cids, names_out);
    return 0;
}

int XOptMINLPAdapter::getConstraintBounds(const std::vector<int>& cids, std::vector<double>& lower_out,
                                          std::vector<double>& upper_out) {
    if (!initialized_) return fail("getConstraintBounds: not connected");
    pick(con_lower_, cids, lower_out);
    pick(con_upper_, cids, upper_out);
    return 0;
}

int XOptMINLPAdapter::getNonlinearConstraintValues(const std::vector<int>& cids,
                                                   std::vector<double>& values_out) {
    if (!initialized_) return fail("getNonlinearConstraintValues: not connected");
    std::vector<double> all(size_.num_constraints, 0.0);
    if (size_.num_constraints > 0 &&
        problem_->evaluateConstraints(all.data(), size_.num_constraints) < 0)
        return fail("getNonlinearConstraintValues: evaluateConstraints failed");
    pick(all, cids, values_out);
    return 0;
}

int XOptMINLPAdapter::getConstraintDerivativeValues(const std::string& type,
                                                    const std::vector<int>& /*cids*/,
                                                    std::vector<double>& values_out) {
    if (!initialized_) return fail("getConstraintDerivativeValues: not connected");
    if (type != cape::kStructJacobian && type != cape::kDerivNonlinear)
        return fail("getConstraintDerivativeValues: unsupported type " + type);
    // 按 Jacobian 稀疏结构顺序返回全部非零值（cids 子集首版不细分）
    const int nnz = static_cast<int>(jac_rowidx_.size());
    values_out.assign(nnz, 0.0);
    if (nnz > 0 && problem_->evaluateConstraintsJacobianValues(values_out.data(), nnz) < 0)
        return fail("getConstraintDerivativeValues: evaluateConstraintsJacobianValues failed");
    return 0;
}

int XOptMINLPAdapter::getObjectiveValue(double& value_out) {
    if (!initialized_) return fail("getObjectiveValue: not connected");
    if (problem_->evaluateObjective(value_out) < 0)
        return fail("getObjectiveValue: evaluateObjective failed");
    return 0;
}

int XOptMINLPAdapter::getObjectiveDerivativeValues(const std::string& /*type*/,
                                                   std::vector<double>& values_out) {
    if (!initialized_) return fail("getObjectiveDerivativeValues: not connected");
    const int gsz = static_cast<int>(objgrad_colidx_.size());
    values_out.assign(gsz, 0.0);
    if (gsz > 0 && problem_->evaluateObjectiveGradient(values_out.data(), gsz) < 0)
        return fail("getObjectiveDerivativeValues: evaluateObjectiveGradient failed");
    return 0;
}

std::string XOptMINLPAdapter::lastError() const { return last_error_; }
