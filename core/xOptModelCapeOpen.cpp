// ***************************************************************
//  xOptModelCapeOpen   version:  1.1   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  DLL 对 xOpt 的唯一 C 入口：xOptModel_createModel。
//  本 DLL 是一个【普通黑箱模型 DLL】，以 type_name="BlackBox" 经 xOpt 既有
//  xOptModelBlackBox 通路加载（xOpt 不感知 CAPE-OPEN，design §4）。
//
//  因此必须填充【完整】xOptModelT vtable：xOptModelBlackBox 的
//  initializeModel()/prepareRuntime() 会调用 getParameters(必须≥1)/setParameters/
//  validateModel/getInPortNum/getInPortVariableMap/getOutPortNum 等（design §4.2）。
//  纯优化黑箱里这些多为平凡桩。真正的问题映射在 buildProblem -> CapeMINLPProblemCore。
//
//  连接目标解析优先级（design §4.3）：
//    1) createModel 的 name 形参（若自带 com:/corba:/mock: scheme）—— 最具体
//    2) 本 DLL 同目录的 <DLL 基名>.target 文件 —— 每个部署目录一份
//    3) 环境变量 XRTO_CAPEOPEN_TARGET —— 全局默认 / 调试覆盖
//    4) 都没有 -> **失败**，不回退
//
//  为什么加第 2 条：宿主只把 DLL 路径传给黑箱 DLL（xOptModelBlackBox 调
//  createModel 时 name 写死是 "BlackBoxModel"），UnitModel.json 里其余字段一个
//  都到不了这里。于是唯一的按单元配置手段就是"DLL 自己旁边的文件"——这也正是
//  这套部署已有的习惯（RCC/RCC_Model.json、Splitter/Splitter_Model.json）。
//  两个单元要连不同的服务端，就各给一个目录、各放一份 DLL 与 .target，
//  跟 RCC 与 RCC_CapeOpen 的做法一致。
//
//  为什么 .target 排在环境变量**前面**：环境变量是全局的，一旦设了就会把所有
//  单元一起盖掉；而 .target 是那个单元自己的。最具体的优先，是唯一不会让人
//  意外的顺序。环境变量因此退化为"只有一个目标时的省事写法"与调试覆盖。
//
//  文件格式故意是纯文本一行，不是 JSON：只有一个值，没有 schema，也就没有
//  解析器和编码陷阱。'#' 开头的行与空行忽略。
//
//  第 3 条原先是"回退 mock:default"，2026-09 改掉。那个默认值害人：宿主传进来的
//  name 写死是 "BlackBoxModel"（xOptModelBlackBox.cpp），不带 scheme，所以只要
//  环境变量没设到 xRto 进程里，这个 DLL 就会安安静静地拿内置 mock（一个二变量
//  的小 NLP）冒充用户配的那个远端模型——而且**求解会成功**。用户看到的是一个
//  解，只是解的不是他的问题。这种失败方式比连不上恶劣得多。
//  mock 仍然可用，但必须显式点名：XRTO_CAPEOPEN_TARGET=mock:default。
//
//  注意：本 TU 导出 xOptModel_createModel，故在包含 xOpt 头之前定义
//  XOPTINTERFACE_EXPORTS，使 XOPTIF_API 解析为 dllexport（design §4 / §6.5）。
// ***************************************************************
#ifdef _WIN32
#    ifndef XOPTINTERFACE_EXPORTS
#        define XOPTINTERFACE_EXPORTS
#    endif
#endif

#include "xOpt/xOptModel.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <new>
#include <string>

#include "CapeBackendFactory.h"
#include "CapeMINLPProblemCore.h"
#ifdef CAPEOPEN_WITH_CORBA
#include "backend/corba/CapeUnitCorba.h"
#endif

#ifdef CAPEOPEN_WITH_COM
void CapeRegisterComBackend();  // backend/com/CapeRegisterComBackend.cpp
#endif
#ifdef CAPEOPEN_WITH_CORBA
void CapeRegisterCorbaBackend();  // backend/corba/CapeRegisterCorbaBackend.cpp
#endif

