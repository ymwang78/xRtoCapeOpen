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
//  --name 另把引用绑进 Naming Service，客户端即可用可读的
//  corbaname::host:port#<名字> 连接（issue #6）——分隔符是 '#'，不是 '/'。
//  IOR 那条路不依赖任何外部服务，--name 则要求另跑一个 naming 进程。
// ***************************************************************
// ACE/TAO 必须最先包含：它拉 winsock2.h，而 <windows.h> 默认拉 winsock.h(v1)，
// 顺序反了会炸出 IPPROTO_IPV6/timeval 一片重定义。
#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>
#include <orbsvcs/CosNamingC.h>  // --name：把引用绑进 Naming Service

#ifdef _WIN32
#    include <windows.h>  // MoveFileExA
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "MINLPServant.h"

namespace {

// 信号处理器里能做的只有「置一个标志」。原先这里直接调 orb->shutdown()，
// 那不是 async-signal-safe——信号可能恰好打断 ORB 自己的运行时状态，
// 于是 shutdown 在半路的数据结构上跑，轻则死锁重则崩。
// 真正的退出由主线程完成：主线程带超时轮转事件循环，看见标志就出来。
volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) { g_stop = 1; }

#ifdef _WIN32
// Windows 上光靠 std::signal 是够不着的：这个系统没有 SIGTERM（CRT 里那个常量
// 基本不会被真正投递），SIGINT 也只在进程有控制台、且经 CRT 的控制台处理时才到。
// 实测 taskkill 打过来时进程纹丝不动，得靠控制台控制处理器。
// 它跑在另一个线程而非信号上下文里，但「只置标志」依然是这里该做的最小动作——
// 换成直接 shutdown 就又回到了 ORB 状态被并发闯入的老问题。
BOOL WINAPI onConsoleCtrl(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_stop = 1;
            return TRUE;
        default:
            return FALSE;
    }
}
#endif

// 控制台输出一律 ASCII：本 exe 是拿去给第三方演示的，源码是 UTF-8，而 Windows
// 控制台默认 GBK 代码页——中文在那里会变成乱码，正好毁掉演示。注释仍用中文。
void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " --problem-dll <path> [--ior-file <path>] [--name <path>] [-ORB<...>]\n"
              << "  --problem-dll <path>  xOptProblem DLL to wrap"
              << " (or set env XRTO_XOPT_PROBLEM_DLL)\n"
              << "  --ior-file <path>     write the IOR to this file (atomically);"
              << " stdout only if omitted\n"
              << "  --name <path>         also bind into the Naming Service; path is '/'-separated,\n"
              << "                        clients then use corbaname::host:port#<path> ('#', not '/')\n"
              << "                        needs -ORBInitRef NameService=corbaloc:iiop:host:port/NameService\n"
              << "  -ORB* args (e.g. -ORBEndpoint iiop://host:port) are consumed by ORB_init\n";
}

void setProblemDllEnv(const std::string& path) {
#ifdef _WIN32
    _putenv_s("XRTO_XOPT_PROBLEM_DLL", path.c_str());
#else
    setenv("XRTO_XOPT_PROBLEM_DLL", path.c_str(), 1);
#endif
}

