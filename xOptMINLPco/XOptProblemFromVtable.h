#pragma once
// ***************************************************************
//  XOptProblemFromVtable   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  xOptProblemT（C 函数表）-> xOptProblem（C++ 纯虚接口）的桥接器。
//
//  求解器 DLL 的 createSolver 消费的是 xOptProblem* —— 一个 C++ 对象；而
//  CapeMINLPProblemCore 把远端 ICapeMINLP 映射出来的是 xOptProblemT —— 一张
//  C 函数表（那是给宿主 xOptModelBlackBox 用的形态）。求解器服务端要把两者
//  接起来，就得有这一层。它是 examples/demo/ProblemFromModelT 的同款，只是
//  不再经过 xOptModelT：这里没有模型，只有一个已经建好的问题。
//
//  所有权：**拥有** vtable 后面的对象，析构时经 problem.destroyProblem 归还。
//  CapeMINLPProblemCore::fillVtable 之后那个 core 就归这里管了。
//
//  XOPTINTERFACE_EXPORTS：xOptProblem 类带 XOPTIF_API。派生类需要基类的隐式
//  构造/析构"就地生成"而不是按 dllimport 去链接某个 xOptInterface DLL——那个
//  DLL 并不存在。与 tests/MockXOptProblem.h 同一个处理：本头必须是 TU 里
//  第一个碰到 xOpt 头文件的包含。
// ***************************************************************
#ifdef _WIN32
#    ifndef XOPTINTERFACE_EXPORTS
#        define XOPTINTERFACE_EXPORTS
#    endif
#endif

#include "xOpt/xOptModel.h"    // xOptProblemT
#include "xOpt/xOptProblem.h"  // xOptProblem

class XOptProblemFromVtable : public xOptProblem {
  public:
    // problem：已填好 handle 与函数指针的 vtable。本对象接管其所有权。
    explicit XOptProblemFromVtable(const xOptProblemT& problem);
    ~XOptProblemFromVtable() override;

    XOptProblemFromVtable(const XOptProblemFromVtable&) = delete;
    XOptProblemFromVtable& operator=(const XOptProblemFromVtable&) = delete;

    // vtable 来的问题已经构造完毕；这里没有第二次初始化可做。
    int initialize() override { return 0; }

    int numVariables() const override;
    int numConstraints() const override;
    int getVariableNames(const char* names[], int names_size) const override;
    int getVariableDescriptions(const char* descriptions[], int descriptions_size) const override;
    int getConstraintNames(const char* names[], int names_size) const override;
    int getOptions(double* options, int options_size) const override;
    int getVariableBounds(double* xlow, double* xupp, int x_size) const override;
    int getConstraintBounds(double* clow, double* cupp, int c_size) const override;
    int getInitialX(double* x0, int x0_size) const override;
    // 结构查询的 size 在 vtable 里是 int*，C++ 接口是 int&：这里转换。
    int getLinearConstraints(int* lcons_rowidx, int* lcons_colidx, double* values,
                             int& lcons_size) const override;
    int getObjectiveGradientStructure(int* obj_colidx, int& obj_colidx_size) const override;
    int getConstraintJacobianStructure(int* cons_rowidx, int* cons_colidx,
                                       int& nnz) const override;
    int setX(const double* x, int x_size) override;
    int runTimeCheck() const override;
    int evaluateObjective(double& obj) const override;
    int evaluateConstraints(double* cons, int cons_size) const override;
    int evaluateObjectiveGradient(double* grad, int grad_size) const override;
    int evaluateConstraintsJacobianValues(double* values, int values_size) const override;

  private:
    xOptProblemT pt_;
};
