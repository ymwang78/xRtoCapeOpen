// ***************************************************************
//  test_xoptminlpco_onebased   version:  1.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  生产端索引基的**非回环**测试。
//
//  为什么必须非回环：其余 CORBA 用例都是「我们的 servant → 我们的消费端」，
//  两端用同一套约定，一起偏也照样全绿——CAPE-OPEN 规范要求 1-based 而全线
//  0-based 这个缺陷（design §6.2 缺口 2）就是这么藏了两个月的。
//
//  所以这里不接消费端，直接对着 CORBA stub 手工构造 1-based 的入参、
//  手工写下 1-based 的期望值，逐条断言 MINLPServant 在**线上**的行为：
//
//    - vids 按规范从 1 开始：{1,2} 才是「全部两个变量」，不是 {0,1}
//    - 结构索引出网时是 1-based
//    - 越界 id 必须报错，不能静默返回 0（v1 的 pick() 就是静默返回 T{}，
//      于是「求解器发了个非法 id」变成「悄悄算了个 0」）
//
//  背靠 MockXOptProblem：nv=2, nc=1, obj=x0^2+x1^2, cons=x0+x1-3。
//  自带 main。
// ***************************************************************
#include "MockXOptProblem.h"  // 首个包含：XOPTINTERFACE_EXPORTS + xOptProblem

#include <gtest/gtest.h>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <vector>

#include "MINLPServant.h"
#include "XOptMINLPAdapter.h"

namespace ct = ::CAPEOPEN100::Common::Types;
namespace cm = ::CAPEOPEN100::Business::Numeric::Minlp;

namespace {

// 手工构造线上序列，刻意不借用 CapeCorbaMarshal 的任何 helper：
// 本用例校验的就是那套 helper 的用法对不对。
ct::CapeArrayLong wireIds(std::initializer_list<CORBA::Long> ids) {
    ct::CapeArrayLong s;
    s.length(static_cast<CORBA::ULong>(ids.size()));
    CORBA::ULong i = 0;
    for (CORBA::Long v : ids) s[i++] = v;
    return s;
}

std::vector<CORBA::Long> toVec(const ct::CapeArrayLong& s) {
    std::vector<CORBA::Long> out(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) out[i] = s[i];
    return out;
}

class OneBasedWireTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_EQ(adapter_.connect(), 0) << adapter_.lastError();
        int argc = 0;
        orb_ = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
        CORBA::Object_var p = orb_->resolve_initial_references("RootPOA");
        PortableServer::POA_var poa = PortableServer::POA::_narrow(p.in());
        poa->the_POAManager()->activate();

        servant_ = new MINLPServant(&adapter_);
        PortableServer::ObjectId_var oid = poa->activate_object(servant_);
        CORBA::Object_var obj = poa->id_to_reference(oid.in());
        minlp_ = cm::ICapeMINLP::_narrow(obj.in());
        ASSERT_FALSE(CORBA::is_nil(minlp_.in()));
    }

    MockXOptProblem mock_;
    XOptMINLPAdapter adapter_{&mock_};
    CORBA::ORB_var orb_;
    MINLPServant* servant_ = nullptr;
    cm::ICapeMINLP_var minlp_;
};

// 规范：「The variable indices vids must be in the range 1,...,nv」。
TEST_F(OneBasedWireTest, VariableIdsAreOneBasedOnTheWire) {
    ct::CapeArrayString_var names;
    minlp_->GetMINLPVariableNames(wireIds({1, 2}), names.out());
    ASSERT_EQ(names->length(), 2u);
    EXPECT_STREQ(names[0u], "x0");
    EXPECT_STREQ(names[1u], "x1");

    // 单独取第 2 个变量：线上的 2 == 内部的 x1。
    ct::CapeArrayString_var second;
    minlp_->GetMINLPVariableNames(wireIds({2}), second.out());
    ASSERT_EQ(second->length(), 1u);
    EXPECT_STREQ(second[0u], "x1");
}

TEST_F(OneBasedWireTest, VariableBoundsFollowOneBasedIds) {
    ct::CapeArrayDouble_var lb, ub;
    minlp_->GetMINLPVariableBounds(wireIds({1, 2}), lb.out(), ub.out());
    ASSERT_EQ(lb->length(), 2u);
    EXPECT_DOUBLE_EQ(lb[0u], -10.0);
    EXPECT_DOUBLE_EQ(ub[1u], 10.0);
}

// 结构索引出网必须是 1-based：mock 的 Jacobian 内部是 rows{0,0} cols{0,1}。
TEST_F(OneBasedWireTest, StructureIndicesAreOneBasedOnTheWire) {
    ct::CapeArrayLong_var r, c, o;
    minlp_->GetMINLPStructure("Jacobian", r.out(), c.out(), o.out());
    EXPECT_EQ(toVec(r.in()), (std::vector<CORBA::Long>{1, 1}));
    EXPECT_EQ(toVec(c.in()), (std::vector<CORBA::Long>{1, 2}));
}

