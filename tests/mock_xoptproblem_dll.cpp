// ***************************************************************
//  mock_xoptproblem_dll   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco tests).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  测试夹具 DLL：导出 C++ ABI 的 createProblem()/destroyProblem()，返回
//  MockXOptProblem（二次问题）。N3 注册冒烟用：xOptMINLPco.dll 经
//  XRTO_XOPT_PROBLEM_DLL 加载本 DLL，发布为 CAPE-OPEN MINLP。
// ***************************************************************
#include "MockXOptProblem.h"  // 含 XOPTINTERFACE_EXPORTS + xOptProblem

extern "C" __declspec(dllexport) xOptProblem* createProblem() { return new MockXOptProblem(); }

extern "C" __declspec(dllexport) void destroyProblem(xOptProblem* p) { delete p; }
