#pragma once
// ***************************************************************
//  XOptMINLPAdapter   version:  2.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  反向适配：把一个 xOptProblem 暴露成传输无关的 ICapeMINLPModel
//  （复用 capeopen_core 的抽象），供 COM/CORBA 前端发布为 CAPE-OPEN MINLP。
//  详见 docs/xOptMINLPco_design.md §2、§3。
//
//  输入来源（自动探测 DLL 导出，N1=C++ ABI / N5=C ABI）：
//    - C++ ABI：DLL 导出 createProblem()/destroyProblem()（返回 xOptProblem*）
//    - C   ABI：DLL 导出 xOptModel_createModel()（填 xOptModelT，buildProblem 填 xOptProblemT）
//    - 注入：xOptProblem*（C++）或 xOptProblemT（C）—— 用于 in-proc 单测，不拥有
// ***************************************************************
#include <memory>
#include <string>
#include <vector>

#include "CapeMINLPModel.h"     // ICapeMINLPModel（来自 capeopen_core）
#include "xOpt/xOptModel.h"     // xOptModelT / xOptProblemT
#include "xOpt/xOptProblem.h"   // xOptProblem / createProblemFunc

struct IXOptProblemView;  // 内部：屏蔽 C++/C ABI 差异（定义在 .cpp）

class XOptMINLPAdapter : public ICapeMINLPModel {
  public:
    explicit XOptMINLPAdapter(const std::string& dll_path);  // 自动探测 ABI
    explicit XOptMINLPAdapter(xOptProblem* injected);        // 注入 C++ 问题（不拥有）
    explicit XOptMINLPAdapter(const xOptProblemT& injected); // 注入 C vtable（不拥有）
    ~XOptMINLPAdapter() override;

    int connect() override;
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

    // 输入来源（connect 时据此建 view_）
    std::string dll_path_;
    xOptProblem* inject_cpp_ = nullptr;
    bool have_capi_inject_ = false;
    xOptProblemT inject_capi_{};

    void* module_ = nullptr;  // HMODULE / dlopen handle
    std::unique_ptr<IXOptProblemView> view_;
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
