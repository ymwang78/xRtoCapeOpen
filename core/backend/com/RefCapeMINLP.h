#pragma once
// ***************************************************************
//  RefCapeMINLP   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  测试用参考 COM 组件：用官方 IID 实现 CAPE-OPEN ICapeMINLP，内部包
//  CapeMINLPModelMock（解析解小 NLP）。作为 CapeMINLPModelCom 的 in-proc
//  测试对端，验证 VARIANT/SAFEARRAY/BSTR/HRESULT 全链路（design §5.3）。
//  仅测试编译，仅 Windows。
// ***************************************************************
#ifdef _WIN32

#include "../../CapeMINLPModelMock.h"
#include "CapeOpenComInterfaces.h"

class RefCapeMINLP : public ICapeMINLP {
  public:
    RefCapeMINLP();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDispatch（桩：本组件经 vtable 直接调用，不走 Invoke）
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* pctinfo) override;
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override;
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override;
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*,
                                     UINT*) override;

    // ICapeMINLP（实现用到的方法，其余返回 E_NOTIMPL）
    HRESULT STDMETHODCALLTYPE GetMINLPSize(long* nv, long* niv, long* nlv, long* nliv, long* nc,
                                           long* nlc, long* nlz, long* nnz, long* nlzof,
                                           long* nnzof) override;
    HRESULT STDMETHODCALLTYPE GetMINLPStructure(BSTR structuretype, VARIANT* rowindex,
                                                VARIANT* columnindex, VARIANT* objindex) override;
    HRESULT STDMETHODCALLTYPE GetMINLPVariableNames(VARIANT vids, VARIANT* vnames) override;
    HRESULT STDMETHODCALLTYPE GetMINLPVariableTypes(VARIANT, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPVariableBooleanAttribute(VARIANT, VARIANT, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPVariableIntegerAttribute(VARIANT, BSTR, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPVariableDoubleAttribute(VARIANT, BSTR, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPVariableStringAttribute(VARIANT, BSTR, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPVariableBounds(VARIANT vids, VARIANT* LB, VARIANT* UB) override;
    HRESULT STDMETHODCALLTYPE GetMINLPVariableValues(VARIANT vids, VARIANT* values) override;
    HRESULT STDMETHODCALLTYPE SetMINLPVariableValues(VARIANT vids, VARIANT values) override;
    HRESULT STDMETHODCALLTYPE GetMINLPConstraintNames(VARIANT cids, VARIANT* cnames) override;
    HRESULT STDMETHODCALLTYPE GetMINLPConstraintBounds(VARIANT cids, VARIANT* LB, VARIANT* UB) override;
    HRESULT STDMETHODCALLTYPE GetMINLPConstraintLinearity(VARIANT, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPConstraintBooleanAttribute(VARIANT, BSTR, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPConstraintIntegerAttribute(VARIANT, BSTR, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPConstraintDoubleAttribute(VARIANT, BSTR, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPConstraintStringAttribute(VARIANT, BSTR, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPNonlinearConstraintValues(VARIANT cids,
                                                                VARIANT* values) override;
    HRESULT STDMETHODCALLTYPE GetMINLPConstraintDerivativeValues(BSTR structtype, VARIANT cids,
                                                                 VARIANT* vals) override;
    HRESULT STDMETHODCALLTYPE GetMINLPObjectiveFunctionType(long*) override;
    HRESULT STDMETHODCALLTYPE GetMINLPNonlinearObjectiveFunctionValue(double* value) override;
    HRESULT STDMETHODCALLTYPE GetMINLPObjectiveFunctionDerivativeValues(BSTR stype,
                                                                        VARIANT* v) override;

  private:
    LONG ref_ = 1;
    CapeMINLPModelMock mock_;
};

#endif  // _WIN32
