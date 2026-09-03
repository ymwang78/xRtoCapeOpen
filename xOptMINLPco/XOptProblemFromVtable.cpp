// ***************************************************************
//  XOptProblemFromVtable   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "XOptProblemFromVtable.h"

// xOptProblemT 的成员都是可选的（宿主按 size 覆盖范围填）。缺哪个就答 -1，
// 而不是解引用一个空指针——求解器会把 -1 当"这个问题没有这项能力"处理。
#define XOPT_VT_CALL(fn, ...) \
    ((pt_.handle != nullptr && pt_.fn != nullptr) ? pt_.fn(pt_.handle, __VA_ARGS__) : -1)

XOptProblemFromVtable::XOptProblemFromVtable(const xOptProblemT& problem) : pt_(problem) {}

XOptProblemFromVtable::~XOptProblemFromVtable() {
    if (pt_.handle != nullptr && pt_.destroyProblem != nullptr) pt_.destroyProblem(pt_.handle);
    pt_.handle = nullptr;
}

int XOptProblemFromVtable::numVariables() const {
    return (pt_.handle != nullptr && pt_.numVariables != nullptr) ? pt_.numVariables(pt_.handle)
                                                                   : -1;
}

int XOptProblemFromVtable::numConstraints() const {
    return (pt_.handle != nullptr && pt_.numConstraints != nullptr)
               ? pt_.numConstraints(pt_.handle)
               : -1;
}

int XOptProblemFromVtable::getVariableNames(const char* names[], int names_size) const {
    return XOPT_VT_CALL(getVariableNames, names, names_size);
}

int XOptProblemFromVtable::getVariableDescriptions(const char* descriptions[],
                                                   int descriptions_size) const {
    return XOPT_VT_CALL(getVariableDescriptions, descriptions, descriptions_size);
}

int XOptProblemFromVtable::getConstraintNames(const char* names[], int names_size) const {
    return XOPT_VT_CALL(getConstraintNames, names, names_size);
}

int XOptProblemFromVtable::getOptions(double* options, int options_size) const {
    return XOPT_VT_CALL(getOptions, options, options_size);
}

int XOptProblemFromVtable::getVariableBounds(double* xlow, double* xupp, int x_size) const {
    return XOPT_VT_CALL(getVariableBounds, xlow, xupp, x_size);
}

int XOptProblemFromVtable::getConstraintBounds(double* clow, double* cupp, int c_size) const {
    return XOPT_VT_CALL(getConstraintBounds, clow, cupp, c_size);
}

int XOptProblemFromVtable::getInitialX(double* x0, int x0_size) const {
    return XOPT_VT_CALL(getInitialX, x0, x0_size);
}

int XOptProblemFromVtable::getLinearConstraints(int* lcons_rowidx, int* lcons_colidx,
                                                double* values, int& lcons_size) const {
    return XOPT_VT_CALL(getLinearConstraints, lcons_rowidx, lcons_colidx, values, &lcons_size);
}

int XOptProblemFromVtable::getObjectiveGradientStructure(int* obj_colidx,
                                                         int& obj_colidx_size) const {
    return XOPT_VT_CALL(getObjectiveGradientStructure, obj_colidx, &obj_colidx_size);
}

int XOptProblemFromVtable::getConstraintJacobianStructure(int* cons_rowidx, int* cons_colidx,
                                                          int& nnz) const {
    return XOPT_VT_CALL(getConstraintJacobianStructure, cons_rowidx, cons_colidx, &nnz);
}

int XOptProblemFromVtable::setX(const double* x, int x_size) {
    return XOPT_VT_CALL(setX, x, x_size);
}

int XOptProblemFromVtable::runTimeCheck() const {
    return (pt_.handle != nullptr && pt_.runTimeCheck != nullptr) ? pt_.runTimeCheck(pt_.handle)
                                                                   : -1;
}

int XOptProblemFromVtable::evaluateObjective(double& obj) const {
    return XOPT_VT_CALL(evaluateObjective, &obj);
}

int XOptProblemFromVtable::evaluateConstraints(double* cons, int cons_size) const {
    return XOPT_VT_CALL(evaluateConstraints, cons, cons_size);
}

int XOptProblemFromVtable::evaluateObjectiveGradient(double* grad, int grad_size) const {
    return XOPT_VT_CALL(evaluateObjectiveGradient, grad, grad_size);
}

int XOptProblemFromVtable::evaluateConstraintsJacobianValues(double* values,
                                                             int values_size) const {
    return XOPT_VT_CALL(evaluateConstraintsJacobianValues, values, values_size);
}

#undef XOPT_VT_CALL
