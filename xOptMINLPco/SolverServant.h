#pragma once
// ***************************************************************
//  SolverServant   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  把一个 xOptSolver DLL（导出 createSolver / destroySolver，见
//  include/xOpt/xOptSolver.h）发布成 CAPE-OPEN 的求解器组件：
//
//    SolverManagerServant -> ICapeMINLPSolverManager
//        CreateMINLPSystem(theMINLP)：收下**客户端**的 ICapeMINLP 引用，
//        为它建一个 MINLPSystemServant 交回去。
//    MINLPSystemServant   -> ICapeMINLPSystem（+ XOPTCO 扩展）
//        Solve / GetParameters 是标准的；求解结果码、X/F、选项通道、
//        Release 走 IXOptMINLPSystemExtension（见 XOPTCO_Ext.idl）。
//
//  方向与 MINLPServant 相反。MINLPServant 把**本地**问题发布出去；这里是
//  把**远端**问题拉进来喂给本地求解器：
//
//    远端 ICapeMINLP（在客户端进程里，通常是 xRto 里的 xRtoCapeOpenSolver.dll）
//      -> CapeMINLPModelCorba（注入引用）          capeopen_core 的 CORBA 后端
//        -> CapeMINLPProblemCore -> xOptProblemT    capeopen_core 的映射核心
//          -> XOptProblemFromVtable -> xOptProblem  本目录
//            -> createSolver(...)                    被包装的求解器 DLL
//
//  于是求解器每一次 setX/evaluate* 都是一次回到客户端的 IIOP 往返。这是
//  这条通路的本性：问题在谁手里，谁就得被回调。
//
//  线程：Solve() 在 ORB 的派发线程上同步跑完。求解器回调远端问题时，
//  客户端那头必须还能受理请求——TAO 默认的 leader-follower 等待策略允许
//  嵌套上行调用，xRtoCapeOpenSolver.dll 就靠它。服务端本身单线程派发，
//  一个 Solve 跑着时别的请求排队。
//
//  异常纪律与 MINLPServant 一致：模型/求解器侧失败抛 IDL 里声明过的
//  ECapeUnknown / ECapeSolvingError，参数不合法抛 ECapeInvalidArgument；
//  不用系统异常，也不返回空值冒充真答案。
// ***************************************************************
// XOPTINTERFACE_EXPORTS 的处理见 XOptProblemFromVtable.h 顶部：它必须是本 TU
// 第一个碰到 xOpt 头文件的包含。
#include "XOptProblemFromVtable.h"

#include <tao/PortableServer/PortableServer.h>

#include <memory>
#include <string>
#include <vector>

#include "CAPEOPEN100_MinlpS.h"  // POA_...::ICapeMINLPSolverManager
#include "XOPTCO_ExtS.h"         // POA_XOPTCO::IXOptMINLPSystemExtension
#include "xOpt/xOptSolver.h"

// —— 被包装的求解器 DLL ——
//
// 加载一次，按需创建实例。createSolver/destroySolver 就是宿主
// （libsrc/xOpt/src/xOpt.cpp::loadSolver）认的那两个导出。
class XOptSolverLibrary {
  public:
    explicit XOptSolverLibrary(std::string dll_path);
    ~XOptSolverLibrary();

    XOptSolverLibrary(const XOptSolverLibrary&) = delete;
    XOptSolverLibrary& operator=(const XOptSolverLibrary&) = delete;

    // 返回 >=0 成功；失败原因在 lastError()。
    int load();
    bool loaded() const { return create_ != nullptr; }

    xOptSolver* create(const char* name, xOptProblem* problem, xOptLogFunc log);
    void destroy(xOptSolver* solver);

    const std::string& path() const { return dll_path_; }
    const std::string& lastError() const { return last_error_; }

  private:
    std::string dll_path_;
    void* module_ = nullptr;
    CreateSolverFunc create_ = nullptr;
    DestroySolverFunc destroy_ = nullptr;
    std::string last_error_;
};

// —— 一个求解器实例，绑在一个远端问题上 ——
class MINLPSystemServant : public POA_XOPTCO::IXOptMINLPSystemExtension {
  public:
    // library 必须已 load；本对象不拥有它。poa 用于激活参数对象与注销自己。
    // problem 是客户端交来的 ICapeMINLP；name 给 createSolver 当实例名。
    MINLPSystemServant(XOptSolverLibrary* library, PortableServer::POA_ptr poa,
                       ::CAPEOPEN100::Business::Numeric::Minlp::ICapeMINLP_ptr problem,
                       std::string name, xOptLogFunc log);
    ~MINLPSystemServant() override;

    // 连上远端问题、读结构、建求解器、发布参数集合。返回 <0 时原因在 initError()。
    // 放在构造之外，因为这里面的每一步都会跨进程，失败是常态而非例外。
    int init();
    const std::string& initError() const { return init_error_; }

    // 激活之后由管理器登记，Release 要用它注销自己。
    void setSelfOid(const PortableServer::ObjectId& oid);

    // ---- ICapeIdentification ----
    char* GetComponentName() override;
    char* GetComponentDescription() override;
    void SetComponentName(const char* name) override;
    void SetComponentDescription(const char* desc) override;

