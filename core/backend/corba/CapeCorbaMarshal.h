#pragma once
// ***************************************************************
//  CapeCorbaMarshal   version:  2.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  CORBA sequence <-> std::vector / UTF-8 转换（design §3.3/§6.4）。
//  依赖 tao_idl 从 CAPEOPEN100_Minlp.idl 生成的 CapeArray* 类型
//  （官方模块路径，见 docs/xOptMINLPco_design.md §6）。
//  编译需定义 WIN32 / ACE_AS_STATIC_LIBS / TAO_AS_STATIC_LIBS（由 CMake 提供）。
//
//  === 索引基：这里是 0-based 和 1-based 的分界线 ===
//
//  CAPE-OPEN MINLP 规范全篇 20 处明写「Constraints and variables in the MINLP
//  are numbered starting from 1」，vids/cids 的合法范围是 1..nv / 1..nc。
//  而 xOpt 与本项目内部（ICapeMINLPModel 抽象、xOptProblem）一律 0-based。
//
//  约定：**ICapeMINLPModel 及其以内保持 0-based，转换只发生在 CAPE-OPEN 线上
//  边界**，也就是消费端 CapeMINLPModelCorba 与生产端 MINLPServant 这两处。
//  这样每个方向各只有一个转换点，不会出现「转了两次」或「谁也没转」。
//
//  下面把 id/index 的转换函数与普通 long 序列的转换函数**分开命名**，是刻意的：
//  ICapeMINLP 里既有需要换基的量（vids/cids、结构的 rowindex/columnindex/
//  objindex），也有不需要换基的量（GetMINLPVariableIntegerAttribute 返回的
//  values 就是普通整数属性）。用同一个 toLongSeq 覆盖两者，误用时没有任何征兆。
// ***************************************************************
#include <string>
#include <vector>

#include "CAPEOPEN100_MinlpC.h"

namespace cape_corba {

namespace co = ::CAPEOPEN100::Common::Types;

// ---- 普通序列（**不**换基）----

inline co::CapeArrayLong toLongSeq(const std::vector<int>& v) {
    co::CapeArrayLong s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = static_cast<CORBA::Long>(v[i]);
    return s;
}

inline co::CapeArrayDouble toDoubleSeq(const std::vector<double>& v) {
    co::CapeArrayDouble s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = v[i];
    return s;
}

inline co::CapeArrayString toStringSeq(const std::vector<std::string>& v) {
    co::CapeArrayString s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = CORBA::string_dup(v[i].c_str());
    return s;
}

inline co::CapeArrayBoolean toBooleanSeq(const std::vector<bool>& v) {
    co::CapeArrayBoolean s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = v[i];
    return s;
}

inline void fromLongSeq(const co::CapeArrayLong& s, std::vector<int>& out) {
    out.resize(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) out[i] = static_cast<int>(s[i]);
}

inline void fromDoubleSeq(const co::CapeArrayDouble& s, std::vector<double>& out) {
    out.resize(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) out[i] = s[i];
}

inline void fromStringSeq(const co::CapeArrayString& s, std::vector<std::string>& out) {
    out.resize(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) out[i] = static_cast<const char*>(s[i]);
}

// ---- 索引序列（换基）。名字里带 Wire 的一侧永远是 1-based。----

// 内部 0-based -> 线上 1-based。
inline co::CapeArrayLong indicesToWire(const std::vector<int>& internal_zero_based) {
    co::CapeArrayLong s;
    s.length(static_cast<CORBA::ULong>(internal_zero_based.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i)
        s[i] = static_cast<CORBA::Long>(internal_zero_based[i]) + 1;
    return s;
}

// 线上 1-based -> 内部 0-based。
// 越界的 id 原样减一后交给上层判断，不在这里静默夹取——静默夹取会把
// 「求解器发来一个非法 id」变成「悄悄算了另一个变量」。
inline void indicesFromWire(const co::CapeArrayLong& wire_one_based, std::vector<int>& out) {
    out.resize(wire_one_based.length());
    for (CORBA::ULong i = 0; i < wire_one_based.length(); ++i)
        out[i] = static_cast<int>(wire_one_based[i]) - 1;
}

}  // namespace cape_corba
