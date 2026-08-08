// ***************************************************************
//  test_xoptminlpco_corba_naming   version:  1.0   -  date:  2026/08/08
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  Naming Service 端到端冒烟（issue #6）：
//
//    [naming 进程]  tao_cosnaming.exe -o <ior>
//    [server 进程]  xOptMINLPcoCorbaServer --name <名字>
//                     -ORBInitRef NameService=corbaloc:iiop:<addr>/NameService
//    [本进程]       CapeBackendFactory("corba:corbaname:...#<名字>")
//                     → string_to_object 解析名字 → _narrow → 驱动对拍
//
//  与 test_xoptminlpco_corba_ipc.cpp 的区别只有一处：那条用 IOR 直连，这条经名字
//  解析。之所以要单独一条，是因为「名字能被解析到」是 --name 唯一真正的产出——
//  绑定代码写没写对，只有真跑一次 naming 才知道。
//
//  naming 起在**固定端口**上：客户端这一侧没法传 -ORBInitRef，因为消费端
//  CapeMINLPModelCorba::connect() 调的是 ORB_init(0, nullptr)。固定端口后
//  corbaname::host:port#name 自带地址，不依赖客户端 ORB 的初始引用配置。
//
//  自带 main。
// ***************************************************************
// ACE/TAO 必须最先包含（winsock2 先于 windows.h），理由见 test_..._corba_ipc.cpp。
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

// 进程拉起只有 Win32 实现；CMake 在非 WIN32 上不会注册本用例（CORBA 开关强制 OFF），
// 但那个前提隔着一个文件，写在这里让将来在 POSIX 上开 CORBA 时得到编译期错误。
#ifndef _WIN32
#    error "test_xoptminlpco_corba_naming needs a POSIX spawn/kill path before it can run here"
#endif

namespace {

std::string exeDir() {
    char buf[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    const std::string p(buf, n);
    const size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

// vcpkg 三元组目录由 CMake 以 -D 传进来（naming service 在 <triplet>/tools/ace/）。
std::string namingServiceExe() { return std::string(XRTO_TAO_TOOLS_DIR) + "\\tao_cosnaming.exe"; }

// GenerateConsoleCtrlEvent 只能打给与调用方共用控制台的进程。CI 里测试进程常常
// 根本没有控制台（被服务或无窗口的 runner 拉起），那样滚动重启用例只能 SKIP——
// 比静默假绿好，但等于这条保护在 CI 上不生效。没有就自己开一个，让覆盖稳定下来。
void ensureConsole() {
    if (GetConsoleWindow() != nullptr) return;
    AllocConsole();
}

class Proc {
  public:
    Proc() = default;
    ~Proc() { stop(); }

    // own_group=true 时用 CREATE_NEW_PROCESS_GROUP 且**不加** CREATE_NO_WINDOW：
    // GenerateConsoleCtrlEvent 只能打给与调用方共用控制台的进程，而 CREATE_NO_WINDOW
    // 会让子进程自带一个新控制台，于是信号送不到。代价是子进程的输出会混进测试控制台。
    bool start(const std::string& cmdline, bool own_group = false) {
        std::string line = cmdline;
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        const DWORD flags = own_group ? CREATE_NEW_PROCESS_GROUP : CREATE_NO_WINDOW;
        started_ = CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, flags, nullptr,
                                  nullptr, &si, &pi_) != 0;
        own_group_ = own_group;
        return started_;
    }

    // 发 CTRL_BREAK 让它走正常停机路径（解绑等清理都在那条路上）。
    // 成功返回 true；触发不了时返回 false，由调用方决定是 SKIP 还是强杀——
    // 绝不能在这里默默 Terminate 后当作「优雅退出过了」。
    bool stopGracefully(DWORD wait_ms) {
        if (!started_ || !own_group_) return false;
        if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi_.dwProcessId)) return false;
        return WaitForSingleObject(pi_.hProcess, wait_ms) == WAIT_OBJECT_0;
    }
    void stop() {
        if (!started_) return;
        TerminateProcess(pi_.hProcess, 0);
        WaitForSingleObject(pi_.hProcess, 5000);
        CloseHandle(pi_.hThread);
        CloseHandle(pi_.hProcess);
        started_ = false;
    }
    bool alive() const {
        if (!started_) return false;
        DWORD code = 0;
        return GetExitCodeProcess(pi_.hProcess, &code) && code == STILL_ACTIVE;
    }
    DWORD exitCode() const {
        DWORD code = 0;
        GetExitCodeProcess(pi_.hProcess, &code);
        return code;
    }

