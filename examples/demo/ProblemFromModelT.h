// ***************************************************************
//  ProblemFromModelT.h
//  -------------------------------------------------------------
//  xOptProblemT（C 函数表）→ xOptProblem（C++ 纯虚接口）的桥接器。
//
//  这是平台宿主 xOptProblemFromBlackBoxModel
//  （libsrc/xOpt/src/Model/xOptModelBlackBox.cpp）的精简镜像：
//  模型 DLL 的 buildProblem 只能填 C 函数表，而求解器消费的是
//  xOptProblem C++ 对象，两者之间必须有这一层适配。架构师在 CORBA
//  服务端做模型接入时需要的是同款适配。
// ***************************************************************
#pragma once

// demo 不导入也不导出平台类：把 XOPTIF_API 定义为空，
// 否则 xOptProblem 会带 dllimport 修饰，其隐式构造/析构无法链接。
#ifndef XOPTIF_API
#define XOPTIF_API
#endif

#include "xOpt/xOptModel.h"
#include "xOpt/xOptProblem.h"

class ProblemFromModelT : public xOptProblem {
  public:
    // model：已填好 handle 与函数表的 xOptModelT（createModel 的产物）
    explicit ProblemFromModelT(xOptModelT* model);
    ~ProblemFromModelT() override;

    // 调模型的 buildProblem 填出内部函数表；失败返回负值
    int initialize() override;

    // ---- xOptProblem 接口：全部转发给内部 C 函数表 ----
    int numVariables() const override;
    int numConstraints() const override;
    int getVariableNames(const char* names[], int names_size) const override;
    int getVariableDescriptions(const char* descriptions[],
                                int descriptions_size) const override;
    int getConstraintNames(const char* names[], int names_size) const override;
    int getOptions(double* options, int options_size) const override;
    int getVariableBounds(double* xlow, double* xupp, int x_size) const override;
    int getConstraintBounds(double* clow, double* cupp, int c_size) const override;
    int getInitialX(double* x0, int x0_size) const override;
    // 注意：xOptProblemT 里结构查询的 size 参数是 int*，
    // 而 C++ 接口是 int&，这里做转换（与宿主包装层一致）
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
    xOptModelT* model_;                              // 借用的模型函数表
    xOptProblemT problem_ = {sizeof(xOptProblemT)};  // buildProblem 的产物
};
