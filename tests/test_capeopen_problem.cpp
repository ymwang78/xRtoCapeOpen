// ***************************************************************
//  test_capeopen_problem   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  capeopen_core 单元测试：经 C 风格 vtable（xOptProblemT）驱动，
//  对拍 Mock 后端的解析值。Mock 问题：
//      min x0^2 + x1^2  s.t. x0 + x1 - 3 = 0, -10<=xi<=10
//  详见 docs/capeopen_problem_design.md §9。
// ***************************************************************
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "CapeBackendFactory.h"
#include "CapeMINLPModelMock.h"
#include "CapeMINLPProblemCore.h"
#include "xOpt/xOptModel.h"
#include "xOpt/xOptProblem.h"

namespace {

// 经 vtable 驱动，镜像生产用法（core 堆分配，destroyProblem 释放）。
class CapeOpenProblemTest : public ::testing::Test {
  protected:
    xOptProblemT pt_{sizeof(xOptProblemT)};

    void SetUp() override {
        auto* core = new CapeMINLPProblemCore(std::make_unique<CapeMINLPModelMock>(""));
        ASSERT_GE(core->initialize(), 0);
        core->fillVtable(&pt_);
        ASSERT_NE(pt_.handle, nullptr);
    }

    void TearDown() override {
        if (pt_.handle) pt_.destroyProblem(pt_.handle);
    }
};

TEST_F(CapeOpenProblemTest, Size_MatchesMock) {
    EXPECT_EQ(pt_.numVariables(pt_.handle), 2);
    EXPECT_EQ(pt_.numConstraints(pt_.handle), 1);
}

TEST_F(CapeOpenProblemTest, Names) {
    const char* vnames[2] = {nullptr, nullptr};
    EXPECT_EQ(pt_.getVariableNames(pt_.handle, vnames, 2), 2);
    EXPECT_STREQ(vnames[0], "x0");
    EXPECT_STREQ(vnames[1], "x1");

    const char* cnames[1] = {nullptr};
    EXPECT_EQ(pt_.getConstraintNames(pt_.handle, cnames, 1), 1);
    EXPECT_STREQ(cnames[0], "c0");
}

TEST_F(CapeOpenProblemTest, Bounds_And_InitialX) {
    double xlow[2] = {0, 0}, xupp[2] = {0, 0};
    EXPECT_EQ(pt_.getVariableBounds(pt_.handle, xlow, xupp, 2), 2);
    EXPECT_DOUBLE_EQ(xlow[0], -10.0);
    EXPECT_DOUBLE_EQ(xupp[1], 10.0);

    double clow[1] = {9}, cupp[1] = {9};
    EXPECT_EQ(pt_.getConstraintBounds(pt_.handle, clow, cupp, 1), 1);
    EXPECT_DOUBLE_EQ(clow[0], 0.0);  // 等式约束
    EXPECT_DOUBLE_EQ(cupp[0], 0.0);

    double x0[2] = {9, 9};
    EXPECT_EQ(pt_.getInitialX(pt_.handle, x0, 2), 2);
    EXPECT_DOUBLE_EQ(x0[0], 0.0);
    EXPECT_DOUBLE_EQ(x0[1], 0.0);
}

TEST_F(CapeOpenProblemTest, Options) {
    double options[xOptProblem::OPTIONS_LIMIT] = {0};
    EXPECT_EQ(pt_.getOptions(pt_.handle, options, xOptProblem::OPTIONS_LIMIT), 0);
    EXPECT_EQ(options[xOptProblem::OPTIONS_MAGIC], 'X');
    EXPECT_EQ(options[xOptProblem::HAS_DERIVATIVE], 1);
    EXPECT_EQ(options[xOptProblem::IS_SIMULATION], 0);
}

TEST_F(CapeOpenProblemTest, JacobianStructure_TwoPassQuery_ZeroBased) {
    int nnz = -1;
    // 第一段：传 null 查长度
    EXPECT_GE(pt_.getConstraintJacobianStructure(pt_.handle, nullptr, nullptr, &nnz), 0);
    EXPECT_EQ(nnz, 2);

    // 第二段：填值
    std::vector<int> rows(nnz, -1), cols(nnz, -1);
    EXPECT_EQ(pt_.getConstraintJacobianStructure(pt_.handle, rows.data(), cols.data(), &nnz), 2);
    EXPECT_EQ(rows[0], 0);
    EXPECT_EQ(rows[1], 0);
    EXPECT_EQ(cols[0], 0);  // 0-based
    EXPECT_EQ(cols[1], 1);
}

TEST_F(CapeOpenProblemTest, ObjectiveGradientStructure_TwoPassQuery) {
    int sz = -1;
    EXPECT_GE(pt_.getObjectiveGradientStructure(pt_.handle, nullptr, &sz), 0);
    EXPECT_EQ(sz, 2);

    std::vector<int> cols(sz, -1);
    EXPECT_EQ(pt_.getObjectiveGradientStructure(pt_.handle, cols.data(), &sz), 2);
    EXPECT_EQ(cols[0], 0);
    EXPECT_EQ(cols[1], 1);
}

TEST_F(CapeOpenProblemTest, LinearConstraints_None) {
    int lsize = -1;
    EXPECT_EQ(pt_.getLinearConstraints(pt_.handle, nullptr, nullptr, nullptr, &lsize), 0);
    EXPECT_EQ(lsize, 0);
}

TEST_F(CapeOpenProblemTest, Evaluate_AgainstAnalytic) {
    const double x[2] = {3.0, 4.0};
    ASSERT_EQ(pt_.setX(pt_.handle, x, 2), 2);

    double obj = 0;
    EXPECT_EQ(pt_.evaluateObjective(pt_.handle, &obj), 0);
    EXPECT_DOUBLE_EQ(obj, 25.0);  // 9 + 16

    double cons[1] = {0};
    EXPECT_EQ(pt_.evaluateConstraints(pt_.handle, cons, 1), 1);
    EXPECT_DOUBLE_EQ(cons[0], 4.0);  // 3 + 4 - 3

    double grad[2] = {0, 0};
    EXPECT_EQ(pt_.evaluateObjectiveGradient(pt_.handle, grad, 2), 2);
    EXPECT_DOUBLE_EQ(grad[0], 6.0);  // 2*3
    EXPECT_DOUBLE_EQ(grad[1], 8.0);  // 2*4

    double jac[2] = {0, 0};
    EXPECT_EQ(pt_.evaluateConstraintsJacobianValues(pt_.handle, jac, 2), 2);
    EXPECT_DOUBLE_EQ(jac[0], 1.0);
    EXPECT_DOUBLE_EQ(jac[1], 1.0);
}

TEST_F(CapeOpenProblemTest, SetX_ChangesObjective) {
    double obj_a = 0, obj_b = 0;
    const double xa[2] = {1.0, 1.0};
    const double xb[2] = {2.0, 0.0};
    ASSERT_EQ(pt_.setX(pt_.handle, xa, 2), 2);
    ASSERT_EQ(pt_.evaluateObjective(pt_.handle, &obj_a), 0);
    ASSERT_EQ(pt_.setX(pt_.handle, xb, 2), 2);
    ASSERT_EQ(pt_.evaluateObjective(pt_.handle, &obj_b), 0);
    EXPECT_DOUBLE_EQ(obj_a, 2.0);  // 1 + 1
    EXPECT_DOUBLE_EQ(obj_b, 4.0);  // 4 + 0
}

// —— 工厂 ——

TEST(CapeBackendFactoryTest, Parse) {
    EXPECT_EQ(CapeBackendFactory::parse("mock:q").first, "mock");
    EXPECT_EQ(CapeBackendFactory::parse("corba:IOR:01").first, "corba");
    EXPECT_EQ(CapeBackendFactory::parse("com:Some.ProgId").first, "com");
    EXPECT_EQ(CapeBackendFactory::parse("C:/path/unit.DLL").first, "com");  // .dll 后缀 -> com
    EXPECT_EQ(CapeBackendFactory::parse("garbage").first, "");              // 盘符不误伤
    EXPECT_EQ(CapeBackendFactory::parse("C:/path/unit.DLL").second, "C:/path/unit.DLL");
}

TEST(CapeBackendFactoryTest, CreateMock) {
    std::string err;
    auto m = CapeBackendFactory::instance().create("mock:quadratic", err);
    ASSERT_NE(m, nullptr) << err;
    ASSERT_EQ(m->connect(), 0);
    CapeMINLPSize size;
    ASSERT_EQ(m->getSize(size), 0);
    EXPECT_EQ(size.num_variables, 2);
    EXPECT_EQ(size.num_constraints, 1);
}

TEST(CapeBackendFactoryTest, UnregisteredScheme_ReturnsNullNotThrow) {
    std::string err;
    auto m = CapeBackendFactory::instance().create("corba:IOR:01", err);  // 无 CORBA DLL 注册
    EXPECT_EQ(m, nullptr);
    EXPECT_FALSE(err.empty());
}

}  // namespace

