// ***************************************************************
//  CapeOpenSolverBridge   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  xRto 侧的求解器桥接 DLL（产物 xRtoCapeOpenSolver.dll）：对宿主是一个普通
//  求解器 DLL——导出 createSolver / destroySolver，在 Solver.json 里按
//  SolverPath 注册；对远端是一个 CAPE-OPEN 客户端，把宿主交来的 xOptProblem
//  发布成 ICapeMINLP，交给远端的 ICapeMINLPSolverManager 去解。
//
//    xRto: createSolver("FLOWSHEET", problem, log)
//      -> XOptMINLPAdapter(problem) -> MINLPServant   本进程里发布问题（POA）
//      -> 连接目标 -> ICapeMINLPSolverManager::CreateMINLPSystem(problem_ref)
//      -> solve(): ICapeMINLPSystem::Solve()          远端求解，期间回调本进程
//      -> X()/F(): IXOptMINLPSystemExtension::GetX/GetF
//
//  **这个 DLL 让 xRto 进程变成了一个 CORBA 服务端**：它跑着一个 ORB 和 RootPOA。
//  没有单独的 ORB 线程——xRto 的线程在 Solve() 里同步等待远端应答时，TAO 默认
//  的 leader-follower 等待策略（-ORBClientConnectionHandler MT）会让这个线程
//  顺手跑事件循环、派发远端回调过来的 ICapeMINLP 请求，即"嵌套上行调用"。
//  于是问题对象只被一个线程碰（那正是阻塞在 Solve 里的线程），不需要锁，
//  xOptProblemComp 也不必是线程安全的。ORB_init 时显式指定 MT，不依赖默认值。
//
//  连接目标的解析优先级（与 xRtoCapeOpen.dll 的模型通路同一套道理）：
//    1) createSolver 的 name 形参（自带 corba: scheme 或直接是 IOR:/corbaname:）
//       ——宿主 xRto 传的是 "FLOWSHEET"，走不到这条；留给别的宿主与测试。
//    2) 本 DLL 同目录的 <DLL 基名>.target 文件 —— 每个部署目录一份
//    3) 环境变量 XRTO_CAPEOPEN_SOLVER_TARGET
//    4) 都没有 -> createSolver 返回 nullptr，并把该建哪个文件打到日志
//
//  异常纪律：CORBA 异常一律在本 DLL 内收口成负返回码 + 日志，不穿透 C ABI。
// ***************************************************************
// xOptSolver 类带 XOPTIF_API：派生类需要基类的隐式构造/析构就地生成，而不是
// 按 dllimport 去链接一个不存在的 xOptInterface DLL。必须在任何 xOpt 头之前。
#ifdef _WIN32
#    ifndef XOPTINTERFACE_EXPORTS
#        define XOPTINTERFACE_EXPORTS
#    endif
#endif

// ACE/TAO 必须最先包含：它拉 winsock2.h，而 <windows.h> 默认拉 winsock.h(v1)。
#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "CAPEOPEN100_MinlpC.h"
#include "MINLPServant.h"
#include "XOPTCO_ExtC.h"
#include "XOptMINLPAdapter.h"
#include "backend/corba/CapeCorbaMarshal.h"
#include "xOpt/xOptProblem.h"
#include "xOpt/xOptSolver.h"

