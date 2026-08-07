// ***************************************************************
//  MINLPServant   version:  2.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "MINLPServant.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "XOptMINLPAdapter.h"
#include "backend/corba/CapeCorbaMarshal.h"

using namespace cape_corba;

namespace {

namespace ct = ::CAPEOPEN100::Common::Types;

// 入网的 vids/cids：1-based -> 0-based，并就地校验范围。
// 规范说 vids 必须落在 1..nv；越界不能静默放过——v1 的 pick() 对越界 id
// 返回 T{}，于是「求解器发了个非法 id」会变成「悄悄返回 0」。
// 空序列按规范表示「全部」，原样传空下去。
//
// 越界抛 IDL 里声明的 ECapeInvalidArgument，而不是 CORBA::BAD_PARAM 系统异常：
// 每个受影响的操作在 CAPEOPEN100_Minlp.idl 里都声明了
// raises(...::ECapeInvalidArgument, ...)，第三方 CO 客户端 catch 的就是它；
// 收到一个未声明的系统异常正好破坏本项目要的互操作性。
//
// interfaceName / operation 按规范（Error Common Interface §3.3）是必填字段。
[[noreturn]] void throwInvalidId(int wire_id, int count, const char* operation,
                                 const char* id_kind) {
    std::string desc = std::string(id_kind) + " " + std::to_string(wire_id) +
                       " is out of range; CAPE-OPEN numbers them 1.." +
                       std::to_string(count);
    throw ::CAPEOPEN100::Common::Error::ECapeInvalidArgument(
        /*name*/ "ECapeInvalidArgument",
        /*code*/ 0,
        /*description*/ desc.c_str(),
        /*scope*/ "CAPEOPEN100::Business::Numeric::Minlp",
        /*interfaceName*/ "ICapeMINLP",
        /*operation*/ operation,
        /*moreInfo*/ "");
}

std::vector<int> wireIdsToInternal(const ct::CapeArrayLong& wire, int count,
                                   const char* operation, const char* id_kind) {
    std::vector<int> ids;
    indicesFromWire(wire, ids);
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] < 0 || ids[i] >= count) {
            throwInvalidId(static_cast<int>(wire[static_cast<CORBA::ULong>(i)]), count, operation,
                           id_kind);
        }
    }
    return ids;
}

// 模型侧失败同样抛 IDL 里声明过的 ECapeUnknown，而不是 CORBA::INTERNAL 系统异常。
// 理由与 ECapeInvalidArgument 一致：每个操作都声明了 raises(...ECapeUnknown)，
// 第三方 CO 客户端 catch 的是它。
[[noreturn]] void throwUnknown(const char* operation, const std::string& what) {
    throw ::CAPEOPEN100::Common::Error::ECapeUnknown(
        /*name*/ "ECapeUnknown",
        /*code*/ 0,
        /*description*/ what.c_str(),
        /*scope*/ "CAPEOPEN100::Business::Numeric::Minlp",
        /*interfaceName*/ "ICapeMINLP",
        /*operation*/ operation,
        /*moreInfo*/ "");
}

CapeMINLPSize sizeOf(ICapeMINLPModel* m, const char* operation) {
    CapeMINLPSize s;
    if (!m) throwUnknown(operation, "the servant has no model attached");
    if (m->getSize(s) < 0) throwUnknown(operation, "the model failed to report its size");
    return s;
}

int variableCount(ICapeMINLPModel* m, const char* operation) {
    return sizeOf(m, operation).num_variables;
}

int constraintCount(ICapeMINLPModel* m, const char* operation) {
    return sizeOf(m, operation).num_constraints;
}

}  // namespace

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

// ---------------------------------------------------------------- 规模与结构

