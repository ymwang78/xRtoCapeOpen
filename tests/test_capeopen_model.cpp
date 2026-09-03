// ***************************************************************
//  test_capeopen_model   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  端到端模型通路测试：模拟 xOptModelBlackBox 加载本 DLL 的全过程——
//  xOptModel_createModel 填 xOptModelT -> 走 initializeModel/prepareRuntime
//  会调用的方法 -> buildProblem 填 xOptProblemT -> 驱动问题求值。
//  覆盖 design §4.2（完整 vtable）。本文件不含 main，复用同 exe 内其它
//  测试文件的 main。
// ***************************************************************

// 本 TU 需直接调用静态链接进来的导出符号 xOptModel_createModel，
// 故让 XOPTIF_API 解析为 dllexport（与 capeopen_core 内定义一致），
// 避免 dllimport 产生 __imp_ 间接符号导致链接失败。
#ifdef _WIN32
#    ifndef XOPTINTERFACE_EXPORTS
#        define XOPTINTERFACE_EXPORTS
#    endif
#endif

#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "xOpt/xOptModel.h"

namespace {

// 让连接目标确定性地指向内置 mock（环境变量优先级最高，见 resolveConnection）。
struct MockTargetEnv {
    MockTargetEnv() {
#ifdef _WIN32
        _putenv_s("XRTO_CAPEOPEN_TARGET", "mock:default");
#else
        setenv("XRTO_CAPEOPEN_TARGET", "mock:default", 1);
#endif
    }
};
MockTargetEnv g_mock_target_env;

// 没有配置连接目标时**必须失败**，不能悄悄拿内置 mock 顶上。
//
// 这条测的是一个真实事故：宿主传给 createModel 的 name 写死是 "BlackBoxModel"
// （xOptModelBlackBox.cpp），不带 scheme；旧代码在环境变量没设时回退
// "mock:default"，于是用户在 xRto 里放一个远端模型、根本没起服务端，**求解照样
// 成功**——解的却是一个二变量的小 NLP。连不上是能查的，解错了不是。
//
// 本用例刻意不用上面那个全局 MockTargetEnv：它必须在"什么都没配"的状态下跑，
// 所以自己临时清掉环境变量再还原。
TEST(CapeOpenModelTest, UnconfiguredTargetFails_RatherThanSilentlyUsingTheMock) {
    const char* saved_raw = std::getenv("XRTO_CAPEOPEN_TARGET");
    const std::string saved = saved_raw != nullptr ? saved_raw : "";
#ifdef _WIN32
    _putenv_s("XRTO_CAPEOPEN_TARGET", "");
#else
    unsetenv("XRTO_CAPEOPEN_TARGET");
#endif

    xOptModelT model{sizeof(xOptModelT)};
    // name 就用宿主真正会传的那个字符串——它不带 scheme，正是事故的前提。
    const int rc = xOptModel_createModel(&model, nullptr, "BlackBoxModel");

#ifdef _WIN32
    _putenv_s("XRTO_CAPEOPEN_TARGET", saved.c_str());
#else
    if (!saved.empty()) setenv("XRTO_CAPEOPEN_TARGET", saved.c_str(), 1);
#endif

    EXPECT_LT(rc, 0) << "an unconfigured target must fail, not fall back to the built-in mock";
    if (rc >= 0 && model.destroyModel != nullptr && model.handle != nullptr) {
        model.destroyModel(model.handle);  // 万一真的成功了，别泄漏
    }
}

// DLL 同目录的 <基名>.target 文件必须被读到，且**优先于**环境变量。
//
// 这条测的是可用性而不是正确性：环境变量必须设进"启动 xRto 的那个进程"，
// 隔一层就失效，实际用起来非常容易漏；同目录的配置文件跟着部署走。
// 优先级也要钉住——环境变量是全局的，若它压过按单元的配置，两个连不同服务端
// 的单元就没法共存了。
//
// 这里 thisModulePath() 解析到的是**测试 exe 自己**（core 是静态库，代码就在
// exe 里），所以 sidecar 就写在 exe 旁边。
TEST(CapeOpenModelTest, SidecarTargetFileWinsOverTheEnvironment) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2] = {0};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    ASSERT_GT(n, 0u);
    std::wstring exe(buf, n);
    const size_t dot = exe.find_last_of(L'.');
    const std::wstring sidecar = (dot == std::wstring::npos ? exe : exe.substr(0, dot)) + L".target";

    // 环境变量指向一个**连不上**的目标；sidecar 指向 mock。若优先级搞反了，
    // createModel 会拿 com: 那个去连，buildProblem 必然失败——用一个"能连上"的
    // 值当对照，就分不出是谁生效了。
    _putenv_s("XRTO_CAPEOPEN_TARGET", "com:No.Such.ProgId");
    {
        std::ofstream out(sidecar);
        ASSERT_TRUE(out.good());
        out << "# comment line, must be skipped" << std::endl;
        out << "mock:default" << std::endl;
    }

    xOptModelT model{sizeof(xOptModelT)};
    const int rc = xOptModel_createModel(&model, nullptr, "BlackBoxModel");
    bool built = false;
    if (rc >= 0) {
        xOptProblemT problem{sizeof(xOptProblemT)};
        built = (model.buildProblem(model.handle, &problem) >= 0);
        if (built) problem.destroyProblem(problem.handle);
        model.destroyModel(model.handle);
    }

    _wremove(sidecar.c_str());
    _putenv_s("XRTO_CAPEOPEN_TARGET", "mock:default");  // 还原给其余用例

    ASSERT_GE(rc, 0) << "the sidecar target file was not picked up";
    EXPECT_TRUE(built) << "the sidecar must win over XRTO_CAPEOPEN_TARGET";
