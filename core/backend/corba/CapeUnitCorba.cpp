// ***************************************************************
//  CapeUnitCorba   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#ifdef CAPEOPEN_WITH_CORBA

#include "CapeUnitCorba.h"

#include "CAPEOPEN100_UnitC.h"
#include "XOPTCO_ExtC.h"

namespace {

namespace uo = ::CAPEOPEN100::Business::UnitOp::Unit;

// 与 CapeMINLPModelCorba 一样：进程内共用一个 ORB。两处各自 ORB_init 会得到
// 同一个默认 ORB 实例，但各自持有一份引用计数，谁先 destroy 谁就把对方的
// 连接一起拆了——这正是当初把它做成共享单例的原因。
CORBA::ORB_var& sharedOrbRef() {
    static CORBA::ORB_var orb;
    return orb;
}

}  // namespace

int CapeUnitCorba::fail(const std::string& msg) {
    last_error_ = msg;
    return -1;
}

int CapeUnitCorba::setComponents(const std::vector<std::string>& components) {
    if (!is_unit_ || target_.empty()) {
        return fail("setComponents: the target is not a unit, so there is nothing to push to");
    }
    if (components.empty()) return fail("setComponents: empty component list");
    try {
        CORBA::Object_var obj = sharedOrbRef()->string_to_object(target_.c_str());
        ::XOPTCO::IXOptUnitExtension_var unit =
            ::XOPTCO::IXOptUnitExtension::_narrow(obj.in());
        if (CORBA::is_nil(unit.in())) return fail("setComponents: _narrow to the unit failed");

        ::CAPEOPEN100::Common::Types::CapeArrayString seq;
        seq.length(static_cast<CORBA::ULong>(components.size()));
        for (CORBA::ULong i = 0; i < seq.length(); ++i) {
            seq[i] = CORBA::string_dup(components[i].c_str());
        }
        unit->SetComponents(seq);
    } catch (const ::CAPEOPEN100::Common::Error::ECapeInvalidArgument& e) {
        return fail(std::string("setComponents rejected: ") +
                    (e.description.in() != nullptr ? e.description.in() : ""));
    } catch (const CORBA::Exception& e) {
        return fail(std::string("setComponents: CORBA exception ") + e._name());
    }
    // 远端已经按新组分重建，本地那份缓存全过期了——重读，别做增量。
    return read(target_);
}

int CapeUnitCorba::generateEstimate(const std::vector<std::string>& fixed_names,
                                   const std::vector<double>& fixed_values,
                                   std::vector<double>& initial_x_out) {
    initial_x_out.clear();
    if (!is_unit_ || target_.empty()) {
        return fail("generateEstimate: the target is not a unit");
    }
    if (fixed_names.size() != fixed_values.size()) {
        return fail("generateEstimate: names and values have different lengths");
    }
    try {
        CORBA::Object_var obj = sharedOrbRef()->string_to_object(target_.c_str());
        ::XOPTCO::IXOptUnitExtension_var unit =
            ::XOPTCO::IXOptUnitExtension::_narrow(obj.in());
        if (CORBA::is_nil(unit.in())) return fail("generateEstimate: _narrow failed");

        ::CAPEOPEN100::Common::Types::CapeArrayString names;
        ::CAPEOPEN100::Common::Types::CapeArrayDouble values;
        names.length(static_cast<CORBA::ULong>(fixed_names.size()));
        values.length(static_cast<CORBA::ULong>(fixed_values.size()));
        for (CORBA::ULong i = 0; i < names.length(); ++i) {
            names[i] = CORBA::string_dup(fixed_names[i].c_str());
            values[i] = fixed_values[i];
        }
        ::CAPEOPEN100::Common::Types::CapeArrayDouble_var x0;
        unit->GenerateEstimate(names, values, x0.out());
        for (CORBA::ULong i = 0; i < x0->length(); ++i) initial_x_out.push_back(x0[i]);
    } catch (const ::CAPEOPEN100::Common::Error::ECapeInvalidArgument& e) {
        return fail(std::string("generateEstimate rejected: ") +
                    (e.description.in() != nullptr ? e.description.in() : ""));
    } catch (const CORBA::Exception& e) {
        return fail(std::string("generateEstimate: CORBA exception ") + e._name());
    }
    return read(target_);  // 远端重建过，缓存全过期
}

