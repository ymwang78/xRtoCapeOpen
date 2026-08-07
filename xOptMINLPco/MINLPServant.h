#pragma once
// ***************************************************************
//  MINLPServant   version:  2.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  CORBA/TAO 前端：POA 实现 CAPE-OPEN 官方模块路径下的
//  CAPEOPEN100::Business::Numeric::Minlp::ICapeMINLP，把调用委托给一个
//  ICapeMINLPModel（通常是 XOptMINLPAdapter，背靠 xOptProblem）。
//
//  v2 相对 v1 的两处实质变化（design §6）：
//
//  1. 基类从自造的 POA_SqpSolver::ICapeMINLP 换成官方模块路径的骨架。
//     _narrow 判的是 Repository ID，自造模块编出来的 servant 会被第三方
//     CO 客户端一律判否——方法签名对不对都一样。
//     RID: IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLP:1.0
//
//  2. 索引基。规范全篇 20 处明写变量/约束从 1 开始编号，vids/cids 合法范围
//     是 1..nv / 1..nc；而内部（ICapeMINLPModel、xOptProblem）一律 0-based。
//     本类是这条边界的生产端：入网 vids/cids 减一，出网结构索引加一。
//     详见 CapeCorbaMarshal.h 顶部。
//
//  未实现的方法（属性族、变量类型/约束线性性、Hessian、Lagrange）一律抛
//  CORBA::NO_IMPLEMENT：它是系统异常，无需出现在 IDL 的 raises 子句里，
//  且语义准确——「这个操作我没实现」，而不是「调用失败了」。
//  ICapeMINLPModel 抽象里没有这些概念，硬造一个空返回会让求解器以为
//  拿到了真实答案。
//
//  编译需 WIN32/ACE_AS_STATIC_LIBS/TAO_AS_STATIC_LIBS（由 capeopen_corba 传递）。
// ***************************************************************
#include <memory>
#include <string>

#include "CAPEOPEN100_MinlpS.h"  // POA_CAPEOPEN100::...::ICapeMINLP（生成）
#include "CapeMINLPModel.h"      // ICapeMINLPModel（capeopen_core）

class MINLPServant : public POA_CAPEOPEN100::Business::Numeric::Minlp::ICapeMINLP {
  public:
    // 生产模式：从环境变量 XRTO_XOPT_PROBLEM_DLL 建 XOptMINLPAdapter 并 connect。
    MINLPServant();
    // 测试模式：委托给已 connect 的外部 model（不拥有）。
    explicit MINLPServant(ICapeMINLPModel* model);

    bool ok() const { return model_ != nullptr; }

    // ---- 规模与结构 ----
    void GetMINLPSize(::CAPEOPEN100::Common::Types::CapeLong_out nv,
                      ::CAPEOPEN100::Common::Types::CapeLong_out niv,
                      ::CAPEOPEN100::Common::Types::CapeLong_out nlv,
                      ::CAPEOPEN100::Common::Types::CapeLong_out nliv,
                      ::CAPEOPEN100::Common::Types::CapeLong_out nc,
                      ::CAPEOPEN100::Common::Types::CapeLong_out nlc,
                      ::CAPEOPEN100::Common::Types::CapeLong_out nlz,
                      ::CAPEOPEN100::Common::Types::CapeLong_out nnz,
                      ::CAPEOPEN100::Common::Types::CapeLong_out nlzof,
                      ::CAPEOPEN100::Common::Types::CapeLong_out nnzof) override;
    void GetMINLPStructure(const char* structuretype,
                           ::CAPEOPEN100::Common::Types::CapeArrayLong_out rowindex,
                           ::CAPEOPEN100::Common::Types::CapeArrayLong_out columnindex,
                           ::CAPEOPEN100::Common::Types::CapeArrayLong_out objindex) override;

    // ---- 变量 ----
    void GetMINLPVariableNames(const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids,
                               ::CAPEOPEN100::Common::Types::CapeArrayString_out vnames) override;
    void GetMINLPVariableTypes(const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids,
                               ::CAPEOPEN100::Common::Types::CapeArrayBoolean_out isinteger) override;
    void GetMINLPVariableBooleanAttribute(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids, const char* attrib,
        ::CAPEOPEN100::Common::Types::CapeArrayBoolean_out values) override;
    void GetMINLPVariableIntegerAttribute(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids, const char* attrib,
        ::CAPEOPEN100::Common::Types::CapeArrayLong_out values) override;
    void GetMINLPVariableDoubleAttribute(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids, const char* attrib,
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_out values) override;
    void GetMINLPVariableStringAttribute(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids, const char* attrib,
        ::CAPEOPEN100::Common::Types::CapeArrayString_out values) override;
    void GetMINLPVariableBounds(const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids,
                                ::CAPEOPEN100::Common::Types::CapeArrayDouble_out LB,
                                ::CAPEOPEN100::Common::Types::CapeArrayDouble_out UB) override;
    void GetMINLPVariableValues(const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids,
                                ::CAPEOPEN100::Common::Types::CapeArrayDouble_out values) override;
    void SetMINLPVariableValues(const ::CAPEOPEN100::Common::Types::CapeArrayLong& vids,
                                const ::CAPEOPEN100::Common::Types::CapeArrayDouble& values) override;