namespace {

namespace ct = ::CAPEOPEN100::Common::Types;
namespace ce = ::CAPEOPEN100::Common::Error;
namespace mi = ::CAPEOPEN100::Business::Numeric::Minlp;

// ---------------------------------------------------------------------------
//  进程级 ORB + RootPOA
// ---------------------------------------------------------------------------
//
// 故意泄漏（new 出来不 delete）：这个 DLL 在 xRto 进程里一直活到进程退出，
// 而 CRT 卸载 DLL 时跑静态析构的顺序与 ACE/TAO 自己的静态对象纠缠在一起，
// 在那个时刻去 destroy 一个 ORB 是崩溃的常见来源。进程退出由 OS 回收。
struct OrbHolder {
    CORBA::ORB_var orb;
    PortableServer::POA_var poa;
};

OrbHolder* ensureOrb(std::string& error) {
    static std::mutex mutex;
    static OrbHolder* holder = nullptr;
    std::lock_guard<std::mutex> lock(mutex);
    if (holder != nullptr) return holder;
    try {
        // MT = leader-follower 等待策略，允许嵌套上行调用（见文件头）。
        // 它是 TAO 的默认值，但这条通路**依赖**它，所以显式写上。
        // -ORBClientConnectionHandler 不是 ORB_init 认的选项（直接给会
        // BAD_PARAM），它属于 Client_Strategy_Factory，得经 svc.conf 指令传——
        // -ORBSvcConfDirective 就是把一行 svc.conf 从命令行塞进去的口子。
        static char a0[] = "xRtoCapeOpenSolver";
        static char a1[] = "-ORBSvcConfDirective";
        static char a2[] = "static Client_Strategy_Factory \"-ORBClientConnectionHandler MT\"";
        char* argv[] = {a0, a1, a2, nullptr};
        int argc = 3;
        std::unique_ptr<OrbHolder> h(new OrbHolder());
        h->orb = CORBA::ORB_init(argc, argv, "xRtoCapeOpenSolver");
        CORBA::Object_var obj = h->orb->resolve_initial_references("RootPOA");
        h->poa = PortableServer::POA::_narrow(obj.in());
        if (CORBA::is_nil(h->poa.in())) {
            error = "resolve_initial_references(RootPOA) failed";
            return nullptr;
        }
        h->poa->the_POAManager()->activate();
        holder = h.release();
        return holder;
    } catch (const CORBA::Exception& e) {
        error = std::string("ORB_init failed: ") + e._name();
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
//  连接目标
// ---------------------------------------------------------------------------

std::string thisModulePath() {
#ifdef _WIN32
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&thisModulePath), &self) == 0) {
        return std::string();
    }
    char buf[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameA(self, buf, MAX_PATH);
    return std::string(buf, n);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&thisModulePath), &info) == 0 || info.dli_fname == nullptr) {
        return std::string();
    }
    return info.dli_fname;
#endif
}

std::string sidecarPath() {
    const std::string dll = thisModulePath();
    if (dll.empty()) return std::string();
    const size_t dot = dll.find_last_of('.');
    const size_t sep = dll.find_last_of("/\\");
    const std::string base =
        (dot != std::string::npos && (sep == std::string::npos || dot > sep)) ? dll.substr(0, dot)
                                                                             : dll;
    return base + ".target";
}

// 读 <DLL 路径去掉扩展名>.target 的第一条非空非注释行。读不到返回空串。
std::string readSidecarTarget() {
    const std::string path = sidecarPath();
    if (path.empty()) return std::string();
    std::ifstream in(path);
    if (!in) return std::string();
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t b = 0;
        while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
        line = line.substr(b);
        if (line.empty() || line[0] == '#') continue;
        return line;
    }
    return std::string();
}

bool looksLikeTarget(const std::string& s) {
    return s.rfind("corba:", 0) == 0 || s.rfind("IOR:", 0) == 0 ||
           s.rfind("corbaname:", 0) == 0 || s.rfind("corbaloc:", 0) == 0;
}

// 去掉我们自己的 "corba:" 前缀（与模型通路的连接串写法一致），剩下的交给
// string_to_object：IOR: / corbaname: / corbaloc: 都是 ORB 认的。
std::string stripScheme(const std::string& s) {
    return s.rfind("corba:", 0) == 0 ? s.substr(6) : s;
}

std::string resolveTarget(const char* name, std::string& source) {
    if (name != nullptr && looksLikeTarget(name)) {
        source = "the name passed to createSolver";
        return name;
    }
    const std::string sidecar = readSidecarTarget();
    if (!sidecar.empty()) {
        source = sidecarPath();
        return sidecar;
    }
    if (const char* env = std::getenv("XRTO_CAPEOPEN_SOLVER_TARGET")) {
        if (env[0] != '\0') {
            source = "XRTO_CAPEOPEN_SOLVER_TARGET";
            return env;
        }
    }
    return std::string();
}

// 把 CAPE-OPEN 用户异常里的 description 挖出来；其余只有异常名可报。
template <class E>
bool describeAs(const CORBA::Exception& e, std::string& out) {
    const E* u = dynamic_cast<const E*>(&e);
    if (u == nullptr) return false;
    out = std::string(e._name()) + ": " + (u->description.in() != nullptr ? u->description.in() : "");
    return true;
}

std::string describe(const CORBA::Exception& e) {
    std::string d;
    if (describeAs<ce::ECapeUnknown>(e, d) || describeAs<ce::ECapeInvalidArgument>(e, d) ||
        describeAs<ce::ECapeSolvingError>(e, d)) {
        return d;
    }
    return e._name();
}