int CapeUnitCorba::read(const std::string& target) {
    target_ = target;
    is_unit_ = false;
    components_.clear();
    ports_.clear();
    fixables_.clear();
    last_error_.clear();

    try {
        if (CORBA::is_nil(sharedOrbRef().in())) {
            int argc = 0;
            sharedOrbRef() = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
        }
        CORBA::Object_var obj = sharedOrbRef()->string_to_object(target.c_str());
        if (CORBA::is_nil(obj.in())) {
            return fail("unit: string_to_object gave nil for '" + target + "'");
        }

        ::XOPTCO::IXOptUnitExtension_var unit =
            ::XOPTCO::IXOptUnitExtension::_narrow(obj.in());
        if (CORBA::is_nil(unit.in())) {
            // 不是单元。合法：老部署里目标就是一个纯 ICapeMINLP。
            return 0;
        }
        is_unit_ = true;

        // ---- 组分表 ----
        {
            ::CAPEOPEN100::Common::Types::CapeArrayString_var comps = unit->GetComponents();
            for (CORBA::ULong i = 0; i < comps->length(); ++i) {
                components_.emplace_back(comps[i].in() ? comps[i].in() : "");
            }
        }

        // ---- 可固定变量 ----
        {
            ::CAPEOPEN100::Common::Types::CapeArrayString_var names;
            ::CAPEOPEN100::Common::Types::CapeArrayDouble_var defaults;
            unit->GetFixableVariables(names.out(), defaults.out());
            const CORBA::ULong n =
                names->length() < defaults->length() ? names->length() : defaults->length();
            for (CORBA::ULong i = 0; i < n; ++i) {
                fixables_.emplace_back(names[i].in() ? names[i].in() : "", defaults[i]);
            }
        }

        // ---- 端口：走标准的 ICapeUnit::GetPorts -> ICapeCollection ----
        CORBA::Object_var ports_obj = unit->GetPorts();
        ::CAPEOPEN100::Common::Collection::ICapeCollection_var coll =
            ::CAPEOPEN100::Common::Collection::ICapeCollection::_narrow(ports_obj.in());
        if (CORBA::is_nil(coll.in())) {
            return fail("unit: GetPorts did not give an ICapeCollection");
        }
        const CORBA::Long count = coll->Count();
        for (CORBA::Long k = 1; k <= count; ++k) {  // 集合下标从 1 开始（规范）
            CORBA::Any id;
            id <<= static_cast<CORBA::Long>(k);
            CORBA::Object_var port_obj = coll->Item(id);
            uo::ICapeUnitPort_var port = uo::ICapeUnitPort::_narrow(port_obj.in());
            if (CORBA::is_nil(port.in())) {
                return fail("unit: collection item is not an ICapeUnitPort");
            }
            CapeUnitPort p;
            CORBA::String_var pname = port->GetComponentName();
            p.name = pname.in() ? pname.in() : "";
            p.is_input = (port->GetDirection() == uo::CAPE_INLET);

            // 变量表走 XOPTCO 扩展：CAPE-OPEN 的 ICapeUnitPortVariables 只能
            // 按 (类型, 组分) 逐个问，没有"把这个口的变量列出来"的操作。
            // 理由写在 XOPTCO_Ext.idl 的文件头。
            ::XOPTCO::XOptPortVariableSeq_var vars =
                unit->GetPortVariableMap(p.name.c_str());
            for (CORBA::ULong i = 0; i < vars->length(); ++i) {
                p.variables.emplace_back(
                    vars[i].streamName.in() ? vars[i].streamName.in() : "",
                    vars[i].variableName.in() ? vars[i].variableName.in() : "");
            }
            ports_.push_back(std::move(p));
        }
        return 0;
    } catch (const CORBA::Exception& e) {
        return fail(std::string("unit: CORBA exception ") + e._name());
    } catch (...) {
        return fail("unit: unknown exception");
    }
}

#endif  // CAPEOPEN_WITH_CORBA
