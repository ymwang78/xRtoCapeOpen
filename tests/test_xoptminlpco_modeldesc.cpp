// ***************************************************************
//  test_xoptminlpco_modeldesc   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  两组测试：
//    1. XOptModelDesc 的解析（纯字符串，无 DLL 依赖）
//    2. XOptMINLPAdapter::initModel 的端到端（经 mock_xoptmodel DLL）
//
//  第 2 组是这次改动真正的靶子：改动前 adapter 的 C-ABI 分支是 createModel
//  完直接 buildProblem，凡"必须先 setSlate"的模型一律失败。所以这里既断言
//  "有描述就能连上并且值对得上"，也断言"没有描述时失败信息指得出病灶"——
//  后者是反向验证：把 initModel 摘掉，NoDescription 会变成"连上了"而不是
//  报出别的错。
// ***************************************************************
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "XOptMINLPAdapter.h"
#include "XOptModelDesc.h"

namespace {

// Splitter_Model.json 的形状（examples/testModel/Splitter_Model.json）。
const char* const kSplitterShapedJson = R"({
  "parameters": { "split_ratio": 0.5, "min_product_flow": 0.1 },
  "fixable_variables": [ "in_T", "in_P", "in_fi_C1", "in_fi_C2" ],
  "inports": [ { "in_T": "T", "in_P": "P", "in_fi_C1": "fi_C1", "in_fi_C2": "fi_C2" } ],
  "outports": [
    { "out1_T": "T", "out1_P": "P", "out1_fi_C1": "fi_C1", "out1_fi_C2": "fi_C2" },
    { "out2_T": "T", "out2_P": "P", "out2_fi_C1": "fi_C1", "out2_fi_C2": "fi_C2" }
  ]
})";

// 用完即删的临时文件，断言失败提前 return 时也能清干净。
class ScopedFile {
  public:
    ScopedFile(std::filesystem::path path, const std::string& text) : path_(std::move(path)) {
        std::ofstream out(path_, std::ios::binary);
        out << text;
    }
    ~ScopedFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;
    std::string str() const { return path_.string(); }

  private:
    std::filesystem::path path_;
};

std::filesystem::path tempDir() {
    std::error_code ec;
    return std::filesystem::temp_directory_path(ec);
}

// ============================================================
//  1) 解析
// ============================================================

TEST(XOptModelDescParse, SplitterShapedJson) {
    XOptModelDesc d;
    std::string err;
    ASSERT_TRUE(XOptModelDesc::parse(kSplitterShapedJson, d, err)) << err;

    ASSERT_EQ(d.parameters.size(), 2u);
    EXPECT_EQ(d.parameters[0].first, "split_ratio");  // 保序：报错时按书写顺序提名字
    EXPECT_DOUBLE_EQ(d.parameters[0].second, 0.5);
    EXPECT_EQ(d.parameters[1].first, "min_product_flow");
    EXPECT_DOUBLE_EQ(d.parameters[1].second, 0.1);

    EXPECT_EQ(d.fixable_variables,
              (std::vector<std::string>{"in_T", "in_P", "in_fi_C1", "in_fi_C2"}));

    // 组分表从端口映射的 fi_<组分> 推出，按首次出现顺序去重
    EXPECT_EQ(d.components, (std::vector<std::string>{"C1", "C2"}));
    EXPECT_EQ(d.slate_name, "StreamPort");
    EXPECT_TRUE(d.thermo_method.empty());
}

TEST(XOptModelDescParse, ExplicitSlateWinsOverPorts) {
    const char* const json = R"({
      "slate": { "name": "Feed", "thermo_method": "SRK", "components": ["A", "B", "C"] },
      "inports": [ { "in_fi_X": "fi_X" } ]
    })";
    XOptModelDesc d;
    std::string err;
    ASSERT_TRUE(XOptModelDesc::parse(json, d, err)) << err;
    EXPECT_EQ(d.slate_name, "Feed");
    EXPECT_EQ(d.thermo_method, "SRK");
    EXPECT_EQ(d.components, (std::vector<std::string>{"A", "B", "C"}));  // 不是 {"X"}
}

TEST(XOptModelDescParse, BareComponentsArray) {
    XOptModelDesc d;
    std::string err;
    ASSERT_TRUE(XOptModelDesc::parse(R"({"components": ["C1"]})", d, err)) << err;
    EXPECT_EQ(d.components, (std::vector<std::string>{"C1"}));
}

