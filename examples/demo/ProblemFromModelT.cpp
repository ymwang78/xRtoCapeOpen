// ***************************************************************
//  ProblemFromModelT.cpp
//  -------------------------------------------------------------
//  桥接器实现：每个虚函数只做"判空 + 转发"，与宿主
//  xOptProblemFromBlackBoxModel 的写法保持一致。
// ***************************************************************
#include "ProblemFromModelT.h"

ProblemFromModelT::ProblemFromModelT(xOptModelT* model) : model_(model) {}

ProblemFromModelT::~ProblemFromModelT() {
    // 问题对象由模型分配，析构时归还（宿主同样在析构时调 destroyProblem）
    if (problem_.handle != nullptr && problem_.destroyProblem != nullptr) {
        problem_.destroyProblem(problem_.handle);
        problem_.handle = nullptr;
    }
}

int ProblemFromModelT::initialize() {
    if (model_ != nullptr && model_->handle != nullptr && model_->buildProblem != nullptr) {
        return model_->buildProblem(model_->handle, &problem_);
    }
    return -1;
}

int ProblemFromModelT::numVariables() const {
    if (problem_.handle != nullptr) return problem_.numVariables(problem_.handle);
    return -1;
}

int ProblemFromModelT::numConstraints() const {
    if (problem_.handle != nullptr) return problem_.numConstraints(problem_.handle);
    return -1;
}

int ProblemFromModelT::getVariableNames(const char* names[], int names_size) const {
    if (problem_.handle != nullptr)
        return problem_.getVariableNames(problem_.handle, names, names_size);
    return -1;
}

int ProblemFromModelT::getVariableDescriptions(const char* descriptions[],
                                               int descriptions_size) const {
    if (problem_.handle != nullptr)
        return problem_.getVariableDescriptions(problem_.handle, descriptions,
                                                descriptions_size);
    return -1;
}

int ProblemFromModelT::getConstraintNames(const char* names[], int names_size) const {
    if (problem_.handle != nullptr)
        return problem_.getConstraintNames(problem_.handle, names, names_size);
    return -1;
}

int ProblemFromModelT::getOptions(double* options, int options_size) const {
    if (problem_.handle != nullptr)
        return problem_.getOptions(problem_.handle, options, options_size);
    return -1;
}

int ProblemFromModelT::getVariableBounds(double* xlow, double* xupp, int x_size) const {
    if (problem_.handle != nullptr)
        return problem_.getVariableBounds(problem_.handle, xlow, xupp, x_size);
    return -1;
}

int ProblemFromModelT::getConstraintBounds(double* clow, double* cupp, int c_size) const {
    if (problem_.handle != nullptr)
        return problem_.getConstraintBounds(problem_.handle, clow, cupp, c_size);
    return -1;
}

int ProblemFromModelT::getInitialX(double* x0, int x0_size) const {
    if (problem_.handle != nullptr) return problem_.getInitialX(problem_.handle, x0, x0_size);
    return -1;
}

// 以下三个结构查询：C++ 接口 size 是 int&，xOptProblemT 是 int*，此处转换
int ProblemFromModelT::getLinearConstraints(int* lcons_rowidx, int* lcons_colidx,
                                            double* values, int& lcons_size) const {
    if (problem_.handle != nullptr)
        return problem_.getLinearConstraints(problem_.handle, lcons_rowidx, lcons_colidx,
                                             values, &lcons_size);
    return -1;
}

int ProblemFromModelT::getObjectiveGradientStructure(int* obj_colidx,
                                                     int& obj_colidx_size) const {
    if (problem_.handle != nullptr)
        return problem_.getObjectiveGradientStructure(problem_.handle, obj_colidx,
                                                      &obj_colidx_size);
    return -1;
}

int ProblemFromModelT::getConstraintJacobianStructure(int* cons_rowidx, int* cons_colidx,
                                                      int& nnz) const {
    if (problem_.handle != nullptr)
        return problem_.getConstraintJacobianStructure(problem_.handle, cons_rowidx,
                                                       cons_colidx, &nnz);
    return -1;
}

int ProblemFromModelT::setX(const double* x, int x_size) {
    if (problem_.handle != nullptr) return problem_.setX(problem_.handle, x, x_size);
    return -1;
}

int ProblemFromModelT::runTimeCheck() const {
    if (problem_.handle != nullptr) return problem_.runTimeCheck(problem_.handle);
    return -1;
}

int ProblemFromModelT::evaluateObjective(double& obj) const {
    // xOptProblemT 收指针，C++ 接口收引用，此处转换
    if (problem_.handle != nullptr) return problem_.evaluateObjective(problem_.handle, &obj);
    return -1;
}

int ProblemFromModelT::evaluateConstraints(double* cons, int cons_size) const {
    if (problem_.handle != nullptr)
        return problem_.evaluateConstraints(problem_.handle, cons, cons_size);
    return -1;
}

int ProblemFromModelT::evaluateObjectiveGradient(double* grad, int grad_size) const {
    if (problem_.handle != nullptr)
        return problem_.evaluateObjectiveGradient(problem_.handle, grad, grad_size);
    return -1;
}

int ProblemFromModelT::evaluateConstraintsJacobianValues(double* values,
                                                         int values_size) const {
    if (problem_.handle != nullptr)
        return problem_.evaluateConstraintsJacobianValues(problem_.handle, values, values_size);
    return -1;
}
