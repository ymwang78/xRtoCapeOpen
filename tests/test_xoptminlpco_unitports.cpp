// ***************************************************************
//  test_xoptminlpco_unitports   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  接线信息的回环：
//    C-ABI 模型 DLL(mock_xoptmodel) -> XOptMINLPAdapter(端口/组分/可固定量)
//      -> UnitServant(POA ICapeUnit + XOPTCO 扩展)          ← 生产端
//        -> 经 IOR 字符串绕一圈 string_to_object            ← 真的过 ORB
//          -> capeopen_core 的 CapeUnitCorba                ← 消费端
//            -> 与模型自报的值对拍。
//
//  为什么不注入引用而要绕 object_to_string/string_to_object：消费端真正走的
//  就是这条路（xOptModelCapeOpen 从环境变量拿到的是一个连接串），而 _narrow
//  到 IXOptUnitExtension 这一步只有在真引用上才考得到——注入构造会把它跳过。
//  这正是 §6.2 缺口 1 那类"回环测试永远抓不到"的问题的防身符。
// ***************************************************************
#include <gtest/gtest.h>

#include <cstdio>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <algorithm>
#include <string>
#include <vector>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#include "xOpt/xOptModel.h"

#include "UnitServant.h"
#include "XOptMINLPAdapter.h"
#include "backend/corba/CapeUnitCorba.h"

namespace {

// 与 test_xoptminlpco_modeldesc.cpp 里用的是同一个夹具模型：gain 模型，
// 2 个组分、1 进 1 出，端口映射由它自己给。
const char* const kGainDesc = R"({
  "parameters": { "gain": 3.0 },
  "fixable_variables": [ "x0" ],
  "fixed_values": { "x0": 7.0 },
  "components": [ "C1", "C2" ]
})";

