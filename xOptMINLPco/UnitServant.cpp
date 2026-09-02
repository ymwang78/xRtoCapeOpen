// ***************************************************************
//  UnitServant   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "UnitServant.h"

#include <string>

namespace {

namespace ct = ::CAPEOPEN100::Common::Types;
namespace ce = ::CAPEOPEN100::Common::Error;
namespace uo = ::CAPEOPEN100::Business::UnitOp::Unit;

// 与 MINLPServant 同一套构造方式：异常体按 Error Common Interface.pdf §5.2.1
// 的 CORBA 映射平摊 ECapeUser 的六个成员，没有 name。
ce::ECapeUnknown unknown(const char* op, const char* why) {
    ce::ECapeUnknown e;
    e.code = -1;
    e.description = CORBA::string_dup(why);
    e.scope = CORBA::string_dup("xOptMINLPco");
    e.interfaceName = CORBA::string_dup("IXOptUnitExtension");
    e.operation = CORBA::string_dup(op);
    e.moreInfo = CORBA::string_dup("");
    return e;
}

ce::ECapeInvalidArgument invalidArgument(const char* op, const char* why, CORBA::Short pos) {
    ce::ECapeInvalidArgument e;
    e.code = -1;
    e.description = CORBA::string_dup(why);
    e.scope = CORBA::string_dup("xOptMINLPco");
    e.interfaceName = CORBA::string_dup("IXOptUnitExtension");
    e.operation = CORBA::string_dup(op);
    e.moreInfo = CORBA::string_dup("");
    e.position = pos;
    return e;
}

}  // namespace

// ===========================================================================
//  PortServant
// ===========================================================================

PortServant::PortServant(std::string name, bool is_input)
    : name_(std::move(name)), is_input_(is_input) {
    description_ = std::string(is_input_ ? "inlet" : "outlet") + " port of the wrapped xOpt model";
}

char* PortServant::GetComponentName() { return CORBA::string_dup(name_.c_str()); }

char* PortServant::GetComponentDescription() {
    return CORBA::string_dup(description_.c_str());
}

void PortServant::SetComponentName(const char* name) {
    if (name != nullptr) name_ = name;
}

void PortServant::SetComponentDescription(const char* desc) {
    if (desc != nullptr) description_ = desc;
}

uo::CapePortType PortServant::GetPortType() {
    // 端口上流的是物料。规范给的另外三个取值里，CAPE_ENERGY / CAPE_INFORMATION
    // 我们的模型没有，CAPE_ANY 规范自己写着 "reserved for future use"。
    return uo::CAPE_MATERIAL;
}

uo::CapePortDirection PortServant::GetDirection() {
    return is_input_ ? uo::CAPE_INLET : uo::CAPE_OUTLET;
}

CORBA::Object_ptr PortServant::GetConnectedObject() {
    // 规范 p.81：未连接时返回空引用或抛 ECapeUnknown，两者都合规。返回 nil，
    // 因为"没连"对 PME 是正常状态，用异常表达会逼客户端 try/catch 走正常路径。
    return CORBA::Object::_duplicate(connected_.in());
}

void PortServant::Connect(CORBA::Object_ptr objectToConnect) {
    // 目前只记住引用：本组件不从连接物上读任何东西（组分表走 XOPTCO 扩展，
    // 见 XOPTCO_Ext.idl 的 PHASE 2）。记住它是为了 GetConnectedObject 有意义，
    // 也为了将来接 Material Object 时这里已经有落点。
    connected_ = CORBA::Object::_duplicate(objectToConnect);
}

void PortServant::Disconnect() { connected_ = CORBA::Object::_nil(); }

// ===========================================================================
//  PortCollectionServant
// ===========================================================================

PortCollectionServant::PortCollectionServant(std::vector<CORBA::Object_var> refs,
                                             std::vector<std::string> names)
    : refs_(std::move(refs)), names_(std::move(names)) {}

char* PortCollectionServant::GetComponentName() { return CORBA::string_dup("ports"); }

char* PortCollectionServant::GetComponentDescription() {
    return CORBA::string_dup("ports of the wrapped xOpt model");
}

void PortCollectionServant::SetComponentName(const char*) {}
void PortCollectionServant::SetComponentDescription(const char*) {}

CORBA::Object_ptr PortCollectionServant::Item(const CORBA::Any& id) {
    // 规范允许按下标或按名字取。下标**从 1 开始**，与 ICapeMINLP 的 vids/cids
    // 同一套约定（Collection Common Interface.pdf）；这里是那条换基线的所在。
    CORBA::Long index = 0;
    if (id >>= index) {
        if (index < 1 || index > static_cast<CORBA::Long>(refs_.size())) {
            throw invalidArgument("Item", "collection index out of range (1-based)", 1);
        }
        return CORBA::Object::_duplicate(refs_[static_cast<size_t>(index - 1)].in());
    }
    const char* name = nullptr;
    if (id >>= name) {
        for (size_t i = 0; i < names_.size(); ++i) {
            if (names_[i] == name) return CORBA::Object::_duplicate(refs_[i].in());
        }
        throw invalidArgument("Item", "no port with that name", 1);
    }
    throw invalidArgument("Item", "id must hold either a long (1-based) or a string", 1);
}

