// ***************************************************************
//  CapeVariantMarshal   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#ifdef _WIN32

#include "CapeVariantMarshal.h"

#include <cstring>

namespace cape_com {

std::string bstrToUtf8(BSTR b) {
    if (b == nullptr) return std::string();
    const int wlen = static_cast<int>(SysStringLen(b));
    if (wlen == 0) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, b, wlen, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, b, wlen, &s[0], n, nullptr, nullptr);
    return s;
}

BSTR utf8ToBstr(const std::string& s) {
    if (s.empty()) return SysAllocStringLen(L"", 0);
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    BSTR b = SysAllocStringLen(nullptr, wlen);
    if (b) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), b, wlen);
    return b;
}

VARIANT makeDoubleArray(const std::vector<double>& v) {
    VARIANT var;
    VariantInit(&var);
    SAFEARRAY* psa = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(v.size()));
    if (psa) {
        if (!v.empty()) {
            void* data = nullptr;
            if (SUCCEEDED(SafeArrayAccessData(psa, &data))) {
                std::memcpy(data, v.data(), v.size() * sizeof(double));
                SafeArrayUnaccessData(psa);
            }
        }
        var.vt = VT_ARRAY | VT_R8;
        var.parray = psa;
    }
    return var;
}

VARIANT makeLongArray(const std::vector<int>& v) {
    VARIANT var;
    VariantInit(&var);
    SAFEARRAY* psa = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(v.size()));
    if (psa) {
        if (!v.empty()) {
            LONG* data = nullptr;
            if (SUCCEEDED(SafeArrayAccessData(psa, reinterpret_cast<void**>(&data)))) {
                for (size_t i = 0; i < v.size(); ++i) data[i] = static_cast<LONG>(v[i]);
                SafeArrayUnaccessData(psa);
            }
        }
        var.vt = VT_ARRAY | VT_I4;
        var.parray = psa;
    }
    return var;
}

VARIANT makeStringArray(const std::vector<std::string>& v) {
    VARIANT var;
    VariantInit(&var);
    SAFEARRAY* psa = SafeArrayCreateVector(VT_BSTR, 0, static_cast<ULONG>(v.size()));
    if (psa) {
        for (LONG i = 0; i < static_cast<LONG>(v.size()); ++i) {
            BSTR b = utf8ToBstr(v[static_cast<size_t>(i)]);
            SafeArrayPutElement(psa, &i, b);  // 内部复制
            SysFreeString(b);
        }
        var.vt = VT_ARRAY | VT_BSTR;
        var.parray = psa;
    }
    return var;
}

namespace {

// 取出 VARIANT 内的一维 SAFEARRAY（处理 VT_BYREF），返回元素个数；失败返回 false。
bool getArray1D(const VARIANT& var, SAFEARRAY** psa_out, LONG* count_out, VARTYPE* vt_out) {
    if ((var.vt & VT_ARRAY) == 0) return false;
    SAFEARRAY* psa = (var.vt & VT_BYREF) ? *var.pparray : var.parray;
    if (psa == nullptr) {
        *psa_out = nullptr;
        *count_out = 0;
        *vt_out = VT_EMPTY;
        return true;  // 空数组
    }
    if (SafeArrayGetDim(psa) != 1) return false;
    LONG lb = 0, ub = -1;
    if (FAILED(SafeArrayGetLBound(psa, 1, &lb)) || FAILED(SafeArrayGetUBound(psa, 1, &ub))) {
        return false;
    }
    *psa_out = psa;
    *count_out = (ub >= lb) ? (ub - lb + 1) : 0;
    SafeArrayGetVartype(psa, vt_out);
    return true;
}

}  // namespace

bool readDoubleArray(const VARIANT& var, std::vector<double>& out) {
    SAFEARRAY* psa = nullptr;
    LONG n = 0;
    VARTYPE vt = VT_EMPTY;
    if (!getArray1D(var, &psa, &n, &vt)) return false;
    out.assign(static_cast<size_t>(n), 0.0);
    if (psa == nullptr || n == 0) return true;
    if (vt == VT_R8) {
        double* d = nullptr;
        if (FAILED(SafeArrayAccessData(psa, reinterpret_cast<void**>(&d)))) return false;
        for (LONG i = 0; i < n; ++i) out[i] = d[i];
        SafeArrayUnaccessData(psa);
        return true;
    }
    // 容错：逐元素取出并转为 double（兼容 VT_R4/VT_I4 等）
    LONG lb = 0;
    SafeArrayGetLBound(psa, 1, &lb);
    for (LONG i = 0; i < n; ++i) {
        LONG idx = lb + i;
        VARIANT elem;
        VariantInit(&elem);
        if (FAILED(SafeArrayGetElement(psa, &idx, &elem))) return false;
        VARIANT d;
        VariantInit(&d);
        if (SUCCEEDED(VariantChangeType(&d, &elem, 0, VT_R8))) out[i] = d.dblVal;
        VariantClear(&elem);
        VariantClear(&d);
    }
    return true;
}

bool readLongArray(const VARIANT& var, std::vector<int>& out) {
    SAFEARRAY* psa = nullptr;
    LONG n = 0;
    VARTYPE vt = VT_EMPTY;
    if (!getArray1D(var, &psa, &n, &vt)) return false;
    out.assign(static_cast<size_t>(n), 0);
    if (psa == nullptr || n == 0) return true;
    if (vt == VT_I4) {
        LONG* d = nullptr;
        if (FAILED(SafeArrayAccessData(psa, reinterpret_cast<void**>(&d)))) return false;
        for (LONG i = 0; i < n; ++i) out[i] = static_cast<int>(d[i]);
        SafeArrayUnaccessData(psa);
        return true;
    }
    LONG lb = 0;
    SafeArrayGetLBound(psa, 1, &lb);
    for (LONG i = 0; i < n; ++i) {
        LONG idx = lb + i;
        VARIANT elem;
        VariantInit(&elem);
        if (FAILED(SafeArrayGetElement(psa, &idx, &elem))) return false;
        VARIANT d;
        VariantInit(&d);
        if (SUCCEEDED(VariantChangeType(&d, &elem, 0, VT_I4))) out[i] = static_cast<int>(d.lVal);
        VariantClear(&elem);
        VariantClear(&d);
    }
    return true;
}

bool readStringArray(const VARIANT& var, std::vector<std::string>& out) {
    SAFEARRAY* psa = nullptr;
    LONG n = 0;
    VARTYPE vt = VT_EMPTY;
    if (!getArray1D(var, &psa, &n, &vt)) return false;
    out.clear();
    out.reserve(static_cast<size_t>(n));
    if (psa == nullptr || n == 0) return true;
    LONG lb = 0;
    SafeArrayGetLBound(psa, 1, &lb);
    for (LONG i = 0; i < n; ++i) {
        LONG idx = lb + i;
        if (vt == VT_BSTR) {
            BSTR b = nullptr;
            if (FAILED(SafeArrayGetElement(psa, &idx, &b))) return false;
            out.push_back(bstrToUtf8(b));
            SysFreeString(b);
        } else {
            VARIANT elem;
            VariantInit(&elem);
            if (FAILED(SafeArrayGetElement(psa, &idx, &elem))) return false;
            VARIANT s;
            VariantInit(&s);
            if (SUCCEEDED(VariantChangeType(&s, &elem, 0, VT_BSTR))) {
                out.push_back(bstrToUtf8(s.bstrVal));
            } else {
                out.push_back(std::string());
            }
            VariantClear(&elem);
            VariantClear(&s);
        }
    }
    return true;
}

}  // namespace cape_com

#endif  // _WIN32
