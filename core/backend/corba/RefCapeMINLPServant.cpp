// ***************************************************************
//  RefCapeMINLPServant   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "RefCapeMINLPServant.h"

#include "CapeCorbaMarshal.h"

using namespace cape_corba;

RefCapeMINLPServant::RefCapeMINLPServant() : mock_("") { mock_.connect(); }

void RefCapeMINLPServant::GetMINLPSize(::SqpSolver::CapeLong_out nv, ::SqpSolver::CapeLong_out niv,
                                       ::SqpSolver::CapeLong_out nlv, ::SqpSolver::CapeLong_out nliv,
                                       ::SqpSolver::CapeLong_out nc, ::SqpSolver::CapeLong_out nlc,
                                       ::SqpSolver::CapeLong_out nlz, ::SqpSolver::CapeLong_out nnz,
                                       ::SqpSolver::CapeLong_out nlzof,
                                       ::SqpSolver::CapeLong_out nnzof) {
    CapeMINLPSize s;
    mock_.getSize(s);
    nv = s.num_variables;
    niv = s.num_integer_variables;
    nlv = s.num_linear_variables;
    nliv = s.num_linear_integer_variables;
    nc = s.num_constraints;
    nlc = s.num_linear_constraints;
    nlz = s.num_linear_jacobian_nz;
    nnz = s.num_nonlinear_jacobian_nz;
    nlzof = s.num_linear_objgrad_nz;
    nnzof = s.num_nonlinear_objgrad_nz;
}

void RefCapeMINLPServant::GetMINLPStructure(const char* structuretype,
                                            ::SqpSolver::CapeArrayLong_out rowindex,
                                            ::SqpSolver::CapeArrayLong_out columnindex,
                                            ::SqpSolver::CapeArrayLong_out objindex) {
    std::vector<int> r, c, o;
    mock_.getStructure(structuretype, r, c, o);
    rowindex = new ::SqpSolver::CapeArrayLong(toLongSeq(r));
    columnindex = new ::SqpSolver::CapeArrayLong(toLongSeq(c));
    objindex = new ::SqpSolver::CapeArrayLong(toLongSeq(o));
}

void RefCapeMINLPServant::GetMINLPVariableNames(const ::SqpSolver::CapeArrayLong& vids,
                                                ::SqpSolver::CapeArrayString_out vnames) {
    std::vector<int> ids;
    fromLongSeq(vids, ids);
    std::vector<std::string> names;
    mock_.getVariableNames(ids, names);
    vnames = new ::SqpSolver::CapeArrayString(toStringSeq(names));
}

void RefCapeMINLPServant::GetMINLPVariableBounds(const ::SqpSolver::CapeArrayLong& vids,
                                                 ::SqpSolver::CapeArrayDouble_out lb,
                                                 ::SqpSolver::CapeArrayDouble_out ub) {
    std::vector<int> ids;
    fromLongSeq(vids, ids);
    std::vector<double> lo, hi;
    mock_.getVariableBounds(ids, lo, hi);
    lb = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(lo));
    ub = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(hi));
}

void RefCapeMINLPServant::GetMINLPVariableValues(const ::SqpSolver::CapeArrayLong& vids,
                                                 ::SqpSolver::CapeArrayDouble_out values) {
    std::vector<int> ids;
    fromLongSeq(vids, ids);
    std::vector<double> v;
    mock_.getVariableValues(ids, v);
    values = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(v));
}

void RefCapeMINLPServant::SetMINLPVariableValues(const ::SqpSolver::CapeArrayLong& vids,
                                                 const ::SqpSolver::CapeArrayDouble& values) {
    std::vector<int> ids;
    std::vector<double> v;
    fromLongSeq(vids, ids);
    fromDoubleSeq(values, v);
    mock_.setVariableValues(ids, v);
}

void RefCapeMINLPServant::GetMINLPConstraintNames(const ::SqpSolver::CapeArrayLong& cids,
                                                  ::SqpSolver::CapeArrayString_out cnames) {
    std::vector<int> ids;
    fromLongSeq(cids, ids);
    std::vector<std::string> names;
    mock_.getConstraintNames(ids, names);
    cnames = new ::SqpSolver::CapeArrayString(toStringSeq(names));
}

void RefCapeMINLPServant::GetMINLPConstraintBounds(const ::SqpSolver::CapeArrayLong& cids,
                                                   ::SqpSolver::CapeArrayDouble_out lb,
                                                   ::SqpSolver::CapeArrayDouble_out ub) {
    std::vector<int> ids;
    fromLongSeq(cids, ids);
    std::vector<double> lo, hi;
    mock_.getConstraintBounds(ids, lo, hi);
    lb = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(lo));
    ub = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(hi));
}

void RefCapeMINLPServant::GetMINLPNonlinearConstraintValues(const ::SqpSolver::CapeArrayLong& cids,
                                                            ::SqpSolver::CapeArrayDouble_out values) {
    std::vector<int> ids;
    fromLongSeq(cids, ids);
    std::vector<double> v;
    mock_.getNonlinearConstraintValues(ids, v);
    values = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(v));
}

void RefCapeMINLPServant::GetMINLPConstraintDerivativeValues(const char* structtype,
                                                             const ::SqpSolver::CapeArrayLong& cids,
                                                             ::SqpSolver::CapeArrayDouble_out vals) {
    std::vector<int> ids;
    fromLongSeq(cids, ids);
    std::vector<double> v;
    mock_.getConstraintDerivativeValues(structtype, ids, v);
    vals = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(v));
}

void RefCapeMINLPServant::GetMINLPNonlinearObjectiveFunctionValue(
    ::SqpSolver::CapeDouble_out value) {
    double v = 0;
    mock_.getObjectiveValue(v);
    value = v;
}

void RefCapeMINLPServant::GetMINLPObjectiveFunctionDerivativeValues(
    const char* stype, ::SqpSolver::CapeArrayDouble_out v) {
    std::vector<double> g;
    mock_.getObjectiveDerivativeValues(stype, g);
    v = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(g));
}

// —— 未使用：抛 NO_IMPLEMENT ——
void RefCapeMINLPServant::GetMINLPVariableDoubleAttribute(const ::SqpSolver::CapeArrayLong&,
                                                          const char*,
                                                          ::SqpSolver::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPConstraintDoubleAttribute(const ::SqpSolver::CapeArrayLong&,
                                                            const char*,
                                                            ::SqpSolver::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPObjectiveFunctionDoubleAttribute(const char*,
                                                                   ::SqpSolver::CapeDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::SetMINLPLagrangeMultipliers(const char*, const ::SqpSolver::CapeArrayLong&,
                                                      const ::SqpSolver::CapeArrayDouble&) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPLagrangeMultipliers(const char*, const ::SqpSolver::CapeArrayLong&,
                                                      ::SqpSolver::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