namespace {

// 一次性把已编译进来的后端注册到工厂。
// 编译进来的都在这里注册：先前只注册了 COM，注释说"corba 后端在其 DLL 内注册"，
// 但 CapeRegisterCorbaBackend() 全仓库没有任何调用点——于是 "corba:IOR:..." 一路
// 走到 CapeBackendFactory::create 才报「未注册的后端 scheme: corba」，
// 而那时错误信息还没有日志出口，对上层只表现为 buildProblem 返回 -1。
void ensureBackendsRegistered() {
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef CAPEOPEN_WITH_COM
        CapeRegisterComBackend();
#endif
#ifdef CAPEOPEN_WITH_CORBA
        CapeRegisterCorbaBackend();
#endif
        // mock 为 core 内置，无需注册。
    });
}

// xOptModelHandle 背后的上下文：连接串，加一份**读回来就不再变**的接线信息。
//
// 为什么把接线信息按值缓存下来：宿主要的是 const char*（端口映射、可固定变量
// 名），那些指针必须活过调用返回；而远端读回来的 CORBA 字符串是临时的。
// 缓存在这里，指针的有效期就等于模型对象的有效期，正是宿主期望的。
//
// 另一层原因是时序：xOptModelBlackBox 在 buildProblem **之前**就问端口
// （prepareRuntime），而 buildProblem 会把问题后端的所有权交给
// CapeMINLPProblemCore。两件事共用一个连接会把所有权搅在一起，所以接线信息
// 走一次独立的"连上、读完、断开"。
struct CapeOpenModelContext {
    std::string conn;
#ifdef CAPEOPEN_WITH_CORBA
    bool topology_read = false;
    CapeUnitCorba topology;
    std::vector<const CapeUnitPort*> in_ports;
    std::vector<const CapeUnitPort*> out_ports;
#endif
};

#ifdef CAPEOPEN_WITH_CORBA
// 首次需要时读一遍。
//
// **只有成功才置位**。先前是先置位再读，于是读失败（服务端还没起、名字还指着
// 上一个已退出的进程……）就把"没有端口"永久钉死在这个模型实例上，之后再也不
// 重试——而宿主拿这个 0 去建端口锚点（UnitModel::fixUnitPort），结果就是单元
// 画出来没有接入点，且没有任何提示。一次瞬时失败不该有终身后果。
//
// 远端不是单元（老部署的纯 ICapeMINLP 目标）则是**确定**的答案，置位不再重试。
void ensureTopology(CapeOpenModelContext* ctx) {
    if (ctx == nullptr || ctx->topology_read) return;
    if (ctx->conn.rfind("corba:", 0) != 0) {
        ctx->topology_read = true;  // 非 CORBA 目标：确定没有单元
        return;
    }
    if (ctx->topology.read(ctx->conn.substr(6)) < 0) {
        // 不置位：下次再问端口时重试。同时把原因说出来——静默返回 0 个端口
        // 正是先前那个"画不出接入点又查不到原因"的成因。
        std::fprintf(stderr, "xRtoCapeOpen: cannot read the unit topology from %s: %s\n",
                     ctx->conn.c_str(), ctx->topology.lastError().c_str());
        return;
    }
    ctx->topology_read = true;
    ctx->in_ports.clear();
    ctx->out_ports.clear();
    for (const CapeUnitPort& p : ctx->topology.ports()) {
        (p.is_input ? ctx->in_ports : ctx->out_ports).push_back(&p);
    }
}
#endif

inline CapeOpenModelContext* ctxOf(xOptModelHandle h) {
    return reinterpret_cast<CapeOpenModelContext*>(h);
}

// 本 DLL 自己的完整路径。宿主不会告诉我们，但模块可以自问。
std::string thisModulePath() {
#ifdef _WIN32
    HMODULE self = nullptr;
    // 取"包含本函数地址的那个模块"，并且不加引用计数——只是问路径，
    // 加了引用就得记得放，而这个 DLL 的卸载时机不由我们决定。
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&thisModulePath), &self) == 0) {
        return std::string();
    }
    wchar_t buf[MAX_PATH * 2] = {0};
    const DWORD n = GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0,
                                        nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), &out[0], len, nullptr, nullptr);
    return out;
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&thisModulePath), &info) == 0 || info.dli_fname == nullptr) {
        return std::string();
    }
    return info.dli_fname;
