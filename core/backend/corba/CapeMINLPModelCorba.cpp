// ***************************************************************
//  CapeMINLPModelCorba   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "CapeMINLPModelCorba.h"

#include <tao/ORB.h>

#include "CapeCorbaMarshal.h"

using namespace cape_corba;

namespace {
// 进程级共享 ORB（仅 production 路径用）。注入测试不经此。
CORBA::ORB_var& sharedOrb() {
    static CORBA::ORB_var orb;
    return orb;
}
}  // namespace

CapeMINLPModelCorba::CapeMINLPModelCorba(const std::string& target) : target_(target) {}

CapeMINLPModelCorba::CapeMINLPModelCorba(SqpSolver::ICapeMINLP_ptr injected)
    : minlp_(SqpSolver::ICapeMINLP::_duplicate(injected)) {}

CapeMINLPModelCorba::~CapeMINLPModelCorba() { disconnect(); }

int CapeMINLPModelCorba::fail(const std::string& msg) const {
    last_error_ = msg;
    return -1;
}

int CapeMINLPModelCorba::connect() {
    if (!CORBA::is_nil(minlp_.in())) return 0;  // 注入或已连接
    try {
        if (CORBA::is_nil(sharedOrb().in())) {
            int argc = 0;
            sharedOrb() = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
            orb_owned_ = true;
        }
        CORBA::Object_var obj = sharedOrb()->string_to_object(target_.c_str());
        minlp_ = SqpSolver::ICapeMINLP::_narrow(obj.in());
        if (CORBA::is_nil(minlp_.in())) {
            return fail("connect: _narrow to ICapeMINLP failed for '" + target_ + "'");
        }
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("connect: CORBA exception ") + e._name());
    }
}

void CapeMINLPModelCorba::disconnect() {
    minlp_ = SqpSolver::ICapeMINLP::_nil();
    // 共享 ORB 不在此销毁（进程退出时回收）；注入路径不拥有 ORB。
}

int CapeMINLPModelCorba::getSize(CapeMINLPSize& s) {
    if (CORBA::is_nil(minlp_.in())) return fail("getSize: not connected");
    try {
        CORBA::Long nv, niv, nlv, nliv, nc, nlc, nlz, nnz, nlzof, nnzof;
        minlp_->GetMINLPSize(nv, niv, nlv, nliv, nc, nlc, nlz, nnz, nlzof, nnzof);
        s.num_variables = nv;
        s.num_integer_variables = niv;
        s.num_linear_variables = nlv;
        s.num_linear_integer_variables = nliv;
        s.num_constraints = nc;
        s.num_linear_constraints = nlc;
        s.num_linear_jacobian_nz = nlz;
        s.num_nonlinear_jacobian_nz = nnz;
        s.num_linear_objgrad_nz = nlzof;
        s.num_nonlinear_objgrad_nz = nnzof;
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPSize: ") + e._name());
    }
}

int CapeMINLPModelCorba::getStructure(const std::string& type, std::vector<int>& row_index,
                                      std::vector<int>& col_index, std::vector<int>& obj_index) {
    if (CORBA::is_nil(minlp_.in())) return fail("getStructure: not connected");
    try {
        SqpSolver::CapeArrayLong_var r, c, o;
        minlp_->GetMINLPStructure(type.c_str(), r.out(), c.out(), o.out());
        fromLongSeq(r.in(), row_index);
        fromLongSeq(c.in(), col_index);
        fromLongSeq(o.in(), obj_index);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPStructure: ") + e._name());
    }
}

int CapeMINLPModelCorba::getVariableNames(const std::vector<int>& vids,
                                          std::vector<std::string>& names_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getVariableNames: not connected");
    try {
        SqpSolver::CapeArrayString_var vnames;
        minlp_->GetMINLPVariableNames(toLongSeq(vids), vnames.out());
        fromStringSeq(vnames.in(), names_out);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPVariableNames: ") + e._name());
    }
}