#else
    GTEST_SKIP() << "module-path lookup is exercised on Windows only";
#endif
}

// 说明：端口/拓扑那部分逻辑**不在本测试二进制里**。xOptModelCapeOpen.cpp 的
// CORBA 分支由 CAPEOPEN_WITH_CORBA 控制，而该宏只加在 capeopen_corba 与
// xrtocapeopen_dll 两个目标上；capeopen_core（本测试所链）编译的是 #else 分支，
// 端口一律返回 0。想覆盖那条路径，测试得去加载真正的 xRtoCapeOpen.dll。
// 目前它由 tests/test_xoptminlpco_unitports.cpp（servant + CapeUnitCorba 回环）
// 和手工探针覆盖，DLL 那一层还没有自动化测试。

TEST(CapeOpenModelTest, FullVtable_NonNull) {
    xOptModelT model{sizeof(xOptModelT)};
    ASSERT_EQ(xOptModel_createModel(&model, nullptr, "mock:default"), 0);
    ASSERT_NE(model.handle, nullptr);

    // initializeModel() / prepareRuntime() 会用到的指针必须非空
    EXPECT_NE(model.destroyModel, nullptr);
    EXPECT_NE(model.buildProblem, nullptr);
    EXPECT_NE(model.getParameters, nullptr);
    EXPECT_NE(model.setParameters, nullptr);
    EXPECT_NE(model.setProblemType, nullptr);
    EXPECT_NE(model.validateModel, nullptr);
    EXPECT_NE(model.getInPortNum, nullptr);
    EXPECT_NE(model.getOutPortNum, nullptr);
    EXPECT_NE(model.getInPortVariableMap, nullptr);
    EXPECT_NE(model.getOutPortVariableMap, nullptr);

    model.destroyModel(model.handle);
}

TEST(CapeOpenModelTest, Parameters_AtLeastOne) {
    xOptModelT model{sizeof(xOptModelT)};
    ASSERT_EQ(xOptModel_createModel(&model, nullptr, "mock:default"), 0);

    // 两段式：先查个数（initializeModel 要求 > 0）
    int size = 0;
    ASSERT_GE(model.getParameters(model.handle, nullptr, nullptr, size), 0);
    ASSERT_GE(size, 1);

    std::vector<const char*> names(size, nullptr);
    std::vector<double> values(size, 0.0);
    ASSERT_GE(model.getParameters(model.handle, names.data(), values.data(), size), 0);
    EXPECT_NE(names[0], nullptr);
    EXPECT_EQ(model.setParameters(model.handle, names.data(), values.data(), size), 0);

    model.destroyModel(model.handle);
}

TEST(CapeOpenModelTest, BuildProblem_DrivesMockEndToEnd) {
    xOptModelT model{sizeof(xOptModelT)};
    ASSERT_EQ(xOptModel_createModel(&model, nullptr, "mock:default"), 0);

    // 模拟 prepareRuntime 的前置校验
    EXPECT_EQ(model.validateModel(model.handle), 0);
    EXPECT_EQ(model.getInPortNum(model.handle), 0);
    EXPECT_EQ(model.getOutPortNum(model.handle), 0);

    // 构造问题 vtable
    xOptProblemT problem{sizeof(xOptProblemT)};
    ASSERT_EQ(model.buildProblem(model.handle, &problem), 0);
    ASSERT_NE(problem.handle, nullptr);

    EXPECT_EQ(problem.numVariables(problem.handle), 2);
    EXPECT_EQ(problem.numConstraints(problem.handle), 1);

    const double x[2] = {3.0, 4.0};
    ASSERT_EQ(problem.setX(problem.handle, x, 2), 2);
    double obj = 0;
    ASSERT_EQ(problem.evaluateObjective(problem.handle, &obj), 0);
    EXPECT_DOUBLE_EQ(obj, 25.0);  // 3^2 + 4^2

    // 生命周期：先释放问题，再释放模型（镜像宿主行为）
    problem.destroyProblem(problem.handle);
    model.destroyModel(model.handle);
}

// 复刻 xOptModelBlackBox::generateEstimate 的判定：模型必须原样退回 size，
// 否则宿主整个「生成初值」失败（返回 -1，界面报"设置初始值失败"）。
// 本项目无估计能力，约定是保留调用方给的点，而不是把 size 置 0。
TEST(CapeOpenModelTest, GenerateEstimate_PreservesCallerSizeAndValues) {
    xOptModelT model{sizeof(xOptModelT)};
    ASSERT_EQ(xOptModel_createModel(&model, nullptr, "mock:default"), 0);
    ASSERT_NE(model.generateEstimate, nullptr);

    std::vector<double> init_x = {3.0, 4.0};
    // 宿主把固定变量名打包成 '\0' 分隔的连续缓冲，值与之一一对应
    const char packed_names[] = "x0\0";
    const double packed_values[] = {3.0};

    int estimate_size = static_cast<int>(init_x.size());
    const int ret = model.generateEstimate(model.handle, init_x.data(), estimate_size, packed_names,
                                           packed_values, 1);

    EXPECT_GE(ret, 0);                                              // 宿主判失败用 < 0
    EXPECT_EQ(estimate_size, static_cast<int>(init_x.size()));      // 宿主要求 size 不变
    EXPECT_DOUBLE_EQ(init_x[0], 3.0);                               // 调用方的点原样保留
    EXPECT_DOUBLE_EQ(init_x[1], 4.0);

    model.destroyModel(model.handle);
}

}  // namespace
