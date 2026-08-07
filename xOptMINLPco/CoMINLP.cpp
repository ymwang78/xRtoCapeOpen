// ***************************************************************
//  CoMINLP   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#ifdef _WIN32

#include "CoMINLP.h"

#include <cstdlib>

#include "XOptMINLPAdapter.h"
#include "backend/com/CapeVariantMarshal.h"

using namespace cape_com;

extern "C" const GUID IID_ICapeMINLP;            // 定义在 capeopen_core/CapeMINLPModelCom.cpp
extern "C" const GUID IID_ICapeIdentification;   // 同上

CoMINLP::CoMINLP(ICapeMINLPModel* model) : model_(model) {}

CoMINLP::CoMINLP() {
    const char* dll = std::getenv("XRTO_XOPT_PROBLEM_DLL");
    if (dll == nullptr || dll[0] == '\0') {
        init_error_ = "XRTO_XOPT_PROBLEM_DLL not set";
        return;
    }
    owned_ = std::make_unique<XOptMINLPAdapter>(std::string(dll));
    if (owned_->connect() < 0) {
        init_error_ = "adapter connect failed: " + owned_->lastError();
        owned_.reset();
        return;
    }
    model_ = owned_.get();
}

// —— IUnknown ——
HRESULT STDMETHODCALLTYPE CoMINLP::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch) ||
        IsEqualIID(riid, IID_ICapeMINLP)) {
        *ppv = static_cast<ICapeMINLP*>(this);  // IUnknown/IDispatch 经此基类消歧
        AddRef();
        return S_OK;
    }
    if (IsEqualIID(riid, IID_ICapeIdentification)) {
        *ppv = static_cast<ICapeIdentification*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE CoMINLP::AddRef() { return InterlockedIncrement(&ref_); }
ULONG STDMETHODCALLTYPE CoMINLP::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

// —— IDispatch（桩）——
HRESULT STDMETHODCALLTYPE CoMINLP::GetTypeInfoCount(UINT* pctinfo) {
    if (pctinfo) *pctinfo = 0;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetTypeInfo(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE CoMINLP::GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*,
                                          EXCEPINFO*, UINT*) {
    return E_NOTIMPL;
}

namespace {

// 入网的 vids/cids：1-based -> 0-based，并就地校验范围。
//
// 规范要求 vids/cids 落在 1..nv / 1..nc（见 CapeVariantMarshal.h 顶部）。
// 越界不能静默放过——`XOptMINLPAdapter::pick()` 对越界 id 返回 `T{}`，
// 于是「求解器发了个非法 id」会变成「悄悄返回 0」，而且毫无征兆。
//
// 「全部」接受两种写法：空 SAFEARRAY，以及**表示「参数没给」的那几种** VARIANT。
// 后者是刻意放宽的——消费端 CapeMINLPModelCom 的 softReadIndices 早就把非数组当空
// 处理，生产端若严格拒绝，同一份约定在两个方向上就不一致了；而且 VB/脚本宿主省略
// 可选参数时给的正是 VT_EMPTY，IDispatch 上则是带 DISP_E_PARAMNOTFOUND 的 VT_ERROR。
//
// 注意这里**只认这三种**，而不是笼统的「凡非数组即全部」。差别很要紧：后者会把误传的
// 标量（比如有人写 `vids = 1` 想取第一个变量）也当成「全部」，于是调用方要一个变量、
// 拿到了全部，而且不报错——正是本项目一直在清的那类静默错误。标量落到下面按数组解析，
// 失败即 E_INVALIDARG。
//
// **宽进严出**：入参形式在「确实表示未指定」的范围内放宽，取值范围照样严格校验。
//
// COM 没有用户异常，报错手段是 HRESULT：越界用 E_INVALIDARG，
// 对应 CORBA 侧抛的 ECapeInvalidArgument。
bool isOmittedArg(const VARIANT& v) {
    if (v.vt == VT_EMPTY || v.vt == VT_NULL) return true;
    // IDispatch 省略可选参数的标准表示
    return v.vt == VT_ERROR && v.scode == DISP_E_PARAMNOTFOUND;
}

bool readIdsChecked(const VARIANT& wire, int count, std::vector<int>& out) {
    if (isOmittedArg(wire)) {
        out.clear();  // 未指定 = 全部
        return true;
    }
    if (!cape_com::readIndicesFromWire(wire, out)) return false;
    for (int id : out) {
        if (id < 0 || id >= count) return false;
    }
    return true;
}

}  // namespace

// —— ICapeMINLP（委托 model_，vector<->VARIANT marshaling）——
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPSize(long* nv, long* niv, long* nlv, long* nliv, long* nc,
                                               long* nlc, long* nlz, long* nnz, long* nlzof,
                                               long* nnzof) {
    if (!model_) return E_FAIL;
    CapeMINLPSize s;
    if (model_->getSize(s) < 0) return E_FAIL;
    *nv = s.num_variables;
    *niv = s.num_integer_variables;
    *nlv = s.num_linear_variables;
    *nliv = s.num_linear_integer_variables;
    *nc = s.num_constraints;
    *nlc = s.num_linear_constraints;
    *nlz = s.num_linear_jacobian_nz;
    *nnz = s.num_nonlinear_jacobian_nz;
    *nlzof = s.num_linear_objgrad_nz;
    *nnzof = s.num_nonlinear_objgrad_nz;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPStructure(BSTR structuretype, VARIANT* rowindex,
                                                    VARIANT* columnindex, VARIANT* objindex) {
    if (!model_) return E_FAIL;
    std::vector<int> r, c, o;
    if (model_->getStructure(bstrToUtf8(structuretype), r, c, o) < 0) return E_FAIL;
    // 内部 0-based -> 线上 1-based。
    if (rowindex) *rowindex = makeIndicesToWire(r);
    if (columnindex) *columnindex = makeIndicesToWire(c);
    if (objindex) *objindex = makeIndicesToWire(o);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableNames(VARIANT vids, VARIANT* vnames) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    CapeMINLPSize size;
    if (model_->getSize(size) < 0) return E_FAIL;
    if (!readIdsChecked(vids, size.num_variables, ids)) return E_INVALIDARG;
    std::vector<std::string> names;
    if (model_->getVariableNames(ids, names) < 0) return E_FAIL;
    if (vnames) *vnames = makeStringArray(names);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableBounds(VARIANT vids, VARIANT* LB, VARIANT* UB) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    CapeMINLPSize size;
    if (model_->getSize(size) < 0) return E_FAIL;
    if (!readIdsChecked(vids, size.num_variables, ids)) return E_INVALIDARG;
    std::vector<double> lb, ub;
    if (model_->getVariableBounds(ids, lb, ub) < 0) return E_FAIL;
    if (LB) *LB = makeDoubleArray(lb);
    if (UB) *UB = makeDoubleArray(ub);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableValues(VARIANT vids, VARIANT* values) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    CapeMINLPSize size;
    if (model_->getSize(size) < 0) return E_FAIL;
    if (!readIdsChecked(vids, size.num_variables, ids)) return E_INVALIDARG;
    std::vector<double> v;
    if (model_->getVariableValues(ids, v) < 0) return E_FAIL;
    if (values) *values = makeDoubleArray(v);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::SetMINLPVariableValues(VARIANT vids, VARIANT values) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    std::vector<double> v;
    CapeMINLPSize size;
    if (model_->getSize(size) < 0) return E_FAIL;
    if (!readIdsChecked(vids, size.num_variables, ids)) return E_INVALIDARG;
    readDoubleArray(values, v);
    return model_->setVariableValues(ids, v) < 0 ? E_FAIL : S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintNames(VARIANT cids, VARIANT* cnames) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    CapeMINLPSize size;
    if (model_->getSize(size) < 0) return E_FAIL;
    if (!readIdsChecked(cids, size.num_constraints, ids)) return E_INVALIDARG;
    std::vector<std::string> names;
    if (model_->getConstraintNames(ids, names) < 0) return E_FAIL;
    if (cnames) *cnames = makeStringArray(names);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintBounds(VARIANT cids, VARIANT* LB, VARIANT* UB) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    CapeMINLPSize size;
    if (model_->getSize(size) < 0) return E_FAIL;
    if (!readIdsChecked(cids, size.num_constraints, ids)) return E_INVALIDARG;
    std::vector<double> lb, ub;
    if (model_->getConstraintBounds(ids, lb, ub) < 0) return E_FAIL;
    if (LB) *LB = makeDoubleArray(lb);
    if (UB) *UB = makeDoubleArray(ub);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPNonlinearConstraintValues(VARIANT cids, VARIANT* values) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    CapeMINLPSize size;
    if (model_->getSize(size) < 0) return E_FAIL;
    if (!readIdsChecked(cids, size.num_constraints, ids)) return E_INVALIDARG;
    std::vector<double> v;
    if (model_->getNonlinearConstraintValues(ids, v) < 0) return E_FAIL;
    if (values) *values = makeDoubleArray(v);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintDerivativeValues(BSTR structtype, VARIANT cids,
                                                                     VARIANT* vals) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    CapeMINLPSize size;
    if (model_->getSize(size) < 0) return E_FAIL;
    if (!readIdsChecked(cids, size.num_constraints, ids)) return E_INVALIDARG;
    std::vector<double> v;
    if (model_->getConstraintDerivativeValues(bstrToUtf8(structtype), ids, v) < 0) return E_FAIL;
    if (vals) *vals = makeDoubleArray(v);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPNonlinearObjectiveFunctionValue(double* value) {
    if (!model_) return E_FAIL;
    double v = 0;
    if (model_->getObjectiveValue(v) < 0) return E_FAIL;
    if (value) *value = v;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPObjectiveFunctionDerivativeValues(BSTR stype, VARIANT* v) {
    if (!model_) return E_FAIL;
    std::vector<double> g;
    if (model_->getObjectiveDerivativeValues(bstrToUtf8(stype), g) < 0) return E_FAIL;
    if (v) *v = makeDoubleArray(g);
    return S_OK;
}

// —— 未实现：返回 E_NOTIMPL ——
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableTypes(VARIANT, VARIANT*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableBooleanAttribute(VARIANT, VARIANT, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableIntegerAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableDoubleAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableStringAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintLinearity(VARIANT, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintBooleanAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintIntegerAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintDoubleAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintStringAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPObjectiveFunctionType(long*) { return E_NOTIMPL; }

// —— ICapeIdentification ——
HRESULT STDMETHODCALLTYPE CoMINLP::get_ComponentName(BSTR* name) {
    if (name == nullptr) return E_POINTER;
    *name = SysAllocString(comp_name_.c_str());
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CoMINLP::put_ComponentName(BSTR name) {
    comp_name_ = name ? name : L"";
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CoMINLP::get_ComponentDescription(BSTR* desc) {
    if (desc == nullptr) return E_POINTER;
    *desc = SysAllocString(comp_desc_.c_str());
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CoMINLP::put_ComponentDescription(BSTR desc) {
    comp_desc_ = desc ? desc : L"";
    return S_OK;
}

#endif  // _WIN32
