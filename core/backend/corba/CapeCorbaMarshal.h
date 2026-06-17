#pragma once
// ***************************************************************
//  CapeCorbaMarshal   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  CORBA sequence <-> std::vector / UTF-8 转换（design §3.3/§6.4）。
//  依赖 tao_idl 生成的 SqpSolver* stub 中的 CapeArray* 类型。
//  编译需定义 WIN32 / ACE_AS_STATIC_LIBS / TAO_AS_STATIC_LIBS（由 CMake 提供）。
// ***************************************************************
#include <string>
#include <vector>

#include "SqpSolverC.h"

namespace cape_corba {

inline SqpSolver::CapeArrayLong toLongSeq(const std::vector<int>& v) {
    SqpSolver::CapeArrayLong s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = static_cast<CORBA::Long>(v[i]);
    return s;
}

inline SqpSolver::CapeArrayDouble toDoubleSeq(const std::vector<double>& v) {
    SqpSolver::CapeArrayDouble s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = v[i];
    return s;
}

inline SqpSolver::CapeArrayString toStringSeq(const std::vector<std::string>& v) {
    SqpSolver::CapeArrayString s;
    s.length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < s.length(); ++i) s[i] = CORBA::string_dup(v[i].c_str());
    return s;
}

inline void fromLongSeq(const SqpSolver::CapeArrayLong& s, std::vector<int>& out) {
    out.resize(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) out[i] = static_cast<int>(s[i]);
}

inline void fromDoubleSeq(const SqpSolver::CapeArrayDouble& s, std::vector<double>& out) {
    out.resize(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) out[i] = s[i];
}

inline void fromStringSeq(const SqpSolver::CapeArrayString& s, std::vector<std::string>& out) {
    out.resize(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) out[i] = static_cast<const char*>(s[i]);
}

}  // namespace cape_corba