// ===========================================================================
//  刷新的提交/失效语义
// ===========================================================================

// 一个规模可变、且能在指定调用上注入故障的后端桩。
//
// 只实现 CapeMINLPProblemCore 在 initialize()/refresh() 里真正会用到的那些；
// 其余按接口给出最小可用实现。
class ResizableStub : public ICapeMINLPModel {
  public:
    int n_var = 3;
    int n_con = 2;
    std::string tag = "old";           // 变量名后缀，用来分辨新旧
    bool fail_variable_names = false;  // 故障注入点

    int connect() override { return 0; }
    void disconnect() override {}
    int getSize(CapeMINLPSize& out) override {
        out = CapeMINLPSize{};
        out.num_variables = n_var;
        out.num_constraints = n_con;
        return 0;
    }
    int getStructure(const std::string&, std::vector<int>& r, std::vector<int>& c,
                     std::vector<int>& o) override {
        r.clear();
        c.clear();
        o.clear();
        return 0;
    }
    int getVariableNames(const std::vector<int>& vids,
                         std::vector<std::string>& out) override {
        if (fail_variable_names) return -1;  // 相当于一次 COMM_FAILURE
        out.clear();
        for (size_t i = 0; i < vids.size(); ++i) out.push_back(tag + "_x" + std::to_string(i));
        return 0;
    }
    int getVariableBounds(const std::vector<int>& vids, std::vector<double>& lo,
                          std::vector<double>& hi) override {
        lo.assign(vids.size(), 0.0);
        hi.assign(vids.size(), 1.0);
        return 0;
    }
    int getVariableValues(const std::vector<int>& vids, std::vector<double>& v) override {
        v.assign(vids.size(), 0.0);
        return 0;
    }
    int setVariableValues(const std::vector<int>&, const std::vector<double>&) override {
        return 0;
    }
    int getConstraintNames(const std::vector<int>& cids,
                           std::vector<std::string>& out) override {
        out.clear();
        for (size_t i = 0; i < cids.size(); ++i) out.push_back(tag + "_c" + std::to_string(i));
        return 0;
    }
    int getConstraintBounds(const std::vector<int>& cids, std::vector<double>& lo,
                            std::vector<double>& hi) override {
        lo.assign(cids.size(), 0.0);
        hi.assign(cids.size(), 0.0);
        return 0;
    }
    int getNonlinearConstraintValues(const std::vector<int>& cids,
                                     std::vector<double>& v) override {
        v.assign(cids.size(), 0.0);
        return 0;
    }
    int getConstraintDerivativeValues(const std::string&, const std::vector<int>&,
                                      std::vector<double>& v) override {
        v.clear();
        return 0;
    }
    int getObjectiveValue(double& v) override {
        v = 0.0;
        return 0;
    }
    int getObjectiveDerivativeValues(const std::string&, std::vector<double>& v) override {
        v.clear();
        return 0;
    }
    std::string lastError() const override { return "stub"; }
};

