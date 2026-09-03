// ***************************************************************
//  test_xoptminlpco_solver_ipc   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  求解器通路的真·跨进程冒烟，按 xRto 的用法一比一来：
//
//    [server 进程] xOptMINLPcoSolverServer.exe --solver-dll test_penalty_solver.dll
//                    -> ICapeMINLPSolverManager -> IOR 文件
//    [本进程]      LoadLibrary("xRtoCapeOpenSolver.dll") + GetProcAddress("createSolver")
//                    -> createSolver("FLOWSHEET", &mock, log)   ← 宿主 xOpt.cpp 就是这么调的
//                    -> setIntOptions / solve / X / F / destroySolver
//
//  这条测试真正要证的是**嵌套上行调用**：本进程阻塞在远端 Solve() 里等应答
//  时，服务端会回过头来在本进程的 ICapeMINLP 上 setX/evaluate。collocated
//  用例永远碰不到这一步——TAO 短路掉网络后根本没有"等应答"这回事。
//  自带 main。
// ***************************************************************
#include "MockXOptProblem.h"  // 首个包含：XOPTINTERFACE_EXPORTS + xOptProblem

#include <gtest/gtest.h>

#ifdef _WIN32
#    include <windows.h>
#endif

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "xOpt/xOptSolver.h"

#ifndef _WIN32
#    error "test_xoptminlpco_solver_ipc needs a POSIX spawn/kill path before it can run here"
#endif

