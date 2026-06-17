#pragma once
// ***************************************************************
//  CapeMINLPModel   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  传输无关的 CAPE-OPEN MINLP 模型抽象。
//  COM / CORBA / Mock 后端都实现本接口；映射核心 CapeMINLPProblemCore
//  只依赖本抽象，不见任何 COM/CORBA 细节。
//  详见 docs/capeopen_problem_design.md §3.1。
//  约定：所有方法返回 >= 0 成功，< 0 失败；索引一律 0-based。
// ***************************************************************
#include <string>
#include <vector>

// GetMINLPSize 返回的问题规模（字段顺序对应 IDL ICapeMINLP::GetMINLPSize）。
struct CapeMINLPSize {
    int num_variables = 0;                 // nv    变量总数
    int num_integer_variables = 0;         // niv   整数变量数（首版按连续松弛处理）
    int num_linear_variables = 0;          // nlv
    int num_linear_integer_variables = 0;  // nliv
    int num_constraints = 0;               // nc    约束总数
    int num_linear_constraints = 0;        // nlc   线性约束数
    int num_linear_jacobian_nz = 0;        // nlz   Jacobian 线性部分非零数
    int num_nonlinear_jacobian_nz = 0;     // nnz   Jacobian 非线性部分非零数
    int num_linear_objgrad_nz = 0;         // nlzof 目标梯度线性部分非零数
    int num_nonlinear_objgrad_nz = 0;      // nnzof 目标梯度非线性部分非零数
};

// GetMINLPStructure / GetMINLP*DerivativeValues 的结构类型字符串。
// 注意：不同厂商组件取值可能不同，落地前需与 DLL 文档对齐（design §5.1）。
namespace cape {
inline constexpr const char* kStructJacobian = "Jacobian";
inline constexpr const char* kStructObjectiveGradient = "ObjectiveGradient";
inline constexpr const char* kDerivLinear = "Linear";
inline constexpr const char* kDerivNonlinear = "Nonlinear";
}  // namespace cape

// CAPE-OPEN MINLP 模型抽象接口（镜像 IDL 的 ICapeMINLP）。
class ICapeMINLPModel {
  public:
    virtual ~ICapeMINLPModel() = default;

    // 建立/断开与底层组件的连接。
    // COM 后端: CoCreateInstance / QueryInterface；CORBA 后端: ORB resolve / _narrow。
    virtual int connect() = 0;
    virtual void disconnect() = 0;

    // —— 与 IDL ICapeMINLP 一一对应（0-based）——

    virtual int getSize(CapeMINLPSize& size_out) = 0;

    // type 取 cape::kStructJacobian / kStructObjectiveGradient。
    // Jacobian: 填 row_index/col_index（obj_index 为空）。
    // ObjectiveGradient: 填 obj_index（row_index/col_index 为空）。
    virtual int getStructure(const std::string& type, std::vector<int>& row_index,
                             std::vector<int>& col_index, std::vector<int>& obj_index) = 0;

    virtual int getVariableNames(const std::vector<int>& vids,
                                 std::vector<std::string>& names_out) = 0;

    virtual int getVariableBounds(const std::vector<int>& vids, std::vector<double>& lower_out,
                                  std::vector<double>& upper_out) = 0;

    virtual int getVariableValues(const std::vector<int>& vids, std::vector<double>& values_out) = 0;

    virtual int setVariableValues(const std::vector<int>& vids, const std::vector<double>& values) = 0;

    virtual int getConstraintNames(const std::vector<int>& cids,
                                   std::vector<std::string>& names_out) = 0;

    virtual int getConstraintBounds(const std::vector<int>& cids, std::vector<double>& lower_out,
                                    std::vector<double>& upper_out) = 0;

    virtual int getNonlinearConstraintValues(const std::vector<int>& cids,
                                             std::vector<double>& values_out) = 0;

    // type 取 cape::kStructJacobian（全部）或 kDerivLinear/kDerivNonlinear（分部）。
    virtual int getConstraintDerivativeValues(const std::string& type, const std::vector<int>& cids,
                                              std::vector<double>& values_out) = 0;

    virtual int getObjectiveValue(double& value_out) = 0;

    // type 取 cape::kDerivNonlinear（默认）/ kDerivLinear。
    virtual int getObjectiveDerivativeValues(const std::string& type,
                                             std::vector<double>& values_out) = 0;

    // 最近一次错误描述，供日志。
    virtual std::string lastError() const = 0;
};
