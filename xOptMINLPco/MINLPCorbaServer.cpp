// ***************************************************************
//  MINLPCorbaServer   version:  1.1   -  date:  2026/09/03
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
//    xOptMINLPcoCorbaServer --problem-dll <path> [--model-desc <path>]
//                           [--ior-file <path>] [--unit-ior-file <path>]
//                           [--unit-name <path>]
//
//  发两个对象，因为 CAPE-OPEN 把两件事放在两份规范里：
//    ICapeMINLP        问题（变量/约束/导数）      --ior-file / --name
//    ICapeUnit(+扩展)  接线（端口/组分/可固定量）  --unit-ior-file / --unit-name
//  单元对象的 GetMINLP() 会交出前者，所以消费端只连单元这一个地址就够了；
//  --ior-file 保持原样是为了不动既有的 MINLP 直连通路与它的两条跨进程测试。
//                           [-ORBEndpoint iiop://host:port] ...
//  -ORB* 参数由 ORB_init 消费，其余由本文件解析。被包装的 xOptProblem DLL
//  也可用环境变量 XRTO_XOPT_PROBLEM_DLL / XRTO_XOPT_MODEL_DESC 指定
//  （命令行参数优先）。
//
//  IOR 同时打到 stdout 和 --ior-file。文件写入是原子的（先写 .tmp 再改名），
//  否则轮询这个文件的客户端会读到半截 IOR。
//
//  --name 另把引用绑进 Naming Service，客户端即可用可读的
//  corbaname::host:port#<名字> 连接（issue #6）——分隔符是 '#'，不是 '/'。
//  IOR 那条路不依赖任何外部服务，--name 则要求另跑一个 naming 进程。
//
//  v1.1：IOR 落盘 / Naming 绑定 / 停机信号这几件事搬进 CorbaPublish.{h,cpp}，
//  与求解器服务端（MINLPSolverCorbaServer.cpp）共用——两个 exe 的发布语义
//  必须一致，而"一致"最可靠的保证是同一份代码。本文件只剩参数解析与两个
//  对象的发布顺序。
// ***************************************************************
// ACE/TAO 必须最先包含：它拉 winsock2.h，而 <windows.h> 默认拉 winsock.h(v1)，
// 顺序反了会炸出 IPPROTO_IPV6/timeval 一片重定义。
#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "CorbaPublish.h"
#include "MINLPServant.h"
#include "UnitServant.h"
#include "XOptMINLPAdapter.h"

namespace {

// 控制台输出一律 ASCII：本 exe 是拿去给第三方演示的，源码是 UTF-8，而 Windows
// 控制台默认 GBK 代码页——中文在那里会变成乱码，正好毁掉演示。注释仍用中文。
void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " --problem-dll <path> [--model-desc <path>] [--ior-file <path>]"
              << " [--name <path>] [-ORB<...>]\n"
              << "  --problem-dll <path>  xOptProblem DLL to wrap"
              << " (or set env XRTO_XOPT_PROBLEM_DLL)\n"
              << "  --unit-ior-file <path> write the ICapeUnit IOR here (topology: ports,"
              << " components, fixables)\n"
              << "  --unit-name <path>    bind the ICapeUnit into the Naming Service under"
              << " this name\n"
              << "  --model-desc <path>   model description JSON for a C-ABI black-box model"
              << " (or set env XRTO_XOPT_MODEL_DESC);\n"
              << "                        omit it and the unique *_Model.json next to the DLL"
              << " is used\n"
              << "  --ior-file <path>     write the IOR to this file (atomically);"
              << " stdout only if omitted\n"
              << "  --name <path>         also bind into the Naming Service; path is '/'-separated,\n"
              << "                        clients then use corbaname::host:port#<path> ('#', not '/')\n"
              << "                        needs -ORBInitRef NameService=corbaloc:iiop:host:port/NameService\n"
              << "  -ORB* args (e.g. -ORBEndpoint iiop://host:port) are consumed by ORB_init\n";
}

}  // namespace

