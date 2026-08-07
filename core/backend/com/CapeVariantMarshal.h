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
//
//  === 索引基：这里是 0-based 和 1-based 的分界线（COM 侧）===
//
//  CAPE-OPEN MINLP 规范全篇 20 处明写「Constraints and variables in the MINLP
//  are numbered starting from 1」，vids/cids 的合法范围是 1..nv / 1..nc。
//  这句话写在**接口规范**里，与 COM/CORBA 绑定无关，所以两边同样适用。
//
//  约定与 CORBA 侧完全一致（见 CapeCorbaMarshal.h 顶部）：**ICapeMINLPModel
//  及其以内保持 0-based，转换只发生在 CAPE-OPEN 线上边界**——COM 侧就是生产端
//  CoMINLP 与消费端 CapeMINLPModelCom 这两处，每个方向各一个转换点。
//
//  下面把 id/index 的转换函数与普通 long 数组的转换函数**分开命名**，是刻意的：
//  ICapeMINLP 里既有需要换基的量（vids/cids、结构的 rowindex/columnindex/
//  objindex），也有不需要换基的量（GetMINLPVariableIntegerAttribute 返回的
//  values 就是普通整数属性）。用同一个 makeLongArray 覆盖两者，误用时没有任何征兆。
//  ——CORBA 侧的 0-based 缺陷正是这么藏了两个月的（design §6.2 缺口 2）。
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

// —— 索引数组（**换基**）。名字里带 Wire 的一侧永远是 1-based。——
// 与上面的 makeLongArray/readLongArray 分开，理由见文件头。

// 内部 0-based -> 线上 1-based。
VARIANT makeIndicesToWire(const std::vector<int>& internal_zero_based);

// 线上 1-based -> 内部 0-based。
// 越界的 id 原样减一后交给上层判断，不在这里静默夹取——静默夹取会把
// 「求解器发来一个非法 id」变成「悄悄算了另一个变量」。
bool readIndicesFromWire(const VARIANT& var, std::vector<int>& out);

}  // namespace cape_com

#endif  // _WIN32
