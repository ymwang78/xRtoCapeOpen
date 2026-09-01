// ***************************************************************
//  SolverEntry.cpp
//  -------------------------------------------------------------
//  测试求解器 DLL 的导出入口。宿主（或 demo）通过
//    LoadLibrary + GetProcAddress("createSolver" / "destroySolver")
//  加载求解器，与平台 loadSolver（libsrc/xOpt/src/xOpt.cpp）一致。
//
//  参考平台真实求解器的导出写法：
//    libsrc/RSQP/libRSQP/Include/xOpt/xOptSolverRSQP.h
// ***************************************************************
#include "PenaltyGradientSolver.h"

#include <new>

extern "C" {

// 创建求解器：name 为求解实例名，problem 是问题对象的本地指针，
// logFunc 为宿主提供的日志回调（可为空）。
__declspec(dllexport) xOptSolver* createSolver(const char* name, xOptProblem* problem,
                                               xOptLogFunc logFunc) {
    return new (std::nothrow) PenaltyGradientSolver(name, problem, logFunc);
}

// 销毁求解器：与 createSolver 配对
__declspec(dllexport) void destroySolver(xOptSolver* solver) { delete solver; }

}  // extern "C"
