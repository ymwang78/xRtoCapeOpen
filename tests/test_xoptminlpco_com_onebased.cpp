// ***************************************************************
//  test_xoptminlpco_com_onebased   version:  1.0   -  date:  2026/08/07
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  COM 侧索引基的**非回环**测试（issue #2）。
//
//  为什么必须非回环：其余 COM 用例都是「我们的 CoMINLP → 我们的
//  CapeMINLPModelCom」，两端用同一套约定，一起偏也照样全绿。CORBA 侧的 0-based
//  缺陷就是这么藏了两个月的（design §6.2 缺口 2）——COM 侧现在补的正是同一个洞，
//  所以这条测试的写法要和 tests/test_xoptminlpco_onebased.cpp 一致：
//  不接消费端，手工构造 1-based 的 VARIANT 入参、手工写死 1-based 期望值。
//
//  背靠 MockXOptProblem：nv=2, nc=1, obj=x0^2+x1^2, cons=x0+x1-3。
//  自带 main。
// ***************************************************************
#include "MockXOptProblem.h"  // 首个包含：XOPTINTERFACE_EXPORTS + xOptProblem

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CoMINLP.h"
#include "XOptMINLPAdapter.h"

namespace {

// 手工构造线上数组，刻意不借用 CapeVariantMarshal 的任何 helper：
// 本用例校验的就是那套 helper 的用法对不对。
VARIANT wireIds(std::initializer_list<LONG> ids) {
    VARIANT v;
    VariantInit(&v);
    SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(ids.size()));
    LONG i = 0;
    for (LONG id : ids) {
        SafeArrayPutElement(sa, &i, const_cast<LONG*>(&id));
        ++i;
    }
    v.vt = VT_ARRAY | VT_I4;
    v.parray = sa;
    return v;
}

VARIANT wireDoubles(std::initializer_list<double> xs) {
    VARIANT v;
    VariantInit(&v);
    SAFEARRAY* sa = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(xs.size()));
    LONG i = 0;
    for (double x : xs) {
        SafeArrayPutElement(sa, &i, &x);
        ++i;
    }
    v.vt = VT_ARRAY | VT_R8;
    v.parray = sa;
    return v;
}

std::vector<LONG> readLongs(const VARIANT& v) {
    std::vector<LONG> out;
    if ((v.vt & VT_ARRAY) == 0 || v.parray == nullptr) return out;
    LONG lo = 0, hi = -1;
    SafeArrayGetLBound(v.parray, 1, &lo);
    SafeArrayGetUBound(v.parray, 1, &hi);
    for (LONG i = lo; i <= hi; ++i) {
        LONG x = 0;
        SafeArrayGetElement(v.parray, &i, &x);
        out.push_back(x);
    }
    return out;
}

std::vector<double> readDoubles(const VARIANT& v) {
    std::vector<double> out;
    if ((v.vt & VT_ARRAY) == 0 || v.parray == nullptr) return out;
    LONG lo = 0, hi = -1;
    SafeArrayGetLBound(v.parray, 1, &lo);
    SafeArrayGetUBound(v.parray, 1, &hi);
    for (LONG i = lo; i <= hi; ++i) {
        double x = 0;
        SafeArrayGetElement(v.parray, &i, &x);
        out.push_back(x);
    }
    return out;
}

std::string nthBstr(const VARIANT& v, LONG index) {
    if ((v.vt & VT_ARRAY) == 0 || v.parray == nullptr) return {};
    LONG lo = 0;
    SafeArrayGetLBound(v.parray, 1, &lo);
    lo += index;
    BSTR b = nullptr;
    if (FAILED(SafeArrayGetElement(v.parray, &lo, &b)) || b == nullptr) return {};
    // 用 -1 求长度时返回值**含**终止符。原先按 n-1 建 string 再让 API 写 n 字节，
    // 那一个字节落在 data()[size()]——标准规定该槽位不得修改。实测无害，但没必要
    // 押在这个细节上：这里改用独立缓冲，再按不含终止符的长度构造。
    const int n = WideCharToMultiByte(CP_UTF8, 0, b, -1, nullptr, 0, nullptr, nullptr);
    std::string s;
    if (n > 1) {
        std::vector<char> buf(static_cast<size_t>(n));
        const int written =
            WideCharToMultiByte(CP_UTF8, 0, b, -1, buf.data(), n, nullptr, nullptr);
        if (written > 1) s.assign(buf.data(), static_cast<size_t>(written - 1));
    }
    SysFreeString(b);
    return s;
}

class ComOneBasedTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_EQ(adapter_.connect(), 0) << adapter_.lastError();
        co_ = new CoMINLP(&adapter_);
    }
    void TearDown() override {
        if (co_) co_->Release();
    }
    MockXOptProblem mock_;
    XOptMINLPAdapter adapter_{&mock_};
    CoMINLP* co_ = nullptr;
};

// 规范：「The variable indices vids must be in the range 1,...,nv」。
TEST_F(ComOneBasedTest, VariableIdsAreOneBasedOnTheWire) {
    VARIANT ids = wireIds({1, 2});
    VARIANT names;
    VariantInit(&names);
    ASSERT_EQ(co_->GetMINLPVariableNames(ids, &names), S_OK);
    ASSERT_EQ((names.vt & VT_ARRAY), VT_ARRAY);
    EXPECT_EQ(nthBstr(names, 0), "x0");
    EXPECT_EQ(nthBstr(names, 1), "x1");
    VariantClear(&names);
    VariantClear(&ids);

    // 单独取第 2 个变量：线上的 2 == 内部的 x1。
    VARIANT one = wireIds({2});
    VARIANT second;
    VariantInit(&second);
    ASSERT_EQ(co_->GetMINLPVariableNames(one, &second), S_OK);
    EXPECT_EQ(nthBstr(second, 0), "x1") << "线上 id=2 应对应 x1，拿到别的说明没换基";
    VariantClear(&second);
    VariantClear(&one);
}

TEST_F(ComOneBasedTest, VariableBoundsFollowOneBasedIds) {
    VARIANT ids = wireIds({1, 2});
    VARIANT lb, ub;
    VariantInit(&lb);
    VariantInit(&ub);
    ASSERT_EQ(co_->GetMINLPVariableBounds(ids, &lb, &ub), S_OK);
    const auto lo = readDoubles(lb);
    const auto hi = readDoubles(ub);
    ASSERT_EQ(lo.size(), 2u);
    EXPECT_DOUBLE_EQ(lo[0], -10.0);
    EXPECT_DOUBLE_EQ(hi[1], 10.0);
    VariantClear(&lb);
    VariantClear(&ub);
    VariantClear(&ids);
}

// 结构索引出网必须是 1-based：mock 的 Jacobian 内部是 rows{0,0} cols{0,1}。
TEST_F(ComOneBasedTest, StructureIndicesAreOneBasedOnTheWire) {
    BSTR t = SysAllocString(L"Jacobian");
    VARIANT r, c, o;
    VariantInit(&r);
    VariantInit(&c);
    VariantInit(&o);
    ASSERT_EQ(co_->GetMINLPStructure(t, &r, &c, &o), S_OK);
    EXPECT_EQ(readLongs(r), (std::vector<LONG>{1, 1}));
    EXPECT_EQ(readLongs(c), (std::vector<LONG>{1, 2}));
    VariantClear(&r);
    VariantClear(&c);
    VariantClear(&o);
    SysFreeString(t);
}

// setX 走 1-based，随后的求值必须落在正确的变量上。
TEST_F(ComOneBasedTest, SetValuesByOneBasedIdsHitsTheRightVariables) {
    VARIANT ids = wireIds({1, 2});
    VARIANT vals = wireDoubles({3.0, 4.0});
    ASSERT_EQ(co_->SetMINLPVariableValues(ids, vals), S_OK);
    VariantClear(&ids);
    VariantClear(&vals);

    double obj = 0;
    ASSERT_EQ(co_->GetMINLPNonlinearObjectiveFunctionValue(&obj), S_OK);
    EXPECT_DOUBLE_EQ(obj, 25.0);  // 3^2 + 4^2

    VARIANT cids = wireIds({1});
    VARIANT cons;
    VariantInit(&cons);
    ASSERT_EQ(co_->GetMINLPNonlinearConstraintValues(cids, &cons), S_OK);
    const auto cv = readDoubles(cons);
    ASSERT_EQ(cv.size(), 1u);
    EXPECT_DOUBLE_EQ(cv[0], 4.0);  // 3 + 4 - 3
    VariantClear(&cons);
    VariantClear(&cids);
}

