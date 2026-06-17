#pragma once
// ***************************************************************
//  CoMINLP   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  CAPE-OPEN MINLP COM 组件：实现官方 IID 的 ICapeMINLP，把调用委托给一个
//  ICapeMINLPModel（通常是 XOptMINLPAdapter，背靠 xOptProblem）。
//  这是 capeopen_core 的 CapeMINLPModelCom 的「生产者」对偶：那边 COM→vector，
//  这边 vector→COM。复用 capeopen_core 的接口头 + VARIANT marshaling。
//  详见 docs/xOptMINLPco_design.md §2（N2）。仅 Windows。
//
//  说明：实现到官方 vtable 的前 23 个方法（与 CapeOpenComInterfaces.h 一致）；
//  Hessian/Lagrange 等 slot 24+ 暂未实现 —— 只用 slot 1..23 的消费者可用
//  （含 capeopen_core 的回环）；需要更高 slot 的真实消费者见 N3 待办。
// ***************************************************************
#ifdef _WIN32

#include <memory>
#include <string>

#include "CapeMINLPModel.h"                     // ICapeMINLPModel（capeopen_core）
#include "backend/com/CapeOpenComInterfaces.h"  // ICapeMINLP（capeopen_core）

class CoMINLP : public ICapeMINLP {
  public:
    // 生产模式：从环境变量 XRTO_XOPT_PROBLEM_DLL 取被包装 DLL，建 XOptMINLPAdapter 并 connect。
    CoMINLP();
    // 测试模式：委托给已 connect 的外部 model（不拥有）。
    explicit CoMINLP(ICapeMINLPModel* model);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDispatch（桩：早绑定 vtable 调用，不走 Invoke）
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT*) override;
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override;
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override;
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*,
                                     UINT*) override;

    // ICapeMINLP（实现用到的；其余返回 E_NOTIMPL）
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
    ICapeMINLPModel* model_ = nullptr;            // 当前委托目标
    std::unique_ptr<ICapeMINLPModel> owned_;      // 生产模式下拥有
    std::string init_error_;
};

#endif  // _WIN32
