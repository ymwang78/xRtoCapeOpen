#pragma once
// ***************************************************************
//  RefCapeMINLPServant   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  测试用参考 CORBA 服务端：POA 实现 SqpSolver::ICapeMINLP，内部包
//  CapeMINLPModelMock。作为 CapeMINLPModelCorba 的 collocated 测试对端
//  （design §3.3/§5.3）。仅测试编译。
// ***************************************************************
#include "../../CapeMINLPModelMock.h"
#include "SqpSolverS.h"

class RefCapeMINLPServant : public POA_SqpSolver::ICapeMINLP {
  public:
    RefCapeMINLPServant();

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
    CapeMINLPModelMock mock_;
};