TEST(XOptModelDescParse, FixedValues) {
    XOptModelDesc d;
    std::string err;
    ASSERT_TRUE(XOptModelDesc::parse(R"({"fixed_values": {"in_T": 300, "in_P": 101.325}})", d,
                                     err))
        << err;
    ASSERT_EQ(d.fixed_values.size(), 2u);
    EXPECT_DOUBLE_EQ(d.fixed_values["in_T"], 300.0);
    EXPECT_DOUBLE_EQ(d.fixed_values["in_P"], 101.325);
}

TEST(XOptModelDescParse, NumbersAndEscapes) {
    XOptModelDesc d;
    std::string err;
    // 源文件里保持纯 ASCII：这条断言要考的是解析器把 unicode 转义转成 UTF-8，
    // 不是编译器怎么读本文件的编码。
    ASSERT_TRUE(XOptModelDesc::parse(
                    "{\"parameters\": {\"a\": -1.5e-3, \"b\": 2E2},"
                    " \"components\": [\"A\\u00B0B\"]}",
                    d, err))
        << err;
    EXPECT_DOUBLE_EQ(d.parameters[0].second, -0.0015);
    EXPECT_DOUBLE_EQ(d.parameters[1].second, 200.0);
    EXPECT_EQ(d.components[0], "A\xC2\xB0" "B");  // U+00B0 的 UTF-8 编码
}

TEST(XOptModelDescParse, RejectsMalformedAndWrongTypes) {
    XOptModelDesc d;
    std::string err;

    EXPECT_FALSE(XOptModelDesc::parse("{", d, err));
    EXPECT_FALSE(err.empty());

    err.clear();
    EXPECT_FALSE(XOptModelDesc::parse("[1,2]", d, err));  // 顶层必须是对象
    EXPECT_NE(err.find("object"), std::string::npos) << err;

    err.clear();
    EXPECT_FALSE(XOptModelDesc::parse(R"({"parameters": {"a": "1"}})", d, err));
    EXPECT_NE(err.find("number"), std::string::npos) << err;

    err.clear();
    EXPECT_FALSE(XOptModelDesc::parse(R"({"components": [1]})", d, err));
    EXPECT_NE(err.find("strings"), std::string::npos) << err;

    err.clear();
    EXPECT_FALSE(XOptModelDesc::parse(R"({"a": 1} trailing)", d, err));
}

TEST(XOptModelDescLoad, EatsUtf8Bom) {
    // 本仓库的源文件按规范都带 BOM，描述文件被同一批编辑器另存时也常带上；
    // 不吃掉它，报错会指向一个肉眼完全正常的文件的第 0 字节。
    const std::string with_bom = std::string("\xEF\xBB\xBF") + R"({"components": ["C1"]})";
    const ScopedFile f(tempDir() / "xrto_desc_bom.json", with_bom);

    XOptModelDesc d;
    std::string err;
    ASSERT_TRUE(XOptModelDesc::load(f.str(), d, err)) << err;
    EXPECT_EQ(d.components, (std::vector<std::string>{"C1"}));
}

TEST(XOptModelDescLoad, MissingFileNamesThePath) {
    XOptModelDesc d;
    std::string err;
    const std::string path = (tempDir() / "xrto_desc_does_not_exist.json").string();
    EXPECT_FALSE(XOptModelDesc::load(path, d, err));
    EXPECT_NE(err.find(path), std::string::npos) << err;
}

// ============================================================
//  2) 端到端：adapter 经 C-ABI 模型 DLL
// ============================================================
#ifdef MOCK_XOPTMODEL_DLL

std::filesystem::path mockDll() { return std::filesystem::path(MOCK_XOPTMODEL_DLL); }
std::filesystem::path mockDllDir() { return mockDll().parent_path(); }

// gain 模型的描述：2 个组分、gain=3、x0 固定在 7。
const char* const kGainDesc = R"({
  "parameters": { "gain": 3.0 },
  "fixable_variables": [ "x0" ],
  "fixed_values": { "x0": 7.0 },
  "components": [ "C1", "C2" ]
})";

TEST(XOptMINLPcoModelInit, NoDescriptionFailsAndPointsAtTheSlate) {
    // 反向验证的锚点：initModel 被摘掉时这条会变成"连上了"。
    // 同时也钉住失败信息——只说 "buildProblem failed" 指不到病灶。
    XOptMINLPAdapter adapter(mockDll().string(), "");
    ASSERT_LT(adapter.connect(), 0) << "a model that needs a slate must not connect bare";
    const std::string err = adapter.lastError();
    EXPECT_NE(err.find("slate"), std::string::npos) << err;
}