// 刷新在中途失败时，绝不能留下"规模是新的、名称还是旧的"这种混合缓存。
//
// 这是真实踩过的形状：readStructure 过去直接往成员上写——先 size_，再逐项
// 覆盖。getVariableNames 半路失败的话，numVariables() 报新规模 4，而
// var_names_ 还是旧的 3 项，getVariableNames(names, 4) 按新规模遍历旧数组，
// 越界读。
//
// 现在的语义是提交/失效二选一：读全了才整体提交；没读全就一个字节都不改，
// 并把对象标为 stale——缓存里那份是**旧问题**的结构，自洽但已经不是远端那个
// 问题了，继续供出去等于安静地解错题。
TEST(CapeOpenProblemRefreshTest, AFailedRefreshNeitherHalfUpdatesNorKeepsServing) {
    auto owned = std::make_unique<ResizableStub>();
    ResizableStub* stub = owned.get();
    CapeMINLPProblemCore core(std::move(owned));
    ASSERT_EQ(core.initialize(), 0);
    ASSERT_EQ(core.numVariables(), 3);

    const char* names[8] = {nullptr};
    ASSERT_EQ(core.getVariableNames(names, 3), 3);
    EXPECT_STREQ(names[0], "old_x0");

    // 远端换了一套：规模变大、名字换新，但取名字这一步失败
    stub->n_var = 4;
    stub->tag = "new";
    stub->fail_variable_names = true;

    EXPECT_LT(core.refresh(), 0);
    EXPECT_TRUE(core.isStale());

    // 关键断言：不能报着新规模去索引旧数组。
    // 现在的行为是整体拒绝服务——比返回一个"4 个变量但只有 3 个名字"安全得多。
    EXPECT_LT(core.numVariables(), 0) << "陈旧的缓存不该继续对外服务";
    EXPECT_LT(core.numConstraints(), 0);
    EXPECT_LT(core.getVariableNames(names, 4), 0)
        << "按新规模读旧名称数组就是越界，必须直接失败";

    // 故障消失后重试：必须能完整恢复，且拿到的是**新**的那套
    stub->fail_variable_names = false;
    ASSERT_EQ(core.refresh(), 0);
    EXPECT_FALSE(core.isStale());
    EXPECT_EQ(core.numVariables(), 4);
    ASSERT_EQ(core.getVariableNames(names, 4), 4);
    EXPECT_STREQ(names[0], "new_x0");
    EXPECT_STREQ(names[3], "new_x3");
}