    // ---- ICapeMINLPSystem ----
    void Solve() override;
    CORBA::Object_ptr GetParameters() override;

    // ---- IXOptMINLPSystemExtension ----
    void GetTunableParameters(::CAPEOPEN100::Common::Types::CapeArrayString_out names,
                              ::CAPEOPEN100::Common::Types::CapeArrayLong_out types) override;
    void SetTunableParameters(const ::CAPEOPEN100::Common::Types::CapeArrayString& names) override;
    void GetIntOptions(const ::CAPEOPEN100::Common::Types::CapeArrayString& names,
                       ::CAPEOPEN100::Common::Types::CapeArrayLong_out values) override;
    void SetIntOptions(const ::CAPEOPEN100::Common::Types::CapeArrayString& names,
                       const ::CAPEOPEN100::Common::Types::CapeArrayLong& values,
                       ::CAPEOPEN100::Common::Types::CapeArrayBoolean_out accepted) override;
    void GetDoubleOptions(const ::CAPEOPEN100::Common::Types::CapeArrayString& names,
                          ::CAPEOPEN100::Common::Types::CapeArrayDouble_out values) override;
    void SetDoubleOptions(const ::CAPEOPEN100::Common::Types::CapeArrayString& names,
                          const ::CAPEOPEN100::Common::Types::CapeArrayDouble& values,
                          ::CAPEOPEN100::Common::Types::CapeArrayBoolean_out accepted) override;
    void GetStringOptions(const ::CAPEOPEN100::Common::Types::CapeArrayString& names,
                          ::CAPEOPEN100::Common::Types::CapeArrayString_out values) override;
    void SetStringOptions(const ::CAPEOPEN100::Common::Types::CapeArrayString& names,
                          const ::CAPEOPEN100::Common::Types::CapeArrayString& values,
                          ::CAPEOPEN100::Common::Types::CapeArrayBoolean_out accepted) override;
    ::CAPEOPEN100::Common::Types::CapeLong GetSolveResult() override;
    ::CAPEOPEN100::Common::Types::CapeArrayDouble* GetX() override;
    ::CAPEOPEN100::Common::Types::CapeArrayDouble* GetF() override;
    ::CAPEOPEN100::Common::Types::CapeArrayDouble* GetXmul() override;
    ::CAPEOPEN100::Common::Types::CapeArrayDouble* GetFmul() override;
    ::CAPEOPEN100::Common::Types::CapeLong PauseSolve() override;
    ::CAPEOPEN100::Common::Types::CapeLong ContinueSolve() override;
    void Release() override;

    // 供测试/服务端观察
    xOptSolver* solver() const { return solver_; }
    int lastResult() const { return result_; }

  private:
    // 求解器与问题链一起拆掉；幂等。
    void teardown();
    void buildParameters();
    void destroyParameters();
    // 求解器或问题已被 Release 掉时抛 ECapeUnknown。
    void requireLive(const char* operation) const;
    ::CAPEOPEN100::Common::Types::CapeArrayDouble* fetchVector(const char* operation,
                                                              bool from_x, bool multipliers);

    XOptSolverLibrary* library_;
    PortableServer::POA_var poa_;
    ::CAPEOPEN100::Business::Numeric::Minlp::ICapeMINLP_var remote_;
    std::string name_;
    xOptLogFunc log_;

    // 问题链的所有权：XOptProblemFromVtable 拥有 CapeMINLPProblemCore（经
    // destroyProblem），core 拥有 CapeMINLPModelCorba。析构顺序：先求解器，
    // 再问题——求解器手里拿着问题的裸指针。
    std::unique_ptr<XOptProblemFromVtable> problem_;
    xOptSolver* solver_ = nullptr;
    int num_variables_ = 0;
    int num_constraints_ = 0;

    int result_ = xOptSolver::RESULT_UNKNOWN;
    bool solved_ = false;
    bool released_ = false;
    std::string init_error_;

    std::vector<CORBA::Object_var> parameter_refs_;
    CORBA::Object_var parameters_collection_;
    PortableServer::ObjectId_var self_oid_;
    bool has_self_oid_ = false;

    std::string comp_name_;
    std::string comp_desc_;
};

// —— 求解器管理器：每来一个问题，建一个系统 ——
class SolverManagerServant
    : public POA_CAPEOPEN100::Business::Numeric::Minlp::ICapeMINLPSolverManager {
  public:
    // library 必须已 load；本对象不拥有它。log 会交给每个 createSolver。
    SolverManagerServant(XOptSolverLibrary* library, PortableServer::POA_ptr poa,
                         xOptLogFunc log);

    // ---- ICapeIdentification ----
    char* GetComponentName() override;
    char* GetComponentDescription() override;
    void SetComponentName(const char* name) override;
    void SetComponentDescription(const char* desc) override;

    // ---- ICapeMINLPSolverManager ----
    void CreateMINLPSystem(CORBA::Object_ptr theMINLP, CORBA::Object_out theMINLPSystem) override;

    // 至今建过几个系统（供测试观察）。
    int systemsCreated() const { return systems_created_; }

  private:
    XOptSolverLibrary* library_;
    PortableServer::POA_var poa_;
    xOptLogFunc log_;
    int systems_created_ = 0;
    std::string comp_name_;
    std::string comp_desc_;
};
