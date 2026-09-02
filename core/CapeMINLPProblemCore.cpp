// ***************************************************************
//  CapeMINLPProblemCore   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "CapeMINLPProblemCore.h"

#include <algorithm>
#include <cstdio>

#include "xOpt/xOptProblem.h"  // xOptProblem::OPTIONS 枚举

// ---------------------------------------------------------------------------
// C vtable trampoline：把 xOptProblemHandle 还原为 CapeMINLPProblemCore*，
// 全部包 try/catch，保证 C++ 异常绝不跨越 C-ABI 边界（design §6.3）。
// ---------------------------------------------------------------------------
namespace {

inline CapeMINLPProblemCore* self(xOptProblemHandle h) {
    return reinterpret_cast<CapeMINLPProblemCore*>(h);
}

int* t_destroyProblem(xOptProblemHandle h) {
    try {
        delete self(h);
    } catch (...) {
    }
    return nullptr;
}

int t_numVariables(xOptProblemHandle h) {
    try {
        return self(h)->numVariables();
    } catch (...) {
        return -1;
    }
}

int t_numConstraints(xOptProblemHandle h) {
    try {
        return self(h)->numConstraints();
    } catch (...) {
        return -1;
    }
}

int t_getVariableNames(xOptProblemHandle h, const char* names[], int names_size) {
    try {
        return self(h)->getVariableNames(names, names_size);
    } catch (...) {
        return -1;
    }
}

int t_getVariableDescriptions(xOptProblemHandle h, const char* descriptions[], int descriptions_size) {
    try {
        return self(h)->getVariableDescriptions(descriptions, descriptions_size);
    } catch (...) {
        return -1;
    }
}

int t_getConstraintNames(xOptProblemHandle h, const char* names[], int names_size) {
    try {
        return self(h)->getConstraintNames(names, names_size);
    } catch (...) {
        return -1;
    }
}

int t_getOptions(xOptProblemHandle h, double* options, int options_size) {
    try {
        return self(h)->getOptions(options, options_size);
    } catch (...) {
        return -1;
    }
}

int t_getVariableBounds(xOptProblemHandle h, double* xlow, double* xupp, int x_size) {
    try {
        return self(h)->getVariableBounds(xlow, xupp, x_size);
    } catch (...) {
        return -1;
    }
}

int t_getConstraintBounds(xOptProblemHandle h, double* clow, double* cupp, int c_size) {
    try {
        return self(h)->getConstraintBounds(clow, cupp, c_size);
    } catch (...) {
        return -1;
    }
}

int t_getInitialX(xOptProblemHandle h, double* x0, int x0_size) {
    try {
        return self(h)->getInitialX(x0, x0_size);
    } catch (...) {
        return -1;
    }
}

int t_getLinearConstraints(xOptProblemHandle h, int* lcons_rowidx, int* lcons_colidx, double* values,
                           int* lcons_size) {
    try {
        return self(h)->getLinearConstraints(lcons_rowidx, lcons_colidx, values, lcons_size);
    } catch (...) {
        return -1;
    }
}

int t_getObjectiveGradientStructure(xOptProblemHandle h, int* obj_colidx, int* obj_colidx_size) {
    try {
        return self(h)->getObjectiveGradientStructure(obj_colidx, obj_colidx_size);
    } catch (...) {
        return -1;
    }
}

int t_getConstraintJacobianStructure(xOptProblemHandle h, int* cons_rowidx, int* cons_colidx,
                                     int* nnz) {
    try {
        return self(h)->getConstraintJacobianStructure(cons_rowidx, cons_colidx, nnz);
    } catch (...) {
        return -1;
    }
}

int t_setX(xOptProblemHandle h, const double* x, int x_size) {
    try {
        return self(h)->setX(x, x_size);
    } catch (...) {
        return -1;
    }
}

int t_runTimeCheck(xOptProblemHandle h) {
    try {
        return self(h)->runTimeCheck();
    } catch (...) {
        return -1;
    }
}

int t_evaluateObjective(xOptProblemHandle h, double* obj) {
    try {
        return self(h)->evaluateObjective(obj);
    } catch (...) {
        return -1;
    }
}

int t_evaluateConstraints(xOptProblemHandle h, double* cons, int cons_size) {
    try {
        return self(h)->evaluateConstraints(cons, cons_size);
    } catch (...) {
        return -1;
    }
}

int t_evaluateObjectiveGradient(xOptProblemHandle h, double* grad, int grad_size) {
    try {
        return self(h)->evaluateObjectiveGradient(grad, grad_size);
    } catch (...) {
        return -1;
    }
}

int t_evaluateConstraintsJacobianValues(xOptProblemHandle h, double* values, int values_size) {
    try {
        return self(h)->evaluateConstraintsJacobianValues(values, values_size);
    } catch (...) {
        return -1;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// CapeMINLPProblemCore
// ---------------------------------------------------------------------------
CapeMINLPProblemCore::CapeMINLPProblemCore(std::unique_ptr<ICapeMINLPModel> model)
    : model_(std::move(model)) {}

CapeMINLPProblemCore::~CapeMINLPProblemCore() {
    // 登记过的话先把槽置空：模型上下文可能比问题活得久，不注销就会拿着一个
    // 已释放的指针去 refresh()。
    if (live_slot_ != nullptr && *live_slot_ == this) *live_slot_ = nullptr;

    if (model_) {
        model_->disconnect();
    }
}

std::vector<int> CapeMINLPProblemCore::allVariableIds() const {
    std::vector<int> ids(size_.num_variables);
    for (int i = 0; i < size_.num_variables; ++i) ids[i] = i;
    return ids;
}

std::vector<int> CapeMINLPProblemCore::allConstraintIds() const {
    std::vector<int> ids(size_.num_constraints);
    for (int i = 0; i < size_.num_constraints; ++i) ids[i] = i;
    return ids;
}

int CapeMINLPProblemCore::initialize() {
    if (!model_) return -1;
    if (model_->connect() < 0) return -1;
    const int ret = readStructure();
    if (ret < 0) return ret;
    initialized_ = true;
    return 0;
}

// 远端问题被就地重建之后重新拉一遍缓存。不重连：连接还是那条，换的是连接
// 那一头的问题对象。
int CapeMINLPProblemCore::refresh() {
    if (!model_ || !initialized_) return -1;
    if (readStructure() < 0) {
        // 读失败不改缓存（readStructure 是事务式的），所以这里留下的是一份
        // **旧问题**的完整结构——自洽，但已经不是远端那个问题了。标记为陈旧，
        // 让所有读取都失败：安静地按旧结构解下去，比报错难查得多。
        structure_stale_ = true;
        return -1;
    }
    structure_stale_ = false;
    return 0;
}

// 事务式：全部读进局部量，**最后一次性提交**。中途失败一个字节都不改。
//
// 过去是直接往成员上写——先 size_，再逐项覆盖名称/界/结构。getVariableNames
// 半路 COMM_FAILURE 的话，留下的是"规模已是新的、名字还是旧的"混合缓存，而
// getVariableNames() 按 size_.num_variables 遍历 var_names_ —— 越界读。
// 组分从 2 个变 3 个时新规模 4、旧数组 3 项，正好踩中。
int CapeMINLPProblemCore::readStructure() {
    CapeMINLPSize size{};
    if (model_->getSize(size) < 0) return -1;

    std::vector<int> vids(static_cast<size_t>(std::max(size.num_variables, 0)));
    std::vector<int> cids(static_cast<size_t>(std::max(size.num_constraints, 0)));
    for (size_t i = 0; i < vids.size(); ++i) vids[i] = static_cast<int>(i);
    for (size_t i = 0; i < cids.size(); ++i) cids[i] = static_cast<int>(i);

    std::vector<std::string> var_names, con_names;
    std::vector<double> var_lower, var_upper, initial_x, con_lower, con_upper;
    std::vector<int> jac_rowidx, jac_colidx, objgrad_colidx;

    if (model_->getVariableNames(vids, var_names) < 0) return -1;
    if (model_->getVariableBounds(vids, var_lower, var_upper) < 0) return -1;
    if (model_->getVariableValues(vids, initial_x) < 0) return -1;
    if (model_->getConstraintNames(cids, con_names) < 0) return -1;
    if (model_->getConstraintBounds(cids, con_lower, con_upper) < 0) return -1;

    // 一次性拉取并缓存稀疏结构，之后两段式查询从缓存返回（design §6.1）
    std::vector<int> obj_unused;
    if (model_->getStructure(cape::kStructJacobian, jac_rowidx, jac_colidx, obj_unused) < 0) {
        return -1;
    }
    std::vector<int> row_unused, col_unused;
    if (model_->getStructure(cape::kStructObjectiveGradient, row_unused, col_unused,
                             objgrad_colidx) < 0) {
        return -1;
    }

    // 后端可以只答一部分吗？不行——访问器按 size_ 遍历这些数组，长度对不上
    // 就是越界。在提交前挡住，比在读取时崩掉好查。
    if (var_names.size() != vids.size() || var_lower.size() != vids.size() ||
        var_upper.size() != vids.size() || initial_x.size() != vids.size() ||
        con_names.size() != cids.size() || con_lower.size() != cids.size() ||
        con_upper.size() != cids.size()) {
        return -1;
    }

    // —— 到这里才动成员：以下不再有失败点 ——
    size_ = size;
    var_names_ = std::move(var_names);
    var_lower_ = std::move(var_lower);
    var_upper_ = std::move(var_upper);
    initial_x_ = std::move(initial_x);
    con_names_ = std::move(con_names);
    con_lower_ = std::move(con_lower);
    con_upper_ = std::move(con_upper);
    jac_rowidx_ = std::move(jac_rowidx);
    jac_colidx_ = std::move(jac_colidx);
    objgrad_colidx_ = std::move(objgrad_colidx);

    // 规模可能变了，setX 的缓冲跟着重开；x_set_ 归位，因为旧的那个点已经
    // 不属于这个问题了——继续拿它求值会静默地算错。
    x_.assign(size_.num_variables, 0.0);
    x_set_ = false;
    return 0;
}

void CapeMINLPProblemCore::fillVtable(xOptProblemT* problem) {
    if (problem == nullptr) return;
    problem->size = sizeof(xOptProblemT);
    problem->handle = reinterpret_cast<xOptProblemHandle>(this);
    problem->destroyProblem = &t_destroyProblem;
    problem->numVariables = &t_numVariables;
    problem->numConstraints = &t_numConstraints;
    problem->getVariableNames = &t_getVariableNames;
    problem->getVariableDescriptions = &t_getVariableDescriptions;
    problem->getConstraintNames = &t_getConstraintNames;
    problem->getOptions = &t_getOptions;
    problem->getVariableBounds = &t_getVariableBounds;
    problem->getConstraintBounds = &t_getConstraintBounds;
    problem->getInitialX = &t_getInitialX;
    problem->getLinearConstraints = &t_getLinearConstraints;
    problem->getObjectiveGradientStructure = &t_getObjectiveGradientStructure;
    problem->getConstraintJacobianStructure = &t_getConstraintJacobianStructure;
    problem->setX = &t_setX;
    problem->runTimeCheck = &t_runTimeCheck;
    problem->evaluateObjective = &t_evaluateObjective;
    problem->evaluateConstraints = &t_evaluateConstraints;
    problem->evaluateObjectiveGradient = &t_evaluateObjectiveGradient;
    problem->evaluateConstraintsJacobianValues = &t_evaluateConstraintsJacobianValues;
}

int CapeMINLPProblemCore::numVariables() const { return usable() ? size_.num_variables : -1; }

int CapeMINLPProblemCore::numConstraints() const { return usable() ? size_.num_constraints : -1; }

int CapeMINLPProblemCore::getVariableNames(const char* names[], int names_size) const {
    if (!usable() || names == nullptr) return -1;
    const int n = std::min<int>(names_size, size_.num_variables);
    for (int i = 0; i < n; ++i) names[i] = var_names_[i].c_str();
    return n;
}

int CapeMINLPProblemCore::getVariableDescriptions(const char* descriptions[],
                                                  int descriptions_size) const {
    // CAPE-OPEN 无变量描述，返回空串（与 BlackBox 行为一致）
    if (descriptions == nullptr) return -1;
    const int n = std::min<int>(descriptions_size, size_.num_variables);
    for (int i = 0; i < n; ++i) descriptions[i] = "";
    return n;
}

int CapeMINLPProblemCore::getConstraintNames(const char* names[], int names_size) const {
    if (!usable() || names == nullptr) return -1;
    const int n = std::min<int>(names_size, size_.num_constraints);
    for (int i = 0; i < n; ++i) names[i] = con_names_[i].c_str();
    return n;
}

int CapeMINLPProblemCore::getOptions(double* options, int options_size) const {
    if (options == nullptr || options_size < xOptProblem::OPTIONS_LIMIT) return -1;
    options[xOptProblem::OPTIONS_MAGIC] = 'X';
    options[xOptProblem::HAS_DERIVATIVE] = 1;  // CAPE-OPEN 提供解析导数
    options[xOptProblem::HAS_LINEAR_A] = size_.num_linear_jacobian_nz > 0 ? 1 : 0;
    options[xOptProblem::IS_SIMULATION] = 0;
    return 0;
}

int CapeMINLPProblemCore::getVariableBounds(double* xlow, double* xupp, int x_size) const {
    if (!usable() || xlow == nullptr || xupp == nullptr) return -1;
    const int n = std::min<int>(x_size, size_.num_variables);
    for (int i = 0; i < n; ++i) {
        xlow[i] = var_lower_[i];
        xupp[i] = var_upper_[i];
    }
    return n;
}

int CapeMINLPProblemCore::getConstraintBounds(double* clow, double* cupp, int c_size) const {
    if (!usable() || clow == nullptr || cupp == nullptr) return -1;
    const int n = std::min<int>(c_size, size_.num_constraints);
    for (int i = 0; i < n; ++i) {
        clow[i] = con_lower_[i];
        cupp[i] = con_upper_[i];
    }
    return n;
}

int CapeMINLPProblemCore::getInitialX(double* x0, int x0_size) const {
    if (!usable() || x0 == nullptr) return -1;
    const int n = std::min<int>(x0_size, size_.num_variables);
    for (int i = 0; i < n; ++i) x0[i] = initial_x_[i];
    return n;
}

int CapeMINLPProblemCore::getLinearConstraints(int* lcons_rowidx, int* lcons_colidx, double* values,
                                               int* lcons_size) const {
    if (lcons_size == nullptr) return -1;
    // 本通路把约束统一按非线性 Jacobian 处理，不暴露独立线性 A（design §5）。
    *lcons_size = 0;
    return 0;
}

int CapeMINLPProblemCore::getObjectiveGradientStructure(int* obj_colidx, int* obj_colidx_size) const {
    if (!usable() || obj_colidx_size == nullptr) return -1;
    const int nnz = static_cast<int>(objgrad_colidx_.size());
    if (obj_colidx == nullptr) {  // 第一段：查长度
        *obj_colidx_size = nnz;
        return nnz;
    }
    const int n = std::min<int>(*obj_colidx_size, nnz);
    for (int i = 0; i < n; ++i) obj_colidx[i] = objgrad_colidx_[i];
    *obj_colidx_size = n;
    return n;
}

int CapeMINLPProblemCore::getConstraintJacobianStructure(int* cons_rowidx, int* cons_colidx,
                                                         int* nnz) const {
    if (!usable() || nnz == nullptr) return -1;
    const int total = static_cast<int>(jac_rowidx_.size());
    if (cons_rowidx == nullptr || cons_colidx == nullptr) {  // 第一段：查长度
        *nnz = total;
        return total;
    }
    const int n = std::min<int>(*nnz, total);
    for (int i = 0; i < n; ++i) {
        cons_rowidx[i] = jac_rowidx_[i];
        cons_colidx[i] = jac_colidx_[i];
    }
    *nnz = n;
    return n;
}

int CapeMINLPProblemCore::setX(const double* x, int x_size) {
    if (!usable() || x == nullptr) return -1;
    const int n = std::min<int>(x_size, size_.num_variables);
    x_.assign(x, x + n);
    const std::vector<int> vids = allVariableIds();
    if (model_->setVariableValues(vids, x_) < 0) return -1;
    x_set_ = true;
    return n;
}

int CapeMINLPProblemCore::runTimeCheck() const { return 1; }

int CapeMINLPProblemCore::evaluateObjective(double* obj) const {
    if (!usable() || obj == nullptr) return -1;
    return model_->getObjectiveValue(*obj);
}

int CapeMINLPProblemCore::evaluateConstraints(double* cons, int cons_size) const {
    if (!usable() || cons == nullptr) return -1;
    std::vector<double> values;
    if (model_->getNonlinearConstraintValues(allConstraintIds(), values) < 0) return -1;
    const int n = std::min<int>(cons_size, static_cast<int>(values.size()));
    for (int i = 0; i < n; ++i) cons[i] = values[i];
    return n;
}

int CapeMINLPProblemCore::evaluateObjectiveGradient(double* grad, int grad_size) const {
    if (!usable() || grad == nullptr) return -1;
    std::vector<double> values;
    if (model_->getObjectiveDerivativeValues(cape::kDerivNonlinear, values) < 0) return -1;
    const int n = std::min<int>(grad_size, static_cast<int>(values.size()));
    for (int i = 0; i < n; ++i) grad[i] = values[i];
    return n;
}

int CapeMINLPProblemCore::evaluateConstraintsJacobianValues(double* values, int values_size) const {
    if (!usable() || values == nullptr) return -1;
    std::vector<double> jac;
    if (model_->getConstraintDerivativeValues(cape::kStructJacobian, allConstraintIds(), jac) < 0) {
        return -1;
    }
    const int n = std::min<int>(values_size, static_cast<int>(jac.size()));
    for (int i = 0; i < n; ++i) values[i] = jac[i];
    return n;
}