#endif
}

// 读 <DLL 路径去掉扩展名>.target。读不到返回空串——"没有这个文件"是常态。
std::string readSidecarTarget(std::string* path_out) {
    const std::string dll = thisModulePath();
    if (dll.empty()) return std::string();
    const size_t dot = dll.find_last_of('.');
    const size_t sep = dll.find_last_of("/\\");
    // 只在最后一段里找扩展名的点，免得目录名里的点被当成扩展名。
    const std::string base =
        (dot != std::string::npos && (sep == std::string::npos || dot > sep)) ? dll.substr(0, dot)
                                                                             : dll;
    const std::string path = base + ".target";
    std::ifstream in(path);
    if (!in) return std::string();
    if (path_out != nullptr) *path_out = path;
    std::string line;
    while (std::getline(in, line)) {
        // 去掉行尾的 CR（文件多半是 Windows 记事本存的）与两端空白
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

// 解析连接目标（见文件头优先级说明）。source_out 记下它来自哪儿，报错时要用。
// 宿主可能把连接串当文件路径改写过。
//
// xRto 的 FlowSheetStyle 对每个 *Path 字段都做「文件不存在就拼上 UnitModelPath
// 前缀、再转绝对路径」，而 ProblemPath 正是我们用来传连接串的字段。于是
// "corba:corbaname::host:port#name" 到这里会变成
// "D:/.../UnitModel/corba:corbaname::host:port#name"。
//
// 从里面把 scheme 起始处捞回来。这是**修补宿主的已知改写**，不是通用解析：
// 只认我们自己支持的三个 scheme，且要求它们出现在路径分隔符之后，免得把
// 一个真的叫 "com:" 的目录名当成连接串。
std::string recoverMangledTarget(const std::string& text) {
    static const char* kSchemes[] = {"corba:", "com:", "mock:"};
    for (const char* scheme : kSchemes) {
        const size_t at = text.find(scheme);
        if (at == std::string::npos || at == 0) continue;
        const char before = text[at - 1];
        if (before != '/' && before != '\\') continue;
        std::string target = text.substr(at);
        // 路径规范化还会把串**里面**的 '/' 换成 '\'，于是
        // "corbaname::host:port#xopt/unit" 变成 "...#xopt\unit"，
        // string_to_object 直接返回 nil。这里一并还原。
        //
        // 只在恢复路径上做：完好送达的目标不碰。com: 的 ProgID 与
        // corbaname/corbaloc/IOR 都不含有意义的反斜杠；而真正以 .dll 结尾的
        // 目标走的是另一条分支（parse 认得出 scheme），根本到不了这里，
        // 所以不会把 Windows 路径里的分隔符改坏。
        for (char& c : target) {
            if (c == '\\') c = '/';
        }
        return target;
    }
    return std::string();
}

std::string resolveConnection(const char* name, std::string* source_out) {
    if (name != nullptr && name[0] != '\0') {
        auto [scheme, target] = CapeBackendFactory::parse(name);
        (void)target;
        if (!scheme.empty()) {
            if (source_out != nullptr) *source_out = "the name passed to createModel";
            return name;  // name 自带 com:/corba:/mock:/.dll
        }
        const std::string recovered = recoverMangledTarget(name);
        if (!recovered.empty()) {
            if (source_out != nullptr) {
                *source_out = "the name passed to createModel (recovered from a path the host "
                              "rewrote it into)";
            }
            return recovered;
        }
    }
    std::string sidecar_path;
    const std::string sidecar = readSidecarTarget(&sidecar_path);
    if (!sidecar.empty()) {
        if (source_out != nullptr) *source_out = sidecar_path;
        return sidecar;
    }
    if (const char* env = std::getenv("XRTO_CAPEOPEN_TARGET")) {
        if (env[0] != '\0') {
            if (source_out != nullptr) *source_out = "XRTO_CAPEOPEN_TARGET";
            return env;
        }
    }
    return std::string();  // 未配置：由调用方报错，不猜
}

// ---- 核心：buildProblem 构造问题 vtable ----
int t_buildProblem(xOptModelHandle h, xOptProblemT* problem) {
    try {
        if (h == nullptr || problem == nullptr) return -1;
        CapeOpenModelContext* ctx = ctxOf(h);

        std::string err;
        std::unique_ptr<ICapeMINLPModel> backend =
            CapeBackendFactory::instance().create(ctx->conn, err);
        if (!backend) {
            return -1;  // err 可在接入日志后输出
        }

        CapeMINLPProblemCore* core = new CapeMINLPProblemCore(std::move(backend));
        if (core->initialize() < 0) {
            delete core;
            return -1;
        }
        // 所有权转移到 xOptProblemT：宿主经 problem->destroyProblem 释放 core。
        core->fillVtable(problem);
        return 0;
    } catch (...) {
        return -1;
    }
}

void t_destroyModel(xOptModelHandle h) {
    try {
        delete ctxOf(h);
    } catch (...) {
    }
}

// ---- 以下为 xOptModelBlackBox 通路要求非空 / 会调用的方法 ----

// 模型参数：纯优化黑箱无可调参数，但 initializeModel() 要求至少返回 1 个，
// 否则判定模型无效。这里给一个占位参数（连接目标走环境变量，不经此通道）。
const char* kReservedParamName = "reserved";

int t_getParameters(xOptModelHandle /*h*/, const char* names[], double values[], int& size) {
    if (names == nullptr || values == nullptr) {
        size = 1;  // 第一段：查个数
        return 0;
    }
    if (size >= 1) {
        names[0] = kReservedParamName;
        values[0] = 0.0;
        size = 1;
    }
    return 0;
}

int t_setParameters(xOptModelHandle /*h*/, const char* /*name*/[], double /*value*/[], int /*size*/) {
    return 0;  // 占位参数无副作用
}

int t_setProblemType(xOptModelHandle /*h*/, XOPTF_PROBLEM_TYPE /*type*/) { return 0; }

int t_validateModel(xOptModelHandle /*h*/) { return 0; }

#ifdef CAPEOPEN_WITH_CORBA
// 端口映射的两段式实现，进出口共用。
int portVariableMap(xOptModelHandle h, bool is_input, int port, const char* streamNames[],
                    const char* variableNames[], int& size) {
    CapeOpenModelContext* ctx = ctxOf(h);
    if (ctx == nullptr) return -1;
    ensureTopology(ctx);
    const std::vector<const CapeUnitPort*>& ports = is_input ? ctx->in_ports : ctx->out_ports;
    if (port < 0 || port >= static_cast<int>(ports.size())) return -1;
    const CapeUnitPort& p = *ports[static_cast<size_t>(port)];
    const int count = static_cast<int>(p.variables.size());
    if (streamNames == nullptr && variableNames == nullptr) {  // 第一段：查个数
        size = count;
        return 0;
    }
    if (size < count) return -1;
    for (int i = 0; i < count; ++i) {
        // c_str() 指向 ctx->topology 里的 std::string，与模型对象同寿。
        if (streamNames != nullptr) streamNames[i] = p.variables[i].first.c_str();
        if (variableNames != nullptr) variableNames[i] = p.variables[i].second.c_str();
    }
    size = count;
    return count;
}
#endif

int t_getInPortNum(xOptModelHandle h) {
#ifdef CAPEOPEN_WITH_CORBA
    CapeOpenModelContext* ctx = ctxOf(h);
    if (ctx == nullptr) return 0;
    ensureTopology(ctx);
    return static_cast<int>(ctx->in_ports.size());
#else
    (void)h;
    return 0;
#endif
}

int t_getOutPortNum(xOptModelHandle h) {
#ifdef CAPEOPEN_WITH_CORBA
    CapeOpenModelContext* ctx = ctxOf(h);
    if (ctx == nullptr) return 0;
    ensureTopology(ctx);
    return static_cast<int>(ctx->out_ports.size());
#else
    (void)h;
    return 0;
#endif
}

int t_getInPortVariableMap(xOptModelHandle h, int port, const char* streamNames[],
                           const char* variableNames[], int& size) {
#ifdef CAPEOPEN_WITH_CORBA
    return portVariableMap(h, /*is_input*/ true, port, streamNames, variableNames, size);
#else
    (void)h; (void)port; (void)streamNames; (void)variableNames;
    size = 0;
    return 0;
#endif
}

int t_getOutPortVariableMap(xOptModelHandle h, int port, const char* streamNames[],
                            const char* variableNames[], int& size) {
#ifdef CAPEOPEN_WITH_CORBA
    return portVariableMap(h, /*is_input*/ false, port, streamNames, variableNames, size);
#else
    (void)h; (void)port; (void)streamNames; (void)variableNames;
    size = 0;
    return 0;
#endif
}

int t_getFixableVariables(xOptModelHandle h, const char* names[], double initial[], int& size) {
#ifdef CAPEOPEN_WITH_CORBA
    CapeOpenModelContext* ctx = ctxOf(h);
    if (ctx == nullptr) return -1;
    ensureTopology(ctx);
    const std::vector<std::pair<std::string, double>>& f = ctx->topology.fixableVariables();
    const int count = static_cast<int>(f.size());
    if (names == nullptr && initial == nullptr) {
        size = count;
        return 0;
    }
    if (size < count) return -1;
    for (int i = 0; i < count; ++i) {
        if (names != nullptr) names[i] = f[i].first.c_str();
        if (initial != nullptr) initial[i] = f[i].second;
    }
    size = count;
    return count;
#else
    (void)h; (void)names; (void)initial;
    size = 0;
    return 0;
#endif
}

// ---- 以下为可选方法（无热力学/slate/report），给安全桩避免空指针 ----

int t_setLanguage(xOptModelHandle /*h*/, const char* /*language_code*/) { return 0; }

// slate：**推下去**。
//
// 组分表的所有者是流程图，不是模型——同一个单元接到哪股流股就用哪套组分。
// 先前这里只做核对（不一致就拒绝），方向是反的：那等于要求每张流程图去迁就
// 服务端启动时配的那套组分。现在 setSlate 把组分表转发给远端，远端按新组分
// 重建模型与问题，然后本地重读整份接线信息。
//
// 代价是一次 setSlate 会触发远端一次完整重建（createModel -> setSlate ->
// validate -> generateEstimate -> buildProblem）。所以只在**确实变了**的时候
// 推：宿主在准备阶段会不止一次走到这里，每次都推等于每次重建。
int t_getNumberOfSlate(xOptModelHandle h) {
#ifdef CAPEOPEN_WITH_CORBA
    CapeOpenModelContext* ctx = ctxOf(h);
    if (ctx == nullptr) return 0;
    ensureTopology(ctx);
    return ctx->topology.components().empty() ? 0 : 1;
#else
    (void)h;
    return 0;
#endif
}

int t_getSlateIdOfPort(xOptModelHandle h, bool /*is_input_port*/, int /*port_index*/) {
    return t_getNumberOfSlate(h) > 0 ? 0 : -1;  // 所有端口共用唯一的 slate
}

int t_setSlate(xOptModelHandle h, int slate_index, const xOptSlate* slate) {
#ifdef CAPEOPEN_WITH_CORBA
    CapeOpenModelContext* ctx = ctxOf(h);
    if (ctx == nullptr || slate == nullptr || slate_index != 0) return -1;
    ensureTopology(ctx);

    std::vector<std::string> wanted;
    for (int i = 0; i < XOPT_MAX_COMPONENTS && slate->components[i] != nullptr; ++i) {
        wanted.emplace_back(slate->components[i]);
    }
    if (wanted.empty()) return -1;
    if (wanted == ctx->topology.components()) return 0;  // 没变，不惊动远端

    if (ctx->topology.setComponents(wanted) < 0) {
        std::fprintf(stderr, "xRtoCapeOpen: setSlate failed: %s\n",
                     ctx->topology.lastError().c_str());
        return -1;
    }
    // 远端重建过了，本地按方向分组的那两份索引也得跟着重建。
    ctx->in_ports.clear();
    ctx->out_ports.clear();
    for (const CapeUnitPort& p : ctx->topology.ports()) {
        (p.is_input ? ctx->in_ports : ctx->out_ports).push_back(&p);
    }
    return 0;
#else
    (void)h; (void)slate_index; (void)slate;
    return 0;
#endif
}

// 「生成初值」：CAPE-OPEN MINLP 没有对应能力——ICapeMINLP 里没有任何一个方法
// 是「给定固定变量求一个好起点」的语义，GetMINLPVariableValues 只是读当前值，
// 而当前值恰恰就是宿主刚在 buildProblem 里 setX 推下来的那一组。所以这里正确的
// 行为是原样退回调用方给的点，而不是伪造一个。
//
// 关键：**必须原样保留 size**。宿主(xOptModelBlackBox::generateEstimate)按
// `estimate_size != init_x.size()` 判失败，写 size = 0 会让整个「生成初值」报错。
int t_generateEstimate(xOptModelHandle h, double initx[], int& size,
                       const char fixed_var_names[], const double fixed_var_values[],
                       int fixed_var_size) {
#ifdef CAPEOPEN_WITH_CORBA
    CapeOpenModelContext* ctx = ctxOf(h);
    if (ctx == nullptr) return -1;
    ensureTopology(ctx);
    if (!ctx->topology.isUnit()) return 0;  // 不是单元：保留调用方的值与 size

    // 宿主的打包格式：变量名以 ' ' 分隔拼在连续缓冲里，值数组一一对应。
    std::vector<std::string> names;
    std::vector<double> values;
    const char* p = fixed_var_names;
    for (int i = 0; i < fixed_var_size; ++i) {
        if (p == nullptr) return -1;
        names.emplace_back(p);
        values.push_back(fixed_var_values != nullptr ? fixed_var_values[i] : 0.0);
        p += names.back().size() + 1;
    }

    // 固定哪些是**流程图**说了算，不是服务端那份描述文件——进料由上游流股决定
    // 时一个都不该固定，而描述文件里通常把它们全列着。空表也照推。
    std::vector<double> x0;
    if (ctx->topology.generateEstimate(names, values, x0) < 0) {
        std::fprintf(stderr, "xRtoCapeOpen: generateEstimate failed: %s\n",
                     ctx->topology.lastError().c_str());
        return -1;
    }
    // 远端重建过，本地端口分组要重建
    ctx->in_ports.clear();
    ctx->out_ports.clear();
    for (const CapeUnitPort& port : ctx->topology.ports()) {
        (port.is_input ? ctx->in_ports : ctx->out_ports).push_back(&port);
    }

    if (initx == nullptr) {  // 第一段：只报个数
        size = static_cast<int>(x0.size());
        return 0;
    }
    // **size 必须原样保留**：宿主按 estimate_size != init_x.size() 判失败。
    // 远端给的维数对不上就不动调用方的向量，只是没有初值可填，不算错误。
    if (static_cast<int>(x0.size()) != size) return 0;
    for (int i = 0; i < size; ++i) initx[i] = x0[static_cast<size_t>(i)];
    return 0;
#else
    (void)h; (void)initx; (void)size;
    (void)fixed_var_names; (void)fixed_var_values; (void)fixed_var_size;
    return 0;
#endif
}

int t_getReportMetaAbstracts(xOptModelHandle /*h*/, const char* /*names*/[], const char* /*titles*/[],
                             const char* /*descriptions*/[], const char* /*display_types*/[],
                             int /*dim_size*/[], int& size) {
    size = 0;
    return 0;
}

int t_getReportMetaDims(xOptModelHandle /*h*/, const char* /*dim_names*/[], const char* /*dim_units*/[],
                        const char* /*name*/, int /*dim_size*/) {
    return 0;
}

int t_getReportData(xOptModelHandle /*h*/, double /*data*/[], int /*shape*/[], const char* /*name*/,
                    int& data_size, int& shape_size) {
    data_size = 0;
    shape_size = 0;
    return 0;
}

int t_getNumberOfThermoBlock(xOptModelHandle /*h*/) { return 0; }

int t_getThermoBlocks(xOptModelHandle /*h*/, int /*count*/, xOptThermoBlock /*blocks*/[]) {
    return 0;
}

const char* t_getVersion(xOptModelHandle /*h*/) { return "v1.0.0"; }

}  // namespace

// xOptModelT model = {sizeof(xOptModelT)};  其余字段由宿主零初始化。
extern "C" XOPTIF_API int xOptModel_createModel(xOptModelT* model, xOptPlatformT* /*platform*/,
                                                const char* name) {
    if (model == nullptr) return -1;
    ensureBackendsRegistered();
    CapeOpenModelContext* ctx = new (std::nothrow) CapeOpenModelContext();
    if (ctx == nullptr) return -1;
    std::string conn_source;
    ctx->conn = resolveConnection(name, &conn_source);
    if (ctx->conn.empty()) {
        // 在 createModel 就失败，而不是拖到 buildProblem：这是**配置**错误，
        // 立刻可判，越早报越靠近病灶。（目标配了但服务端没起，仍然是
        // createModel 成功、buildProblem 失败——那是运行期错误，不在此列。）
        const std::string self = thisModulePath();
        const size_t dot = self.find_last_of('.');
        const size_t sep = self.find_last_of("/\\");
        const std::string expected =
            self.empty() ? std::string("<dll>.target")
                         : ((dot != std::string::npos && (sep == std::string::npos || dot > sep))
                                ? self.substr(0, dot)
                                : self) +
                               ".target";
        std::fprintf(stderr,
                     "xRtoCapeOpen: no CAPE-OPEN target configured.\n"
                     "  put one line in    %s\n"
                     "  or set             XRTO_CAPEOPEN_TARGET\n"
                     "  accepted forms:\n"
                     "    corba:corbaname::localhost:24567#xopt/unit\n"
                     "    corba:IOR:0100...\n"
                     "    com:xOpt.MINLP.1\n"
                     "    mock:default   (the built-in 2-variable NLP, for smoke tests only)\n",
                     expected.c_str());
        delete ctx;
        return -1;
    }
    // 一行就够，但这一行值得永远打：连的是谁，是这条通路唯一说不清就查不下去的事。
    std::fprintf(stderr, "xRtoCapeOpen: target = %s   (from %s)\n", ctx->conn.c_str(),
                 conn_source.c_str());

    model->handle = reinterpret_cast<xOptModelHandle>(ctx);

    // 核心
    model->destroyModel = &t_destroyModel;
    model->buildProblem = &t_buildProblem;

    // xOptModelBlackBox 通路要求（initializeModel / prepareRuntime）
    model->getParameters = &t_getParameters;
    model->setParameters = &t_setParameters;
    model->setProblemType = &t_setProblemType;
    model->validateModel = &t_validateModel;
    model->getInPortNum = &t_getInPortNum;
    model->getOutPortNum = &t_getOutPortNum;
    model->getInPortVariableMap = &t_getInPortVariableMap;
    model->getOutPortVariableMap = &t_getOutPortVariableMap;
    model->getFixableVariables = &t_getFixableVariables;

    // 可选（安全桩）
    model->setLanguage = &t_setLanguage;
    model->getNumberOfSlate = &t_getNumberOfSlate;
    model->getSlateIdOfPort = &t_getSlateIdOfPort;
    model->setSlate = &t_setSlate;
    model->generateEstimate = &t_generateEstimate;
    model->getReportMetaAbstracts = &t_getReportMetaAbstracts;
    model->getReportMetaDims = &t_getReportMetaDims;
    model->getReportData = &t_getReportData;
    model->getNumberOfThermoBlock = &t_getNumberOfThermoBlock;
    model->getThermoBlocks = &t_getThermoBlocks;
    model->getVersion = &t_getVersion;
    return 0;
}