// 后端只答了一部分（名称数组比规模短）同样要在提交前挡住，而不是等到访问器
// 越界时才崩。桩这里通过"规模说 4、名称给 3"来制造。
TEST(CapeOpenProblemRefreshTest, ARefreshWithShortArraysIsRejectedBeforeCommit) {
    auto owned = std::make_unique<ResizableStub>();
    ResizableStub* stub = owned.get();
    CapeMINLPProblemCore core(std::move(owned));
    ASSERT_EQ(core.initialize(), 0);

    class ShortNames : public ResizableStub {
      public:
        int getVariableNames(const std::vector<int>&, std::vector<std::string>& out) override {
            out.assign(2, "short");  // 比 num_variables 短
            return 0;
        }
    };
    (void)stub;

    auto owned2 = std::make_unique<ShortNames>();
    owned2->n_var = 4;
    CapeMINLPProblemCore core2(std::move(owned2));
    EXPECT_LT(core2.initialize(), 0) << "长度对不上必须在提交前被挡住";
}

// 外部可以把问题对象标记为失效，且这个标记必须真的挡住读取。
//
// 用在"远端可能已经被改掉，但我们还没能重新读回来"的时刻：CapeUnitCorba 的
// setComponents 是"远端 SetComponents 成功 -> 回读拓扑"两步，第二步抛
// COMM_FAILURE 时远端**已经是新问题**了，而调用方连 refresh 都没走到。这时
// 缓存里那份是不是最新的无从判断，只能按失效处理 —— 否则故障消失后拿同一份
// slate 重试，会因为"组分没变"直接返回成功，把旧结构一路带进求解器。
//
// 完整的那条路径（真实 CORBA + 在 GetComponents 上注入一次可解除的故障）本
// 套件测不了：capeopen_core 编译 xOptModelCapeOpen.cpp 时不带
// CAPEOPEN_WITH_CORBA，那段上下文逻辑根本没进这个二进制。这里钉住的是它依赖
// 的那个原语。
TEST(CapeOpenProblemRefreshTest, MarkStaleBlocksReadsUntilARefreshSucceeds) {
    auto owned = std::make_unique<ResizableStub>();
    ResizableStub* stub = owned.get();
    CapeMINLPProblemCore core(std::move(owned));
    ASSERT_EQ(core.initialize(), 0);
    ASSERT_EQ(core.numVariables(), 3);
    EXPECT_FALSE(core.isStale());

    // 远端换了一套，但我们是从别的途径知道的（拓扑回读失败），没能刷新
    stub->n_var = 4;
    stub->tag = "new";
    core.markStale();

    EXPECT_TRUE(core.isStale());
    EXPECT_LT(core.numVariables(), 0) << "标记失效后不能继续按旧结构服务";
    const char* names[8] = {nullptr};
    EXPECT_LT(core.getVariableNames(names, 3), 0);

    // 恢复只能靠一次成功的刷新，而且拿到的必须是新的那套
    ASSERT_EQ(core.refresh(), 0);
    EXPECT_FALSE(core.isStale());
    EXPECT_EQ(core.numVariables(), 4);
    ASSERT_EQ(core.getVariableNames(names, 4), 4);
    EXPECT_STREQ(names[0], "new_x0");
}

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
