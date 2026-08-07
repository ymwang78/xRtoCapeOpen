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

#include <cstdlib>

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
