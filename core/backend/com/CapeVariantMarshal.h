#pragma once
// ***************************************************************
//  CapeVariantMarshal   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  VARIANT(内含 SAFEARRAY) / BSTR <-> std::vector / UTF-8 转换工具。
//  CAPE-OPEN MINLP COM ABI 的数组一律是 VARIANT(VT_ARRAY|VT_R8/I4/BSTR)
//  （design §5.2、§6.4）。仅 Windows。
// ***************************************************************
#ifdef _WIN32

#include <windows.h>
#include <oleauto.h>

#include <string>
#include <vector>

namespace cape_com {

// —— 字符串 ——
std::string bstrToUtf8(BSTR b);
BSTR utf8ToBstr(const std::string& s);  // 调用方负责 SysFreeString

// RAII：自动释放 BSTR
class BstrGuard {
  public:
    explicit BstrGuard(const std::string& s) : b_(utf8ToBstr(s)) {}
    ~BstrGuard() { if (b_) SysFreeString(b_); }
    BstrGuard(const BstrGuard&) = delete;
    BstrGuard& operator=(const BstrGuard&) = delete;
    BSTR get() const { return b_; }
  private:
    BSTR b_;
};

// RAII：自动 VariantClear（用于 [in,out] VARIANT* 输出参数）
class VariantGuard {
  public:
    VariantGuard() { VariantInit(&v_); }
    ~VariantGuard() { VariantClear(&v_); }
    VariantGuard(const VariantGuard&) = delete;
    VariantGuard& operator=(const VariantGuard&) = delete;
    VARIANT* ptr() { return &v_; }
    const VARIANT& ref() const { return v_; }
  private:
    VARIANT v_;
};

// —— 打包（std::vector -> VARIANT，调用方用 VariantClear 释放）——
VARIANT makeDoubleArray(const std::vector<double>& v);
VARIANT makeLongArray(const std::vector<int>& v);
VARIANT makeStringArray(const std::vector<std::string>& v);  // VT_ARRAY|VT_BSTR

// —— 解包（VARIANT -> std::vector）。成功返回 true ——
bool readDoubleArray(const VARIANT& var, std::vector<double>& out);
bool readLongArray(const VARIANT& var, std::vector<int>& out);
bool readStringArray(const VARIANT& var, std::vector<std::string>& out);

}  // namespace cape_com

#endif  // _WIN32
