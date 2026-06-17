#pragma once
// ***************************************************************
//  XOptMINLPAdapter   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  反向适配：把一个 xOptProblem 暴露成传输无关的 ICapeMINLPModel
//  （复用 capeopen_core 的抽象），供 COM/CORBA 前端发布为 CAPE-OPEN MINLP。
//  详见 docs/xOptMINLPco_design.md §2、§3。
//
//  输入来源（C++ ABI）：
//    - dll_path：加载导出 createProblem()/destroyProblem() 的 DLL；
//    - 注入 xOptProblem*：用于 in-proc 单测（不拥有该指针）。
// ***************************************************************
#include <string>
#include <vector>

#include "CapeMINLPModel.h"     // ICapeMINLPModel（来自 capeopen_core）
#include "xOpt/xOptProblem.h"   // xOptProblem / createProblemFunc

class XOptMINLPAdapter : public ICapeMINLPModel {
  public:
    explicit XOptMINLPAdapter(const std::string& dll_path);
    explicit XOptMINLPAdapter(xOptProblem* injected);  // 测试注入，不拥有
    ~XOptMINLPAdapter() override;

    int connect() override;  // 加载/创建 problem、initialize、缓存规模与结构
    void disconnect() override;

    int getSize(CapeMINLPSize& size_out) override;
    int getStructure(const std::string& type, std::vector<int>& row_index,
                     std::vector<int>& col_index, std::vector<int>& obj_index) override;
    int getVariableNames(const std::vector<int>& vids, std::vector<std::string>& names_out) override;
    int getVariableBounds(const std::vector<int>& vids, std::vector<double>& lower_out,
                          std::vector<double>& upper_out) override;
    int getVariableValues(const std::vector<int>& vids, std::vector<double>& values_out) override;
    int setVariableValues(const std::vector<int>& vids, const std::vector<double>& values) override;
    int getConstraintNames(const std::vector<int>& cids,
                           std::vector<std::string>& names_out) override;
    int getConstraintBounds(const std::vector<int>& cids, std::vector<double>& lower_out,
                            std::vector<double>& upper_out) override;
    int getNonlinearConstraintValues(const std::vector<int>& cids,
                                     std::vector<double>& values_out) override;
    int getConstraintDerivativeValues(const std::string& type, const std::vector<int>& cids,
                                      std::vector<double>& values_out) override;
    int getObjectiveValue(double& value_out) override;
    int getObjectiveDerivativeValues(const std::string& type,
                                     std::vector<double>& values_out) override;
    std::string lastError() const override;

  private:
    int fail(const std::string& msg) const;

    std::string dll_path_;
    void* module_ = nullptr;                  // HMODULE / dlopen handle
    destroyProblemFunc destroy_func_ = nullptr;
    xOptProblem* problem_ = nullptr;
    bool owns_problem_ = false;               // 由本对象 createProblem 创建则 true
    bool initialized_ = false;

    CapeMINLPSize size_{};
    std::vector<std::string> var_names_;
    std::vector<std::string> con_names_;
    std::vector<double> var_lower_;
    std::vector<double> var_upper_;
    std::vector<double> con_lower_;
    std::vector<double> con_upper_;
    std::vector<double> initial_x_;
    std::vector<double> current_x_;
    std::vector<int> jac_rowidx_;
    std::vector<int> jac_colidx_;
    std::vector<int> objgrad_colidx_;

    mutable std::string last_error_;
};
