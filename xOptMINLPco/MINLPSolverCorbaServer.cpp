// ***************************************************************
//  MINLPSolverCorbaServer   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  独立进程 CORBA server：把一个 xOptSolver DLL 发布成 CAPE-OPEN 的
//  ICapeMINLPSolverManager。与 MINLPCorbaServer（发布**问题**）互为镜像：
//
//    xOptMINLPcoCorbaServer        问题在这边，求解器在远端  —— 远端调我们
//    xOptMINLPcoSolverServer（本） 求解器在这边，问题在远端  —— 我们调远端
//
//  用法：
//    xOptMINLPcoSolverServer --solver-dll <path> [--ior-file <path>] [--name <path>]
//                            [-ORBEndpoint iiop://host:port] [-ORBInitRef ...]
//
//  发布一个 ICapeMINLPSolverManager。客户端拿着自己的 ICapeMINLP 调
//  CreateMINLPSystem，得到一个 ICapeMINLPSystem（+ XOPTCO 扩展），然后 Solve。
//  求解期间本进程回调客户端的问题——客户端必须能在等待 Solve 返回的同时受理
//  请求（xRtoCapeOpenSolver.dll 靠 TAO 的嵌套上行调用做到这一点）。
//
//  --solver-dll 也可用环境变量 XRTO_XOPT_SOLVER_DLL 指定（命令行优先）。
//  IOR 同时打到 stdout 和 --ior-file（原子写）；--name 把引用绑进 Naming
//  Service，与 MINLPCorbaServer 的语义一字不差（CorbaPublish.h）。
//
//  求解器的日志（xOptLogFunc）打到本进程的 stderr：那是求解器 DLL 唯一的
//  输出口，也是排查"远端为什么解不出来"时该看的地方。
// ***************************************************************
// ACE/TAO 必须最先包含：它拉 winsock2.h，而 <windows.h> 默认拉 winsock.h(v1)。
#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "CorbaPublish.h"
#include "SolverServant.h"

namespace {

// 控制台输出一律 ASCII（理由见 CorbaPublish.h）。
void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " --solver-dll <path> [--ior-file <path>] [--name <path>] [-ORB<...>]\n"
              << "  --solver-dll <path>   xOptSolver DLL to publish (exports createSolver /"
              << " destroySolver)\n"
              << "                        (or set env XRTO_XOPT_SOLVER_DLL)\n"
              << "  --ior-file <path>     write the ICapeMINLPSolverManager IOR here (atomically);"
              << " stdout only if omitted\n"
              << "  --name <path>         also bind into the Naming Service; path is '/'-separated,\n"
              << "                        clients then use corbaname::host:port#<path> ('#', not '/')\n"
              << "                        needs -ORBInitRef NameService=corbaloc:iiop:host:port/NameService\n"
              << "  -ORB* args (e.g. -ORBEndpoint iiop://host:port) are consumed by ORB_init\n";
}

const char* levelTag(ZLOG_LEVEL level) {
    switch (level) {
        case ZLOG_TRACE: return "TRACE";
        case ZLOG_DEBUG: return "DEBUG";
        case ZLOG_INFOR: return "INFO";
        case ZLOG_WARNI: return "WARN";
        case ZLOG_ERROR: return "ERROR";
        case ZLOG_FATAL: return "FATAL";
        default: return "LOG";
    }
}

// 交给 createSolver 的日志回调：求解器的每一行都进 stderr。
void solverLog(ZLOG_LEVEL level, const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format != nullptr ? format : "", args);
    va_end(args);
    std::fprintf(stderr, "[solver %s] %s\n", levelTag(level), buf);
    std::fflush(stderr);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace xoptco_publish;

    std::string solver_dll;
    std::string ior_file;
    std::string bind_path;

    try {
        // ORB_init 先跑：它会就地摘掉 -ORB* 参数，剩下的才轮到我们解析。
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--solver-dll" && i + 1 < argc) {
                solver_dll = argv[++i];
            } else if (a == "--ior-file" && i + 1 < argc) {
                ior_file = argv[++i];
            } else if (a == "--name" && i + 1 < argc) {
                bind_path = argv[++i];
            } else if (a == "--help" || a == "-h") {
                usage(argv[0]);
                return 0;
            } else {
                std::cerr << "unrecognized argument: " << a << "\n";
                usage(argv[0]);
                return 2;
            }
        }
        if (solver_dll.empty()) {
            if (const char* env = std::getenv("XRTO_XOPT_SOLVER_DLL")) solver_dll = env;
        }
        if (solver_dll.empty()) {
            std::cerr << "no solver DLL given (--solver-dll or XRTO_XOPT_SOLVER_DLL)\n";
            usage(argv[0]);
            return 2;
        }

        CORBA::Object_var poa_obj = orb->resolve_initial_references("RootPOA");
        PortableServer::POA_var poa = PortableServer::POA::_narrow(poa_obj.in());
        if (CORBA::is_nil(poa.in())) {
            std::cerr << "resolve_initial_references(\"RootPOA\") failed\n";
            return 3;
        }
        poa->the_POAManager()->activate();

        // 求解器 DLL 在发布之前就加载：加载失败是配置错误，得在有人连上来
        // 之前报出来，而不是等第一个 CreateMINLPSystem 才炸。
        XOptSolverLibrary library(solver_dll);
        if (library.load() < 0) {
            std::cerr << "solver library not ready: " << library.lastError() << "\n";
            return 4;
        }

        SolverManagerServant* manager = new SolverManagerServant(&library, poa.in(), &solverLog);
        PortableServer::ServantBase_var manager_owner(manager);  // 出作用域 _remove_ref
        PortableServer::ObjectId_var oid = poa->activate_object(manager);
        CORBA::Object_var ref = poa->id_to_reference(oid.in());
        CORBA::String_var ior = orb->object_to_string(ref.in());

        // 先绑名字，再写 IOR 文件（理由见 MINLPCorbaServer.cpp / CorbaPublish.h）。
        if (!bind_path.empty() && !bindName(orb.in(), bind_path, ref.in())) {
            std::cerr << "failed to bind into the Naming Service: " << bind_path << "\n";
            return 7;
        }
        if (!ior_file.empty() && !writeIorFileAtomically(ior_file, ior.in())) {
            std::cerr << "failed to write the IOR file: " << ior_file << "\n";
            return 5;
        }

        std::cout << ior.in() << std::endl;
        std::cerr << "xOptMINLPcoSolverServer: ICapeMINLPSolverManager published for "
                  << solver_dll << ", serving"
                  << (ior_file.empty() ? "" : (" (IOR written to " + ior_file + ")"))
                  << (bind_path.empty() ? "" : (" (bound as " + bind_path + ")")) << "\n";

        installStopHandlers();
        runUntilStopped(orb.in());

        std::cerr << "xOptMINLPcoSolverServer: shutting down\n";
        // 先解绑再停服；deactivate_object 必须在 shutdown 之前（反过来抛 BAD_INV_ORDER）。
        if (!bind_path.empty()) unbindName(orb.in(), bind_path, ref.in(), "xOptMINLPcoSolverServer");
        poa->deactivate_object(oid.in());
        orb->shutdown(/*wait_for_completion*/ false);
        orb->destroy();
        return 0;
    } catch (const CORBA::Exception& e) {
        std::cerr << "CORBA exception: " << e._name() << "\n";
        return 6;
    } catch (const std::exception& e) {
        std::cerr << "exception: " << e.what() << "\n";
        return 6;
    }
}
