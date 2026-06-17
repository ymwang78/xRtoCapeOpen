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

extern "C" const GUID IID_ICapeMINLP;  // 定义在 capeopen_core/CapeMINLPModelCom.cpp

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
        *ppv = static_cast<ICapeMINLP*>(this);
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
    if (rowindex) *rowindex = makeLongArray(r);
    if (columnindex) *columnindex = makeLongArray(c);
    if (objindex) *objindex = makeLongArray(o);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableNames(VARIANT vids, VARIANT* vnames) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    readLongArray(vids, ids);
    std::vector<std::string> names;
    if (model_->getVariableNames(ids, names) < 0) return E_FAIL;
    if (vnames) *vnames = makeStringArray(names);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableBounds(VARIANT vids, VARIANT* LB, VARIANT* UB) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    readLongArray(vids, ids);
    std::vector<double> lb, ub;
    if (model_->getVariableBounds(ids, lb, ub) < 0) return E_FAIL;
    if (LB) *LB = makeDoubleArray(lb);
    if (UB) *UB = makeDoubleArray(ub);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPVariableValues(VARIANT vids, VARIANT* values) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    readLongArray(vids, ids);
    std::vector<double> v;
    if (model_->getVariableValues(ids, v) < 0) return E_FAIL;
    if (values) *values = makeDoubleArray(v);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::SetMINLPVariableValues(VARIANT vids, VARIANT values) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    std::vector<double> v;
    readLongArray(vids, ids);
    readDoubleArray(values, v);
    return model_->setVariableValues(ids, v) < 0 ? E_FAIL : S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintNames(VARIANT cids, VARIANT* cnames) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    readLongArray(cids, ids);
    std::vector<std::string> names;
    if (model_->getConstraintNames(ids, names) < 0) return E_FAIL;
    if (cnames) *cnames = makeStringArray(names);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintBounds(VARIANT cids, VARIANT* LB, VARIANT* UB) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    readLongArray(cids, ids);
    std::vector<double> lb, ub;
    if (model_->getConstraintBounds(ids, lb, ub) < 0) return E_FAIL;
    if (LB) *LB = makeDoubleArray(lb);
    if (UB) *UB = makeDoubleArray(ub);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPNonlinearConstraintValues(VARIANT cids, VARIANT* values) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    readLongArray(cids, ids);
    std::vector<double> v;
    if (model_->getNonlinearConstraintValues(ids, v) < 0) return E_FAIL;
    if (values) *values = makeDoubleArray(v);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CoMINLP::GetMINLPConstraintDerivativeValues(BSTR structtype, VARIANT cids,
                                                                     VARIANT* vals) {
    if (!model_) return E_FAIL;
    std::vector<int> ids;
    readLongArray(cids, ids);
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

#endif  // _WIN32
