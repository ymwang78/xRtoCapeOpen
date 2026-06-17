#pragma once
// ***************************************************************
//  CapeMINLPModelCom   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  COM 后端：把一个实现 CAPE-OPEN ICapeMINLP 的 COM 组件适配为
//  传输无关的 ICapeMINLPModel（design §3.1、§5.2、§6.4）。仅 Windows。
//
//  连接目标（target）支持 ProgID（"Some.ProgId"）或 CLSID 字符串
//  （"{GUID}"）。connect() 内做 CoInitializeEx + CoCreateInstance + QI。
//  另提供注入构造（传入已有 ICapeMINLP*），用于 in-proc 单测，绕过注册表。
// ***************************************************************
#ifdef _WIN32

#include <string>

#include "../../CapeMINLPModel.h"

struct ICapeMINLP;  // 前向声明，避免在头里拉入 windows.h

class CapeMINLPModelCom : public ICapeMINLPModel {
  public:
    explicit CapeMINLPModelCom(const std::string& target);
    // 测试注入：使用已存在的接口指针（会 AddRef），不走 CoCreateInstance/CoUninitialize。
    explicit CapeMINLPModelCom(ICapeMINLP* injected);
    ~CapeMINLPModelCom() override;

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
    int failHr(const std::string& method, long hr) const;  // hr 为 HRESULT

    std::string target_;
    ICapeMINLP* minlp_ = nullptr;
    bool com_initialized_ = false;
    bool injected_ = false;
    mutable std::string last_error_;
};

#endif  // _WIN32