ct::CapeLong PortCollectionServant::Count() {
    return static_cast<ct::CapeLong>(refs_.size());
}

// ===========================================================================
//  UnitServant
// ===========================================================================

UnitServant::UnitServant(XOptMINLPAdapter* adapter, PortableServer::POA_ptr poa,
                         CORBA::Object_ptr minlp_ref)
    : adapter_(adapter),
      name_("xOpt Unit"),
      description_("xOpt black-box model published as a CAPE-OPEN unit operation"),
      minlp_ref_(CORBA::Object::_duplicate(minlp_ref)),
      poa_(PortableServer::POA::_duplicate(poa)) {
    buildPorts(poa);
}

UnitServant::~UnitServant() { destroyPorts(); }

// 注销上一批端口与端口集合。不注销就每换一次组分表泄漏一批 servant，
// 而且旧引用还能被解析到——客户端拿着过期的端口读到的会是旧组分的映射。
void UnitServant::destroyPorts() {
    if (CORBA::is_nil(poa_.in())) return;
    auto deactivate = [this](CORBA::Object_ptr ref) {
        if (CORBA::is_nil(ref)) return;
        try {
            PortableServer::ObjectId_var oid = poa_->reference_to_id(ref);
            poa_->deactivate_object(oid.in());
        } catch (const CORBA::Exception&) {
            // 已经不在了就算了：这里是清理路径，报错帮不上任何人。
        }
    };
    for (CORBA::Object_var& r : port_refs_) deactivate(r.in());
    port_refs_.clear();
    deactivate(ports_collection_.in());
    ports_collection_ = CORBA::Object::_nil();
}

void UnitServant::buildPorts(PortableServer::POA_ptr poa) {
    std::vector<CORBA::Object_var> refs;
    std::vector<std::string> names;
    if (adapter_ != nullptr) {
        for (const XOptPortDesc& p : adapter_->ports()) {
            PortServant* servant = new PortServant(p.name, p.is_input);
            // 激活后立刻 _remove_ref：所有权交给 POA，本对象只留引用。
            PortableServer::ObjectId_var oid = poa->activate_object(servant);
            refs.push_back(poa->id_to_reference(oid.in()));
            port_refs_.push_back(refs.back());
            names.push_back(p.name);
            servant->_remove_ref();
        }
    }
    PortCollectionServant* coll = new PortCollectionServant(std::move(refs), std::move(names));
    PortableServer::ObjectId_var oid = poa->activate_object(coll);
    ports_collection_ = poa->id_to_reference(oid.in());
    coll->_remove_ref();
}

char* UnitServant::GetComponentName() { return CORBA::string_dup(name_.c_str()); }

char* UnitServant::GetComponentDescription() {
    return CORBA::string_dup(description_.c_str());
}

void UnitServant::SetComponentName(const char* name) {
    if (name != nullptr) name_ = name;
}

void UnitServant::SetComponentDescription(const char* desc) {
    if (desc != nullptr) description_ = desc;
}

ct::CapeValidationStatus UnitServant::GetValStatus() {
    return (adapter_ != nullptr) ? ct::CAPE_VALID : ct::CAPE_INVALID;
}

void UnitServant::Calculate() {
    // 本组件发布的是**问题**，求解由外部求解器做（那正是 ICapeMINLP 那条通路
    // 存在的理由）。这里不假装算过：谎报成功会让 PME 拿着没动过的出口变量
    // 继续往下走，比报错难查得多。
    throw unknown("Calculate",
                  "this component publishes a problem over ICapeMINLP for an external "
                  "solver to solve; it does not compute the unit itself");
}

CORBA::Object_ptr UnitServant::GetPorts() {
    return CORBA::Object::_duplicate(ports_collection_.in());
}

ct::CapeBoolean UnitServant::Validate(ct::CapeString_out message) {
    if (adapter_ == nullptr) {
        message = CORBA::string_dup("no model is attached");
        return false;
    }
    message = CORBA::string_dup("");
    return true;
}

ct::CapeArrayString* UnitServant::GetComponents() {
    if (adapter_ == nullptr) throw unknown("GetComponents", "no model is attached");
    const std::vector<std::string>& comps = adapter_->components();
    ct::CapeArrayString_var out = new ct::CapeArrayString();
    out->length(static_cast<CORBA::ULong>(comps.size()));
    for (CORBA::ULong i = 0; i < out->length(); ++i) {
        out[i] = CORBA::string_dup(comps[i].c_str());
    }
    return out._retn();
}

