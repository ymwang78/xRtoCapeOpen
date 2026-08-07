#pragma once
// ***************************************************************
//  RefCapeMINLPServant   version:  2.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  测试用参考 CORBA 服务端：POA 实现官方模块路径下的
//  CAPEOPEN100::Business::Numeric::Minlp::ICapeMINLP，内部包 CapeMINLPModelMock。
//  作为 CapeMINLPModelCorba 的 collocated 测试对端（design §3.3/§5.3）。
//
//  它扮演的是「一个合规的第三方 CO 组件」，因此：
//
//  1. 严格 1-based。规范：constraints and variables are numbered starting
//     from 1，vids/cids 合法范围 1..nv / 1..nc。
//
//  2. **刻意不复用 CapeCorbaMarshal 的 indicesToWire/indicesFromWire**，
//     而是自己显式做加减一。理由：本 servant 存在的意义就是给消费端
//     CapeMINLPModelCorba 的换基做独立校验。若两端共用同一对 helper，
//     helper 里的错会让两端一致地偏，测试照样全绿——那正是缺口 2 藏了这么久
//     的原因（design §6.2）。这里的重复是有意的。
//
//  仅测试编译。
// ***************************************************************
#include <string>

#include "../../CapeMINLPModelMock.h"
#include "CAPEOPEN100_MinlpS.h"

class RefCapeMINLPServant : public POA_CAPEOPEN100::Business::Numeric::Minlp::ICapeMINLP {
  public:
    RefCapeMINLPServant();

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

    // ---- 继承自 ICapeIdentification（规范要求每个 PMC object 都提供标识）----
    char* GetComponentName() override;
    char* GetComponentDescription() override;
    void SetComponentName(const char* name) override;
    void SetComponentDescription(const char* desc) override;

  private:
    CapeMINLPModelMock mock_;
    std::string name_ = "RefCapeMINLP";
    std::string description_ = "Reference CAPE-OPEN MINLP component (test fixture)";
};
