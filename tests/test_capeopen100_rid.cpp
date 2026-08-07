// ***************************************************************
//  test_capeopen100_rid   version:  1.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  钉住重建 IDL（CAPEOPEN100_Minlp.idl）的 Repository ID。
//
//  为什么这条断言值得单独存在：CORBA 里接口的身份**就是** Repository ID，
//  地位等同 COM 的 IID。第三方 CO 客户端 `_narrow` 内部做的是
//  `_is_a("IDL:CAPEOPEN100/...")`——方法名和签名再对，RID 不对就一律判否。
//  而 RID 是由 IDL 的 module 嵌套隐式推出来的：谁手滑改了一层 module 名，
//  编译照过、测试照绿、只有跟真实 PME 对接时才炸。所以把它显式写死在这里。
//
//  期望值的来源见 docs/xOptMINLPco_design.md §6.1（Methods&Tools 指南内联的
//  官方 module 骨架 + 「#pragma version ... isn't used here」）。
// ***************************************************************
#include "CAPEOPEN100_MinlpC.h"

#include <gtest/gtest.h>

namespace {

TEST(CapeOpen100RidTest, ICapeMINLP_MatchesOfficialModulePath) {
    EXPECT_STREQ(::CAPEOPEN100::Business::Numeric::Minlp::_tc_ICapeMINLP->id(),
                 "IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLP:1.0");
}

TEST(CapeOpen100RidTest, ICapeIdentification_MatchesOfficialModulePath) {
    EXPECT_STREQ(::CAPEOPEN100::Common::Identification::_tc_ICapeIdentification->id(),
                 "IDL:CAPEOPEN100/Common/Identification/ICapeIdentification:1.0");
}

// 另两个 Minlp 接口一并钉住：它们和 ICapeMINLP 同处一个 module，
// 改动 module 嵌套时会一起错，多一层交叉验证。
TEST(CapeOpen100RidTest, SiblingInterfaces_MatchOfficialModulePath) {
    EXPECT_STREQ(::CAPEOPEN100::Business::Numeric::Minlp::_tc_ICapeMINLPSystem->id(),
                 "IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLPSystem:1.0");
    EXPECT_STREQ(::CAPEOPEN100::Business::Numeric::Minlp::_tc_ICapeMINLPSolverManager->id(),
                 "IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLPSolverManager:1.0");
}

// 规范 §3.6.1 定义了 32 个操作，当前 SqpSolver.idl 只声明 17 个（design §6.2
// 缺口 3）。这里不测数量——POA 骨架的纯虚函数会在 servant 迁移时强制补齐——
// 但把 Minlp 自有的两个类型钉住，确认它们没被误挪进 Common。
TEST(CapeOpen100RidTest, OptimisationOwnedTypes_StayInMinlpModule) {
    EXPECT_STREQ(::CAPEOPEN100::Business::Numeric::Minlp::_tc_CapeMINLPObjFunType->id(),
                 "IDL:CAPEOPEN100/Business/Numeric/Minlp/CapeMINLPObjFunType:1.0");
    EXPECT_STREQ(::CAPEOPEN100::Business::Numeric::Minlp::_tc_ECapeHessianInfoNotAvailable->id(),
                 "IDL:CAPEOPEN100/Business/Numeric/Minlp/ECapeHessianInfoNotAvailable:1.0");
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
