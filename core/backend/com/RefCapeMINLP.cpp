// ***************************************************************
//  RefCapeMINLP   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#ifdef _WIN32

#include "RefCapeMINLP.h"

#include "CapeVariantMarshal.h"

using namespace cape_com;

extern "C" const GUID IID_ICapeMINLP;  // 定义在 CapeMINLPModelCom.cpp

RefCapeMINLP::RefCapeMINLP() : mock_("") { mock_.connect(); }

// —— IUnknown ——
HRESULT STDMETHODCALLTYPE RefCapeMINLP::QueryInterface(REFIID riid, void** ppv) {
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
ULONG STDMETHODCALLTYPE RefCapeMINLP::AddRef() { return InterlockedIncrement(&ref_); }
ULONG STDMETHODCALLTYPE RefCapeMINLP::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

// —— IDispatch（桩）——
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetTypeInfoCount(UINT* pctinfo) {
    if (pctinfo) *pctinfo = 0;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetTypeInfo(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*,
                                               EXCEPINFO*, UINT*) {
    return E_NOTIMPL;
}

// —— ICapeMINLP（实现）——
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPSize(long* nv, long* niv, long* nlv, long* nliv,
                                                    long* nc, long* nlc, long* nlz, long* nnz,
                                                    long* nlzof, long* nnzof) {
    CapeMINLPSize s;
    if (mock_.getSize(s) < 0) return E_FAIL;
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

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPStructure(BSTR structuretype, VARIANT* rowindex,
                                                         VARIANT* columnindex, VARIANT* objindex) {
    std::vector<int> r, c, o;
    if (mock_.getStructure(bstrToUtf8(structuretype), r, c, o) < 0) return E_FAIL;
    if (rowindex) *rowindex = makeLongArray(r);
    if (columnindex) *columnindex = makeLongArray(c);
    if (objindex) *objindex = makeLongArray(o);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPVariableNames(VARIANT vids, VARIANT* vnames) {
    std::vector<int> ids;
    readLongArray(vids, ids);
    std::vector<std::string> names;
    if (mock_.getVariableNames(ids, names) < 0) return E_FAIL;
    if (vnames) *vnames = makeStringArray(names);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPVariableBounds(VARIANT vids, VARIANT* LB, VARIANT* UB) {
    std::vector<int> ids;
    readLongArray(vids, ids);
    std::vector<double> lb, ub;
    if (mock_.getVariableBounds(ids, lb, ub) < 0) return E_FAIL;
    if (LB) *LB = makeDoubleArray(lb);
    if (UB) *UB = makeDoubleArray(ub);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPVariableValues(VARIANT vids, VARIANT* values) {
    std::vector<int> ids;
    readLongArray(vids, ids);
    std::vector<double> v;
    if (mock_.getVariableValues(ids, v) < 0) return E_FAIL;
    if (values) *values = makeDoubleArray(v);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::SetMINLPVariableValues(VARIANT vids, VARIANT values) {
    std::vector<int> ids;
    std::vector<double> v;
    readLongArray(vids, ids);
    readDoubleArray(values, v);
    return mock_.setVariableValues(ids, v) < 0 ? E_FAIL : S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPConstraintNames(VARIANT cids, VARIANT* cnames) {
    std::vector<int> ids;
    readLongArray(cids, ids);
    std::vector<std::string> names;
    if (mock_.getConstraintNames(ids, names) < 0) return E_FAIL;
    if (cnames) *cnames = makeStringArray(names);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPConstraintBounds(VARIANT cids, VARIANT* LB, VARIANT* UB) {
    std::vector<int> ids;
    readLongArray(cids, ids);
    std::vector<double> lb, ub;
    if (mock_.getConstraintBounds(ids, lb, ub) < 0) return E_FAIL;
    if (LB) *LB = makeDoubleArray(lb);
    if (UB) *UB = makeDoubleArray(ub);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPNonlinearConstraintValues(VARIANT cids,
                                                                         VARIANT* values) {
    std::vector<int> ids;
    readLongArray(cids, ids);
    std::vector<double> v;
    if (mock_.getNonlinearConstraintValues(ids, v) < 0) return E_FAIL;
    if (values) *values = makeDoubleArray(v);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPConstraintDerivativeValues(BSTR structtype,
                                                                          VARIANT cids,
                                                                          VARIANT* vals) {
    std::vector<int> ids;
    readLongArray(cids, ids);
    std::vector<double> v;
    if (mock_.getConstraintDerivativeValues(bstrToUtf8(structtype), ids, v) < 0) return E_FAIL;
    if (vals) *vals = makeDoubleArray(v);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPNonlinearObjectiveFunctionValue(double* value) {
    double v = 0;
    if (mock_.getObjectiveValue(v) < 0) return E_FAIL;
    if (value) *value = v;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPObjectiveFunctionDerivativeValues(BSTR stype,
                                                                                 VARIANT* v) {
    std::vector<double> g;
    if (mock_.getObjectiveDerivativeValues(bstrToUtf8(stype), g) < 0) return E_FAIL;
    if (v) *v = makeDoubleArray(g);
    return S_OK;
}

// —— 未使用的方法：返回 E_NOTIMPL ——
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPVariableTypes(VARIANT, VARIANT*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPVariableBooleanAttribute(VARIANT, VARIANT, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPVariableIntegerAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPVariableDoubleAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPVariableStringAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPConstraintLinearity(VARIANT, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPConstraintBooleanAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPConstraintIntegerAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPConstraintDoubleAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPConstraintStringAttribute(VARIANT, BSTR, VARIANT*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RefCapeMINLP::GetMINLPObjectiveFunctionType(long*) { return E_NOTIMPL; }

#endif  // _WIN32
