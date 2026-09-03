// ***************************************************************
//  test_capeopen100_unit_rid   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  钉住 CAPEOPEN100_Unit.idl 与 XOPTCO_Ext.idl 的 Repository ID。
//
//  与 test_capeopen100_rid.cpp 同一个理由，不重复展开：CORBA 里接口的身份
//  就是 RID，而 RID 由 module 嵌套隐式推出——改错了编译照过、测试照绿，
//  只在对接时才炸。Unit 这批比 Minlp 那批更需要钉：它多了两级 module
//  （Business::UnitOp::Unit），而且 Common 下一口气新增了 Collection /
//  Utilities / Parameter 三个同级 module，正是容易放错层的地方。
//
//  期望值出处：Methods&Tools_Integrated_Guidelines.pdf p.56-60 内联的官方
//  module 骨架，见 tools/gen_capeopen_unit_idl.py 的 PROVENANCE 第 2 条。
// ***************************************************************
#include "CAPEOPEN100_UnitC.h"
#include "XOPTCO_ExtC.h"

#include <gtest/gtest.h>

#include <string>

namespace {

namespace UO = ::CAPEOPEN100::Business::UnitOp::Unit;

TEST(CapeOpen100UnitRidTest, UnitOperationInterfaces_MatchOfficialModulePath) {
    EXPECT_STREQ(UO::_tc_ICapeUnit->id(),
                 "IDL:CAPEOPEN100/Business/UnitOp/Unit/ICapeUnit:1.0");
    EXPECT_STREQ(UO::_tc_ICapeUnitPort->id(),
                 "IDL:CAPEOPEN100/Business/UnitOp/Unit/ICapeUnitPort:1.0");
    EXPECT_STREQ(UO::_tc_ICapeUnitReport->id(),
                 "IDL:CAPEOPEN100/Business/UnitOp/Unit/ICapeUnitReport:1.0");
    EXPECT_STREQ(UO::_tc_ICapeUnitPortVariables->id(),
                 "IDL:CAPEOPEN100/Business/UnitOp/Unit/ICapeUnitPortVariables:1.0");
}

// 端口的两个枚举刻意留在 Unit module 内（生成器已知偏差 d）。若哪天决定把
// 它们提到 Common::Types，这两条会红——那正是我们想要的提醒。
TEST(CapeOpen100UnitRidTest, PortEnums_StayInUnitModule) {
    EXPECT_STREQ(UO::_tc_CapePortType->id(),
                 "IDL:CAPEOPEN100/Business/UnitOp/Unit/CapePortType:1.0");
    EXPECT_STREQ(UO::_tc_CapePortDirection->id(),
                 "IDL:CAPEOPEN100/Business/UnitOp/Unit/CapePortDirection:1.0");
}

TEST(CapeOpen100UnitRidTest, CommonInterfaces_MatchOfficialModulePath) {
    EXPECT_STREQ(::CAPEOPEN100::Common::Collection::_tc_ICapeCollection->id(),
                 "IDL:CAPEOPEN100/Common/Collection/ICapeCollection:1.0");
    EXPECT_STREQ(::CAPEOPEN100::Common::Utilities::_tc_ICapeUtilities->id(),
                 "IDL:CAPEOPEN100/Common/Utilities/ICapeUtilities:1.0");
    EXPECT_STREQ(::CAPEOPEN100::Common::Parameter::_tc_ICapeParameter->id(),
                 "IDL:CAPEOPEN100/Common/Parameter/ICapeParameter:1.0");
    EXPECT_STREQ(::CAPEOPEN100::Common::Parameter::_tc_ICapeParameterSpec->id(),
                 "IDL:CAPEOPEN100/Common/Parameter/ICapeParameterSpec:1.0");
}

// ECapeBadCOParameter 是本文件在 Common::Error 里补的（Minlp 那份没发，因为
// 没有 Minlp 操作 raise 它）。钉住它，确认重开 module 没把它挪到别处。
TEST(CapeOpen100UnitRidTest, AddedErrorReopensCommonError) {
    EXPECT_STREQ(::CAPEOPEN100::Common::Error::_tc_ECapeBadCOParameter->id(),
                 "IDL:CAPEOPEN100/Common/Error/ECapeBadCOParameter:1.0");
}

// Minlp 那份文件被 #include 进来了；顺手确认重开 module 没有把已有的 RID
// 改掉——这正是"两份 IDL 共用 Common::* "这个做法唯一可能出错的地方。
TEST(CapeOpen100UnitRidTest, IncludingMinlpDoesNotDisturbItsRids) {
    EXPECT_STREQ(::CAPEOPEN100::Business::Numeric::Minlp::_tc_ICapeMINLP->id(),
                 "IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLP:1.0");
    EXPECT_STREQ(::CAPEOPEN100::Common::Identification::_tc_ICapeIdentification->id(),
                 "IDL:CAPEOPEN100/Common/Identification/ICapeIdentification:1.0");
}

// 我们自己的扩展**必须**落在 XOPTCO 而不是 CAPEOPEN100 下。这条断言的意义
// 不是防手滑，是防"哪天有人觉得放进 CAPEOPEN100 更整齐"——那会让一个非
// 标准接口顶着 CAPE-OPEN 的 RID 出现在 IOR 里，对第三方就是谎报。
TEST(CapeOpen100UnitRidTest, OurExtensionIsNotUnderCapeOpen) {
    const char* id = ::XOPTCO::_tc_IXOptUnitExtension->id();
    EXPECT_STREQ(id, "IDL:XOPTCO/IXOptUnitExtension:1.0");
    EXPECT_EQ(std::string(id).find("CAPEOPEN"), std::string::npos)
        << "our extension must not carry a CAPE-OPEN Repository ID: " << id;
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