  private:
    bool started_ = false;
    bool own_group_ = false;
    PROCESS_INFORMATION pi_{};
};

// 轮询文件出现且内容以 prefix 开头；进程中途死掉就立即失败（带退出码），
// 不干等到超时——「naming 退出码 N」远比「30 秒超时」好定位。
std::string waitForFile(const std::string& path, const char* prefix, const Proc& p,
                        std::chrono::seconds timeout, std::string& error_out) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!p.alive()) {
            error_out = "进程提前退出, 退出码 " + std::to_string(p.exitCode());
            return {};
        }
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            const std::string s = ss.str();
            if (s.rfind(prefix, 0) == 0) return s;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    error_out = "等待超时: " + path;
    return {};
}

TEST(XOptMINLPcoCorbaNaming, ResolvesThroughTheNamingService) {
    const std::string dir = exeDir();
    const std::string ns_ior = dir + "\\ns.ior";
    const std::string srv_ior = dir + "\\named.ior";
    const std::string name = "xOpt/MINLP";
    // 固定端口：客户端这一侧没法传 -ORBInitRef（消费端调的是 ORB_init(0, nullptr)），
    // 所以让 corbaname::host:port#name 自带地址，不依赖客户端 ORB 的初始引用配置。
    const std::string ns_addr = "localhost:24567";
    std::remove(ns_ior.c_str());
    std::remove(srv_ior.c_str());

    // 1) naming service
    Proc ns;
    ASSERT_TRUE(ns.start("\"" + namingServiceExe() + "\" -ORBEndpoint iiop://" + ns_addr +
                         " -o \"" + ns_ior + "\""))
        << "起不来 naming service: " << namingServiceExe();
    std::string err;
    const std::string ns_ref = waitForFile(ns_ior, "IOR:", ns, std::chrono::seconds(30), err);
    ASSERT_FALSE(ns_ref.empty()) << err;

    // 2) server，绑进 naming
    Proc srv;
    const std::string cmd = "\"" + dir + "\\xOptMINLPcoCorbaServer.exe\"" +
                            " --problem-dll \"" + dir + "\\mock_xoptproblem.dll\"" +
                            " --ior-file \"" + srv_ior + "\"" +
                            " --name " + name +
                            " -ORBInitRef NameService=corbaloc:iiop:" + ns_addr + "/NameService";
    ASSERT_TRUE(srv.start(cmd)) << "起不来 server";
    ASSERT_FALSE(waitForFile(srv_ior, "IOR:", srv, std::chrono::seconds(30), err).empty()) << err;

    // 3) 经**名字**连回来——本用例的全部意义在这一步。
    //    corbaname 的 URL 形式：corbaname:<addr>#<名字>
    CapeRegisterCorbaBackend();
    const std::string target = "corba:corbaname::" + ns_addr + "#" + name;

    std::string ferr;
    std::unique_ptr<ICapeMINLPModel> consumer = CapeBackendFactory::instance().create(target, ferr);
    ASSERT_NE(consumer, nullptr) << ferr;
    ASSERT_EQ(consumer->connect(), 0) << consumer->lastError();

    CapeMINLPSize s;
    ASSERT_EQ(consumer->getSize(s), 0) << consumer->lastError();
    EXPECT_EQ(s.num_variables, 2);
    EXPECT_EQ(s.num_constraints, 1);

    std::vector<std::string> names;
    ASSERT_EQ(consumer->getVariableNames({0, 1}, names), 0) << consumer->lastError();
    EXPECT_EQ(names, (std::vector<std::string>{"x0", "x1"}));

    ASSERT_EQ(consumer->setVariableValues({0, 1}, {3.0, 4.0}), 0) << consumer->lastError();
    double obj = 0;
    ASSERT_EQ(consumer->getObjectiveValue(obj), 0) << consumer->lastError();
    EXPECT_DOUBLE_EQ(obj, 25.0);

    consumer->disconnect();
    srv.stop();
    ns.stop();
    std::remove(ns_ior.c_str());
    std::remove(srv_ior.c_str());
}

