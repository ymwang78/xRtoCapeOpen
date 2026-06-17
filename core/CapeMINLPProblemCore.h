#pragma once
// ***************************************************************
//  CapeMINLPProblemCore   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  把一个 ICapeMINLPModel（COM/CORBA/Mock 任一后端）适配为 xOpt 的
//  C 风格问题 vtable（xOptProblemT）。本类是后端无关的映射核心：
//  §5 映射 / 结构缓存 / setX 状态机 / 异常→负返回码，全部写一遍。
//  详见 docs/capeopen_problem_design.md §3.1、§5、§6。
// ***************************************************************
#include <memory>
#include <string>
#include <vector>

#include "CapeMINLPModel.h"
#include "xOpt/xOptModel.h"  // xOptProblemT

class CapeMINLPProblemCore {
  public:
    explicit CapeMINLPProblemCore(std::unique_ptr<ICapeMINLPModel> model);
    ~CapeMINLPProblemCore();

    // connect 后端、读取规模、预取并缓存名称/界/稀疏结构。返回 >=0 成功。
    int initialize();

    // 把本对象绑定到 C 风格 vtable：problem->handle = this，函数指针 = 内部 trampoline。
    void fillVtable(xOptProblemT* problem);

    // —— 逻辑方法（由 trampoline 调用，对应 xOptProblemT 各指针）——
    int numVariables() const;
    int numConstraints() const;
    int getVariableNames(const char* names[], int names_size) const;
    int getVariableDescriptions(const char* descriptions[], int descriptions_size) const;
    int getConstraintNames(const char* names[], int names_size) const;
    int getOptions(double* options, int options_size) const;
    int getVariableBounds(double* xlow, double* xupp, int x_size) const;
    int getConstraintBounds(double* clow, double* cupp, int c_size) const;
    int getInitialX(double* x0, int x0_size) const;
    int getLinearConstraints(int* lcons_rowidx, int* lcons_colidx, double* values,
                             int* lcons_size) const;
    int getObjectiveGradientStructure(int* obj_colidx, int* obj_colidx_size) const;
    int getConstraintJacobianStructure(int* cons_rowidx, int* cons_colidx, int* nnz) const;
    int setX(const double* x, int x_size);
    int runTimeCheck() const;
    int evaluateObjective(double* obj) const;
    int evaluateConstraints(double* cons, int cons_size) const;
    int evaluateObjectiveGradient(double* grad, int grad_size) const;
    int evaluateConstraintsJacobianValues(double* values, int values_size) const;

  private:
    std::vector<int> allVariableIds() const;
    std::vector<int> allConstraintIds() const;

    std::unique_ptr<ICapeMINLPModel> model_;
    bool initialized_ = false;
    CapeMINLPSize size_{};

    // initialize() 阶段缓存（避免重复跨界调用、保证字符串指针稳定）
    std::vector<std::string> var_names_;
    std::vector<std::string> con_names_;
    std::vector<double> var_lower_;
    std::vector<double> var_upper_;
    std::vector<double> con_lower_;
    std::vector<double> con_upper_;
    std::vector<double> initial_x_;
    std::vector<int> jac_rowidx_;   // 约束 Jacobian 稀疏结构（0-based）
    std::vector<int> jac_colidx_;
    std::vector<int> objgrad_colidx_;  // 目标梯度非零列（0-based）

    std::vector<double> x_;  // 最近一次 setX
    bool x_set_ = false;
};