CORBA::Object_ptr UnitServant::GetMINLP() {
    if (CORBA::is_nil(minlp_ref_.in())) {
        throw unknown("GetMINLP", "no ICapeMINLP reference was handed to this unit");
    }
    return CORBA::Object::_duplicate(minlp_ref_.in());
}

void UnitServant::SetComponents(const ct::CapeArrayString& components) {
    if (adapter_ == nullptr) throw unknown("SetComponents", "no model is attached");
    if (components.length() == 0) {
        throw invalidArgument("SetComponents", "the component list is empty", 1);
    }
    std::vector<std::string> comps;
    comps.reserve(components.length());
    for (CORBA::ULong i = 0; i < components.length(); ++i) {
        comps.emplace_back(components[i].in() != nullptr ? components[i].in() : "");
    }
    if (adapter_->setComponents(comps) < 0) {
        // 把 adapter 的原话带出去：模型拒绝一套组分表的原因（validateModel 没过、
        // buildProblem 失败……）都在那句里，压成 "failed" 等于把线索扔掉。
        const std::string why = "the model rejected the component list: " + adapter_->lastError();
        throw invalidArgument("SetComponents", why.c_str(), 1);
    }
    destroyPorts();
    buildPorts(poa_.in());
}

void UnitServant::GenerateEstimate(const ct::CapeArrayString& fixedNames,
                                   const ct::CapeArrayDouble& fixedValues,
                                   ct::CapeArrayDouble_out initialX) {
    if (adapter_ == nullptr) throw unknown("GenerateEstimate", "no model is attached");
    if (fixedNames.length() != fixedValues.length()) {
        throw invalidArgument("GenerateEstimate",
                              "fixedNames and fixedValues have different lengths", 1);
    }
    std::vector<std::string> names;
    std::vector<double> values;
    names.reserve(fixedNames.length());
    values.reserve(fixedValues.length());
    for (CORBA::ULong i = 0; i < fixedNames.length(); ++i) {
        names.emplace_back(fixedNames[i].in() != nullptr ? fixedNames[i].in() : "");
        values.push_back(fixedValues[i]);
    }
    if (adapter_->setFixedVariables(names, values) < 0) {
        const std::string why =
            "the model rejected the fixed-variable set: " + adapter_->lastError();
        throw invalidArgument("GenerateEstimate", why.c_str(), 1);
    }
    destroyPorts();
    buildPorts(poa_.in());

    const std::vector<double>& x0 = adapter_->initialX();
    ct::CapeArrayDouble_var out = new ct::CapeArrayDouble();
    out->length(static_cast<CORBA::ULong>(x0.size()));
    for (CORBA::ULong i = 0; i < out->length(); ++i) out[i] = x0[i];
    initialX = out._retn();
}

::XOPTCO::XOptPortVariableSeq* UnitServant::GetPortVariableMap(const char* portName) {
    if (adapter_ == nullptr) throw unknown("GetPortVariableMap", "no model is attached");
    if (portName == nullptr) {
        throw invalidArgument("GetPortVariableMap", "portName is null", 1);
    }
    for (const XOptPortDesc& p : adapter_->ports()) {
        if (p.name != portName) continue;
        ::XOPTCO::XOptPortVariableSeq_var out = new ::XOPTCO::XOptPortVariableSeq();
        out->length(static_cast<CORBA::ULong>(p.variables.size()));
        for (CORBA::ULong i = 0; i < out->length(); ++i) {
            out[i].streamName = CORBA::string_dup(p.variables[i].first.c_str());
            out[i].variableName = CORBA::string_dup(p.variables[i].second.c_str());
        }
        return out._retn();
    }
    // 名字不认识就报错，不返回空表：空表和"这个口没有变量"分不开，
    // 而前者是调用方拼错了名字，后者是合法状态。
    throw invalidArgument("GetPortVariableMap", "no port with that name", 1);
}

void UnitServant::GetFixableVariables(ct::CapeArrayString_out names,
                                      ct::CapeArrayDouble_out defaults) {
    if (adapter_ == nullptr) throw unknown("GetFixableVariables", "no model is attached");
    const std::vector<std::pair<std::string, double>>& f = adapter_->fixableVariables();
    ct::CapeArrayString_var n = new ct::CapeArrayString();
    ct::CapeArrayDouble_var d = new ct::CapeArrayDouble();
    n->length(static_cast<CORBA::ULong>(f.size()));
    d->length(static_cast<CORBA::ULong>(f.size()));
    for (CORBA::ULong i = 0; i < n->length(); ++i) {
        n[i] = CORBA::string_dup(f[i].first.c_str());
        d[i] = f[i].second;
    }
    names = n._retn();
    defaults = d._retn();
}
