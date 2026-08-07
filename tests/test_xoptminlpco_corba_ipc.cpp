// ***************************************************************
//  test_xoptminlpco_corba_ipc   version:  1.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  N4：真·跨进程 CORBA 冒烟。与 test_xoptminlpco_corba.cpp（collocated，
//  TAO 短路掉网络）互补——这里 servant 在【另一个进程】里，调用真的走 IIOP。
//
//    [server 进程] mock_xoptproblem.dll → XOptMINLPAdapter → MINLPServant
//                    → POA → IOR 文件
//    [本进程]      读 IOR → CapeBackendFactory("corba:IOR:...")
//                    → CapeMINLPModelCorba::connect()（string_to_object + _narrow）
//                    → 驱动并与 mock 的解析值对拍
//
//  同时验掉 design §3.2 挂着的 production 连接串路径：collocated 测试走的是
//  注入构造，从不碰 string_to_object，也就从不验证 IOR 解析和 _narrow。
//  自带 main。
// ***************************************************************
// ACE/TAO 必须最先包含：它拉的是 winsock2.h，而 <windows.h> 默认拉 winsock.h(v1)，
// 两者冲突（IPPROTO_IPV6/timeval 一片重定义）。winsock2.h 会定义 _WINSOCKAPI_ 把
// windows.h 里的 v1 关掉，所以这个顺序不能反。
#include "backend/corba/CapeMINLPModelCorba.h"

#include "CapeBackendFactory.h"

#include <gtest/gtest.h>

#ifdef _WIN32
#    include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

void CapeRegisterCorbaBackend();  // backend/corba/CapeRegisterCorbaBackend.cpp

namespace {

std::string exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    const std::string p(buf, n);
    const size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
#else
    return ".";
#endif
}

// 起 server 进程、等它把 IOR 落盘、退出时收尸。
class ServerProcess {
  public:
    ServerProcess(const std::string& exe, const std::string& problem_dll,
                  const std::string& ior_file)
        : ior_file_(ior_file) {
        std::remove(ior_file_.c_str());  // 别读到上一轮的陈旧 IOR
#ifdef _WIN32
        std::ostringstream cmd;
        cmd << '"' << exe << "\" --problem-dll \"" << problem_dll << "\" --ior-file \"" << ior_file
            << '"';
        std::string line = cmd.str();
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        started_ = CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                  nullptr, nullptr, &si, &pi_) != 0;
#endif
    }

    ~ServerProcess() {
#ifdef _WIN32
        if (started_) {
            TerminateProcess(pi_.hProcess, 0);
            WaitForSingleObject(pi_.hProcess, 5000);
            CloseHandle(pi_.hThread);
            CloseHandle(pi_.hProcess);
        }
#endif
        std::remove(ior_file_.c_str());
    }

    bool started() const { return started_; }

    // 轮询 IOR 文件；server 若中途死掉就立刻带着退出码失败，而不是干等到超时——
    // 「server 退出码 4」比「30 秒超时」好定位得多。
    std::string waitForIor(std::chrono::seconds timeout, std::string& error_out) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
            DWORD code = 0;
            if (GetExitCodeProcess(pi_.hProcess, &code) && code != STILL_ACTIVE) {
                error_out = "server 提前退出, 退出码 " + std::to_string(code);
                return {};
            }
#endif
            std::ifstream in(ior_file_, std::ios::binary);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                const std::string ior = ss.str();
                if (ior.rfind("IOR:", 0) == 0) return ior;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        error_out = "等待 IOR 文件超时: " + ior_file_;
        return {};
    }

  private:
    std::string ior_file_;
    bool started_ = false;
#ifdef _WIN32
    PROCESS_INFORMATION pi_{};
#endif
};

TEST(XOptMINLPcoCorbaIpc, CrossProcessRoundTripThroughIor) {
    const std::string dir = exeDir();
    const std::string server = dir + "\\xOptMINLPcoCorbaServer.exe";
    const std::string mock_dll = dir + "\\mock_xoptproblem.dll";
    const std::string ior_file = dir + "\\xoptminlpco_ipc.ior";

    ServerProcess server_proc(server, mock_dll, ior_file);
    ASSERT_TRUE(server_proc.started()) << "起不来 server: " << server;

    std::string err;
    const std::string ior = server_proc.waitForIor(std::chrono::seconds(30), err);
    ASSERT_FALSE(ior.empty()) << err;

    // 走 production 连接串路径：scheme 解析 → string_to_object → _narrow。
    CapeRegisterCorbaBackend();
    std::string factory_err;
    std::unique_ptr<ICapeMINLPModel> consumer =
        CapeBackendFactory::instance().create("corba:" + ior, factory_err);
    ASSERT_NE(consumer, nullptr) << factory_err;
    ASSERT_EQ(consumer->connect(), 0) << consumer->lastError();

    // 以下每一次调用都真的过了一遍 GIOP。
    CapeMINLPSize s;
    ASSERT_EQ(consumer->getSize(s), 0) << consumer->lastError();
    EXPECT_EQ(s.num_variables, 2);
    EXPECT_EQ(s.num_constraints, 1);
    EXPECT_EQ(s.num_nonlinear_jacobian_nz, 2);

    std::vector<std::string> names;
    ASSERT_EQ(consumer->getVariableNames({0, 1}, names), 0) << consumer->lastError();
    EXPECT_EQ(names, (std::vector<std::string>{"x0", "x1"}));

    std::vector<double> lb, ub;
    ASSERT_EQ(consumer->getVariableBounds({0, 1}, lb, ub), 0) << consumer->lastError();
    EXPECT_DOUBLE_EQ(lb[0], -10.0);
    EXPECT_DOUBLE_EQ(ub[1], 10.0);

    ASSERT_EQ(consumer->setVariableValues({0, 1}, {3.0, 4.0}), 0) << consumer->lastError();
    double objv = 0;
    ASSERT_EQ(consumer->getObjectiveValue(objv), 0) << consumer->lastError();
    EXPECT_DOUBLE_EQ(objv, 25.0);  // 3^2 + 4^2

    std::vector<double> cons;
    ASSERT_EQ(consumer->getNonlinearConstraintValues({0}, cons), 0) << consumer->lastError();
    EXPECT_DOUBLE_EQ(cons[0], 4.0);  // 3 + 4 - 3

    std::vector<double> grad;
    ASSERT_EQ(consumer->getObjectiveDerivativeValues("Nonlinear", grad), 0)
        << consumer->lastError();
    EXPECT_EQ(grad, (std::vector<double>{6.0, 8.0}));

    std::vector<double> jac;
    ASSERT_EQ(consumer->getConstraintDerivativeValues("Jacobian", {0}, jac), 0)
        << consumer->lastError();
    EXPECT_EQ(jac, (std::vector<double>{1.0, 1.0}));

    consumer->disconnect();
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
