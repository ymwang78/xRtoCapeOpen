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

    // 重新拉一遍规模与结构缓存，**不重连**。返回 >=0 成功。
    //
    // 用在远端问题被就地重建之后：本类在 initialize() 里把规模、名称、界、
    // Jacobian 稀疏结构全缓存住了，远端换了一套之后这些缓存就是错的，而
    // 宿主还拿着本对象的 vtable。不刷新的话，numConstraints() 报的是旧数，
    // 下一次 evaluateConstraints() 因长度对不上直接返回 -1。
    int refresh();

    // 上一次 refresh() 失败、缓存尚未与远端对齐。此状态下所有读取一律返回 -1：
    // 缓存里那份是**旧问题**的结构，拿它继续求解等于安静地解错题。
    bool isStale() const { return structure_stale_; }

    // 由外部标记为失效。用在"远端可能已经被改掉，但我们还没能重新读回来"的
    // 时刻——此时缓存里那份是不是最新的**无从判断**，按失效处理是唯一安全的
    // 选择。刷新成功会自动清掉这个状态。
    void markStale() { structure_stale_ = true; }

    // 登记一个"活动实例槽"。本对象析构时会把该槽置空，模型上下文因此不会
    // 拿着一个已释放的指针去 refresh()。传 nullptr 解除登记（模型先于问题
    // 销毁时用）。所有权仍在宿主手里，这里只是双向注销。
    void setLiveSlot(CapeMINLPProblemCore** slot) { live_slot_ = slot; }

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

    // initialize()/refresh() 共用的结构读取（不含 connect）
    int readStructure();

    // 缓存可用 = 已初始化且与远端对齐。所有读取都走这个判据，而不是只看
    // initialized_：刷新失败后缓存里那份是旧问题的结构，供出去就是解错题。
    bool usable() const { return initialized_ && !structure_stale_; }

    std::unique_ptr<ICapeMINLPModel> model_;
    CapeMINLPProblemCore** live_slot_ = nullptr;
    bool initialized_ = false;
    bool structure_stale_ = false;
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