    // ---- 约束 ----
    void GetMINLPConstraintNames(const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids,
                                 ::CAPEOPEN100::Common::Types::CapeArrayString_out cnames) override;
    void GetMINLPConstraintBounds(const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids,
                                  ::CAPEOPEN100::Common::Types::CapeArrayDouble_out LB,
                                  ::CAPEOPEN100::Common::Types::CapeArrayDouble_out UB) override;
    void GetMINLPConstraintLinearity(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids,
        ::CAPEOPEN100::Common::Types::CapeArrayBoolean_out islinear) override;
    void GetMINLPConstraintBooleanAttribute(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids, const char* attrib,
        ::CAPEOPEN100::Common::Types::CapeArrayBoolean_out values) override;
    void GetMINLPConstraintIntegerAttribute(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids, const char* attrib,
        ::CAPEOPEN100::Common::Types::CapeArrayLong_out values) override;
    void GetMINLPConstraintDoubleAttribute(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids, const char* attrib,
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_out values) override;
    void GetMINLPConstraintStringAttribute(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids, const char* attrib,
        ::CAPEOPEN100::Common::Types::CapeArrayString_out values) override;
    void GetMINLPNonlinearConstraintValues(
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids,
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_out values) override;
    void GetMINLPConstraintDerivativeValues(
        const char* structtype, const ::CAPEOPEN100::Common::Types::CapeArrayLong& cids,
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_out vals) override;

    // ---- 目标函数 ----
    void GetMINLPObjectiveFunctionType(
        ::CAPEOPEN100::Business::Numeric::Minlp::CapeMINLPObjFunType_out otype) override;
    void GetMINLPNonlinearObjectiveFunctionValue(
        ::CAPEOPEN100::Common::Types::CapeDouble_out value) override;
    void GetMINLPObjectiveFunctionDerivativeValues(
        const char* stype, ::CAPEOPEN100::Common::Types::CapeArrayDouble_out v) override;
    void GetMINLPObjectiveFunctionBooleanAttribute(
        const char* attrib, ::CAPEOPEN100::Common::Types::CapeBoolean_out value) override;
    void GetMINLPObjectiveFunctionIntegerAttribute(
        const char* attrib, ::CAPEOPEN100::Common::Types::CapeLong_out values) override;
    void GetMINLPObjectiveFunctionDoubleAttribute(
        const char* attrib, ::CAPEOPEN100::Common::Types::CapeDouble_out value) override;
    void GetMINLPObjectiveFunctionStringAttribute(const char* attrib,
                                                  ::CORBA::String_out value) override;

    // ---- Lagrange / Hessian ----
    void SetMINLPLagrangeMultipliers(
        const char* lmtype, const ::CAPEOPEN100::Common::Types::CapeArrayLong& ids,
        const ::CAPEOPEN100::Common::Types::CapeArrayDouble& values) override;
    void GetMINLPLagrangeMultipliers(
        const char* lmtype, const ::CAPEOPEN100::Common::Types::CapeArrayLong& ids,
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_out values) override;
    void GetMINLPHessianStructure(
        ::CAPEOPEN100::Common::Types::CapeLong size,
        const ::CAPEOPEN100::Common::Types::CapeArrayLong& rowindex,
        ::CAPEOPEN100::Common::Types::CapeArrayLong_out columnindex) override;
    void SetMINLPHessianValues(
        const ::CAPEOPEN100::Common::Types::CapeArrayDouble& values) override;
    void GetMINLPHessianValues(
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_out values) override;

    // ---- 继承自 ICapeIdentification ----
    // 规范（Methods&Tools §9.2.2）要求每个 PMC object 都提供标识；CORBA 侧的
    // 实现方式是 IDL 继承（同文 §6.1.2，与 COM 的 QueryInterface 相反）。
    // 名称/描述与 COM 侧 CoMINLP 用同一对字符串，两个绑定对外身份一致。
    char* GetComponentName() override;
    char* GetComponentDescription() override;
    void SetComponentName(const char* name) override;
    void SetComponentDescription(const char* desc) override;

  private:
    ICapeMINLPModel* model_ = nullptr;
    std::unique_ptr<ICapeMINLPModel> owned_;
    std::string comp_name_ = "xOpt MINLP";
    std::string comp_desc_ = "xOpt problem published as CAPE-OPEN MINLP";
};