// 滚动重启：新实例 rebind 抢走名字后，**旧实例停机不得把它删掉**。
//
// 这是评审（@chatgpt-codex-connector，P1）指出的：旧实例原先无条件 unbind，
// 删掉的是新实例那份活的注册，于是两个都解析不到，而且悄无声息。
// 单实例的用例覆盖不到——必须真让两个进程重叠一次。
TEST(XOptMINLPcoCorbaNaming, ShutdownKeepsAReplacementsBinding) {
    const std::string dir = exeDir();
    const std::string ns_ior = dir + "\\ns2.ior";
    const std::string ior_a = dir + "\\a.ior";
    const std::string ior_b = dir + "\\b.ior";
    const std::string name = "xOpt/Rolling";
    const std::string ns_addr = "localhost:24568";
    for (const auto* f : {&ns_ior, &ior_a, &ior_b}) std::remove(f->c_str());

    Proc ns;
    ASSERT_TRUE(ns.start("\"" + namingServiceExe() + "\" -ORBEndpoint iiop://" + ns_addr +
                         " -o \"" + ns_ior + "\""));
    std::string err;
    ASSERT_FALSE(waitForFile(ns_ior, "IOR:", ns, std::chrono::seconds(30), err).empty()) << err;

    const std::string init_ref =
        " -ORBInitRef NameService=corbaloc:iiop:" + ns_addr + "/NameService";
    auto serverCmd = [&](const std::string& ior_file) {
        return "\"" + dir + "\\xOptMINLPcoCorbaServer.exe\"" + " --problem-dll \"" + dir +
               "\\mock_xoptproblem.dll\"" + " --ior-file \"" + ior_file + "\" --name " + name +
               init_ref;
    };

    // A 要能优雅停机（解绑逻辑在那条路径上），故给它自己的进程组；
    // 并确保本进程有控制台，否则 CTRL_BREAK 送不到、这条只能 SKIP。
    ensureConsole();
    Proc a;
    ASSERT_TRUE(a.start(serverCmd(ior_a), /*own_group*/ true));
    ASSERT_FALSE(waitForFile(ior_a, "IOR:", a, std::chrono::seconds(30), err).empty()) << err;

    // B 起来并 rebind，抢走名字
    Proc b;
    ASSERT_TRUE(b.start(serverCmd(ior_b)));
    const std::string b_ior = waitForFile(ior_b, "IOR:", b, std::chrono::seconds(30), err);
    ASSERT_FALSE(b_ior.empty()) << err;

    // A 走**正常停机**路径——解绑逻辑只在那条路上，Terminate 会让本用例空过。
    if (!a.stopGracefully(10000)) {
        a.stop();
        b.stop();
        ns.stop();
        GTEST_SKIP() << "触发不了 A 的优雅停机（CTRL_BREAK 未送达，连 AllocConsole 也没救回来），"
                        "本用例无法验证解绑的所有权判断——不做无效断言";
    }

    // 名字必须仍然可解析，且指向 B
    CapeRegisterCorbaBackend();
    std::string ferr;
    std::unique_ptr<ICapeMINLPModel> consumer = CapeBackendFactory::instance().create(
        "corba:corbaname::" + ns_addr + "#" + name, ferr);
    ASSERT_NE(consumer, nullptr) << ferr;
    EXPECT_EQ(consumer->connect(), 0)
        << "旧实例停机后名字解析不到了——它把新实例的绑定删掉了: " << consumer->lastError();

    consumer->disconnect();
    b.stop();
    ns.stop();
    for (const auto* f : {&ns_ior, &ior_a, &ior_b}) std::remove(f->c_str());
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
