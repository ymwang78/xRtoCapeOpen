#pragma once
// ***************************************************************
//  UnitServant   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  把被包装模型的**接线信息**发布成 CAPE-OPEN Unit Operation 接口。
//
//  与 MINLPServant 的分工：
//    MINLPServant  -> ICapeMINLP        问题：变量/约束/界/稀疏结构/导数
//    UnitServant   -> ICapeUnit + ports 拓扑：几个进出口、每个口接哪些变量
//  两者背后是同一个 XOptMINLPAdapter，也就是同一个模型 DLL；CAPE-OPEN 把这
//  两件事放在两个规范里，我们也就发两个对象。
//
//  为什么骨架是 POA_XOPTCO::IXOptUnitExtension 而不是 POA_...::ICapeUnit：
//  XOPTCO_Ext.idl 里那个扩展接口继承了 ICapeUnit，于是 TAO 只生成一个骨架。
//  第三方 CAPE-OPEN 客户端 _narrow 到 ICapeUnit 照样成立（_is_a 走继承链），
//  它只是拿不到扩展的三个操作——而那三个操作本来就不是 CAPE-OPEN 的。
//
//  索引基：ICapeCollection::Item 的下标按规范从 1 开始（Collection Common
//  Interface.pdf），与 ICapeMINLP 的 vids/cids 同一套约定。转换只在这里发生。
// ***************************************************************
#include <memory>
#include <string>
#include <vector>

#include "XOPTCO_ExtS.h"  // POA_XOPTCO::IXOptUnitExtension（含 ICapeUnit 骨架）
#include "XOptMINLPAdapter.h"

// —— 一个端口 ——
class PortServant : public POA_CAPEOPEN100::Business::UnitOp::Unit::ICapeUnitPort {
  public:
    PortServant(std::string name, bool is_input);

    // ICapeIdentification（经 ICapeUnitPort 继承而来）
    char* GetComponentName() override;
    char* GetComponentDescription() override;
    void SetComponentName(const char* name) override;
    void SetComponentDescription(const char* desc) override;

    // ICapeUnitPort
    ::CAPEOPEN100::Business::UnitOp::Unit::CapePortType GetPortType() override;
    ::CAPEOPEN100::Business::UnitOp::Unit::CapePortDirection GetDirection() override;
    CORBA::Object_ptr GetConnectedObject() override;
    void Connect(CORBA::Object_ptr objectToConnect) override;
    void Disconnect() override;

  private:
    std::string name_;
    std::string description_;
    bool is_input_;
    CORBA::Object_var connected_;
};

// —— 端口集合（ICapeUnit::GetPorts 的返回物）——
class PortCollectionServant : public POA_CAPEOPEN100::Common::Collection::ICapeCollection {
  public:
    // refs 与 names 一一对应；集合不拥有 servant，只持引用。
    PortCollectionServant(std::vector<CORBA::Object_var> refs, std::vector<std::string> names);

    // ICapeIdentification
    char* GetComponentName() override;
    char* GetComponentDescription() override;
    void SetComponentName(const char* name) override;
    void SetComponentDescription(const char* desc) override;

    // ICapeCollection
    CORBA::Object_ptr Item(const CORBA::Any& id) override;
    ::CAPEOPEN100::Common::Types::CapeLong Count() override;

  private:
    std::vector<CORBA::Object_var> refs_;
    std::vector<std::string> names_;
};

// —— 单元本体 ——
class UnitServant : public POA_XOPTCO::IXOptUnitExtension {
  public:
    // adapter 必须已 connect；本对象不拥有它。poa 用于激活端口与端口集合。
    // minlp_ref 是同一个模型的 ICapeMINLP 引用，由服务端在激活 MINLPServant
    // 之后传进来；GetMINLP 原样交出去。
    UnitServant(XOptMINLPAdapter* adapter, PortableServer::POA_ptr poa,
                CORBA::Object_ptr minlp_ref);
    ~UnitServant() override;

    // ICapeIdentification
    char* GetComponentName() override;
    char* GetComponentDescription() override;
    void SetComponentName(const char* name) override;
    void SetComponentDescription(const char* desc) override;

    // ICapeUnit
    ::CAPEOPEN100::Common::Types::CapeValidationStatus GetValStatus() override;
    void Calculate() override;
    CORBA::Object_ptr GetPorts() override;
    ::CAPEOPEN100::Common::Types::CapeBoolean Validate(
        ::CAPEOPEN100::Common::Types::CapeString_out message) override;

    // IXOptUnitExtension（我们自己的，非 CAPE-OPEN）
    ::CAPEOPEN100::Common::Types::CapeArrayString* GetComponents() override;
    void SetComponents(
        const ::CAPEOPEN100::Common::Types::CapeArrayString& components) override;
    void GenerateEstimate(
        const ::CAPEOPEN100::Common::Types::CapeArrayString& fixedNames,
        const ::CAPEOPEN100::Common::Types::CapeArrayDouble& fixedValues,
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_out initialX) override;
    CORBA::Object_ptr GetMINLP() override;
    ::XOPTCO::XOptPortVariableSeq* GetPortVariableMap(const char* portName) override;
    void GetFixableVariables(
        ::CAPEOPEN100::Common::Types::CapeArrayString_out names,
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_out defaults) override;

  private:
    void buildPorts(PortableServer::POA_ptr poa);
    void destroyPorts();

    XOptMINLPAdapter* adapter_;
    std::string name_;
    std::string description_;
    CORBA::Object_var ports_collection_;
    CORBA::Object_var minlp_ref_;
    // SetComponents 要重建端口，得记住 POA 才能把旧的注销掉。
    PortableServer::POA_var poa_;
    std::vector<CORBA::Object_var> port_refs_;
};
