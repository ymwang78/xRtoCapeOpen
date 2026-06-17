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
//    1) 环境变量 XRTO_CAPEOPEN_TARGET（部署期配置，如 "com:Prog.Id" / "corba:IOR:..."）
//    2) createModel 的 name 形参（若自带 com:/corba:/mock: scheme）
//    3) 回退 "mock:default"（便于无外部组件时端到端冒烟）
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

#include <cstdlib>
#include <mutex>
#include <new>
#include <string>

#include "CapeBackendFactory.h"
#include "CapeMINLPProblemCore.h"

#ifdef CAPEOPEN_WITH_COM
void CapeRegisterComBackend();  // backend/com/CapeRegisterComBackend.cpp
#endif

namespace {

// 一次性把已编译进来的后端注册到工厂（com 在 WIN32+WITH_CAPEOPEN_COM 时）。
void ensureBackendsRegistered() {
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef CAPEOPEN_WITH_COM
        CapeRegisterComBackend();
#endif
        // mock 为 core 内置，无需注册；corba 后端在其 DLL 内注册（M4）。
    });
}

// xOptModelHandle 背后的上下文：只保存解析后的连接串，buildProblem 时按需建后端。
struct CapeOpenModelContext {
    std::string conn;
};

inline CapeOpenModelContext* ctxOf(xOptModelHandle h) {
    return reinterpret_cast<CapeOpenModelContext*>(h);
}

// 解析连接目标（见文件头优先级说明）。
std::string resolveConnection(const char* name) {
    if (const char* env = std::getenv("XRTO_CAPEOPEN_TARGET")) {
        if (env[0] != '\0') return env;
    }
    if (name != nullptr && name[0] != '\0') {
        auto [scheme, target] = CapeBackendFactory::parse(name);
        (void)target;
        if (!scheme.empty()) return name;  // name 自带 com:/corba:/mock:/.dll
    }
    return "mock:default";
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

int t_getInPortNum(xOptModelHandle /*h*/) { return 0; }

int t_getOutPortNum(xOptModelHandle /*h*/) { return 0; }

int t_getInPortVariableMap(xOptModelHandle /*h*/, int /*port*/, const char* /*streamNames*/[],
                           const char* /*variableNames*/[], int& size) {
    size = 0;
    return 0;
}

int t_getOutPortVariableMap(xOptModelHandle /*h*/, int /*port*/, const char* /*streamNames*/[],
                            const char* /*variableNames*/[], int& size) {
    size = 0;
    return 0;
}

int t_getFixableVariables(xOptModelHandle /*h*/, const char* /*names*/[], double /*initial*/[],
                          int& size) {
    size = 0;
    return 0;
}

// ---- 以下为可选方法（无热力学/slate/report），给安全桩避免空指针 ----

int t_setLanguage(xOptModelHandle /*h*/, const char* /*language_code*/) { return 0; }

int t_getNumberOfSlate(xOptModelHandle /*h*/) { return 0; }

int t_getSlateIdOfPort(xOptModelHandle /*h*/, bool /*is_input_port*/, int /*port_index*/) {
    return -1;  // 无 slate
}

int t_setSlate(xOptModelHandle /*h*/, int /*slate_index*/, const xOptSlate* /*slate*/) { return 0; }

int t_generateEstimate(xOptModelHandle /*h*/, double /*initx*/[], int& size,
                       const char /*fixed_var_names*/[], const double /*fixed_var_values*/[],
                       int /*fixed_var_size*/) {
    size = 0;
    return 0;
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
    ctx->conn = resolveConnection(name);

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
