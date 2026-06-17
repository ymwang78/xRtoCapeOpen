#pragma once
// ***************************************************************
//  CapeOpenComInterfaces   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  手写的精简 CAPE-OPEN MINLP COM 接口声明，签名与官方类型库
//  CAPE-OPENv1-1-0.tlb 完全一致（经 MSVC #import 核对，design §5.2）。
//
//  vtable 布局必须与官方一致：IUnknown(3) + IDispatch(4) + 按 tlb 顺序的
//  raw 方法。ICapeMINLP 我们只声明到「目标导数」(第 23 个方法) 为止——
//  这是用到的最后一个方法；之后的 Hessian/Lagrange 等方法不声明，等价于
//  只取真实 vtable 的前缀视图（COM 常用做法），保证 1..23 槽位偏移一致。
//
//  仅 Windows 编译。
// ***************************************************************
#ifdef _WIN32

#include <windows.h>
#include <oaidl.h>

// 官方 IID（design §5.2）
// ICapeMINLP              {678C09CC-7D66-11D2-A67D-00105A42887F}
// ICapeMINLPSystem        {678C09CD-7D66-11D2-A67D-00105A42887F}
// ICapeMINLPSolverManager {678C09CE-7D66-11D2-A67D-00105A42887F}
extern "C" const GUID IID_ICapeMINLP;
extern "C" const GUID IID_ICapeMINLPSystem;
extern "C" const GUID IID_ICapeMINLPSolverManager;
// ICapeIdentification {678C0990-7D66-11D2-A67D-00105A42887F}
extern "C" const GUID IID_ICapeIdentification;

// CAPE-OPEN 通用标识接口（dual）。组件名称/描述。
struct ICapeIdentification : public IDispatch {
    virtual HRESULT STDMETHODCALLTYPE get_ComponentName(BSTR* name) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ComponentName(BSTR name) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ComponentDescription(BSTR* desc) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ComponentDescription(BSTR desc) = 0;
};

// 模型接口（dual，派生自 IDispatch）。方法顺序严格对应官方 tlb。
struct ICapeMINLP : public IDispatch {
    // 1
    virtual HRESULT STDMETHODCALLTYPE GetMINLPSize(long* nv, long* niv, long* nlv, long* nliv,
                                                   long* nc, long* nlc, long* nlz, long* nnz,
                                                   long* nlzof, long* nnzof) = 0;
    // 2
    virtual HRESULT STDMETHODCALLTYPE GetMINLPStructure(BSTR structuretype, VARIANT* rowindex,
                                                        VARIANT* columnindex, VARIANT* objindex) = 0;
    // 3
    virtual HRESULT STDMETHODCALLTYPE GetMINLPVariableNames(VARIANT vids, VARIANT* vnames) = 0;
    // 4
    virtual HRESULT STDMETHODCALLTYPE GetMINLPVariableTypes(VARIANT vids, VARIANT* isinteger) = 0;
    // 5
    virtual HRESULT STDMETHODCALLTYPE GetMINLPVariableBooleanAttribute(VARIANT vids, VARIANT attribute,
                                                                       VARIANT* values) = 0;
    // 6
    virtual HRESULT STDMETHODCALLTYPE GetMINLPVariableIntegerAttribute(VARIANT vids, BSTR attribute,
                                                                       VARIANT* values) = 0;
    // 7
    virtual HRESULT STDMETHODCALLTYPE GetMINLPVariableDoubleAttribute(VARIANT vids, BSTR attribute,
                                                                      VARIANT* values) = 0;
    // 8
    virtual HRESULT STDMETHODCALLTYPE GetMINLPVariableStringAttribute(VARIANT vids, BSTR attribute,
                                                                      VARIANT* values) = 0;
    // 9
    virtual HRESULT STDMETHODCALLTYPE GetMINLPVariableBounds(VARIANT vids, VARIANT* LB,
                                                             VARIANT* UB) = 0;
    // 10
    virtual HRESULT STDMETHODCALLTYPE GetMINLPVariableValues(VARIANT vids, VARIANT* values) = 0;
    // 11
    virtual HRESULT STDMETHODCALLTYPE SetMINLPVariableValues(VARIANT vids, VARIANT values) = 0;
    // 12
    virtual HRESULT STDMETHODCALLTYPE GetMINLPConstraintNames(VARIANT cids, VARIANT* cnames) = 0;
    // 13
    virtual HRESULT STDMETHODCALLTYPE GetMINLPConstraintBounds(VARIANT cids, VARIANT* LB,
                                                               VARIANT* UB) = 0;
    // 14
    virtual HRESULT STDMETHODCALLTYPE GetMINLPConstraintLinearity(VARIANT cids, VARIANT* islinear) = 0;
    // 15
    virtual HRESULT STDMETHODCALLTYPE GetMINLPConstraintBooleanAttribute(VARIANT cids, BSTR attribute,
                                                                         VARIANT* values) = 0;
    // 16
    virtual HRESULT STDMETHODCALLTYPE GetMINLPConstraintIntegerAttribute(VARIANT cids, BSTR attribute,
                                                                         VARIANT* values) = 0;
    // 17
    virtual HRESULT STDMETHODCALLTYPE GetMINLPConstraintDoubleAttribute(VARIANT cids, BSTR attribute,
                                                                        VARIANT* values) = 0;
    // 18
    virtual HRESULT STDMETHODCALLTYPE GetMINLPConstraintStringAttribute(VARIANT cids, BSTR attribute,
                                                                        VARIANT* values) = 0;
    // 19
    virtual HRESULT STDMETHODCALLTYPE GetMINLPNonlinearConstraintValues(VARIANT cids,
                                                                        VARIANT* values) = 0;
    // 20
    virtual HRESULT STDMETHODCALLTYPE GetMINLPConstraintDerivativeValues(BSTR structtype, VARIANT cids,
                                                                         VARIANT* vals) = 0;
    // 21
    virtual HRESULT STDMETHODCALLTYPE GetMINLPObjectiveFunctionType(long* otype) = 0;
    // 22
    virtual HRESULT STDMETHODCALLTYPE GetMINLPNonlinearObjectiveFunctionValue(double* value) = 0;
    // 23
    virtual HRESULT STDMETHODCALLTYPE GetMINLPObjectiveFunctionDerivativeValues(BSTR stype,
                                                                                VARIANT* v) = 0;
};

// 求解器系统接口（极简：Solve + parameters 集合）。
struct ICapeMINLPSystem : public IDispatch {
    virtual HRESULT STDMETHODCALLTYPE Solve() = 0;
    virtual HRESULT STDMETHODCALLTYPE get_parameters(IDispatch** parameters) = 0;
};

#endif  // _WIN32