TEST_F(OneBasedWireTest, ObjectiveGradientStructureIsOneBased) {
    ct::CapeArrayLong_var r, c, o;
    minlp_->GetMINLPStructure("ObjectiveGradient", r.out(), c.out(), o.out());
    EXPECT_EQ(toVec(o.in()), (std::vector<CORBA::Long>{1, 2}));
}

// setX 走 1-based，随后的求值必须落在正确的变量上。
TEST_F(OneBasedWireTest, SetValuesByOneBasedIdsHitsTheRightVariables) {
    ct::CapeArrayDouble v;
    v.length(2);
    v[0u] = 3.0;
    v[1u] = 4.0;
    minlp_->SetMINLPVariableValues(wireIds({1, 2}), v);

    CORBA::Double obj = 0;
    minlp_->GetMINLPNonlinearObjectiveFunctionValue(obj);
    EXPECT_DOUBLE_EQ(obj, 25.0);  // 3^2 + 4^2

    ct::CapeArrayDouble_var cons;
    minlp_->GetMINLPNonlinearConstraintValues(wireIds({1}), cons.out());
    ASSERT_EQ(cons->length(), 1u);
    EXPECT_DOUBLE_EQ(cons[0u], 4.0);  // 3 + 4 - 3
}

// 关键回归：0 在 1-based 里是非法的。v1 会把它当成内部下标 0 悄悄返回 x0。
TEST_F(OneBasedWireTest, ZeroIsRejectedBecauseNumberingStartsAtOne) {
    ct::CapeArrayString_var names;
    EXPECT_THROW(minlp_->GetMINLPVariableNames(wireIds({0}), names.out()), CORBA::BAD_PARAM);
}

// 关键回归：nv+1 越界。v1 的 pick() 对越界 id 静默返回 T{}——
// 于是最后一个变量会变成 0 而不报错。
TEST_F(OneBasedWireTest, PastTheEndIsRejectedNotSilentlyZeroed) {
    ct::CapeArrayString_var names;
    EXPECT_THROW(minlp_->GetMINLPVariableNames(wireIds({3}), names.out()), CORBA::BAD_PARAM);

    ct::CapeArrayDouble_var cons;
    EXPECT_THROW(minlp_->GetMINLPNonlinearConstraintValues(wireIds({2}), cons.out()),
                 CORBA::BAD_PARAM);  // nc == 1，故 2 越界
}

// 规范里空序列表示「全部」，不应被当成越界。
TEST_F(OneBasedWireTest, EmptyIdListMeansAll) {
    ct::CapeArrayString_var names;
    minlp_->GetMINLPVariableNames(wireIds({}), names.out());
    ASSERT_EQ(names->length(), 2u);
    EXPECT_STREQ(names[0u], "x0");
    EXPECT_STREQ(names[1u], "x1");
}

// 目标函数方向：xOptProblem 的约定是最小化。
TEST_F(OneBasedWireTest, ObjectiveFunctionTypeIsMin) {
    cm::CapeMINLPObjFunType t;
    minlp_->GetMINLPObjectiveFunctionType(t);
    EXPECT_EQ(t, cm::MIN);
}

// 规范（Methods&Tools §9.2.2）要求每个 PMC object 都提供标识，CORBA 侧靠 IDL
// 继承实现。这里不只调方法，而是从 ICapeMINLP 引用 _narrow 到
// ICapeIdentification——那才是 PME 实际会走的路径，也是 _is_a 真正被考的地方。
TEST_F(OneBasedWireTest, NarrowsToICapeIdentificationAsAPmeWould) {
    ::CAPEOPEN100::Common::Identification::ICapeIdentification_var ident =
        ::CAPEOPEN100::Common::Identification::ICapeIdentification::_narrow(minlp_.in());
    ASSERT_FALSE(CORBA::is_nil(ident.in())) << "PME 无法把 MINLP 引用窄化到标识接口";

    CORBA::String_var name = ident->GetComponentName();
    EXPECT_STREQ(name.in(), "xOpt MINLP");  // 与 COM 侧 CoMINLP 同一对字符串

    ident->SetComponentName("renamed");
    CORBA::String_var again = ident->GetComponentName();
    EXPECT_STREQ(again.in(), "renamed");
}

// 没实现的方法要明确报「未实现」，而不是返回一个看着像真答案的空值。
TEST_F(OneBasedWireTest, UnimplementedMethodsSaySoRatherThanFakeAnAnswer) {
    ct::CapeArrayBoolean_var isint;
    EXPECT_THROW(minlp_->GetMINLPVariableTypes(wireIds({1, 2}), isint.out()),
                 CORBA::NO_IMPLEMENT);

    ct::CapeArrayDouble_var hv;
    EXPECT_THROW(minlp_->GetMINLPHessianValues(hv.out()), cm::ECapeHessianInfoNotAvailable);
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
