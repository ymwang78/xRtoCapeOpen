#pragma once
// ***************************************************************
//  CorbaPublish   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  独立进程 CORBA server 共用的"发布"工具：IOR 原子落盘、Naming Service
//  绑定/解绑、停机信号。原先都住在 MINLPCorbaServer.cpp 的匿名命名空间里；
//  求解器服务端（MINLPSolverCorbaServer.cpp）需要一模一样的一套，于是搬到
//  这里。两个 exe 的发布语义必须一致——先绑名字再落盘、只解绑仍属于自己的
//  名字、信号处理器只置标志——而"一致"最可靠的保证是同一份代码。
//
//  控制台输出一律 ASCII：这两个 exe 是拿去给第三方演示的，源码是 UTF-8 而
//  Windows 控制台默认 GBK 代码页。注释仍用中文。
// ***************************************************************
// ACE/TAO 必须最先包含：它拉 winsock2.h，而 <windows.h> 默认拉 winsock.h(v1)。
#include <tao/ORB.h>

#include <string>

namespace xoptco_publish {

// 进程内设环境变量（Windows _putenv_s / POSIX setenv）。
void setEnvVar(const char* name, const std::string& value);

// 把引用绑进 Naming Service。path 按 '/' 拆成多级，中间各级 bind_new_context
// （已存在则 resolve），最后一级 rebind——服务端重启时名字多半还在，bind 会抛
// AlreadyBound，"重启一次就再也绑不上"是运维最不想要的失败方式。
// 失败时已把原因打到 stderr（含 NameService 没配时那句 -ORBInitRef 提示）。
bool bindName(CORBA::ORB_ptr orb, const std::string& path, CORBA::Object_ptr ref);

// 退出时解绑，但**只在这个名字还绑在我们自己身上时**：滚动重启里新实例先
// rebind 抢走名字，旧实例无条件 unbind 删掉的会是新实例那份活的注册。
// who 只用于日志前缀。停机路径上尽力而为，不抛。
void unbindName(CORBA::ORB_ptr orb, const std::string& path, CORBA::Object_ptr ours,
                const char* who);

// 原子发布 IOR：先写 .tmp 再改名，读者要么看到旧内容要么看到完整新内容。
bool writeIorFileAtomically(const std::string& path, const char* ior);

// 装停机处理器（SIGINT/SIGTERM + Windows 控制台控制处理器）。处理器只置标志，
// 真正的 shutdown 由 runUntilStopped 的调用线程做——信号上下文里调
// orb->shutdown() 不是 async-signal-safe。
void installStopHandlers();
bool stopRequested();

// 带超时轮转事件循环直到 stopRequested()。超时值只影响 Ctrl-C 后的退出延迟。
void runUntilStopped(CORBA::ORB_ptr orb);

}  // namespace xoptco_publish