class ScopedFile {
  public:
    ScopedFile(std::string path, const std::string& text) : path_(std::move(path)) {
        FILE* f = std::fopen(path_.c_str(), "wb");
        if (f != nullptr) {
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    }
    ~ScopedFile() { std::remove(path_.c_str()); }
    const std::string& str() const { return path_; }

  private:
    std::string path_;
};

TEST(XOptMINLPcoUnitPorts, TopologyRoundTripsOverCorba) {
    const std::string desc_path = std::string(MOCK_XOPTMODEL_DLL) + ".unitports.json";
    const ScopedFile desc(desc_path, kGainDesc);

    XOptMINLPAdapter adapter(MOCK_XOPTMODEL_DLL, desc.str());
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();
    ASSERT_FALSE(adapter.ports().empty()) << "the fixture model must expose ports";

    int argc = 0;
    CORBA::ORB_ptr orb = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
    CORBA::Object_var p = orb->resolve_initial_references("RootPOA");
    PortableServer::POA_var poa = PortableServer::POA::_narrow(p.in());
    poa->the_POAManager()->activate();

    UnitServant* unit = new UnitServant(&adapter, poa.in(), CORBA::Object::_nil());
    PortableServer::ObjectId_var oid = poa->activate_object(unit);
    CORBA::Object_var ref = poa->id_to_reference(oid.in());
    CORBA::String_var ior = orb->object_to_string(ref.in());

    CapeUnitCorba consumer;
    ASSERT_EQ(consumer.read(ior.in()), 0) << consumer.lastError();
    EXPECT_TRUE(consumer.isUnit()) << "_narrow to IXOptUnitExtension must succeed";

    EXPECT_EQ(consumer.components(), (std::vector<std::string>{"C1", "C2"}));

    // 端口拓扑与模型自报的一致
    ASSERT_EQ(consumer.ports().size(), adapter.ports().size());
    for (size_t i = 0; i < consumer.ports().size(); ++i) {
        const CapeUnitPort& got = consumer.ports()[i];
        const XOptPortDesc& want = adapter.ports()[i];
        EXPECT_EQ(got.name, want.name);
        EXPECT_EQ(got.is_input, want.is_input) << "port " << got.name;
        ASSERT_EQ(got.variables.size(), want.variables.size()) << "port " << got.name;
        for (size_t k = 0; k < got.variables.size(); ++k) {
            EXPECT_EQ(got.variables[k].first, want.variables[k].first);
            EXPECT_EQ(got.variables[k].second, want.variables[k].second);
        }
    }

    // 可固定变量连同默认值
    ASSERT_EQ(consumer.fixableVariables().size(), adapter.fixableVariables().size());
    for (size_t i = 0; i < consumer.fixableVariables().size(); ++i) {
        EXPECT_EQ(consumer.fixableVariables()[i].first, adapter.fixableVariables()[i].first);
        EXPECT_DOUBLE_EQ(consumer.fixableVariables()[i].second,
                         adapter.fixableVariables()[i].second);
    }

    unit->_remove_ref();
}

// 组分表下推：流程图才是 slate 的所有者，服务端启动时配的那套只是默认值。
// 推下去之后**一切派生物都得跟着变**——端口映射、可固定变量、问题规模。
// 只断言组分表本身变了是不够的：那样即使 servant 忘了重建端口也照样绿。
// 端口映射表两列的**绝对**方向：streamName 是规范流股名（T / fi_C1），
// variableName 是模型内部的变量名（x0 / f_C1）。
//
// 上面那条往返测试比的是"消费端 == 适配器"，两边一起把列写反照样全绿——方向
// 从来没被钉住过。这不是假想的风险：xOpt 侧的 xOptModelBlackBox::prepareRuntime
// 就是把这两列反着装进 var_comp_map 的，而宿主连线时拿键去查本单元的变量表，
// 于是每根线都接不上，报"单元设备 X 中不存在第 0 流股声称的 T 流股变量"。
// 那条路径在 xOpt 仓库里，这里管不着；能管的是**别让我们这一侧也漂移**。
//
// 约定的出处不是这份 IDL，而是宿主已有的三处一致用法：JsonDesc 的
// *_Model.json 写 {"in_T": "T"}、Python 模型的 getInPortVariableMap 返回
// {"In_T": "T"}、FlowSheetModel 拿映射的键去 opt_variables.getIndex()。
// 注意那三处的键值顺序与 C ABI 的两个数组是**相反**的，转换发生在宿主侧。
// generateEstimate 的固定集是「替换」，不是「累积」。
//
// 每次调用带来的是当次的**完整**固定集。只插不清的话，把一个原本用户固定的
// 进料接上流股之后，宿主这次不再点它的名字，模型却仍认为它被固定 —— 于是继续
// 为它追加一条进料固定等式，把方程组多约束住。而「空表 = 一个都不固定」这个
// 语义(见 AnEmptyFixedSetMeansPinNothingNotUseYourDefault)会反过来变成
// 「空表 = 沿用上次」，与设计正相反。
//
// 这条走 C ABI 直调,不经适配器:适配器的 setFixedVariables 是整体重建
// (disconnect + connect),模型对象换了新的,累积的毛病在那条路上看不见。
// 而宿主(xOptModelBlackBox::generateEstimate)是在**同一个** handle 上反复调的,
// 那才是真实的调用方式。
TEST(XOptMINLPcoUnitPorts, GenerateEstimateReplacesTheFixedSetRatherThanAccumulating) {
#ifdef _WIN32
    HMODULE mod = ::LoadLibraryA(MOCK_XOPTMODEL_DLL);
    ASSERT_NE(mod, nullptr) << "cannot load " << MOCK_XOPTMODEL_DLL;
    auto create = reinterpret_cast<int (*)(xOptModelT*, xOptPlatformT*, const char*)>(
        ::GetProcAddress(mod, "xOptModel_createModel"));
#else
    void* mod = ::dlopen(MOCK_XOPTMODEL_DLL, RTLD_NOW);
    ASSERT_NE(mod, nullptr) << "cannot load " << MOCK_XOPTMODEL_DLL;
    auto create = reinterpret_cast<int (*)(xOptModelT*, xOptPlatformT*, const char*)>(
        ::dlsym(mod, "xOptModel_createModel"));
#endif
    ASSERT_NE(create, nullptr);

    xOptModelT model{};
    model.size = sizeof(xOptModelT);
    ASSERT_EQ(create(&model, nullptr, "mock"), 0);
    ASSERT_NE(model.handle, nullptr);

    xOptSlate slate{};
    slate.name = "StreamPort";
    slate.thermo_method = "";
    slate.components[0] = "C1";
    slate.components[1] = "C2";
    slate.components[2] = nullptr;
    ASSERT_EQ(model.setSlate(model.handle, 0, &slate), 0);

    int n = 0;
    ASSERT_EQ(model.generateEstimate(model.handle, nullptr, n, nullptr, nullptr, 0), 0);
    ASSERT_GT(n, 0);
    std::vector<double> initx(static_cast<size_t>(n), 0.0);

    // 第一次：固定 x0 = 7
    // 字符串字面量末尾本来就有终止符，解包读一个名字就停
    const char kFixed[] = "x0";
    const double kValue = 7.0;
    int size = n;
    ASSERT_EQ(model.generateEstimate(model.handle, initx.data(), size, kFixed, &kValue, 1), 0);
    EXPECT_DOUBLE_EQ(initx[0], 7.0) << "第一次固定应当生效";

    // 第二次：同一个 handle，空固定集。此时必须回到「一个都不固定」的默认值，
    // 而不是把上一次的 7 留着。
    std::fill(initx.begin(), initx.end(), -1.0);
    size = n;
    ASSERT_EQ(model.generateEstimate(model.handle, initx.data(), size, nullptr, nullptr, 0), 0);
    EXPECT_DOUBLE_EQ(initx[0], 0.0)
        << "空固定集必须清掉上一次的选择，否则「空表 = 一个都不固定」形同虚设";

    model.destroyModel(model.handle);
#ifdef _WIN32
    ::FreeLibrary(mod);
#else
    ::dlclose(mod);
#endif
}

TEST(XOptMINLPcoUnitPorts, ThePortMapColumnsKeepTheirMeaning) {
    const std::string desc_path = std::string(MOCK_XOPTMODEL_DLL) + ".portcols.json";
    const ScopedFile desc(desc_path, kGainDesc);

    XOptMINLPAdapter adapter(MOCK_XOPTMODEL_DLL, desc.str());
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();

    const XOptPortDesc* in_port = nullptr;
    const XOptPortDesc* out_port = nullptr;
    for (const XOptPortDesc& port : adapter.ports()) {
        (port.is_input ? in_port : out_port) = &port;
    }
    ASSERT_NE(in_port, nullptr);
    ASSERT_NE(out_port, nullptr);

    // fixture: in_map_ = {("T", "x0")}, out_map_ = {("fi_C1","f_C1"), ("fi_C2","f_C2")}
    ASSERT_EQ(in_port->variables.size(), 1u);
    EXPECT_EQ(in_port->variables[0].first, "T") << "first 必须是规范流股名";
    EXPECT_EQ(in_port->variables[0].second, "x0") << "second 必须是模型内部变量名";

    ASSERT_EQ(out_port->variables.size(), 2u);
    EXPECT_EQ(out_port->variables[0].first, "fi_C1");
    EXPECT_EQ(out_port->variables[0].second, "f_C1");

    // 规范名与模型变量名必须真的是两套命名，否则这条测试钉不住任何东西
    EXPECT_NE(in_port->variables[0].first, in_port->variables[0].second);

    // 模型内部变量名必须在模型的变量表里查得到，规范流股名则不该在里面——
    // 这正是宿主连线时做的那次查表，方向搞反就会全军覆没。
    // 空 id 表 = 全部。注意适配器这一层是 0-based，1-based 的换算在 servant 里，
    // 所以这里不要自己去拼 id。
    std::vector<std::string> vars;
    ASSERT_EQ(adapter.getVariableNames(std::vector<int>(), vars), 0) << adapter.lastError();
    ASSERT_FALSE(vars.empty());

    EXPECT_NE(std::find(vars.begin(), vars.end(), "x0"), vars.end())
        << "second 那一列应当是模型自己的变量";
    EXPECT_EQ(std::find(vars.begin(), vars.end(), "T"), vars.end())
        << "first 那一列是流股侧的规范名，不该出现在模型变量表里";
}

TEST(XOptMINLPcoUnitPorts, PushingComponentsRebuildsEverythingDerivedFromThem) {
    const std::string desc_path = std::string(MOCK_XOPTMODEL_DLL) + ".push.json";
    const ScopedFile desc(desc_path, kGainDesc);  // 起点是 C1, C2

    XOptMINLPAdapter adapter(MOCK_XOPTMODEL_DLL, desc.str());
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();

    int argc = 0;
    CORBA::ORB_ptr orb = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
    CORBA::Object_var p = orb->resolve_initial_references("RootPOA");
    PortableServer::POA_var poa = PortableServer::POA::_narrow(p.in());
    poa->the_POAManager()->activate();

    UnitServant* unit = new UnitServant(&adapter, poa.in(), CORBA::Object::_nil());
    PortableServer::ObjectId_var oid = poa->activate_object(unit);
    CORBA::Object_var ref = poa->id_to_reference(oid.in());
    CORBA::String_var ior = orb->object_to_string(ref.in());

    CapeUnitCorba consumer;
    ASSERT_EQ(consumer.read(ior.in()), 0) << consumer.lastError();
    ASSERT_TRUE(consumer.isUnit());
    ASSERT_EQ(consumer.components(), (std::vector<std::string>{"C1", "C2"}));

    // 夹具模型的出口是每个组分一条 fi_<组分>，所以出口变量数 == 组分数：
    // 这就是"端口确实按新组分重建过"的可观察证据。
    auto outPortVarCount = [&consumer]() -> size_t {
        for (const CapeUnitPort& port : consumer.ports()) {
            if (!port.is_input) return port.variables.size();
        }
        return 0;
    };
    ASSERT_EQ(outPortVarCount(), 2u);

    // 推一套完全不同的、而且个数也不同的组分表
    const std::vector<std::string> pushed{"H2", "N2", "CH4"};
    ASSERT_EQ(consumer.setComponents(pushed), 0) << consumer.lastError();

    EXPECT_EQ(consumer.components(), pushed);
    EXPECT_EQ(outPortVarCount(), 3u) << "the port map must be rebuilt, not just the slate";
    for (const CapeUnitPort& port : consumer.ports()) {
        if (port.is_input) continue;
        ASSERT_EQ(port.variables.size(), pushed.size());
        for (size_t i = 0; i < pushed.size(); ++i) {
            EXPECT_EQ(port.variables[i].first, "fi_" + pushed[i]);
            EXPECT_EQ(port.variables[i].second, "f_" + pushed[i]);
        }
    }
    // adapter 侧也真的重建了（不是消费端自己编出来的）
    EXPECT_EQ(adapter.components(), pushed);

    unit->_remove_ref();
}

// 空组分表要被拒。空表推下去会让模型 setSlate 失败，但那时错误已经离病灶很远。
TEST(XOptMINLPcoUnitPorts, PushingAnEmptyComponentListIsRejected) {
    const std::string desc_path = std::string(MOCK_XOPTMODEL_DLL) + ".empty.json";
    const ScopedFile desc(desc_path, kGainDesc);
    XOptMINLPAdapter adapter(MOCK_XOPTMODEL_DLL, desc.str());
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();
    EXPECT_LT(adapter.setComponents({}), 0);
    // 拒绝之后原来那套必须还在，不能被半路清空
    EXPECT_EQ(adapter.components(), (std::vector<std::string>{"C1", "C2"}));
}

// 固定变量集合也归流程图管，而且**空表是有效答案**。
//
// 这条测的是一个把 xRto 卡住的真实事故：服务端按自己描述文件里的
// fixable_variables 把进料全钉住了，而流程图里进料由上游流股决定——同一件事
// 两个方程，求解直接返回 -1。空表必须能表达"一个都不固定"，不能被当成
// "没指定，用你的默认值"。
TEST(XOptMINLPcoUnitPorts, AnEmptyFixedSetMeansPinNothingNotUseYourDefault) {
    const std::string desc_path = std::string(MOCK_XOPTMODEL_DLL) + ".fixed.json";
    // 描述文件里点名固定 x0 —— 这正是"服务端默认值"
    const ScopedFile desc(desc_path, kGainDesc);

    XOptMINLPAdapter adapter(MOCK_XOPTMODEL_DLL, desc.str());
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();
    ASSERT_EQ(adapter.fixableVariables().size(), 1u);  // 模型报出的可固定变量

    // 推空表 = 一个都不固定
    ASSERT_EQ(adapter.setFixedVariables({}, {}), 0) << adapter.lastError();
    const std::vector<double> x_none = adapter.initialX();
    EXPECT_FALSE(x_none.empty()) << "an estimate must still come back";

    // 再推一个非空的，初值要跟着变——证明推下去的值真的进了模型，
    // 而不是每次都把描述文件里那份原样吐回来。
    ASSERT_EQ(adapter.setFixedVariables({"x0"}, {42.0}), 0) << adapter.lastError();
    const std::vector<double> x_fixed = adapter.initialX();
    ASSERT_FALSE(x_fixed.empty());
    EXPECT_DOUBLE_EQ(x_fixed[0], 42.0) << "the pushed value must reach the model";
    EXPECT_NE(x_none, x_fixed) << "pinning nothing and pinning x0=42 cannot give the same estimate";
}

// 推失败必须**回滚**，不能把模型丢在断开状态。
//
// 这条是端到端跑真 Splitter 时抓出来的：setComponents 里先 disconnect 再
// connect，connect 失败就没人管了——对象停在"已断开"，之后每个调用都失败，
// 而真正的原因（组分表被拒）早被冲掉，查起来离病灶极远。
TEST(XOptMINLPcoUnitPorts, ARejectedPushLeavesThePreviousModelIntact) {
    const std::string desc_path = std::string(MOCK_XOPTMODEL_DLL) + ".rollback.json";
    const ScopedFile desc(desc_path, kGainDesc);

    XOptMINLPAdapter adapter(MOCK_XOPTMODEL_DLL, desc.str());
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();
    const size_t ports_before = adapter.ports().size();
    ASSERT_GT(ports_before, 0u);

    // 夹具对含 "NOPE" 的组分表一定说不
    ASSERT_LT(adapter.setComponents({"H2", "NOPE"}), 0);
    EXPECT_NE(adapter.lastError().find("NOPE"), std::string::npos)
        << "the failure must still name what was rejected: " << adapter.lastError();

    // 关键：旧模型还在，还能用
    EXPECT_EQ(adapter.components(), (std::vector<std::string>{"C1", "C2"}));
    EXPECT_EQ(adapter.ports().size(), ports_before);
    CapeMINLPSize size{};
    EXPECT_EQ(adapter.getSize(size), 0) << adapter.lastError();
    EXPECT_GT(size.num_variables, 0);

    // 回滚之后还能正常再推一次
    EXPECT_EQ(adapter.setComponents({"H2", "N2"}), 0) << adapter.lastError();
    EXPECT_EQ(adapter.components(), (std::vector<std::string>{"H2", "N2"}));
}

// 目标不是单元时不能报错：老部署里 XRTO_CAPEOPEN_TARGET 指的就是一个纯
// ICapeMINLP，那时"没有端口"是正常状态，不是连接失败。这条一红，说明消费端
// 把"没端口"和"连不上"混起来了，会让既有部署在升级后突然起不来。
// 推过组分表之后，宿主推下来的**非法**固定变量仍然要被拒绝。
//
// 那条"名字对不上就跳过"的宽容分支是给**旧部署描述**准备的：组分表一换，
// 描述文件里按组分写死的名字(in_fi_C1 之类)必然失效，对它报错等于要求描述
// 文件预知将来用哪套组分。但 setFixedVariables 推下来的是本次调用的明确意图,
// 同样放行的话，一个拼错的名字会被静默丢掉、调用还返回 0，用户要固定的条件
// 实际没生效 —— 而同一个非法集合在没推过组分时是会被拒绝的。同样的输入两种
// 结果，属于最难查的那类。
TEST(XOptMINLPcoUnitPorts, AnInvalidHostFixedSetIsStillRejectedAfterAComponentPush) {
    const std::string desc_path = std::string(MOCK_XOPTMODEL_DLL) + ".fixedvalidate.json";
    const ScopedFile desc(desc_path, kGainDesc);

    XOptMINLPAdapter adapter(MOCK_XOPTMODEL_DLL, desc.str());
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();

    // 基线：没推过组分表时，非法固定变量被拒绝
    EXPECT_LT(adapter.setFixedVariables({"not_a_var"}, {42.0}), 0)
        << "没推组分时就该拒绝";

    // 推一套新组分（fixture 只认 x0 作为可固定变量，组分怎么换都不影响这一点）
    ASSERT_EQ(adapter.setComponents({"H2", "N2"}), 0) << adapter.lastError();
    EXPECT_EQ(adapter.components(), (std::vector<std::string>{"H2", "N2"}));

    // 关键：推过组分之后，同一个非法集合**仍然**要被拒绝
    EXPECT_LT(adapter.setFixedVariables({"not_a_var"}, {42.0}), 0)
        << "推过组分表不该把宿主自己推下来的固定变量也一起放行";
    EXPECT_NE(adapter.lastError().find("not_a_var"), std::string::npos)
        << "报错要指得出是哪个名字，实际: " << adapter.lastError();

    // 而合法的固定集合照常生效 —— 拒绝那一次不能把适配器留在坏状态
    EXPECT_EQ(adapter.setFixedVariables({"x0"}, {5.0}), 0) << adapter.lastError();
    EXPECT_EQ(adapter.components(), (std::vector<std::string>{"H2", "N2"}))
        << "回滚不该把组分表也退回去";
}

TEST(XOptMINLPcoUnitPorts, ANonUnitTargetIsNotAnError) {
    int argc = 0;
    CORBA::ORB_ptr orb = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
    CORBA::Object_var p = orb->resolve_initial_references("RootPOA");
    PortableServer::POA_var poa = PortableServer::POA::_narrow(p.in());
    poa->the_POAManager()->activate();

    // 目标必须是一个**真能被远端访问**的对象，只是类型不对：单独激活一个
    // ICapeUnitPort。拿 RootPOA 之类的本地对象当靶子测不了这条——它连
    // object_to_string 都过不去，测的就变成别的东西了。
    PortServant* stray = new PortServant("stray", true);
    PortableServer::ObjectId_var oid = poa->activate_object(stray);
    CORBA::Object_var ref = poa->id_to_reference(oid.in());
    CORBA::String_var ior = orb->object_to_string(ref.in());
    stray->_remove_ref();

    CapeUnitCorba consumer;
    EXPECT_EQ(consumer.read(ior.in()), 0) << consumer.lastError();
    EXPECT_FALSE(consumer.isUnit());
    EXPECT_TRUE(consumer.ports().empty());
    EXPECT_TRUE(consumer.components().empty());
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
