// ***************************************************************
//  CorbaPublish   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "CorbaPublish.h"

#include <orbsvcs/CosNamingC.h>

#ifdef _WIN32
#    include <windows.h>  // MoveFileExA / SetConsoleCtrlHandler
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace xoptco_publish {

namespace {

// 信号处理器里能做的只有「置一个标志」。原先直接调 orb->shutdown()，那不是
// async-signal-safe——信号可能恰好打断 ORB 自己的运行时状态，于是 shutdown 在
// 半路的数据结构上跑，轻则死锁重则崩。真正的退出由主线程完成。
volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) { g_stop = 1; }

#ifdef _WIN32
// Windows 上光靠 std::signal 是够不着的：这个系统没有 SIGTERM（CRT 里那个常量
// 基本不会被真正投递），SIGINT 也只在进程有控制台、且经 CRT 的控制台处理时才到。
// 实测 taskkill 打过来时进程纹丝不动，得靠控制台控制处理器。
// 它跑在另一个线程而非信号上下文里，但「只置标志」依然是这里该做的最小动作。
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

}  // namespace

void setEnvVar(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

bool bindName(CORBA::ORB_ptr orb, const std::string& path, CORBA::Object_ptr ref) {
    CosNaming::NamingContext_var root = resolveNaming(orb);
    if (CORBA::is_nil(root.in())) {
        // 两件事都要说：既得有一个在跑的 naming service，也得告诉本进程它在哪。
        // 只说后者，第一次用 --name 的人会以为补个 -ORBInitRef 就完事了。
        std::cerr << "cannot resolve NameService -- is one running, and were we told where?\n"
                  << "  1) start it:  tao_cosnaming.exe -ORBEndpoint iiop://localhost:24567\n"
                  << "  2) tell us:   -ORBInitRef NameService=corbaloc:iiop:localhost:24567/NameService\n"
                  << "  clients then use:  corbaname::localhost:24567#<name>\n";
        return false;
    }
    const CosNaming::Name name = toName(path);
    if (name.length() == 0) {
        std::cerr << "--name is empty\n";
        return false;
    }
    // rebind 等调用同样会抛（naming 中途挂掉就是 COMM_FAILURE）。在这里收口，
    // 让调用方报出针对性的退出码与名字，而不是笼统的 CORBA exception。
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
                if (CORBA::is_nil(sub.in())) {
                    // 这一级已经被绑成了一个**对象**，不是上下文。最常见的成因是
                    // 拿 --name 的名字当 --unit-name 的父级（--name a
                    // --unit-name a/b）：a 先被 rebind 成 ICapeMINLP，再想往
                    // a 底下挂东西就不成立了。CosNaming 里一个名字只能是二者之一。
                    const char* level = name[i].id.in();
                    std::cerr << "Naming Service: '" << (level != nullptr ? level : "")
                              << "' is already bound to an object, so it cannot also be a"
                                 " naming context for '" << path << "'.\n"
                              << "  a name is either an object or a context, never both --"
                                 " use sibling names\n"
                              << "  (--name testsplitter --unit-name testsplitter_unit)"
                                 " or put both under a shared context\n"
                              << "  (--name xopt/minlp --unit-name xopt/unit).\n";
                    return false;
                }
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

void unbindName(CORBA::ORB_ptr orb, const std::string& path, CORBA::Object_ptr ours,
                const char* who) {
    // 判据用 _is_equivalent：按规范它返回 true 即确定等价，返回 false 只表示
    // 「不确定」。方向正好是安全的——误判为「不是我们的」只会留下一条陈旧条目，
    // 下次 rebind 就覆盖掉；反过来误删别人的才是灾难。
    try {
        CosNaming::NamingContext_var root = resolveNaming(orb);
        if (CORBA::is_nil(root.in())) return;
        const CosNaming::Name name = toName(path);
        CORBA::Object_var current = root->resolve(name);
        if (CORBA::is_nil(current.in()) || !current->_is_equivalent(ours)) {
            std::cerr << (who != nullptr ? who : "server") << ": " << path
                      << " now points elsewhere, leaving it alone\n";
            return;
        }
        root->unbind(name);
    } catch (const CORBA::Exception&) {
        // 停机路径上尽力而为：naming 可能已经先于我们退出。
    }
}

// Windows 上必须用 MoveFileExA(MOVEFILE_REPLACE_EXISTING) 而不是
// remove + std::rename：std::rename 在目标已存在时会失败，所以先 remove 的话，
// 从 remove 到 rename 之间目标文件是**不存在**的。轮询这个文件的客户端会看到
// 「文件没了」，于是当成错误或进入重试——那就不再是承诺的 old-or-new 了。
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

void installStopHandlers() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
#endif
}

bool stopRequested() { return g_stop != 0; }

void runUntilStopped(CORBA::ORB_ptr orb) {
    // 带超时轮转事件循环，而不是 orb->run() 一直阻塞：信号处理器只置标志，
    // 停机得由这个线程发起。
    while (g_stop == 0) {
        ACE_Time_Value tv(0, 200 * 1000);  // 200ms
        orb->run(tv);
    }
}

}  // namespace xoptco_publish
