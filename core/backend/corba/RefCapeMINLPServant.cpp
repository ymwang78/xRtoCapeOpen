// ***************************************************************
//  RefCapeMINLPServant   version:  2.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "RefCapeMINLPServant.h"

#include <string>
#include <vector>

namespace ct = ::CAPEOPEN100::Common::Types;
namespace cm = ::CAPEOPEN100::Business::Numeric::Minlp;

namespace {

// 本文件**不用** CapeCorbaMarshal 的 indicesToWire/indicesFromWire——见头文件
// 说明：本 servant 是消费端换基的独立校验，共用 helper 就失去了校验意义。
// 下面这四个是本地的、独立写的一份。

std::vector<int> idsFromWire(const ct::CapeArrayLong& wire) {
    std::vector<int> out(wire.length());
    for (CORBA::ULong i = 0; i < wire.length(); ++i) {
        // 规范：vids/cids 从 1 开始编号。
        out[i] = static_cast<int>(wire[i]) - 1;
    }
    return out;
}

ct::CapeArrayLong idsToWire(const std::vector<int>& internal_zero_based) {
    ct::CapeArrayLong s;
    s.length(static_cast<CORBA::ULong>(internal_zero_based.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) {
        s[i] = static_cast<CORBA::Long>(internal_zero_based[i]) + 1;
    }
    return s;
}

ct::CapeArrayDouble doublesToWire(const std::vector<double>& v) {
    ct::CapeArrayDouble s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = v[i];
    return s;
}

ct::CapeArrayString stringsToWire(const std::vector<std::string>& v) {
    ct::CapeArrayString s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = CORBA::string_dup(v[i].c_str());
    return s;
}

std::vector<double> doublesFromWire(const ct::CapeArrayDouble& s) {
    std::vector<double> out(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) out[i] = s[i];
    return out;
}

}  // namespace

RefCapeMINLPServant::RefCapeMINLPServant() : mock_("") { mock_.connect(); }

void RefCapeMINLPServant::GetMINLPSize(ct::CapeLong_out nv, ct::CapeLong_out niv,
                                       ct::CapeLong_out nlv, ct::CapeLong_out nliv,
                                       ct::CapeLong_out nc, ct::CapeLong_out nlc,
                                       ct::CapeLong_out nlz, ct::CapeLong_out nnz,
                                       ct::CapeLong_out nlzof, ct::CapeLong_out nnzof) {
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
                                            ct::CapeArrayLong_out rowindex,
                                            ct::CapeArrayLong_out columnindex,
                                            ct::CapeArrayLong_out objindex) {
    std::vector<int> r, c, o;
    mock_.getStructure(structuretype, r, c, o);
    rowindex = new ct::CapeArrayLong(idsToWire(r));
    columnindex = new ct::CapeArrayLong(idsToWire(c));
    objindex = new ct::CapeArrayLong(idsToWire(o));
}

void RefCapeMINLPServant::GetMINLPVariableNames(const ct::CapeArrayLong& vids,
                                                ct::CapeArrayString_out vnames) {
    std::vector<std::string> names;
    mock_.getVariableNames(idsFromWire(vids), names);
    vnames = new ct::CapeArrayString(stringsToWire(names));
}

void RefCapeMINLPServant::GetMINLPVariableBounds(const ct::CapeArrayLong& vids,
                                                 ct::CapeArrayDouble_out LB,
                                                 ct::CapeArrayDouble_out UB) {
    std::vector<double> lo, hi;
    mock_.getVariableBounds(idsFromWire(vids), lo, hi);
    LB = new ct::CapeArrayDouble(doublesToWire(lo));
    UB = new ct::CapeArrayDouble(doublesToWire(hi));
}

void RefCapeMINLPServant::GetMINLPVariableValues(const ct::CapeArrayLong& vids,
                                                 ct::CapeArrayDouble_out values) {
    std::vector<double> v;
    mock_.getVariableValues(idsFromWire(vids), v);
    values = new ct::CapeArrayDouble(doublesToWire(v));
}

void RefCapeMINLPServant::SetMINLPVariableValues(const ct::CapeArrayLong& vids,
                                                 const ct::CapeArrayDouble& values) {
    mock_.setVariableValues(idsFromWire(vids), doublesFromWire(values));
}

void RefCapeMINLPServant::GetMINLPConstraintNames(const ct::CapeArrayLong& cids,
                                                  ct::CapeArrayString_out cnames) {
    std::vector<std::string> names;
    mock_.getConstraintNames(idsFromWire(cids), names);
    cnames = new ct::CapeArrayString(stringsToWire(names));
}

void RefCapeMINLPServant::GetMINLPConstraintBounds(const ct::CapeArrayLong& cids,
                                                   ct::CapeArrayDouble_out LB,
                                                   ct::CapeArrayDouble_out UB) {
    std::vector<double> lo, hi;
    mock_.getConstraintBounds(idsFromWire(cids), lo, hi);
    LB = new ct::CapeArrayDouble(doublesToWire(lo));
    UB = new ct::CapeArrayDouble(doublesToWire(hi));
}

void RefCapeMINLPServant::GetMINLPNonlinearConstraintValues(const ct::CapeArrayLong& cids,
                                                            ct::CapeArrayDouble_out values) {
    std::vector<double> v;
    mock_.getNonlinearConstraintValues(idsFromWire(cids), v);
    values = new ct::CapeArrayDouble(doublesToWire(v));
}

void RefCapeMINLPServant::GetMINLPConstraintDerivativeValues(const char* structtype,
                                                             const ct::CapeArrayLong& cids,
                                                             ct::CapeArrayDouble_out vals) {
    std::vector<double> v;
    mock_.getConstraintDerivativeValues(structtype, idsFromWire(cids), v);
    vals = new ct::CapeArrayDouble(doublesToWire(v));
}

void RefCapeMINLPServant::GetMINLPObjectiveFunctionType(cm::CapeMINLPObjFunType_out otype) {
    otype = cm::MIN;
}

void RefCapeMINLPServant::GetMINLPNonlinearObjectiveFunctionValue(ct::CapeDouble_out value) {
    double v = 0;
    mock_.getObjectiveValue(v);
    value = v;
}

void RefCapeMINLPServant::GetMINLPObjectiveFunctionDerivativeValues(const char* stype,
                                                                    ct::CapeArrayDouble_out v) {
    std::vector<double> g;
    mock_.getObjectiveDerivativeValues(stype, g);
    v = new ct::CapeArrayDouble(doublesToWire(g));
}

// —— mock 不涉及的部分：抛 NO_IMPLEMENT ——

void RefCapeMINLPServant::GetMINLPVariableTypes(const ct::CapeArrayLong&,
                                                ct::CapeArrayBoolean_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPVariableBooleanAttribute(const ct::CapeArrayLong&, const char*,
                                                           ct::CapeArrayBoolean_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPVariableIntegerAttribute(const ct::CapeArrayLong&, const char*,
                                                           ct::CapeArrayLong_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPVariableDoubleAttribute(const ct::CapeArrayLong&, const char*,
                                                          ct::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPVariableStringAttribute(const ct::CapeArrayLong&, const char*,
                                                          ct::CapeArrayString_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPConstraintLinearity(const ct::CapeArrayLong&,
                                                      ct::CapeArrayBoolean_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPConstraintBooleanAttribute(const ct::CapeArrayLong&, const char*,
                                                             ct::CapeArrayBoolean_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPConstraintIntegerAttribute(const ct::CapeArrayLong&, const char*,
                                                             ct::CapeArrayLong_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPConstraintDoubleAttribute(const ct::CapeArrayLong&, const char*,
                                                            ct::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPConstraintStringAttribute(const ct::CapeArrayLong&, const char*,
                                                            ct::CapeArrayString_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPObjectiveFunctionBooleanAttribute(const char*,
                                                                    ct::CapeBoolean_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPObjectiveFunctionIntegerAttribute(const char*,
                                                                    ct::CapeLong_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPObjectiveFunctionDoubleAttribute(const char*,
                                                                   ct::CapeDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPObjectiveFunctionStringAttribute(const char*,
                                                                   ::CORBA::String_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::SetMINLPLagrangeMultipliers(const char*, const ct::CapeArrayLong&,
                                                      const ct::CapeArrayDouble&) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPLagrangeMultipliers(const char*, const ct::CapeArrayLong&,
                                                      ct::CapeArrayDouble_out) {
    throw CORBA::NO_IMPLEMENT();
}
void RefCapeMINLPServant::GetMINLPHessianStructure(ct::CapeLong, const ct::CapeArrayLong&,
                                                   ct::CapeArrayLong_out) {
    throw cm::ECapeHessianInfoNotAvailable("mock exposes no Hessian");
}
void RefCapeMINLPServant::SetMINLPHessianValues(const ct::CapeArrayDouble&) {
    throw cm::ECapeHessianInfoNotAvailable("mock exposes no Hessian");
}
void RefCapeMINLPServant::GetMINLPHessianValues(ct::CapeArrayDouble_out) {
    throw cm::ECapeHessianInfoNotAvailable("mock exposes no Hessian");
}

// —— ICapeIdentification ——
// 返回值按 CORBA C++ 映射的所有权约定：调用方接管，故 string_dup。

char* RefCapeMINLPServant::GetComponentName() { return CORBA::string_dup(name_.c_str()); }

char* RefCapeMINLPServant::GetComponentDescription() {
    return CORBA::string_dup(description_.c_str());
}

void RefCapeMINLPServant::SetComponentName(const char* name) { name_ = name ? name : ""; }

void RefCapeMINLPServant::SetComponentDescription(const char* desc) {
    description_ = desc ? desc : "";
}
