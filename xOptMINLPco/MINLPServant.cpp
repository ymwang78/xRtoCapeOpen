// ***************************************************************
//  MINLPServant   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "MINLPServant.h"

#include <cstdlib>

#include "XOptMINLPAdapter.h"
#include "backend/corba/CapeCorbaMarshal.h"

using namespace cape_corba;

MINLPServant::MINLPServant(ICapeMINLPModel* model) : model_(model) {}

MINLPServant::MINLPServant() {
    const char* dll = std::getenv("XRTO_XOPT_PROBLEM_DLL");
    if (dll == nullptr || dll[0] == '\0') return;
    owned_ = std::make_unique<XOptMINLPAdapter>(std::string(dll));
    if (owned_->connect() < 0) {
        owned_.reset();
        return;
    }
    model_ = owned_.get();
}

void MINLPServant::GetMINLPSize(::SqpSolver::CapeLong_out nv, ::SqpSolver::CapeLong_out niv,
                                ::SqpSolver::CapeLong_out nlv, ::SqpSolver::CapeLong_out nliv,
                                ::SqpSolver::CapeLong_out nc, ::SqpSolver::CapeLong_out nlc,
                                ::SqpSolver::CapeLong_out nlz, ::SqpSolver::CapeLong_out nnz,
                                ::SqpSolver::CapeLong_out nlzof, ::SqpSolver::CapeLong_out nnzof) {
    CapeMINLPSize s;
    if (!model_ || model_->getSize(s) < 0) throw CORBA::INTERNAL();
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

void MINLPServant::GetMINLPStructure(const char* structuretype,
                                     ::SqpSolver::CapeArrayLong_out rowindex,
                                     ::SqpSolver::CapeArrayLong_out columnindex,
                                     ::SqpSolver::CapeArrayLong_out objindex) {
    std::vector<int> r, c, o;
    if (!model_ || model_->getStructure(structuretype, r, c, o) < 0) throw CORBA::INTERNAL();
    rowindex = new ::SqpSolver::CapeArrayLong(toLongSeq(r));
    columnindex = new ::SqpSolver::CapeArrayLong(toLongSeq(c));
    objindex = new ::SqpSolver::CapeArrayLong(toLongSeq(o));
}

void MINLPServant::GetMINLPVariableNames(const ::SqpSolver::CapeArrayLong& vids,
                                         ::SqpSolver::CapeArrayString_out vnames) {
    std::vector<int> ids;
    fromLongSeq(vids, ids);
    std::vector<std::string> names;
    if (!model_ || model_->getVariableNames(ids, names) < 0) throw CORBA::INTERNAL();
    vnames = new ::SqpSolver::CapeArrayString(toStringSeq(names));
}

void MINLPServant::GetMINLPVariableBounds(const ::SqpSolver::CapeArrayLong& vids,
                                          ::SqpSolver::CapeArrayDouble_out lb,
                                          ::SqpSolver::CapeArrayDouble_out ub) {
    std::vector<int> ids;
    fromLongSeq(vids, ids);
    std::vector<double> lo, hi;
    if (!model_ || model_->getVariableBounds(ids, lo, hi) < 0) throw CORBA::INTERNAL();
    lb = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(lo));
    ub = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(hi));
}

void MINLPServant::GetMINLPVariableValues(const ::SqpSolver::CapeArrayLong& vids,
                                          ::SqpSolver::CapeArrayDouble_out values) {
    std::vector<int> ids;
    fromLongSeq(vids, ids);
    std::vector<double> v;
    if (!model_ || model_->getVariableValues(ids, v) < 0) throw CORBA::INTERNAL();
    values = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(v));
}

void MINLPServant::SetMINLPVariableValues(const ::SqpSolver::CapeArrayLong& vids,
                                          const ::SqpSolver::CapeArrayDouble& values) {
    std::vector<int> ids;
    std::vector<double> v;
    fromLongSeq(vids, ids);
    fromDoubleSeq(values, v);
    if (!model_ || model_->setVariableValues(ids, v) < 0) throw CORBA::INTERNAL();
}

void MINLPServant::GetMINLPConstraintNames(const ::SqpSolver::CapeArrayLong& cids,
                                           ::SqpSolver::CapeArrayString_out cnames) {
    std::vector<int> ids;
    fromLongSeq(cids, ids);
    std::vector<std::string> names;
    if (!model_ || model_->getConstraintNames(ids, names) < 0) throw CORBA::INTERNAL();
    cnames = new ::SqpSolver::CapeArrayString(toStringSeq(names));
}

void MINLPServant::GetMINLPConstraintBounds(const ::SqpSolver::CapeArrayLong& cids,
                                            ::SqpSolver::CapeArrayDouble_out lb,
                                            ::SqpSolver::CapeArrayDouble_out ub) {
    std::vector<int> ids;
    fromLongSeq(cids, ids);
    std::vector<double> lo, hi;
    if (!model_ || model_->getConstraintBounds(ids, lo, hi) < 0) throw CORBA::INTERNAL();
    lb = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(lo));
    ub = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(hi));
}

void MINLPServant::GetMINLPNonlinearConstraintValues(const ::SqpSolver::CapeArrayLong& cids,
                                                     ::SqpSolver::CapeArrayDouble_out values) {
    std::vector<int> ids;
    fromLongSeq(cids, ids);
    std::vector<double> v;
    if (!model_ || model_->getNonlinearConstraintValues(ids, v) < 0) throw CORBA::INTERNAL();
    values = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(v));
}

void MINLPServant::GetMINLPConstraintDerivativeValues(const char* structtype,
                                                      const ::SqpSolver::CapeArrayLong& cids,
                                                      ::SqpSolver::CapeArrayDouble_out vals) {
    std::vector<int> ids;
    fromLongSeq(cids, ids);
    std::vector<double> v;
    if (!model_ || model_->getConstraintDerivativeValues(structtype, ids, v) < 0)
        throw CORBA::INTERNAL();
    vals = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(v));
}

void MINLPServant::GetMINLPNonlinearObjectiveFunctionValue(::SqpSolver::CapeDouble_out value) {
    double v = 0;
    if (!model_ || model_->getObjectiveValue(v) < 0) throw CORBA::INTERNAL();
    value = v;
}

void MINLPServant::GetMINLPObjectiveFunctionDerivativeValues(const char* stype,
                                                             ::SqpSolver::CapeArrayDouble_out v) {
    std::vector<double> g;
    if (!model_ || model_->getObjectiveDerivativeValues(stype, g) < 0) throw CORBA::INTERNAL();
    v = new ::SqpSolver::CapeArrayDouble(toDoubleSeq(g));
}

// —— 未实现 ——
void MINLPServant::GetMINLPVariableDoubleAttribute(const ::SqpSolver::CapeArrayLong&, const char*,
                                                   ::SqpSolver::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void MINLPServant::GetMINLPConstraintDoubleAttribute(const ::SqpSolver::CapeArrayLong&, const char*,
                                                     ::SqpSolver::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void MINLPServant::GetMINLPObjectiveFunctionDoubleAttribute(const char*,
                                                            ::SqpSolver::CapeDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void MINLPServant::SetMINLPLagrangeMultipliers(const char*, const ::SqpSolver::CapeArrayLong&,
                                               const ::SqpSolver::CapeArrayDouble&) {
    throw CORBA::NO_IMPLEMENT();
}
void MINLPServant::GetMINLPLagrangeMultipliers(const char*, const ::SqpSolver::CapeArrayLong&,
                                               ::SqpSolver::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
