#pragma once
// ***************************************************************
//  MINLPServant   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  CORBA/TAO 前端：POA 实现 SqpSolver::ICapeMINLP，把调用委托给一个
//  ICapeMINLPModel（通常是 XOptMINLPAdapter，背靠 xOptProblem）。
//  这是 capeopen_core 的 CapeMINLPModelCorba 的「生产者」对偶。复用
//  SqpSolver* stub + CapeCorbaMarshal。详见 docs/xOptMINLPco_design.md（N4）。
//  编译需 WIN32/ACE_AS_STATIC_LIBS/TAO_AS_STATIC_LIBS（由 capeopen_corba 传递）。
// ***************************************************************
#include <memory>

#include "CapeMINLPModel.h"  // ICapeMINLPModel（capeopen_core）
#include "SqpSolverS.h"      // POA_SqpSolver::ICapeMINLP（生成）

class MINLPServant : public POA_SqpSolver::ICapeMINLP {
  public:
    // 生产模式：从环境变量 XRTO_XOPT_PROBLEM_DLL 建 XOptMINLPAdapter 并 connect。
    MINLPServant();
    // 测试模式：委托给已 connect 的外部 model（不拥有）。
    explicit MINLPServant(ICapeMINLPModel* model);

    bool ok() const { return model_ != nullptr; }

    void GetMINLPSize(::SqpSolver::CapeLong_out nv, ::SqpSolver::CapeLong_out niv,
                      ::SqpSolver::CapeLong_out nlv, ::SqpSolver::CapeLong_out nliv,
                      ::SqpSolver::CapeLong_out nc, ::SqpSolver::CapeLong_out nlc,
                      ::SqpSolver::CapeLong_out nlz, ::SqpSolver::CapeLong_out nnz,
                      ::SqpSolver::CapeLong_out nlzof, ::SqpSolver::CapeLong_out nnzof) override;
    void GetMINLPStructure(const char* structuretype, ::SqpSolver::CapeArrayLong_out rowindex,
                           ::SqpSolver::CapeArrayLong_out columnindex,
                           ::SqpSolver::CapeArrayLong_out objindex) override;
    void GetMINLPVariableNames(const ::SqpSolver::CapeArrayLong& vids,
                               ::SqpSolver::CapeArrayString_out vnames) override;
    void GetMINLPVariableDoubleAttribute(const ::SqpSolver::CapeArrayLong& vids, const char* attrib,
                                         ::SqpSolver::CapeArrayDouble_out values) override;
    void GetMINLPVariableBounds(const ::SqpSolver::CapeArrayLong& vids,
                                ::SqpSolver::CapeArrayDouble_out lb,
                                ::SqpSolver::CapeArrayDouble_out ub) override;
    void GetMINLPVariableValues(const ::SqpSolver::CapeArrayLong& vids,
                                ::SqpSolver::CapeArrayDouble_out values) override;
    void SetMINLPVariableValues(const ::SqpSolver::CapeArrayLong& vids,
                                const ::SqpSolver::CapeArrayDouble& values) override;
    void GetMINLPConstraintNames(const ::SqpSolver::CapeArrayLong& cids,
                                 ::SqpSolver::CapeArrayString_out cnames) override;
    void GetMINLPConstraintBounds(const ::SqpSolver::CapeArrayLong& cids,
                                  ::SqpSolver::CapeArrayDouble_out lb,
                                  ::SqpSolver::CapeArrayDouble_out ub) override;
    void GetMINLPConstraintDoubleAttribute(const ::SqpSolver::CapeArrayLong& cids, const char* attrib,
                                           ::SqpSolver::CapeArrayDouble_out values) override;
    void GetMINLPNonlinearConstraintValues(const ::SqpSolver::CapeArrayLong& cids,
                                           ::SqpSolver::CapeArrayDouble_out values) override;
    void GetMINLPConstraintDerivativeValues(const char* structtype,
                                            const ::SqpSolver::CapeArrayLong& cids,
                                            ::SqpSolver::CapeArrayDouble_out vals) override;
    void GetMINLPNonlinearObjectiveFunctionValue(::SqpSolver::CapeDouble_out value) override;
    void GetMINLPObjectiveFunctionDerivativeValues(const char* stype,
                                                   ::SqpSolver::CapeArrayDouble_out v) override;
    void GetMINLPObjectiveFunctionDoubleAttribute(const char* attrib,
                                                  ::SqpSolver::CapeDouble_out value) override;
    void SetMINLPLagrangeMultipliers(const char* lmtype, const ::SqpSolver::CapeArrayLong& ids,
                                     const ::SqpSolver::CapeArrayDouble& values) override;
    void GetMINLPLagrangeMultipliers(const char* lmtype, const ::SqpSolver::CapeArrayLong& ids,
                                     ::SqpSolver::CapeArrayDouble_out values) override;

  private:
    ICapeMINLPModel* model_ = nullptr;
    std::unique_ptr<ICapeMINLPModel> owned_;
};