ct::CapeArrayString toNames(const char* const names[], int n) {
    ct::CapeArrayString s;
    s.length(static_cast<CORBA::ULong>(n > 0 ? n : 0));
    for (CORBA::ULong i = 0; i < s.length(); ++i) {
        s[i] = CORBA::string_dup(names[i] != nullptr ? names[i] : "");
    }
    return s;
}

// ---------------------------------------------------------------------------
//  远端求解器：xOptSolver 的每个方法都是一次 IIOP 往返
// ---------------------------------------------------------------------------
class CapeOpenRemoteSolver : public xOptSolver {
  public:
    CapeOpenRemoteSolver(const char* name, xOptProblem* problem, xOptLogFunc log)
        : problem_(problem), name_(name != nullptr ? name : ""), log_(log) {}

    ~CapeOpenRemoteSolver() override { disconnect(); }

    // 发布本地问题、连远端、建系统。返回 <0 时已写日志。
    int connect(const std::string& target) {
        std::string err;
        OrbHolder* h = ensureOrb(err);
        if (h == nullptr) {
            log(ZLOG_ERROR, "[%s] %s", name_.c_str(), err.c_str());
            return -1;
        }
        // 宿主交来的问题已经初始化过；再 initialize 一次会把组合问题整个重建。
        adapter_.reset(new XOptMINLPAdapter(problem_, /*initialize_problem*/ false));
        if (adapter_->connect() < 0) {
            log(ZLOG_ERROR, "[%s] cannot read the problem: %s", name_.c_str(),
                adapter_->lastError().c_str());
            adapter_.reset();
            return -1;
        }
        try {
            MINLPServant* servant = new MINLPServant(adapter_.get());
            PortableServer::ServantBase_var owner(servant);
            problem_oid_ = h->poa->activate_object(servant);
            problem_ref_ = h->poa->id_to_reference(problem_oid_.in());

            CORBA::Object_var obj = h->orb->string_to_object(stripScheme(target).c_str());
            manager_ = mi::ICapeMINLPSolverManager::_narrow(obj.in());
            if (CORBA::is_nil(manager_.in())) {
                log(ZLOG_ERROR, "[%s] '%s' is not an ICapeMINLPSolverManager", name_.c_str(),
                    target.c_str());
                disconnect();
                return -1;
            }
            // 这一步远端就会回调本进程读问题结构——嵌套上行调用在这里第一次派上用场。
            CORBA::Object_var sys;
            manager_->CreateMINLPSystem(problem_ref_.in(), sys.out());
            system_ = mi::ICapeMINLPSystem::_narrow(sys.in());
            if (CORBA::is_nil(system_.in())) {
                log(ZLOG_ERROR, "[%s] CreateMINLPSystem did not return an ICapeMINLPSystem",
                    name_.c_str());
                disconnect();
                return -1;
            }
            ext_ = ::XOPTCO::IXOptMINLPSystemExtension::_narrow(sys.in());
            if (CORBA::is_nil(ext_.in())) {
                // 一个只说标准 CAPE-OPEN 的求解器：能解，但结果码/选项/F 拿不到。
                log(ZLOG_WARNI,
                    "[%s] the remote system has no XOPTCO extension: options are unavailable and "
                    "X is read back from the problem, F is re-evaluated locally",
                    name_.c_str());
            }
            log(ZLOG_INFOR, "[%s] connected to %s (nv=%d, nc=%d)", name_.c_str(), target.c_str(),
                problem_->numVariables(), problem_->numConstraints());
            return 0;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_ERROR, "[%s] connect to '%s' failed: %s", name_.c_str(), target.c_str(),
                describe(e).c_str());
            disconnect();
            return -1;
        }
    }

    // ---- xOptSolver ----
    xOptProblem* getProblem() const override { return problem_; }

    int getTunableParamList(const char* p_name[], OPTION_TYPE p_type[], int& p_size) const override {
        if (!ext()) return -1;
        if (tunable_names_.empty() && !fetchTunables()) return -1;
        if (p_name == nullptr && p_type == nullptr) {
            p_size = static_cast<int>(tunable_names_.size());
            return 0;
        }
        if (p_size < static_cast<int>(tunable_names_.size())) return -1;
        for (size_t i = 0; i < tunable_names_.size(); ++i) {
            if (p_name != nullptr) p_name[i] = tunable_names_[i].c_str();
            if (p_type != nullptr) p_type[i] = tunable_types_[i];
        }
        p_size = static_cast<int>(tunable_names_.size());
        return 0;
    }

    int setTunableParamList(const char* p_name[], int p_size) override {
        if (!ext()) return -1;
        try {
            ext_->SetTunableParameters(toNames(p_name, p_size));
            return 0;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] setTunableParamList: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int getStringOptions(const char* option_names[], const char* option_values[],
                         int& options_size) const override {
        // 契约的"只报个数"查询这里答不上来：远端只按名字答值，没有枚举。
        if (!ext() || option_names == nullptr || option_values == nullptr) return -1;
        try {
            ct::CapeArrayString_var values;
            ext_->GetStringOptions(toNames(option_names, options_size), values.out());
            cape_corba::fromStringSeq(values.in(), string_cache_);
            for (int i = 0; i < options_size && i < static_cast<int>(string_cache_.size()); ++i) {
                option_values[i] = string_cache_[static_cast<size_t>(i)].c_str();
            }
            return 0;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] getStringOptions: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int setStringOptions(boolean option_results[], const char* option_names[],
                         const char* option_values[], int options_size) override {
        if (!ext()) return -1;
        try {
            ct::CapeArrayBoolean_var accepted;
            ext_->SetStringOptions(toNames(option_names, options_size),
                                   toNames(option_values, options_size), accepted.out());
            copyAccepted(accepted.in(), option_results, options_size);
            return 0;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] setStringOptions: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int getIntOptions(const char* option_names[], int option_values[],
                      int& options_size) const override {
        if (!ext() || option_names == nullptr || option_values == nullptr) return -1;
        try {
            ct::CapeArrayLong_var values;
            ext_->GetIntOptions(toNames(option_names, options_size), values.out());
            std::vector<int> v;
            cape_corba::fromLongSeq(values.in(), v);
            for (int i = 0; i < options_size && i < static_cast<int>(v.size()); ++i) {
                option_values[i] = v[static_cast<size_t>(i)];
            }
            return 0;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] getIntOptions: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int setIntOptions(boolean option_results[], const char* option_names[],
                      const int option_values[], int options_size) override {
        if (!ext()) return -1;
        try {
            std::vector<int> v(option_values, option_values + (options_size > 0 ? options_size : 0));
            ct::CapeArrayBoolean_var accepted;
            ext_->SetIntOptions(toNames(option_names, options_size), cape_corba::toLongSeq(v),
                                accepted.out());
            copyAccepted(accepted.in(), option_results, options_size);
            return 0;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] setIntOptions: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int getDoubleOptions(const char* option_names[], double option_values[],
                         int& options_size) const override {
        if (!ext() || option_names == nullptr || option_values == nullptr) return -1;
        try {
            ct::CapeArrayDouble_var values;
            ext_->GetDoubleOptions(toNames(option_names, options_size), values.out());
            std::vector<double> v;
            cape_corba::fromDoubleSeq(values.in(), v);
            for (int i = 0; i < options_size && i < static_cast<int>(v.size()); ++i) {
                option_values[i] = v[static_cast<size_t>(i)];
            }
            return 0;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] getDoubleOptions: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int setDoubleOptions(boolean option_results[], const char* option_names[],
                         const double option_values[], int options_size) override {
        if (!ext()) return -1;
        try {
            std::vector<double> v(option_values,
                                  option_values + (options_size > 0 ? options_size : 0));
            ct::CapeArrayBoolean_var accepted;
            ext_->SetDoubleOptions(toNames(option_names, options_size),
                                   cape_corba::toDoubleSeq(v), accepted.out());
            copyAccepted(accepted.in(), option_results, options_size);
            return 0;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] setDoubleOptions: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int solve() override {
        if (CORBA::is_nil(system_.in())) {
            log(ZLOG_ERROR, "[%s] solve: not connected", name_.c_str());
            return RESULT_INVALID_SETTINGS;
        }
        x_.clear();
        f_.clear();
        result_ = RESULT_UNKNOWN;
        try {
            // 阻塞在这里期间，远端对本进程 ICapeMINLP 的每一次回调都在本线程上派发。
            system_->Solve();
            result_ = CORBA::is_nil(ext_.in()) ? RESULT_OPTIMAL : ext_->GetSolveResult();
        } catch (const ce::ECapeSolvingError& e) {
            // 求解器跑完了但说不行：原码在扩展里。
            result_ = RESULT_UNKNOWN;
            if (!CORBA::is_nil(ext_.in())) {
                try {
                    result_ = ext_->GetSolveResult();
                } catch (const CORBA::Exception&) {
                }
            }
            log(ZLOG_WARNI, "[%s] remote solve failed: %s (result=%d)", name_.c_str(),
                (e.description.in() != nullptr ? e.description.in() : ""), result_);
        } catch (const CORBA::Exception& e) {
            log(ZLOG_ERROR, "[%s] remote solve aborted: %s", name_.c_str(), describe(e).c_str());
            result_ = RESULT_NUMERICAL_ISSUES;
            return result_;
        }
        fetchSolution();
        log(ZLOG_INFOR, "[%s] remote solve finished: result=%d, objective=%.6e", name_.c_str(),
            result_, f_.empty() ? 0.0 : f_[0]);
        return result_;
    }

    int pauseSolve() override {
        if (!ext()) return -1;
        try {
            return ext_->PauseSolve();
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] pauseSolve: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int continueSolve() override {
        if (!ext()) return -1;
        try {
            return ext_->ContinueSolve();
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] continueSolve: %s", name_.c_str(), describe(e).c_str());
            return -1;
        }
    }

    int X(double* x, int x_size) const override { return copyOut(x_, x, x_size); }
    int F(double* f, int f_size) const override { return copyOut(f_, f, f_size); }

    int Xmul(double* x, int x_size) const override {
        if (!ext()) return -1;
        try {
            ct::CapeArrayDouble_var v = ext_->GetXmul();
            std::vector<double> out;
            cape_corba::fromDoubleSeq(v.in(), out);
            return copyOut(out, x, x_size);
        } catch (const CORBA::Exception&) {
            return -1;
        }
    }

    int Fmul(double* f, int f_size) const override {
        if (!ext()) return -1;
        try {
            ct::CapeArrayDouble_var v = ext_->GetFmul();
            std::vector<double> out;
            cape_corba::fromDoubleSeq(v.in(), out);
            return copyOut(out, f, f_size);
        } catch (const CORBA::Exception&) {
            return -1;
        }
    }

  private:
    bool ext() const {
        if (!CORBA::is_nil(ext_.in())) return true;
        if (!warned_no_ext_) {
            warned_no_ext_ = true;
            log(ZLOG_WARNI, "[%s] the remote system carries no XOPTCO extension; option and "
                            "result channels are unavailable",
                name_.c_str());
        }
        return false;
    }

    bool fetchTunables() const {
        try {
            ct::CapeArrayString_var names;
            ct::CapeArrayLong_var types;
            ext_->GetTunableParameters(names.out(), types.out());
            cape_corba::fromStringSeq(names.in(), tunable_names_);
            std::vector<int> t;
            cape_corba::fromLongSeq(types.in(), t);
            tunable_types_.clear();
            for (int v : t) tunable_types_.push_back(static_cast<OPTION_TYPE>(v));
            tunable_types_.resize(tunable_names_.size(), OPTION_REAL);
            return true;
        } catch (const CORBA::Exception& e) {
            log(ZLOG_WARNI, "[%s] getTunableParamList: %s", name_.c_str(), describe(e).c_str());
            return false;
        }
    }

    // 解：优先问扩展（那是求解器自己报的 X/F）；没有扩展就退回问题——远端
    // Solve 之后会经 SetMINLPVariableValues 把解写回本地问题，F 在本地重算。
    void fetchSolution() {
        const int n = problem_->numVariables();
        const int m = problem_->numConstraints();
        if (!CORBA::is_nil(ext_.in())) {
            try {
                ct::CapeArrayDouble_var x = ext_->GetX();
                cape_corba::fromDoubleSeq(x.in(), x_);
            } catch (const CORBA::Exception& e) {
                log(ZLOG_WARNI, "[%s] GetX: %s", name_.c_str(), describe(e).c_str());
            }
            try {
                ct::CapeArrayDouble_var f = ext_->GetF();
                cape_corba::fromDoubleSeq(f.in(), f_);
            } catch (const CORBA::Exception& e) {
                log(ZLOG_WARNI, "[%s] GetF: %s", name_.c_str(), describe(e).c_str());
            }
        }
        if (x_.empty() && adapter_ && n > 0) {
            adapter_->getVariableValues({}, x_);  // 远端最后一次 SetMINLPVariableValues 的值
        }
        if (f_.empty() && n > 0 && m >= 0 && !x_.empty()) {
            f_.assign(static_cast<size_t>(m) + 1, 0.0);
            problem_->setX(x_.data(), static_cast<int>(x_.size()));
            problem_->evaluateObjective(f_[0]);
            if (m > 0) problem_->evaluateConstraints(f_.data() + 1, m);
        }
    }

    static int copyOut(const std::vector<double>& src, double* dst, int dst_size) {
        if (src.empty() || dst == nullptr || dst_size < static_cast<int>(src.size())) return -1;
        std::copy(src.begin(), src.end(), dst);
        return static_cast<int>(src.size());
    }

    static void copyAccepted(const ct::CapeArrayBoolean& accepted, boolean* out, int n) {
        for (int i = 0; i < n; ++i) {
            out[i] = (static_cast<CORBA::ULong>(i) < accepted.length() &&
                      accepted[static_cast<CORBA::ULong>(i)])
                         ? 1
                         : 0;
        }
    }

    void disconnect() {
        // 顺序：先让远端放手（它握着我们问题对象的引用），再注销自己的问题对象。
        if (!CORBA::is_nil(ext_.in())) {
            try {
                ext_->Release();
            } catch (const CORBA::Exception&) {
                // 远端可能已经不在了；本地清理照做。
            }
        }
        ext_ = ::XOPTCO::IXOptMINLPSystemExtension::_nil();
        system_ = mi::ICapeMINLPSystem::_nil();
        manager_ = mi::ICapeMINLPSolverManager::_nil();
        if (!CORBA::is_nil(problem_ref_.in())) {
            std::string err;
            OrbHolder* h = ensureOrb(err);
            if (h != nullptr) {
                try {
                    h->poa->deactivate_object(problem_oid_.in());
                } catch (const CORBA::Exception&) {
                }
            }
            problem_ref_ = CORBA::Object::_nil();
        }
        adapter_.reset();
    }

    void log(ZLOG_LEVEL level, const char* format, ...) const {
        char buf[1024];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        if (log_ != nullptr) {
            log_(level, "%s", buf);
        } else {
            std::fprintf(stderr, "xRtoCapeOpenSolver: %s\n", buf);
        }
    }

    xOptProblem* problem_;
    std::string name_;
    xOptLogFunc log_;

    std::unique_ptr<XOptMINLPAdapter> adapter_;
    PortableServer::ObjectId_var problem_oid_;
    CORBA::Object_var problem_ref_;

    mi::ICapeMINLPSolverManager_var manager_;
    mi::ICapeMINLPSystem_var system_;
    ::XOPTCO::IXOptMINLPSystemExtension_var ext_;

    std::vector<double> x_;
    std::vector<double> f_;
    int result_ = RESULT_UNKNOWN;

    // const 接口要交出 const char*，得有人持有那些字符串。
    mutable std::vector<std::string> tunable_names_;
    mutable std::vector<OPTION_TYPE> tunable_types_;
    mutable std::vector<std::string> string_cache_;
    mutable bool warned_no_ext_ = false;
};

void hostLog(xOptLogFunc log, ZLOG_LEVEL level, const std::string& text) {
    if (log != nullptr) {
        log(level, "%s", text.c_str());
    } else {
        std::fprintf(stderr, "xRtoCapeOpenSolver: %s\n", text.c_str());
    }
}

}  // namespace

extern "C" {

// 宿主（xOpt.cpp::loadSolver）认的两个导出。
__declspec(dllexport) xOptSolver* createSolver(const char* name, xOptProblem* problem,
                                               xOptLogFunc logFunc) {
    if (problem == nullptr) {
        hostLog(logFunc, ZLOG_ERROR, "createSolver: problem is null");
        return nullptr;
    }
    std::string source;
    const std::string target = resolveTarget(name, source);
    if (target.empty()) {
        // 配置错误在 createSolver 就报，越早越靠近病灶。
        const std::string sidecar = sidecarPath();
        hostLog(logFunc, ZLOG_ERROR,
                "xRtoCapeOpenSolver: no CAPE-OPEN solver target configured.\n"
                "  put one line in    " +
                    (sidecar.empty() ? std::string("<dll>.target") : sidecar) +
                    "\n"
                    "  or set             XRTO_CAPEOPEN_SOLVER_TARGET\n"
                    "  accepted forms:\n"
                    "    corba:corbaname::localhost:24567#xopt/solver\n"
                    "    corba:IOR:0100...");
        return nullptr;
    }
    hostLog(logFunc, ZLOG_INFOR, "xRtoCapeOpenSolver: target = " + target + "   (from " + source + ")");

    std::unique_ptr<CapeOpenRemoteSolver> solver(new CapeOpenRemoteSolver(name, problem, logFunc));
    if (solver->connect(target) < 0) return nullptr;
    return solver.release();
}

__declspec(dllexport) void destroySolver(xOptSolver* solver) { delete solver; }

}  // extern "C"