int CapeMINLPModelCorba::getVariableBounds(const std::vector<int>& vids,
                                           std::vector<double>& lower_out,
                                           std::vector<double>& upper_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getVariableBounds: not connected");
    try {
        SqpSolver::CapeArrayDouble_var lb, ub;
        minlp_->GetMINLPVariableBounds(toLongSeq(vids), lb.out(), ub.out());
        fromDoubleSeq(lb.in(), lower_out);
        fromDoubleSeq(ub.in(), upper_out);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPVariableBounds: ") + e._name());
    }
}

int CapeMINLPModelCorba::getVariableValues(const std::vector<int>& vids,
                                           std::vector<double>& values_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getVariableValues: not connected");
    try {
        SqpSolver::CapeArrayDouble_var v;
        minlp_->GetMINLPVariableValues(toLongSeq(vids), v.out());
        fromDoubleSeq(v.in(), values_out);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPVariableValues: ") + e._name());
    }
}

int CapeMINLPModelCorba::setVariableValues(const std::vector<int>& vids,
                                           const std::vector<double>& values) {
    if (CORBA::is_nil(minlp_.in())) return fail("setVariableValues: not connected");
    try {
        minlp_->SetMINLPVariableValues(toLongSeq(vids), toDoubleSeq(values));
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("SetMINLPVariableValues: ") + e._name());
    }
}

int CapeMINLPModelCorba::getConstraintNames(const std::vector<int>& cids,
                                            std::vector<std::string>& names_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getConstraintNames: not connected");
    try {
        SqpSolver::CapeArrayString_var cnames;
        minlp_->GetMINLPConstraintNames(toLongSeq(cids), cnames.out());
        fromStringSeq(cnames.in(), names_out);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPConstraintNames: ") + e._name());
    }
}

int CapeMINLPModelCorba::getConstraintBounds(const std::vector<int>& cids,
                                             std::vector<double>& lower_out,
                                             std::vector<double>& upper_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getConstraintBounds: not connected");
    try {
        SqpSolver::CapeArrayDouble_var lb, ub;
        minlp_->GetMINLPConstraintBounds(toLongSeq(cids), lb.out(), ub.out());
        fromDoubleSeq(lb.in(), lower_out);
        fromDoubleSeq(ub.in(), upper_out);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPConstraintBounds: ") + e._name());
    }
}

int CapeMINLPModelCorba::getNonlinearConstraintValues(const std::vector<int>& cids,
                                                      std::vector<double>& values_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getNonlinearConstraintValues: not connected");
    try {
        SqpSolver::CapeArrayDouble_var v;
        minlp_->GetMINLPNonlinearConstraintValues(toLongSeq(cids), v.out());
        fromDoubleSeq(v.in(), values_out);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPNonlinearConstraintValues: ") + e._name());
    }
}

int CapeMINLPModelCorba::getConstraintDerivativeValues(const std::string& type,
                                                       const std::vector<int>& cids,
                                                       std::vector<double>& values_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getConstraintDerivativeValues: not connected");
    try {
        SqpSolver::CapeArrayDouble_var v;
        minlp_->GetMINLPConstraintDerivativeValues(type.c_str(), toLongSeq(cids), v.out());
        fromDoubleSeq(v.in(), values_out);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPConstraintDerivativeValues: ") + e._name());
    }
}

int CapeMINLPModelCorba::getObjectiveValue(double& value_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getObjectiveValue: not connected");
    try {
        CORBA::Double v = 0;
        minlp_->GetMINLPNonlinearObjectiveFunctionValue(v);
        value_out = v;
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPNonlinearObjectiveFunctionValue: ") + e._name());
    }
}

int CapeMINLPModelCorba::getObjectiveDerivativeValues(const std::string& type,
                                                      std::vector<double>& values_out) {
    if (CORBA::is_nil(minlp_.in())) return fail("getObjectiveDerivativeValues: not connected");
    try {
        SqpSolver::CapeArrayDouble_var v;
        minlp_->GetMINLPObjectiveFunctionDerivativeValues(type.c_str(), v.out());
        fromDoubleSeq(v.in(), values_out);
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("GetMINLPObjectiveFunctionDerivativeValues: ") + e._name());
    }
}

std::string CapeMINLPModelCorba::lastError() const { return last_error_; }