// —— Naming Service（--name，issue #6）——
//
// 名字按 '/' 拆成多级 CosNaming::Name，中间各级用 bind_new_context 建出来
// （已存在则忽略 AlreadyBound），最后一级 rebind 绑对象引用。
//
// 用 rebind 而不是 bind：服务端重启时名字多半还在，bind 会抛 AlreadyBound，
// 于是「重启一次就再也绑不上」——这正是运维最不想要的失败方式。
// rebind 覆盖旧的那个（多半指向已死进程）。
CosNaming::Name toName(const std::string& path) {
    CosNaming::Name n;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        const std::string part =
            (slash == std::string::npos) ? path.substr(start) : path.substr(start, slash - start);
        if (!part.empty()) {
            n.length(n.length() + 1);
            n[n.length() - 1].id = CORBA::string_dup(part.c_str());
            n[n.length() - 1].kind = CORBA::string_dup("");
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return n;
}

// resolve_initial_references 会抛（NameService 没配时 ORB::InvalidName，
// naming 连不上时 TRANSIENT）。这里吞掉换成 nil，是为了让调用方能给出那句
// 有用的「pass -ORBInitRef ...」提示——放任它冒到 main 的通用 catch，
// 用户看到的就只是一句 "CORBA exception: TRANSIENT"，等于没说。
CosNaming::NamingContext_ptr resolveNaming(CORBA::ORB_ptr orb) {
    try {
        CORBA::Object_var obj = orb->resolve_initial_references("NameService");
        return CosNaming::NamingContext::_narrow(obj.in());
    } catch (const CORBA::Exception&) {
        return CosNaming::NamingContext::_nil();
    }
}

bool bindName(CORBA::ORB_ptr orb, const std::string& path, CORBA::Object_ptr ref) {
    CosNaming::NamingContext_var root = resolveNaming(orb);
    if (CORBA::is_nil(root.in())) {
        std::cerr << "cannot resolve NameService"
                  << " (pass -ORBInitRef NameService=corbaloc:iiop:host:port/NameService)\n";
        return false;
    }
    const CosNaming::Name name = toName(path);
    if (name.length() == 0) {
        std::cerr << "--name is empty\n";
        return false;
    }
    // rebind 等调用同样会抛（naming 中途挂掉就是 COMM_FAILURE）。在这里收口，
    // 让 main 报出针对性的 exit 7 与名字，而不是笼统的 exit 6。
    try {
        // 逐级建上下文，最后一级留给 rebind
        CosNaming::NamingContext_var ctx = CosNaming::NamingContext::_duplicate(root.in());
        for (CORBA::ULong i = 0; i + 1 < name.length(); ++i) {
            CosNaming::Name one;
            one.length(1);
            one[0] = name[i];
            try {
                CosNaming::NamingContext_var sub = ctx->bind_new_context(one);
                ctx = sub._retn();
            } catch (const CosNaming::NamingContext::AlreadyBound&) {
                CORBA::Object_var o = ctx->resolve(one);
                CosNaming::NamingContext_var sub = CosNaming::NamingContext::_narrow(o.in());
                if (CORBA::is_nil(sub.in())) return false;
                ctx = sub._retn();
            }
        }
        CosNaming::Name last;
        last.length(1);
        last[0] = name[name.length() - 1];
        ctx->rebind(last, ref);
        return true;
    } catch (const CORBA::Exception& e) {
        std::cerr << "Naming Service bind failed: " << e._name() << "\n";
        return false;
    }
}

void unbindName(CORBA::ORB_ptr orb, const std::string& path, CORBA::Object_ptr ours) {
    // 退出时解绑，免得留下一个指向已死进程的名字——下一个客户端解析到它，
    // 拿到的引用会在第一次调用时才失败，比「名字不存在」难查得多。
    //
    // **但只在这个名字还绑在我们自己身上时才解绑。** 滚动重启的时序是：
    // 新实例先 rebind 抢走名字，旧实例随后停机——若旧实例无条件 unbind，
    // 删掉的是**新实例**那份活的注册，于是两个都解析不到，且悄无声息。
    //
    // 判据用 _is_equivalent：按规范它返回 true 即确定等价，返回 false 只表示
    // 「不确定」。方向正好是安全的——误判为「不是我们的」只会留下一条陈旧条目，
    // 下次 rebind 就覆盖掉；反过来误删别人的才是灾难。
    try {
        CosNaming::NamingContext_var root = resolveNaming(orb);
        if (CORBA::is_nil(root.in())) return;
        const CosNaming::Name name = toName(path);
        CORBA::Object_var current = root->resolve(name);
        if (CORBA::is_nil(current.in()) || !current->_is_equivalent(ours)) {
            std::cerr << "xOptMINLPcoCorbaServer: " << path
                      << " now points elsewhere, leaving it alone\n";
            return;
        }
        root->unbind(name);
    } catch (const CORBA::Exception&) {
        // 停机路径上尽力而为：naming 可能已经先于我们退出。
    }
}

// 原子发布：先写临时文件再改名，读者要么看到旧内容要么看到完整新内容，
// 不会读到写了一半的 IOR。
//
// Windows 上必须用 MoveFileExA(MOVEFILE_REPLACE_EXISTING) 而不是
// remove + std::rename：std::rename 在目标已存在时会失败，所以先 remove 的话，
// 从 remove 到 rename 之间目标文件是**不存在**的。轮询这个文件的客户端会看到
// 「文件没了」，于是当成错误或进入重试——那就不再是上面承诺的 old-or-new 了。
// POSIX 的 rename 本身就是覆盖式原子替换，直接用。
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
#ifdef _WIN32
    if (MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
#endif
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string problem_dll;
    std::string ior_file;
    std::string bind_path;

    try {
        // ORB_init 先跑：它会就地摘掉 -ORB* 参数，剩下的才轮到我们解析。
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--problem-dll" && i + 1 < argc) {
                problem_dll = argv[++i];
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

        // 发布顺序：**先绑名字，再写 IOR 文件**。
        //
        // IOR 文件是外界唯一能轮询的就绪信号（本项目两条跨进程测试都拿它当就绪）。
        // 若先落盘再绑，观察者看到文件就认为可用，而此刻名字还没绑好——解析必然失败；
        // 更糟的是绑定若最终失败，进程会退出，而那份「就绪」文件已经发出去了。
        // 反过来则没有这个窗口：文件出现时，两种发布方式都已经成立。
        if (!bind_path.empty() && !bindName(orb.in(), bind_path, ref.in())) {
            std::cerr << "failed to bind into the Naming Service: " << bind_path << "\n";
            return 7;
        }

        if (!ior_file.empty() && !writeIorFileAtomically(ior_file, ior.in())) {
            std::cerr << "failed to write the IOR file: " << ior_file << "\n";
            return 5;
        }

        // stdout 只放 IOR 本身，方便 `server --ior-file x` 之外直接管道取用。
        std::cout << ior.in() << std::endl;
        std::cerr << "xOptMINLPcoCorbaServer: ICapeMINLP published, serving"
                  << (ior_file.empty() ? "" : (" (IOR written to " + ior_file + ")"))
                  << (bind_path.empty() ? "" : (" (bound as " + bind_path + ")")) << "\n";

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);
#ifdef _WIN32
        SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
#endif

        // 带超时轮转事件循环，而不是 orb->run() 一直阻塞：信号处理器只置标志
        // （见上），停机得由主线程发起。超时值只影响 Ctrl-C 后的退出延迟。
        while (g_stop == 0) {
            ACE_Time_Value tv(0, 200 * 1000);  // 200ms
            orb->run(tv);
        }

        // 到这里是主线程上下文，调 shutdown 是安全的。
        // 顺序要紧：deactivate_object 必须在 shutdown 之前。反过来的话 POA 已经
        // 不再受理操作，deactivate_object 抛 BAD_INV_ORDER——实测如此。
        std::cerr << "xOptMINLPcoCorbaServer: shutting down\n";
        // 先解绑再停服：反过来的话，名字会在 ORB 已经不收请求之后仍短暂可解析，
        // 客户端拿到引用、第一次调用才失败，比「名字不存在」难查得多。
        if (!bind_path.empty()) unbindName(orb.in(), bind_path, ref.in());
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
