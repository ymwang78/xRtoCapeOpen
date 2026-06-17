#pragma once
// ***************************************************************
//  CapeMINLPModelMock   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  ICapeMINLPModel 的内存内 Mock 实现，用于 gtest 与端到端打通，
//  不依赖任何 COM/CORBA。承载一个有解析解的小型约束 NLP：
//
//      minimize   f(x) = x0^2 + x1^2
//      subject to c0(x) = x0 + x1 - 3 = 0
//                 -10 <= x0, x1 <= 10
//      解析最优:  x* = (1.5, 1.5),  f* = 4.5
//
//  详见 docs/capeopen_problem_design.md §8 M2。
// ***************************************************************
#include "CapeMINLPModel.h"

class CapeMINLPModelMock : public ICapeMINLPModel {
  public:
    // target 预留（可用于选择不同 mock 问题），当前忽略。
    explicit CapeMINLPModelMock(const std::string& target = "");
    ~CapeMINLPModelMock() override = default;

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
    static constexpr int kNumVars = 2;
    static constexpr int kNumCons = 1;

    std::string target_;
    std::vector<double> x_{0.0, 0.0};  // 当前点（setVariableValues 更新）
    bool connected_ = false;
    mutable std::string last_error_;

    int fail(const std::string& msg) const;
};