int main(int argc, char** argv) {
    using namespace xoptco_publish;

    std::string problem_dll;
    std::string model_desc;
    std::string ior_file;
    std::string bind_path;
    std::string unit_ior_file;
    std::string unit_bind_path;

    try {
        // ORB_init 先跑：它会就地摘掉 -ORB* 参数，剩下的才轮到我们解析。
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--problem-dll" && i + 1 < argc) {
                problem_dll = argv[++i];
            } else if (a == "--model-desc" && i + 1 < argc) {
                model_desc = argv[++i];
            } else if (a == "--ior-file" && i + 1 < argc) {
                ior_file = argv[++i];
            } else if (a == "--name" && i + 1 < argc) {
                bind_path = argv[++i];
            } else if (a == "--unit-ior-file" && i + 1 < argc) {
                unit_ior_file = argv[++i];
            } else if (a == "--unit-name" && i + 1 < argc) {
                unit_bind_path = argv[++i];
            } else if (a == "--help" || a == "-h") {
                usage(argv[0]);
                return 0;
            } else {
                std::cerr << "unrecognized argument: " << a << "\n";
                usage(argv[0]);
                return 2;
            }
        }

        if (!problem_dll.empty()) setEnvVar("XRTO_XOPT_PROBLEM_DLL", problem_dll);
        if (!model_desc.empty()) setEnvVar("XRTO_XOPT_MODEL_DESC", model_desc);

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
            // 把 adapter 的原话透出去：模型初始化失败的原因（参数名不认、缺组分表、
            // validateModel 没过……）都在那句话里，压成一句 "not ready" 等于把
            // 唯一的线索扔掉。
            std::cerr << "servant not ready: " << servant->initError()
                      << " (--problem-dll / --model-desc)\n";
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

        // ---- ICapeUnit（+ XOPTCO 扩展）：接线信息 ----
        // 只有 C-ABI 模型才有端口；C++ ABI 的 createProblem 输入没有模型这一层，
        // adapter 的 ports() 会是空的，那时发一个零端口的单元也没有意义。
        // 引用与 ObjectId 提到块外：关停时要用它们解绑名字、注销对象。留在块内
        // 的话，unit 名字会在进程死后继续解析到一个死引用——正是 unbindName
        // 那段注释想避免的情形，而 xRto 解析的就是这个名字。
        CORBA::Object_var unit_ref;
        PortableServer::ObjectId_var unit_oid;
        if (servant->ownedAdapter() != nullptr && !servant->ownedAdapter()->ports().empty()) {
            UnitServant* unit = new UnitServant(servant->ownedAdapter(), poa.in(), ref.in());
            PortableServer::ServantBase_var unit_owner(unit);
            unit_oid = poa->activate_object(unit);
            unit_ref = poa->id_to_reference(unit_oid.in());
            CORBA::String_var unit_ior = orb->object_to_string(unit_ref.in());

            // 与 MINLP 那边同样的顺序：先绑名字再落盘（理由见上面那段注释）。
            if (!unit_bind_path.empty() &&
                !bindName(orb.in(), unit_bind_path, unit_ref.in())) {
                std::cerr << "failed to bind the unit into the Naming Service: "
                          << unit_bind_path << "\n";
                return 7;
            }
            if (!unit_ior_file.empty() &&
                !writeIorFileAtomically(unit_ior_file, unit_ior.in())) {
                std::cerr << "failed to write the unit IOR file: " << unit_ior_file << "\n";
                return 5;
            }
            std::cerr << "xOptMINLPcoCorbaServer: ICapeUnit published ("
                      << servant->ownedAdapter()->ports().size() << " ports)"
                      << (unit_ior_file.empty() ? "" : (" (IOR written to " + unit_ior_file + ")"))
                      << (unit_bind_path.empty() ? "" : (" (bound as " + unit_bind_path + ")"))
                      << "\n";
        } else if (!unit_ior_file.empty() || !unit_bind_path.empty()) {
            // 要了单元却发不出来，必须说清楚：静默跳过会让消费端一路连到超时。
            std::cerr << "--unit-ior-file/--unit-name given, but the wrapped model exposes no"
                         " ports (a C++-ABI createProblem DLL has no model layer at all)\n";
            return 6;
        }

        installStopHandlers();
        runUntilStopped(orb.in());

        // 到这里是主线程上下文，调 shutdown 是安全的。
        // 顺序要紧：deactivate_object 必须在 shutdown 之前。反过来的话 POA 已经
        // 不再受理操作，deactivate_object 抛 BAD_INV_ORDER——实测如此。
        std::cerr << "xOptMINLPcoCorbaServer: shutting down\n";
        // 先解绑再停服：反过来的话，名字会在 ORB 已经不收请求之后仍短暂可解析，
        // 客户端拿到引用、第一次调用才失败，比「名字不存在」难查得多。
        // 单元名字先解：它才是流程图里被解析的那个（corbaname:...#xopt/unit）。
        if (!unit_bind_path.empty() && !CORBA::is_nil(unit_ref.in())) {
            unbindName(orb.in(), unit_bind_path, unit_ref.in(), "xOptMINLPcoCorbaServer");
        }
        if (!bind_path.empty()) unbindName(orb.in(), bind_path, ref.in(), "xOptMINLPcoCorbaServer");
        if (!CORBA::is_nil(unit_ref.in())) poa->deactivate_object(unit_oid.in());
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
