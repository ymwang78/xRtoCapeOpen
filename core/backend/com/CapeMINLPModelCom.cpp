// ***************************************************************
//  CapeMINLPModelCom   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#ifdef _WIN32

#include "CapeMINLPModelCom.h"

#include <cstdio>

#include "CapeOpenComInterfaces.h"
#include "CapeVariantMarshal.h"

using namespace cape_com;

// —— 官方 CAPE-OPEN MINLP IID 定义（design §5.2）——
extern "C" const GUID IID_ICapeMINLP = {
    0x678C09CC, 0x7D66, 0x11D2, {0xA6, 0x7D, 0x00, 0x10, 0x5A, 0x42, 0x88, 0x7F}};
extern "C" const GUID IID_ICapeMINLPSystem = {
    0x678C09CD, 0x7D66, 0x11D2, {0xA6, 0x7D, 0x00, 0x10, 0x5A, 0x42, 0x88, 0x7F}};
extern "C" const GUID IID_ICapeMINLPSolverManager = {
    0x678C09CE, 0x7D66, 0x11D2, {0xA6, 0x7D, 0x00, 0x10, 0x5A, 0x42, 0x88, 0x7F}};

namespace {
// 软解包：输出参数若不是数组（空），按空向量处理而非报错。
void softReadInt(const VARIANT& v, std::vector<int>& out) {
    if ((v.vt & VT_ARRAY) == 0) { out.clear(); return; }
    readLongArray(v, out);
}
void softReadDouble(const VARIANT& v, std::vector<double>& out) {
    if ((v.vt & VT_ARRAY) == 0) { out.clear(); return; }
    readDoubleArray(v, out);
}
}  // namespace

CapeMINLPModelCom::CapeMINLPModelCom(const std::string& target) : target_(target) {}

CapeMINLPModelCom::CapeMINLPModelCom(ICapeMINLP* injected) : injected_(true), minlp_(injected) {
    if (minlp_) minlp_->AddRef();
}

CapeMINLPModelCom::~CapeMINLPModelCom() { disconnect(); }

int CapeMINLPModelCom::fail(const std::string& msg) const {
    last_error_ = msg;
    return -1;
}

int CapeMINLPModelCom::failHr(const std::string& method, long hr) const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), " (hr=0x%08lX)", static_cast<unsigned long>(hr));
    last_error_ = method + buf;
    return -1;
}

int CapeMINLPModelCom::connect() {
    if (injected_) {
        return minlp_ ? 0 : fail("connect: injected interface is null");
    }
    if (minlp_) return 0;  // 已连接

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // S_FALSE 表示本线程已初始化过；两种成功我们都记为「由我们负责的初始化」以便对称 Uninitialize。
    if (hr == S_OK || hr == S_FALSE) com_initialized_ = true;
    else if (FAILED(hr)) return failHr("CoInitializeEx", hr);

    CLSID clsid{};
    std::wstring wtarget;
    {
        const int n = MultiByteToWideChar(CP_UTF8, 0, target_.c_str(), -1, nullptr, 0);
        wtarget.resize(n > 0 ? n - 1 : 0);
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, target_.c_str(), -1, &wtarget[0], n);
    }
    if (!wtarget.empty() && wtarget[0] == L'{') {
        hr = CLSIDFromString(wtarget.c_str(), &clsid);
    } else {
        hr = CLSIDFromProgID(wtarget.c_str(), &clsid);
    }
    if (FAILED(hr)) return failHr("CLSIDFrom(ProgID|String): '" + target_ + "'", hr);

    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_ICapeMINLP,
                          reinterpret_cast<void**>(&minlp_));
    if (FAILED(hr) || minlp_ == nullptr) {
        return failHr("CoCreateInstance(ICapeMINLP) — 组件不存在或未暴露 ICapeMINLP", hr);
    }
    return 0;
}