TEST(XOptMINLPcoModelInit, ExplicitDescriptionDrivesTheHandshake) {
    const ScopedFile desc(tempDir() / "xrto_gain_desc.json", kGainDesc);

    XOptMINLPAdapter adapter(mockDll().string(), desc.str());
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();

    // 组分表 -> 规模
    CapeMINLPSize size{};
    ASSERT_EQ(adapter.getSize(size), 0);
    EXPECT_EQ(size.num_variables, 3);    // x0, f_C1, f_C2
    EXPECT_EQ(size.num_constraints, 3);  // bal_C1, bal_C2, fix_x0

    std::vector<std::string> names;
    ASSERT_EQ(adapter.getVariableNames({}, names), 0);
    EXPECT_EQ(names, (std::vector<std::string>{"x0", "f_C1", "f_C2"}));

    // 固定值 -> 初值（generateEstimate 的估计，经 initial_x_ 传到这里）
    std::vector<double> values;
    ASSERT_EQ(adapter.getVariableValues({}, values), 0);
    ASSERT_EQ(values.size(), 3u);
    EXPECT_DOUBLE_EQ(values[0], 7.0);
    EXPECT_DOUBLE_EQ(values[1], 21.0);  // gain * x0
    EXPECT_DOUBLE_EQ(values[2], 21.0);

    // 参数 -> 雅可比值：d bal_i / d x0 = -gain。这一条只有 setParameters
    // 真的被回送过才成立，默认 gain=1 会给出 -1。
    std::vector<double> jac;
    ASSERT_EQ(adapter.getConstraintDerivativeValues("Jacobian", {}, jac), 0);
    ASSERT_EQ(jac.size(), 5u);  // 2 * 2 + 1
    EXPECT_DOUBLE_EQ(jac[0], -3.0);
    EXPECT_DOUBLE_EQ(jac[1], 1.0);
    EXPECT_DOUBLE_EQ(jac[2], -3.0);
    EXPECT_DOUBLE_EQ(jac[3], 1.0);
    EXPECT_DOUBLE_EQ(jac[4], 1.0);

    // 约束值：在初值处应当全 0（f_i = gain*x0，x0 = 固定值）
    ASSERT_EQ(adapter.setVariableValues({}, values), 0);
    std::vector<double> cons;
    ASSERT_EQ(adapter.getNonlinearConstraintValues({}, cons), 0);
    ASSERT_EQ(cons.size(), 3u);
    for (double c : cons) EXPECT_NEAR(c, 0.0, 1e-12);
}

TEST(XOptMINLPcoModelInit, DiscoversTheModelJsonNextToTheDll) {
    // 部署形态就是 DLL 与 <名称>_Model.json 同目录，所以不给路径也该找得到。
    const ScopedFile desc(mockDllDir() / "MockGain_Model.json", kGainDesc);

    XOptMINLPAdapter adapter(mockDll().string());  // 不给描述路径
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();
    CapeMINLPSize size{};
    ASSERT_EQ(adapter.getSize(size), 0);
    EXPECT_EQ(size.num_variables, 3);
}

TEST(XOptMINLPcoModelInit, AmbiguousDiscoveryIsAnErrorNotAGuess) {
    // 两份描述时随便挑一个，会让"换了 json 却没生效"变成静默行为。
    const ScopedFile a(mockDllDir() / "MockGainA_Model.json", kGainDesc);
    const ScopedFile b(mockDllDir() / "MockGainB_Model.json", kGainDesc);

    XOptMINLPAdapter adapter(mockDll().string());
    ASSERT_LT(adapter.connect(), 0);
    EXPECT_NE(adapter.lastError().find("several"), std::string::npos) << adapter.lastError();
}

TEST(XOptMINLPcoModelInit, UnknownParameterIsRejectedNotDropped) {
    const ScopedFile desc(tempDir() / "xrto_gain_bad_param.json",
                          R"({"parameters": {"no_such_param": 1.0}, "components": ["C1"]})");

    XOptMINLPAdapter adapter(mockDll().string(), desc.str());
    ASSERT_LT(adapter.connect(), 0);
    // 名字要出现在消息里：否则用户拿着一个"配了却没生效"的文件无从下手
    EXPECT_NE(adapter.lastError().find("no_such_param"), std::string::npos)
        << adapter.lastError();
}

TEST(XOptMINLPcoModelInit, UnknownFixableVariableIsRejected) {
    const ScopedFile desc(tempDir() / "xrto_gain_bad_fix.json",
                          R"({"components": ["C1"], "fixable_variables": ["not_a_var"]})");

    XOptMINLPAdapter adapter(mockDll().string(), desc.str());
    ASSERT_LT(adapter.connect(), 0);
    EXPECT_NE(adapter.lastError().find("not_a_var"), std::string::npos) << adapter.lastError();
}

#endif  // MOCK_XOPTMODEL_DLL

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
