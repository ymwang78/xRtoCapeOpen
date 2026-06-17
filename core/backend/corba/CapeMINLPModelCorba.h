#pragma once
// ***************************************************************
//  CapeMINLPModelCorba   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  CORBA/TAO 后端：把一个实现 CAPE-OPEN ICapeMINLP 的 CORBA 对象适配为
//  传输无关的 ICapeMINLPModel（design §3.1/§3.3）。复用 SqpSolver* stub。
//
//  target：IOR / corbaloc / corbaname 字符串；connect() 内 ORB_init +
//  string_to_object + _narrow。另有注入构造（传入已 _narrow 的对象引用），
//  用于 in-proc 协同（collocated）单测。
//  编译需 WIN32/ACE_AS_STATIC_LIBS/TAO_AS_STATIC_LIBS（CMake 提供）。
// ***************************************************************
#include <string>

#include "../../CapeMINLPModel.h"
#include "SqpSolverC.h"

class CapeMINLPModelCorba : public ICapeMINLPModel {
  public:
    explicit CapeMINLPModelCorba(const std::string& target);
    // 注入：使用已有对象引用（会 _duplicate），不 ORB_init。
    explicit CapeMINLPModelCorba(SqpSolver::ICapeMINLP_ptr injected);
    ~CapeMINLPModelCorba() override;

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

    std::string target_;
    SqpSolver::ICapeMINLP_var minlp_;
    bool orb_owned_ = false;  // 是否由本对象 ORB_init（注入时为 false）
    mutable std::string last_error_;
};