void MINLPServant::GetMINLPSize(ct::CapeLong_out nv, ct::CapeLong_out niv, ct::CapeLong_out nlv,
                                ct::CapeLong_out nliv, ct::CapeLong_out nc, ct::CapeLong_out nlc,
                                ct::CapeLong_out nlz, ct::CapeLong_out nnz, ct::CapeLong_out nlzof,
                                ct::CapeLong_out nnzof) {
    CapeMINLPSize s;
    if (!model_) throwUnknown("GetMINLPSize", "the servant has no model attached");
    if (model_->getSize(s) < 0) throwUnknown("GetMINLPSize", "the model failed to report its size");
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

void MINLPServant::GetMINLPStructure(const char* structuretype, ct::CapeArrayLong_out rowindex,
                                     ct::CapeArrayLong_out columnindex,
                                     ct::CapeArrayLong_out objindex) {
    std::vector<int> r, c, o;
    if (!model_ || model_->getStructure(structuretype, r, c, o) < 0)
        throwUnknown("GetMINLPStructure", "the model rejected getStructure");
    // 内部 0-based -> 线上 1-based。
    rowindex = new ct::CapeArrayLong(indicesToWire(r));
    columnindex = new ct::CapeArrayLong(indicesToWire(c));
    objindex = new ct::CapeArrayLong(indicesToWire(o));
}

// -------------------------------------------------------------------- 变量

void MINLPServant::GetMINLPVariableNames(const ct::CapeArrayLong& vids,
                                         ct::CapeArrayString_out vnames) {
    const std::vector<int> ids = wireIdsToInternal(vids, variableCount(model_, "GetMINLPVariableNames"), "GetMINLPVariableNames", "vids");
    std::vector<std::string> names;
    if (model_->getVariableNames(ids, names) < 0)
        throwUnknown("GetMINLPVariableNames", "the model rejected getVariableNames");
    vnames = new ct::CapeArrayString(toStringSeq(names));
}

// 先前这里抛 NO_IMPLEMENT，理由是「返回全连续是编造的答案」。那个理由站不住：
// GetMINLPSize 已经上报 niv（整数变量数）。niv == 0 时「这些变量都是连续的」
// 是从我们自己刚说过的话里**推导**出来的，不是编造——反倒是抛 NO_IMPLEMENT
// 会让无条件查询这一项的求解器直接中止。
//
// niv > 0 时我们确实不知道是哪几个是整数（ICapeMINLPModel 没这个概念），
// 那才是真的答不上来，仍抛 NO_IMPLEMENT。
void MINLPServant::GetMINLPVariableTypes(const ct::CapeArrayLong& vids,
                                         ct::CapeArrayBoolean_out isinteger) {
    const CapeMINLPSize s = sizeOf(model_, "GetMINLPVariableTypes");
    if (s.num_integer_variables != 0) throw CORBA::NO_IMPLEMENT();
    const std::vector<int> ids =
        wireIdsToInternal(vids, s.num_variables, "GetMINLPVariableTypes", "vids");
    const size_t n = ids.empty() ? static_cast<size_t>(s.num_variables) : ids.size();
    isinteger = new ct::CapeArrayBoolean(toBooleanSeq(std::vector<bool>(n, false)));
}

void MINLPServant::GetMINLPVariableBooleanAttribute(const ct::CapeArrayLong& /*vids*/,
                                                    const char* /*attrib*/,
                                                    ct::CapeArrayBoolean_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPVariableIntegerAttribute(const ct::CapeArrayLong& /*vids*/,
                                                    const char* /*attrib*/,
                                                    ct::CapeArrayLong_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPVariableDoubleAttribute(const ct::CapeArrayLong& /*vids*/,
                                                   const char* /*attrib*/,
                                                   ct::CapeArrayDouble_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPVariableStringAttribute(const ct::CapeArrayLong& /*vids*/,
                                                   const char* /*attrib*/,
                                                   ct::CapeArrayString_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPVariableBounds(const ct::CapeArrayLong& vids,
                                          ct::CapeArrayDouble_out LB, ct::CapeArrayDouble_out UB) {
    const std::vector<int> ids = wireIdsToInternal(vids, variableCount(model_, "GetMINLPVariableBounds"), "GetMINLPVariableBounds", "vids");
    std::vector<double> lo, hi;
    if (model_->getVariableBounds(ids, lo, hi) < 0)
        throwUnknown("GetMINLPVariableBounds", "the model rejected getVariableBounds");
    LB = new ct::CapeArrayDouble(toDoubleSeq(lo));
    UB = new ct::CapeArrayDouble(toDoubleSeq(hi));
}

void MINLPServant::GetMINLPVariableValues(const ct::CapeArrayLong& vids,
                                          ct::CapeArrayDouble_out values) {
    const std::vector<int> ids = wireIdsToInternal(vids, variableCount(model_, "GetMINLPVariableValues"), "GetMINLPVariableValues", "vids");
    std::vector<double> v;
    if (model_->getVariableValues(ids, v) < 0)
        throwUnknown("GetMINLPVariableValues", "the model rejected getVariableValues");
    values = new ct::CapeArrayDouble(toDoubleSeq(v));
}

void MINLPServant::SetMINLPVariableValues(const ct::CapeArrayLong& vids,
                                          const ct::CapeArrayDouble& values) {
    const std::vector<int> ids = wireIdsToInternal(vids, variableCount(model_, "SetMINLPVariableValues"), "SetMINLPVariableValues", "vids");
    std::vector<double> v;
    fromDoubleSeq(values, v);
    if (model_->setVariableValues(ids, v) < 0)
        throwUnknown("SetMINLPVariableValues", "the model rejected setVariableValues");
}

// -------------------------------------------------------------------- 约束

void MINLPServant::GetMINLPConstraintNames(const ct::CapeArrayLong& cids,
                                           ct::CapeArrayString_out cnames) {
    const std::vector<int> ids = wireIdsToInternal(cids, constraintCount(model_, "GetMINLPConstraintNames"), "GetMINLPConstraintNames", "cids");
    std::vector<std::string> names;
    if (model_->getConstraintNames(ids, names) < 0)
        throwUnknown("GetMINLPConstraintNames", "the model rejected getConstraintNames");
    cnames = new ct::CapeArrayString(toStringSeq(names));
}

void MINLPServant::GetMINLPConstraintBounds(const ct::CapeArrayLong& cids,
                                            ct::CapeArrayDouble_out LB,
                                            ct::CapeArrayDouble_out UB) {
    const std::vector<int> ids = wireIdsToInternal(cids, constraintCount(model_, "GetMINLPConstraintBounds"), "GetMINLPConstraintBounds", "cids");
    std::vector<double> lo, hi;
    if (model_->getConstraintBounds(ids, lo, hi) < 0)
        throwUnknown("GetMINLPConstraintBounds", "the model rejected getConstraintBounds");
    LB = new ct::CapeArrayDouble(toDoubleSeq(lo));
    UB = new ct::CapeArrayDouble(toDoubleSeq(hi));
}

// 同 GetMINLPVariableTypes：nlc（线性约束数）已上报，nlc == 0 时
// 「这些约束都不是线性的」是推导出来的，不是编造。
void MINLPServant::GetMINLPConstraintLinearity(const ct::CapeArrayLong& cids,
                                               ct::CapeArrayBoolean_out islinear) {
    const CapeMINLPSize s = sizeOf(model_, "GetMINLPConstraintLinearity");
    if (s.num_linear_constraints != 0) throw CORBA::NO_IMPLEMENT();
    const std::vector<int> ids =
        wireIdsToInternal(cids, s.num_constraints, "GetMINLPConstraintLinearity", "cids");
    const size_t n = ids.empty() ? static_cast<size_t>(s.num_constraints) : ids.size();
    islinear = new ct::CapeArrayBoolean(toBooleanSeq(std::vector<bool>(n, false)));
}

void MINLPServant::GetMINLPConstraintBooleanAttribute(const ct::CapeArrayLong& /*cids*/,
                                                      const char* /*attrib*/,
                                                      ct::CapeArrayBoolean_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPConstraintIntegerAttribute(const ct::CapeArrayLong& /*cids*/,
                                                      const char* /*attrib*/,
                                                      ct::CapeArrayLong_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPConstraintDoubleAttribute(const ct::CapeArrayLong& /*cids*/,
                                                     const char* /*attrib*/,
                                                     ct::CapeArrayDouble_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPConstraintStringAttribute(const ct::CapeArrayLong& /*cids*/,
                                                     const char* /*attrib*/,
                                                     ct::CapeArrayString_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPNonlinearConstraintValues(const ct::CapeArrayLong& cids,
                                                     ct::CapeArrayDouble_out values) {
    const std::vector<int> ids = wireIdsToInternal(cids, constraintCount(model_, "GetMINLPNonlinearConstraintValues"), "GetMINLPNonlinearConstraintValues", "cids");
    std::vector<double> v;
    if (model_->getNonlinearConstraintValues(ids, v) < 0)
        throwUnknown("GetMINLPNonlinearConstraintValues", "the model rejected getNonlinearConstraintValues");
    values = new ct::CapeArrayDouble(toDoubleSeq(v));
}

void MINLPServant::GetMINLPConstraintDerivativeValues(const char* structtype,
                                                      const ct::CapeArrayLong& cids,
                                                      ct::CapeArrayDouble_out vals) {
    const std::vector<int> ids = wireIdsToInternal(cids, constraintCount(model_, "GetMINLPConstraintDerivativeValues"), "GetMINLPConstraintDerivativeValues", "cids");
    std::vector<double> v;
    if (model_->getConstraintDerivativeValues(structtype, ids, v) < 0)
        throwUnknown("GetMINLPConstraintDerivativeValues", "the model rejected getConstraintDerivativeValues");
    vals = new ct::CapeArrayDouble(toDoubleSeq(v));
}

// ---------------------------------------------------------------- 目标函数

void MINLPServant::GetMINLPObjectiveFunctionType(
    ::CAPEOPEN100::Business::Numeric::Minlp::CapeMINLPObjFunType_out otype) {
    // xOptProblem 的约定是最小化。这一条不是编造：它是 xOpt 侧确定的事实。
    otype = ::CAPEOPEN100::Business::Numeric::Minlp::MIN;
}

void MINLPServant::GetMINLPNonlinearObjectiveFunctionValue(ct::CapeDouble_out value) {
    double v = 0;
    if (!model_ || model_->getObjectiveValue(v) < 0)
        throwUnknown("GetMINLPNonlinearObjectiveFunctionValue", "the model rejected getObjectiveValue");
    value = v;
}

void MINLPServant::GetMINLPObjectiveFunctionDerivativeValues(const char* stype,
                                                             ct::CapeArrayDouble_out v) {
    std::vector<double> g;
    if (!model_ || model_->getObjectiveDerivativeValues(stype, g) < 0)
        throwUnknown("GetMINLPObjectiveFunctionDerivativeValues", "the model rejected getObjectiveDerivativeValues");
    v = new ct::CapeArrayDouble(toDoubleSeq(g));
}

void MINLPServant::GetMINLPObjectiveFunctionBooleanAttribute(const char* /*attrib*/,
                                                             ct::CapeBoolean_out /*value*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPObjectiveFunctionIntegerAttribute(const char* /*attrib*/,
                                                             ct::CapeLong_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPObjectiveFunctionDoubleAttribute(const char* /*attrib*/,
                                                            ct::CapeDouble_out /*value*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPObjectiveFunctionStringAttribute(const char* /*attrib*/,
                                                            ::CORBA::String_out /*value*/) {
    throw CORBA::NO_IMPLEMENT();
}

// ------------------------------------------------------------ Lagrange / Hessian

void MINLPServant::SetMINLPLagrangeMultipliers(const char* /*lmtype*/,
                                               const ct::CapeArrayLong& /*ids*/,
                                               const ct::CapeArrayDouble& /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPLagrangeMultipliers(const char* /*lmtype*/,
                                               const ct::CapeArrayLong& /*ids*/,
                                               ct::CapeArrayDouble_out /*values*/) {
    throw CORBA::NO_IMPLEMENT();
}

void MINLPServant::GetMINLPHessianStructure(ct::CapeLong /*size*/,
                                            const ct::CapeArrayLong& /*rowindex*/,
                                            ct::CapeArrayLong_out /*columnindex*/) {
    // 规范为此专门定义了 ECapeHessianInfoNotAvailable，但那是 IDL 里声明过的
    // 用户异常，抛它需要 raises 子句支持——这里三个方法都声明了，故用它而非
    // NO_IMPLEMENT，语义更贴合「Hessian 信息拿不到」。
    throw ::CAPEOPEN100::Business::Numeric::Minlp::ECapeHessianInfoNotAvailable(
        "xOptProblem exposes no Hessian");
}

void MINLPServant::SetMINLPHessianValues(const ct::CapeArrayDouble& /*values*/) {
    throw ::CAPEOPEN100::Business::Numeric::Minlp::ECapeHessianInfoNotAvailable(
        "xOptProblem exposes no Hessian");
}

void MINLPServant::GetMINLPHessianValues(ct::CapeArrayDouble_out /*values*/) {
    throw ::CAPEOPEN100::Business::Numeric::Minlp::ECapeHessianInfoNotAvailable(
        "xOptProblem exposes no Hessian");
}

// -------------------------------------------------------- ICapeIdentification
// 返回值按 CORBA C++ 映射的所有权约定：调用方接管，故 string_dup。

char* MINLPServant::GetComponentName() { return CORBA::string_dup(comp_name_.c_str()); }

char* MINLPServant::GetComponentDescription() { return CORBA::string_dup(comp_desc_.c_str()); }

void MINLPServant::SetComponentName(const char* name) { comp_name_ = name ? name : ""; }

void MINLPServant::SetComponentDescription(const char* desc) { comp_desc_ = desc ? desc : ""; }