namespace {

std::string exeDir() {
    char buf[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    const std::string p(buf, n);
    const size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

void hostLog(ZLOG_LEVEL level, const char* format, ...) {
    char buf[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    std::printf("  [host log %d] %s\n", static_cast<int>(level), buf);
}

// 起 server 进程、等它把 IOR 落盘、退出时收尸（与 test_xoptminlpco_corba_ipc 同款）。
class ServerProcess {
  public:
    ServerProcess(const std::string& exe, const std::string& solver_dll, const std::string& ior_file)
        : ior_file_(ior_file) {
        std::remove(ior_file_.c_str());
        std::ostringstream cmd;
        cmd << '"' << exe << "\" --solver-dll \"" << solver_dll << "\" --ior-file \"" << ior_file
            << '"';
        std::string line = cmd.str();
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        started_ = CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                  nullptr, nullptr, &si, &pi_) != 0;
    }

    ~ServerProcess() {
        if (started_) {
            TerminateProcess(pi_.hProcess, 0);
            WaitForSingleObject(pi_.hProcess, 5000);
            CloseHandle(pi_.hThread);
            CloseHandle(pi_.hProcess);
        }
        std::remove(ior_file_.c_str());
    }

    bool started() const { return started_; }

    std::string waitForIor(std::chrono::seconds timeout, std::string& error_out) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            DWORD code = 0;
            if (GetExitCodeProcess(pi_.hProcess, &code) && code != STILL_ACTIVE) {
                error_out = "server exited early with code " + std::to_string(code);
                return {};
            }
            std::ifstream in(ior_file_, std::ios::binary);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                const std::string ior = ss.str();
                if (ior.rfind("IOR:", 0) == 0) return ior;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        error_out = "timed out waiting for the IOR file: " + ior_file_;
        return {};
    }

  private:
    std::string ior_file_;
    bool started_ = false;
    PROCESS_INFORMATION pi_{};
};

TEST(XOptMINLPcoSolverIpc, HostStyleSolveThroughTheBridgeDll) {
    const std::string dir = exeDir();
    const std::string server = dir + "\\xOptMINLPcoSolverServer.exe";
    const std::string bridge = dir + "\\xRtoCapeOpenSolver.dll";
    const std::string ior_file = dir + "\\xoptminlpco_solver_ipc.ior";

    ServerProcess server_proc(server, TEST_PENALTY_SOLVER_DLL, ior_file);
    ASSERT_TRUE(server_proc.started()) << "cannot start the server: " << server;
    std::string err;
    const std::string ior = server_proc.waitForIor(std::chrono::seconds(30), err);
    ASSERT_FALSE(ior.empty()) << err;

    // 连接目标走环境变量那条（xRto 部署里走的是 DLL 旁边的 .target 文件，
    // 解析出来是同一个字符串）。
    _putenv_s("XRTO_CAPEOPEN_SOLVER_TARGET", ("corba:" + ior).c_str());

    // 宿主的加载方式：LoadLibrary + GetProcAddress，不链接导入库。
    HMODULE lib = LoadLibraryA(bridge.c_str());
    ASSERT_NE(lib, nullptr) << "cannot load " << bridge << " (error " << GetLastError() << ")";
    auto create = reinterpret_cast<CreateSolverFunc>(GetProcAddress(lib, "createSolver"));
    auto destroy = reinterpret_cast<DestroySolverFunc>(GetProcAddress(lib, "destroySolver"));
    ASSERT_NE(create, nullptr);
    ASSERT_NE(destroy, nullptr);

    MockXOptProblem mock;
    xOptSolver* solver = create("FLOWSHEET", &mock, &hostLog);
    ASSERT_NE(solver, nullptr) << "createSolver failed (see host log above)";
    EXPECT_EQ(solver->getProblem(), &mock);

    // 可调参数列表跨进程取回（两段式，与宿主一致）
    int count = 0;
    ASSERT_EQ(solver->getTunableParamList(nullptr, nullptr, count), 0);
    ASSERT_EQ(count, 4);
    std::vector<const char*> pnames(static_cast<size_t>(count), nullptr);
    std::vector<xOptSolver::OPTION_TYPE> ptypes(static_cast<size_t>(count));
    ASSERT_EQ(solver->getTunableParamList(pnames.data(), ptypes.data(), count), 0);
    EXPECT_STREQ(pnames[0], "max_iter");
    EXPECT_EQ(ptypes[0], xOptSolver::OPTION_INT);

    // 选项：xRto 就是这样一次设一个。
    // max_iter 压到 300：这条通路上求解器的每一次 setX/evaluate 都是一次 IIOP
    // 往返，罚函数梯度下降一轮几千次求值，按出厂值跑一次要两三分钟，而本测试
    // 要证的是通路，不是求解器的收敛速度。
    {
        xOptSolver::boolean result[1] = {0};
        const char* name[1] = {"max_iter"};
        const int value[1] = {300};
        ASSERT_EQ(solver->setIntOptions(result, name, value, 1), 0);
        EXPECT_EQ(result[0], 1);
        int back[1] = {0};
        int n = 1;
        ASSERT_EQ(solver->getIntOptions(name, back, n), 0);
        EXPECT_EQ(back[0], 300);
    }
    {
        xOptSolver::boolean result[1] = {0};
        const char* name[1] = {"tol"};
        const double value[1] = {1e-8};
        ASSERT_EQ(solver->setDoubleOptions(result, name, value, 1), 0);
        EXPECT_EQ(result[0], 1);
    }
    {
        // 字符串选项只读：求解器拒绝，accepted 为 0，调用本身成功
        xOptSolver::boolean result[1] = {1};
        const char* name[1] = {"solver_name"};
        const char* value[1] = {"renamed"};
        ASSERT_EQ(solver->setStringOptions(result, name, value, 1), 0);
        EXPECT_EQ(result[0], 0);
        const char* got[1] = {nullptr};
        int n = 1;
        ASSERT_EQ(solver->getStringOptions(name, got, n), 0);
        ASSERT_NE(got[0], nullptr);
        EXPECT_STREQ(got[0], "xOptMINLPSystem#1");  // 服务端给这次实例起的名字
    }

    // 求解：本线程阻塞在远端 Solve 里，期间服务端回调本进程的问题对象。
    const int ret = solver->solve();
    // 罚函数法在这个问题上以 RESULT_FEASIBLE 或 RESULT_OPTIMAL 收尾，都是成功。
    EXPECT_TRUE(ret == xOptSolver::RESULT_OPTIMAL || ret == xOptSolver::RESULT_FEASIBLE)
        << "solve returned " << ret;

    std::vector<double> x(2, 0.0);
    ASSERT_EQ(solver->X(x.data(), 2), 2);
    EXPECT_NEAR(x[0], 1.5, 1e-3);
    EXPECT_NEAR(x[1], 1.5, 1e-3);

    std::vector<double> f(2, 0.0);
    ASSERT_EQ(solver->F(f.data(), 2), 2);
    EXPECT_NEAR(f[0], 4.5, 1e-2);
    EXPECT_NEAR(f[1], 0.0, 1e-3);

    // 解也写回了本地问题（服务端 Solve 末尾的 SetMINLPVariableValues 经
    // 桥接 DLL 里的 servant 落到 mock.setX）——xRto 之后读单元变量靠的就是它。
    double obj = 0;
    ASSERT_EQ(mock.evaluateObjective(obj), 0);
    EXPECT_NEAR(obj, 4.5, 1e-2);

    EXPECT_LT(solver->Xmul(x.data(), 2), 0);  // 罚函数法没有对偶信息

    destroy(solver);
    // 桥接 DLL 里的 ORB 与进程同寿，故意不 FreeLibrary（见 CapeOpenSolverBridge.cpp）。
}

// 继续求解之后桥接 DLL 的 X()/F() 必须刷新：上一次 solve() 抓下来的缓存作废，
// 结果码与解按同一套逻辑重取。服务端换成支持暂停/继续的剧本式求解器
// （solve 停在全 1 并返回 RESULT_USER_PAUSE，continue 推到全 2）。
TEST(XOptMINLPcoSolverIpc, ContinueSolveRefreshesTheBridgeSolution) {
    const std::string dir = exeDir();
    const std::string server = dir + "\\xOptMINLPcoSolverServer.exe";
    const std::string bridge = dir + "\\xRtoCapeOpenSolver.dll";
    const std::string ior_file = dir + "\\xoptminlpco_solver_ipc_pause.ior";

    ServerProcess server_proc(server, MOCK_XOPTSOLVER_DLL, ior_file);
    ASSERT_TRUE(server_proc.started()) << "cannot start the server: " << server;
    std::string err;
    const std::string ior = server_proc.waitForIor(std::chrono::seconds(30), err);
    ASSERT_FALSE(ior.empty()) << err;
    _putenv_s("XRTO_CAPEOPEN_SOLVER_TARGET", ("corba:" + ior).c_str());

    HMODULE lib = LoadLibraryA(bridge.c_str());
    ASSERT_NE(lib, nullptr);
    auto create = reinterpret_cast<CreateSolverFunc>(GetProcAddress(lib, "createSolver"));
    auto destroy = reinterpret_cast<DestroySolverFunc>(GetProcAddress(lib, "destroySolver"));
    ASSERT_NE(create, nullptr);
    ASSERT_NE(destroy, nullptr);

    MockXOptProblem mock;
    xOptSolver* solver = create("FLOWSHEET", &mock, &hostLog);
    ASSERT_NE(solver, nullptr);

    EXPECT_EQ(solver->solve(), xOptSolver::RESULT_USER_PAUSE);
    std::vector<double> x(2, 0.0), f(2, 0.0);
    ASSERT_EQ(solver->X(x.data(), 2), 2);
    EXPECT_DOUBLE_EQ(x[0], 1.0);
    ASSERT_EQ(solver->F(f.data(), 2), 2);
    EXPECT_DOUBLE_EQ(f[0], 2.0);

    EXPECT_EQ(solver->continueSolve(), xOptSolver::RESULT_OPTIMAL);
    ASSERT_EQ(solver->X(x.data(), 2), 2);
    EXPECT_DOUBLE_EQ(x[0], 2.0);
    EXPECT_DOUBLE_EQ(x[1], 2.0);
    ASSERT_EQ(solver->F(f.data(), 2), 2);
    EXPECT_DOUBLE_EQ(f[0], 8.0);
    double obj = 0;
    ASSERT_EQ(mock.evaluateObjective(obj), 0);
    EXPECT_DOUBLE_EQ(obj, 8.0) << "the continued solution must have been written back";

    destroy(solver);
}

// 求解器说成功、却交不出解向量（X() 答 -1）：服务端按 ECapeSolvingError 报，
// 桥接层**不能**拿扩展里存的成功码加本地问题里的初值冒充"成功 + 解"。
// 宿主必须看到 solve() < 0 且 X() < 0。
TEST(XOptMINLPcoSolverIpc, SolverWithoutASolutionVector_IsReportedAsFailure) {
    const std::string dir = exeDir();
    const std::string server = dir + "\\xOptMINLPcoSolverServer.exe";
    const std::string bridge = dir + "\\xRtoCapeOpenSolver.dll";
    const std::string ior_file = dir + "\\xoptminlpco_solver_ipc_failx.ior";

    ServerProcess server_proc(server, MOCK_XOPTSOLVER_DLL, ior_file);
    ASSERT_TRUE(server_proc.started()) << "cannot start the server: " << server;
    std::string err;
    const std::string ior = server_proc.waitForIor(std::chrono::seconds(30), err);
    ASSERT_FALSE(ior.empty()) << err;
    _putenv_s("XRTO_CAPEOPEN_SOLVER_TARGET", ("corba:" + ior).c_str());

    HMODULE lib = LoadLibraryA(bridge.c_str());
    ASSERT_NE(lib, nullptr);
    auto create = reinterpret_cast<CreateSolverFunc>(GetProcAddress(lib, "createSolver"));
    auto destroy = reinterpret_cast<DestroySolverFunc>(GetProcAddress(lib, "destroySolver"));
    ASSERT_NE(create, nullptr);
    ASSERT_NE(destroy, nullptr);

    MockXOptProblem mock;
    xOptSolver* solver = create("FLOWSHEET", &mock, &hostLog);
    ASSERT_NE(solver, nullptr);

    xOptSolver::boolean accepted[1] = {0};
    const char* name[1] = {"fail_x"};
    const int one[1] = {1};
    ASSERT_EQ(solver->setIntOptions(accepted, name, one, 1), 0);
    ASSERT_EQ(accepted[0], 1);

    EXPECT_LT(solver->solve(), 0) << "no authoritative solution must never read as success";
    std::vector<double> x(2, 7.0);
    EXPECT_LT(solver->X(x.data(), 2), 0) << "X() must not hand out the problem's stale values";
    EXPECT_DOUBLE_EQ(x[0], 7.0);  // 缓冲没被碰
    std::vector<double> f(2, 7.0);
    EXPECT_LT(solver->F(f.data(), 2), 0);

    // 同一个求解器把开关关掉之后又能正常给解：失败状态不粘连
    const int zero[1] = {0};
    ASSERT_EQ(solver->setIntOptions(accepted, name, zero, 1), 0);
    EXPECT_EQ(solver->solve(), xOptSolver::RESULT_USER_PAUSE);
    ASSERT_EQ(solver->X(x.data(), 2), 2);
    EXPECT_DOUBLE_EQ(x[0], 1.0);

    destroy(solver);
}

// 没配连接目标时 createSolver 必须失败，并且是在 createSolver 就失败。
// 静默连上一个默认值（或返回一个不能用的求解器）会让 xRto 在 solve 时才炸，
// 而且炸得不知所云。
TEST(XOptMINLPcoSolverIpc, NoTargetConfigured_CreateSolverFails) {
    const std::string bridge = exeDir() + "\\xRtoCapeOpenSolver.dll";
    _putenv_s("XRTO_CAPEOPEN_SOLVER_TARGET", "");
    HMODULE lib = LoadLibraryA(bridge.c_str());
    ASSERT_NE(lib, nullptr);
    auto create = reinterpret_cast<CreateSolverFunc>(GetProcAddress(lib, "createSolver"));
    ASSERT_NE(create, nullptr);
    MockXOptProblem mock;
    // 构建目录里没有 xRtoCapeOpenSolver.target；若哪天有人放了一个，这条会失败，
    // 那也正是它该提醒的事。
    EXPECT_EQ(create("FLOWSHEET", &mock, &hostLog), nullptr);
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
