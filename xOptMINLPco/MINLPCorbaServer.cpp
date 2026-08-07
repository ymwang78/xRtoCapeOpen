// ***************************************************************
//  MINLPCorbaServer   version:  1.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  N4：独立进程 CORBA server。把 MINLPServant 激活在 POA 里，把对象引用以
//  IOR 字符串发布出去，然后 orb->run() 等远端调用。
//
//  为什么 CORBA 必须是独立 exe，而 COM 一个 DLL 就够：
//    COM 有进程内激活——客户端 CoCreateInstance，COM 运行时按注册表把
//    xOptMINLPco.dll 加载进【客户端进程】。CORBA 没有等价物，没有
//    「注册表 + DllGetClassObject」这条路。客户端够到 servant 只有两条：
//      1) 同进程 collocated（tests/test_xoptminlpco_corba.cpp 那条）——
//         TAO 直接短路掉网络，验证 servant 逻辑，但对互操作性零证明；
//      2) 经 IIOP 连到一个真在跑 ORB 的进程——就是本文件。
//    外部 PME / 第三方 ORB 只能走 (2)。
//
//  用法：
//    xOptMINLPcoCorbaServer --problem-dll <path> [--ior-file <path>]
//                           [-ORBEndpoint iiop://host:port] ...
//  -ORB* 参数由 ORB_init 消费，其余由本文件解析。被包装的 xOptProblem DLL
//  也可用环境变量 XRTO_XOPT_PROBLEM_DLL 指定（--problem-dll 优先）。
//
//  IOR 同时打到 stdout 和 --ior-file。文件写入是原子的（先写 .tmp 再改名），
//  否则轮询这个文件的客户端会读到半截 IOR。
//
//  注：Naming Service 绑定（corbaname:）尚未做——它要求另跑一个 naming 进程
//  才有意义、也才测得了，故与本次分开。IOR 这条路不依赖任何外部服务。
// ***************************************************************
#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "MINLPServant.h"

namespace {

CORBA::ORB_ptr g_orb = CORBA::ORB::_nil();  // 供信号处理器 shutdown 用

void onSignal(int) {
    // 只做异步信号安全的最小动作：请求 ORB 退出事件循环，清理交给 main。
    if (!CORBA::is_nil(g_orb)) g_orb->shutdown(/*wait_for_completion*/ false);
}

// 控制台输出一律 ASCII：本 exe 是拿去给第三方演示的，源码是 UTF-8，而 Windows
// 控制台默认 GBK 代码页——中文在那里会变成乱码，正好毁掉演示。注释仍用中文。
void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " --problem-dll <path> [--ior-file <path>] [-ORB<...>]\n"
              << "  --problem-dll <path>  xOptProblem DLL to wrap"
              << " (or set env XRTO_XOPT_PROBLEM_DLL)\n"
              << "  --ior-file <path>     write the IOR to this file (atomically);"
              << " stdout only if omitted\n"
              << "  -ORB* args (e.g. -ORBEndpoint iiop://host:port) are consumed by ORB_init\n";
}

void setProblemDllEnv(const std::string& path) {
#ifdef _WIN32
    _putenv_s("XRTO_XOPT_PROBLEM_DLL", path.c_str());
#else
    setenv("XRTO_XOPT_PROBLEM_DLL", path.c_str(), 1);
#endif
}

// 原子发布：先写临时文件再改名，读者要么看到旧内容要么看到完整新内容，
// 不会读到写了一半的 IOR。
bool writeIorFileAtomically(const std::string& path, const char* ior) {
    const std::string tmp = path + ".tmp";
    FILE* fp = std::fopen(tmp.c_str(), "wb");
    if (fp == nullptr) return false;
    const size_t len = std::strlen(ior);
    const bool written = std::fwrite(ior, 1, len, fp) == len;
    const bool closed = std::fclose(fp) == 0;
    if (!written || !closed) {
        std::remove(tmp.c_str());
        return false;
    }
    std::remove(path.c_str());  // Windows 的 rename 不覆盖已存在目标
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string problem_dll;
    std::string ior_file;

    try {
        // ORB_init 先跑：它会就地摘掉 -ORB* 参数，剩下的才轮到我们解析。
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);
        g_orb = orb.in();

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--problem-dll" && i + 1 < argc) {
                problem_dll = argv[++i];
            } else if (a == "--ior-file" && i + 1 < argc) {
                ior_file = argv[++i];
            } else if (a == "--help" || a == "-h") {
                usage(argv[0]);
                return 0;
            } else {
                std::cerr << "unrecognized argument: " << a << "\n";
                usage(argv[0]);
                return 2;
            }
        }

        if (!problem_dll.empty()) setProblemDllEnv(problem_dll);

        CORBA::Object_var poa_obj = orb->resolve_initial_references("RootPOA");
        PortableServer::POA_var poa = PortableServer::POA::_narrow(poa_obj.in());
        if (CORBA::is_nil(poa.in())) {
            std::cerr << "resolve_initial_references(\"RootPOA\") failed\n";
            return 3;
        }
        poa->the_POAManager()->activate();

        // 生产构造：读 XRTO_XOPT_PROBLEM_DLL，建 XOptMINLPAdapter 并 connect。
        MINLPServant* servant = new MINLPServant();
        PortableServer::ServantBase_var servant_owner(servant);  // 出作用域 _remove_ref
        if (!servant->ok()) {
            std::cerr << "servant not ready: the xOptProblem DLL was not given or failed to load"
                      << " (--problem-dll / XRTO_XOPT_PROBLEM_DLL)\n";
            return 4;
        }

        PortableServer::ObjectId_var oid = poa->activate_object(servant);
        CORBA::Object_var ref = poa->id_to_reference(oid.in());
        CORBA::String_var ior = orb->object_to_string(ref.in());

        if (!ior_file.empty() && !writeIorFileAtomically(ior_file, ior.in())) {
            std::cerr << "failed to write the IOR file: " << ior_file << "\n";
            return 5;
        }

        // stdout 只放 IOR 本身，方便 `server --ior-file x` 之外直接管道取用。
        std::cout << ior.in() << std::endl;
        std::cerr << "xOptMINLPcoCorbaServer: ICapeMINLP published, serving"
                  << (ior_file.empty() ? "" : (" (IOR written to " + ior_file + ")")) << "\n";

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        orb->run();

        // 走到这里说明 shutdown 被调用过：先摘 servant 再销毁 ORB。
        poa->deactivate_object(oid.in());
        orb->destroy();
        g_orb = CORBA::ORB::_nil();
        return 0;
    } catch (const CORBA::Exception& e) {
        std::cerr << "CORBA exception: " << e._name() << "\n";
        return 6;
    } catch (const std::exception& e) {
        std::cerr << "exception: " << e.what() << "\n";
        return 6;
    }
}