// 关键回归：0 在 1-based 里是非法的。换基前会把它当内部下标 0 悄悄返回 x0。
TEST_F(ComOneBasedTest, ZeroIsRejectedBecauseNumberingStartsAtOne) {
    VARIANT ids = wireIds({0});
    VARIANT names;
    VariantInit(&names);
    EXPECT_EQ(co_->GetMINLPVariableNames(ids, &names), E_INVALIDARG);
    VariantClear(&names);
    VariantClear(&ids);
}

// 关键回归：nv+1 越界。换基前 pick() 对越界 id 静默返回 T{}——
// 于是最后一个变量会变成 0 而不报错。
TEST_F(ComOneBasedTest, PastTheEndIsRejectedNotSilentlyZeroed) {
    VARIANT bad = wireIds({3});
    VARIANT names;
    VariantInit(&names);
    EXPECT_EQ(co_->GetMINLPVariableNames(bad, &names), E_INVALIDARG);
    VariantClear(&names);
    VariantClear(&bad);

    VARIANT badc = wireIds({2});  // nc == 1，故 2 越界
    VARIANT cons;
    VariantInit(&cons);
    EXPECT_EQ(co_->GetMINLPNonlinearConstraintValues(badc, &cons), E_INVALIDARG);
    VariantClear(&cons);
    VariantClear(&badc);
}

// 规范里空数组表示「全部」，不应被当成越界。
TEST_F(ComOneBasedTest, EmptyIdListMeansAll) {
    VARIANT empty = wireIds({});
    VARIANT names;
    VariantInit(&names);
    ASSERT_EQ(co_->GetMINLPVariableNames(empty, &names), S_OK);
    ASSERT_EQ((names.vt & VT_ARRAY), VT_ARRAY);
    EXPECT_EQ(nthBstr(names, 0), "x0") << "空数组应表示全部两个变量";
    EXPECT_EQ(nthBstr(names, 1), "x1");
    VariantClear(&names);
    VariantClear(&empty);
}

// 与 Jacobian 同一条出网路径，但走的是 objindex 那一路，一并钉住。
TEST_F(ComOneBasedTest, ObjectiveGradientStructureIsOneBased) {
    BSTR t = SysAllocString(L"ObjectiveGradient");
    VARIANT r, c, o;
    VariantInit(&r);
    VariantInit(&c);
    VariantInit(&o);
    ASSERT_EQ(co_->GetMINLPStructure(t, &r, &c, &o), S_OK);
    EXPECT_EQ(readLongs(o), (std::vector<LONG>{1, 2}));
    VariantClear(&r);
    VariantClear(&c);
    VariantClear(&o);
    SysFreeString(t);
}

// 「全部」的另一种写法：表示「参数没给」的 VARIANT。
// VB/脚本宿主省略参数给 VT_EMPTY，IDispatch 上则是带 DISP_E_PARAMNOTFOUND 的
// VT_ERROR。消费端 softReadIndices 早就把非数组当空处理，生产端须一致。
TEST_F(ComOneBasedTest, OmittedArgumentMeansAll) {
    for (int which = 0; which < 2; ++which) {
        VARIANT omitted;
        VariantInit(&omitted);  // VT_EMPTY
        if (which == 1) {
            omitted.vt = VT_ERROR;
            omitted.scode = DISP_E_PARAMNOTFOUND;
        }
        VARIANT names;
        VariantInit(&names);
        ASSERT_EQ(co_->GetMINLPVariableNames(omitted, &names), S_OK)
            << "省略参数（形式 " << which << "）应被当作「全部」，而不是非法入参";
        EXPECT_EQ(nthBstr(names, 0), "x0");
        EXPECT_EQ(nthBstr(names, 1), "x1");
        VariantClear(&names);
    }
}

// 「未指定」只认那几种，不是笼统的「凡非数组即全部」。
// 有人写 vids = 1 想取第一个变量时，必须报错——若当成「全部」，调用方要一个变量
// 却拿到全部且毫无征兆，正是本项目一直在清的那类静默错误。
TEST_F(ComOneBasedTest, ScalarIsNotMistakenForAll) {
    VARIANT scalar;
    VariantInit(&scalar);
    scalar.vt = VT_I4;
    scalar.lVal = 1;
    VARIANT names;
    VariantInit(&names);
    EXPECT_EQ(co_->GetMINLPVariableNames(scalar, &names), E_INVALIDARG)
        << "标量被当成了「全部」——调用方要一个变量会静默拿到全部";
    VariantClear(&names);
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