void CapeMINLPModelCom::disconnect() {
    if (minlp_) {
        minlp_->Release();
        minlp_ = nullptr;
    }
    if (com_initialized_ && !injected_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

int CapeMINLPModelCom::getSize(CapeMINLPSize& s) {
    if (!minlp_) return fail("getSize: not connected");
    long nv, niv, nlv, nliv, nc, nlc, nlz, nnz, nlzof, nnzof;
    HRESULT hr = minlp_->GetMINLPSize(&nv, &niv, &nlv, &nliv, &nc, &nlc, &nlz, &nnz, &nlzof, &nnzof);
    if (FAILED(hr)) return failHr("GetMINLPSize", hr);
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
}

int CapeMINLPModelCom::getStructure(const std::string& type, std::vector<int>& row_index,
                                    std::vector<int>& col_index, std::vector<int>& obj_index) {
    if (!minlp_) return fail("getStructure: not connected");
    BstrGuard bt(type);
    VariantGuard vr, vc, vo;
    HRESULT hr = minlp_->GetMINLPStructure(bt.get(), vr.ptr(), vc.ptr(), vo.ptr());
    if (FAILED(hr)) return failHr("GetMINLPStructure", hr);
    softReadInt(vr.ref(), row_index);
    softReadInt(vc.ref(), col_index);
    softReadInt(vo.ref(), obj_index);
    return 0;
}

int CapeMINLPModelCom::getVariableNames(const std::vector<int>& vids,
                                        std::vector<std::string>& names_out) {
    if (!minlp_) return fail("getVariableNames: not connected");
    VARIANT ids = makeLongArray(vids);
    VariantGuard vn;
    HRESULT hr = minlp_->GetMINLPVariableNames(ids, vn.ptr());
    VariantClear(&ids);
    if (FAILED(hr)) return failHr("GetMINLPVariableNames", hr);
    if (!readStringArray(vn.ref(), names_out)) return fail("GetMINLPVariableNames: bad result array");
    return 0;
}

int CapeMINLPModelCom::getVariableBounds(const std::vector<int>& vids, std::vector<double>& lower_out,
                                         std::vector<double>& upper_out) {
    if (!minlp_) return fail("getVariableBounds: not connected");
    VARIANT ids = makeLongArray(vids);
    VariantGuard lb, ub;
    HRESULT hr = minlp_->GetMINLPVariableBounds(ids, lb.ptr(), ub.ptr());
    VariantClear(&ids);
    if (FAILED(hr)) return failHr("GetMINLPVariableBounds", hr);
    softReadDouble(lb.ref(), lower_out);
    softReadDouble(ub.ref(), upper_out);
    return 0;
}

int CapeMINLPModelCom::getVariableValues(const std::vector<int>& vids,
                                         std::vector<double>& values_out) {
    if (!minlp_) return fail("getVariableValues: not connected");
    VARIANT ids = makeLongArray(vids);
    VariantGuard vv;
    HRESULT hr = minlp_->GetMINLPVariableValues(ids, vv.ptr());
    VariantClear(&ids);
    if (FAILED(hr)) return failHr("GetMINLPVariableValues", hr);
    softReadDouble(vv.ref(), values_out);
    return 0;
}

int CapeMINLPModelCom::setVariableValues(const std::vector<int>& vids,
                                         const std::vector<double>& values) {
    if (!minlp_) return fail("setVariableValues: not connected");
    VARIANT ids = makeLongArray(vids);
    VARIANT vals = makeDoubleArray(values);
    HRESULT hr = minlp_->SetMINLPVariableValues(ids, vals);
    VariantClear(&ids);
    VariantClear(&vals);
    if (FAILED(hr)) return failHr("SetMINLPVariableValues", hr);
    return 0;
}

int CapeMINLPModelCom::getConstraintNames(const std::vector<int>& cids,
                                          std::vector<std::string>& names_out) {
    if (!minlp_) return fail("getConstraintNames: not connected");
    VARIANT ids = makeLongArray(cids);
    VariantGuard cn;
    HRESULT hr = minlp_->GetMINLPConstraintNames(ids, cn.ptr());
    VariantClear(&ids);
    if (FAILED(hr)) return failHr("GetMINLPConstraintNames", hr);
    if (!readStringArray(cn.ref(), names_out)) return fail("GetMINLPConstraintNames: bad result");
    return 0;
}

int CapeMINLPModelCom::getConstraintBounds(const std::vector<int>& cids, std::vector<double>& lower_out,
                                           std::vector<double>& upper_out) {
    if (!minlp_) return fail("getConstraintBounds: not connected");
    VARIANT ids = makeLongArray(cids);
    VariantGuard lb, ub;
    HRESULT hr = minlp_->GetMINLPConstraintBounds(ids, lb.ptr(), ub.ptr());
    VariantClear(&ids);
    if (FAILED(hr)) return failHr("GetMINLPConstraintBounds", hr);
    softReadDouble(lb.ref(), lower_out);
    softReadDouble(ub.ref(), upper_out);
    return 0;
}

int CapeMINLPModelCom::getNonlinearConstraintValues(const std::vector<int>& cids,
                                                    std::vector<double>& values_out) {
    if (!minlp_) return fail("getNonlinearConstraintValues: not connected");
    VARIANT ids = makeLongArray(cids);
    VariantGuard vv;
    HRESULT hr = minlp_->GetMINLPNonlinearConstraintValues(ids, vv.ptr());
    VariantClear(&ids);
    if (FAILED(hr)) return failHr("GetMINLPNonlinearConstraintValues", hr);
    softReadDouble(vv.ref(), values_out);
    return 0;
}

int CapeMINLPModelCom::getConstraintDerivativeValues(const std::string& type,
                                                     const std::vector<int>& cids,
                                                     std::vector<double>& values_out) {
    if (!minlp_) return fail("getConstraintDerivativeValues: not connected");
    BstrGuard bt(type);
    VARIANT ids = makeLongArray(cids);
    VariantGuard vv;
    HRESULT hr = minlp_->GetMINLPConstraintDerivativeValues(bt.get(), ids, vv.ptr());
    VariantClear(&ids);
    if (FAILED(hr)) return failHr("GetMINLPConstraintDerivativeValues", hr);
    softReadDouble(vv.ref(), values_out);
    return 0;
}

int CapeMINLPModelCom::getObjectiveValue(double& value_out) {
    if (!minlp_) return fail("getObjectiveValue: not connected");
    double v = 0;
    HRESULT hr = minlp_->GetMINLPNonlinearObjectiveFunctionValue(&v);
    if (FAILED(hr)) return failHr("GetMINLPNonlinearObjectiveFunctionValue", hr);
    value_out = v;
    return 0;
}

int CapeMINLPModelCom::getObjectiveDerivativeValues(const std::string& type,
                                                    std::vector<double>& values_out) {
    if (!minlp_) return fail("getObjectiveDerivativeValues: not connected");
    BstrGuard bt(type);
    VariantGuard vv;
    HRESULT hr = minlp_->GetMINLPObjectiveFunctionDerivativeValues(bt.get(), vv.ptr());
    if (FAILED(hr)) return failHr("GetMINLPObjectiveFunctionDerivativeValues", hr);
    softReadDouble(vv.ref(), values_out);
    return 0;
}

std::string CapeMINLPModelCom::lastError() const { return last_error_; }

#endif  // _WIN32
