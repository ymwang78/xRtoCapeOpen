// ***************************************************************
//  test_xoptminlpco_solver   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  求解器通路的回环（collocated，同进程）：
//
//    MockXOptProblem -> XOptMINLPAdapter -> MINLPServant(ICapeMINLP)   问题在"客户端"
//      -> SolverManagerServant::CreateMINLPSystem(problem_ref)          求解器在"服务端"
//        -> MINLPSystemServant: CapeMINLPModelCorba(问题引用) -> CapeMINLPProblemCore
//           -> XOptProblemFromVtable -> test_penalty_solver.dll::createSolver
//          -> Solve() -> 罚函数梯度下降在 mock 问题上跑到解析最优 (1.5, 1.5)
//
//  两端都在本进程，TAO 短路掉网络；它验证的是 servant 逻辑、参数集合与
//  Release 语义，不证明 IIOP 上的嵌套上行调用——那是
//  test_xoptminlpco_solver_ipc.cpp 的事。自带 main。
// ***************************************************************
#include "MockXOptProblem.h"  // 首个包含：XOPTINTERFACE_EXPORTS + xOptProblem

#include <gtest/gtest.h>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "CAPEOPEN100_UnitC.h"
#include "MINLPServant.h"
#include "SolverServant.h"
#include "XOPTCO_ExtC.h"
#include "XOptMINLPAdapter.h"
#include "backend/corba/CapeCorbaMarshal.h"

namespace {

namespace ct = ::CAPEOPEN100::Common::Types;
namespace ce = ::CAPEOPEN100::Common::Error;
namespace mi = ::CAPEOPEN100::Business::Numeric::Minlp;
namespace cp = ::CAPEOPEN100::Common::Parameter;
namespace cc = ::CAPEOPEN100::Common::Collection;

void testLog(ZLOG_LEVEL level, const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    std::printf("  [solver log %d] %s\n", static_cast<int>(level), buf);
}

ct::CapeArrayString names1(const char* a) {
    ct::CapeArrayString s;
    s.length(1);
    s[0] = CORBA::string_dup(a);
    return s;
}

class SolverLoopbackTest : public ::testing::Test {
  protected:
    void SetUp() override {
        int argc = 0;
        orb_ = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
        CORBA::Object_var p = orb_->resolve_initial_references("RootPOA");
        poa_ = PortableServer::POA::_narrow(p.in());
        poa_->the_POAManager()->activate();

        // "客户端"：把 mock 问题发布成 ICapeMINLP
        adapter_.reset(new XOptMINLPAdapter(&mock_));
        ASSERT_EQ(adapter_->connect(), 0) << adapter_->lastError();
        MINLPServant* ps = new MINLPServant(adapter_.get());
        PortableServer::ServantBase_var ps_owner(ps);
        problem_oid_ = poa_->activate_object(ps);
        problem_ref_ = poa_->id_to_reference(problem_oid_.in());

        // "服务端"：加载真实的示例求解器 DLL，发布管理器
        library_.reset(new XOptSolverLibrary(TEST_PENALTY_SOLVER_DLL));
        ASSERT_EQ(library_->load(), 0) << library_->lastError();
        SolverManagerServant* ms = new SolverManagerServant(library_.get(), poa_.in(), &testLog);
        PortableServer::ServantBase_var ms_owner(ms);
        manager_oid_ = poa_->activate_object(ms);
        CORBA::Object_var mref = poa_->id_to_reference(manager_oid_.in());
        manager_ = mi::ICapeMINLPSolverManager::_narrow(mref.in());
        ASSERT_FALSE(CORBA::is_nil(manager_.in()));
    }

    void TearDown() override {
        try {
            poa_->deactivate_object(manager_oid_.in());
            poa_->deactivate_object(problem_oid_.in());
        } catch (const CORBA::Exception&) {
        }
        // ORB 是进程级共享的（多个用例复用同一个），不 destroy。
    }

    MockXOptProblem mock_;
    std::unique_ptr<XOptMINLPAdapter> adapter_;
    std::unique_ptr<XOptSolverLibrary> library_;
    CORBA::ORB_var orb_;
    PortableServer::POA_var poa_;
    PortableServer::ObjectId_var problem_oid_;
    PortableServer::ObjectId_var manager_oid_;
    CORBA::Object_var problem_ref_;
    mi::ICapeMINLPSolverManager_var manager_;
};

TEST_F(SolverLoopbackTest, CreateSystem_SolveReachesTheAnalyticOptimum) {
    CORBA::Object_var sys_obj;
    ASSERT_NO_THROW(manager_->CreateMINLPSystem(problem_ref_.in(), sys_obj.out()));
    mi::ICapeMINLPSystem_var sys = mi::ICapeMINLPSystem::_narrow(sys_obj.in());
    ASSERT_FALSE(CORBA::is_nil(sys.in())) << "_narrow to ICapeMINLPSystem must succeed";
    ::XOPTCO::IXOptMINLPSystemExtension_var ext =
        ::XOPTCO::IXOptMINLPSystemExtension::_narrow(sys_obj.in());
    ASSERT_FALSE(CORBA::is_nil(ext.in())) << "_narrow to the XOPTCO extension must succeed";

    // 标识经继承链可达
    CORBA::String_var cname = sys->GetComponentName();
    EXPECT_STREQ(cname.in(), "xOpt MINLP System");

    // 选项通道：整数与实数都到得了求解器，且名字不认识时 accepted 为 false
    {
        ct::CapeArrayString names;
        names.length(2);
        names[0] = CORBA::string_dup("max_iter");
        names[1] = CORBA::string_dup("no_such_option");
        std::vector<int> v{5000, 1};
        ct::CapeArrayBoolean_var accepted;
        ext->SetIntOptions(names, cape_corba::toLongSeq(v), accepted.out());
        ASSERT_EQ(accepted->length(), 2u);
        EXPECT_TRUE(accepted[0u]);
        EXPECT_FALSE(accepted[1u]);

        ct::CapeArrayLong_var back;
        ext->GetIntOptions(names1("max_iter"), back.out());
        ASSERT_EQ(back->length(), 1u);
        EXPECT_EQ(back[0u], 5000);
    }
    {
        std::vector<double> v{1e-8};
        ct::CapeArrayBoolean_var accepted;
        ext->SetDoubleOptions(names1("tol"), cape_corba::toDoubleSeq(v), accepted.out());
        ASSERT_EQ(accepted->length(), 1u);
        EXPECT_TRUE(accepted[0u]);
    }

    // 解之前没有结果
    EXPECT_EQ(ext->GetSolveResult(), xOptSolver::RESULT_UNKNOWN);
    EXPECT_THROW(ext->GetX(), ce::ECapeUnknown);

    ASSERT_NO_THROW(sys->Solve());
    // 罚函数法在这个问题上通常以"可行、梯度条件未达"收尾（RESULT_FEASIBLE），
    // 偶尔两条都达标（RESULT_OPTIMAL）；两者都是成功，负码才是失败。
    const int result = ext->GetSolveResult();
    EXPECT_TRUE(result == xOptSolver::RESULT_OPTIMAL || result == xOptSolver::RESULT_FEASIBLE)
        << "result = " << result;

    ct::CapeArrayDouble_var x = ext->GetX();
    ASSERT_EQ(x->length(), 2u);
    EXPECT_NEAR(x[0u], 1.5, 1e-3);
    EXPECT_NEAR(x[1u], 1.5, 1e-3);

    ct::CapeArrayDouble_var f = ext->GetF();
    ASSERT_EQ(f->length(), 2u);  // 目标 + 1 个约束
    EXPECT_NEAR(f[0u], 4.5, 1e-2);
    EXPECT_NEAR(f[1u], 0.0, 1e-3);

    // 解被写回了问题：Solve 之后经 SetMINLPVariableValues -> 我们的 servant ->
    // adapter -> mock.setX。这是 CAPE-OPEN 消费者取解的标准路径。
    double obj = 0;
    ASSERT_EQ(mock_.evaluateObjective(obj), 0);
    EXPECT_NEAR(obj, 4.5, 1e-2);

    // 罚函数求解器没有对偶信息：契约里 Xmul 返回负值，这里翻成 ECapeUnknown
    EXPECT_THROW(ext->GetXmul(), ce::ECapeUnknown);
    EXPECT_LT(ext->PauseSolve(), 0);

    // 可调参数列表原样透出
    ct::CapeArrayString_var tnames;
    ct::CapeArrayLong_var ttypes;
    ext->GetTunableParameters(tnames.out(), ttypes.out());
    ASSERT_EQ(tnames->length(), 4u);
    EXPECT_STREQ(tnames[0u].in(), "max_iter");
    EXPECT_EQ(ttypes[0u], xOptSolver::OPTION_INT);
    EXPECT_STREQ(tnames[1u].in(), "tol");
    EXPECT_EQ(ttypes[1u], xOptSolver::OPTION_REAL);

    ext->Release();
    // 注销之后这个引用不再指向任何东西：每个操作（包括再来一次 Release）都是
    // 系统异常 OBJECT_NOT_EXIST，而不是崩溃或悬空访问。
    EXPECT_THROW(ext->GetSolveResult(), CORBA::Exception);
    EXPECT_THROW(ext->Release(), CORBA::Exception);
}

// GetParameters 是标准的那一半：可调参数以 ICapeParameter 集合的形式发布，
// 类型化的 spec 经 IDL 多继承同时应答 ICapeParameterSpec 与 ICapeRealParameterSpec。
TEST_F(SolverLoopbackTest, GetParameters_PublishesTunablesAsCapeParameters) {
    CORBA::Object_var sys_obj;
    ASSERT_NO_THROW(manager_->CreateMINLPSystem(problem_ref_.in(), sys_obj.out()));
    mi::ICapeMINLPSystem_var sys = mi::ICapeMINLPSystem::_narrow(sys_obj.in());
    ::XOPTCO::IXOptMINLPSystemExtension_var ext =
        ::XOPTCO::IXOptMINLPSystemExtension::_narrow(sys_obj.in());
    ASSERT_FALSE(CORBA::is_nil(sys.in()));

    CORBA::Object_var coll_obj = sys->GetParameters();
    cc::ICapeCollection_var coll = cc::ICapeCollection::_narrow(coll_obj.in());
    ASSERT_FALSE(CORBA::is_nil(coll.in())) << "GetParameters must return an ICapeCollection";
    EXPECT_EQ(coll->Count(), 4);  // max_iter, tol, feas_tol, mu0

    // 按下标（1-based）取第一个：max_iter，整数
    {
        CORBA::Any id;
        id <<= static_cast<CORBA::Long>(1);
        CORBA::Object_var item = coll->Item(id);
        cp::ICapeParameter_var param = cp::ICapeParameter::_narrow(item.in());
        ASSERT_FALSE(CORBA::is_nil(param.in()));
        CORBA::String_var pname = param->GetComponentName();
        EXPECT_STREQ(pname.in(), "max_iter");
        EXPECT_EQ(param->GetMode(), cp::CAPE_INPUT);

        CORBA::Object_var spec_obj = param->Specification();
        cp::ICapeParameterSpec_var spec = cp::ICapeParameterSpec::_narrow(spec_obj.in());
        ASSERT_FALSE(CORBA::is_nil(spec.in())) << "spec must answer ICapeParameterSpec";
        EXPECT_EQ(spec->Type(), cp::CAPE_INT);
        cp::ICapeIntegerParameterSpec_var ispec =
            cp::ICapeIntegerParameterSpec::_narrow(spec_obj.in());
        ASSERT_FALSE(CORBA::is_nil(ispec.in())) << "spec must ALSO answer the typed spec";
        EXPECT_EQ(ispec->DefaultValue(), 2000);  // 求解器的出厂值

        CORBA::Any_var v = param->GetValue();
        CORBA::Long l = 0;
        ASSERT_TRUE(v.in() >>= l);
        EXPECT_EQ(l, 2000);

        CORBA::Any nv;
        nv <<= static_cast<CORBA::Long>(777);
        param->SetValue(nv);
        ct::CapeArrayLong_var back;
        ext->GetIntOptions(names1("max_iter"), back.out());
        EXPECT_EQ(back[0u], 777) << "SetValue must reach the solver's option channel";

        param->Reset();
        ext->GetIntOptions(names1("max_iter"), back.out());
        EXPECT_EQ(back[0u], 2000);
    }
    // 按名字取：tol，实数；整数值也接受（转成 double）
    {
        CORBA::Any id;
        id <<= "tol";
        CORBA::Object_var item = coll->Item(id);
        cp::ICapeParameter_var param = cp::ICapeParameter::_narrow(item.in());
        ASSERT_FALSE(CORBA::is_nil(param.in()));
        CORBA::Object_var spec_obj = param->Specification();
        cp::ICapeRealParameterSpec_var rspec = cp::ICapeRealParameterSpec::_narrow(spec_obj.in());
        ASSERT_FALSE(CORBA::is_nil(rspec.in()));
        EXPECT_DOUBLE_EQ(rspec->DefaultValue(), 1e-6);

        CORBA::Any nv;
        nv <<= static_cast<CORBA::Double>(2.5e-7);
        param->SetValue(nv);
        ct::CapeArrayDouble_var back;
        ext->GetDoubleOptions(names1("tol"), back.out());
        EXPECT_DOUBLE_EQ(back[0u], 2.5e-7);

        // 输出模式对求解器选项没有意义
        EXPECT_THROW(param->SetMode(cp::CAPE_OUTPUT), ce::ECapeInvalidArgument);
    }
    // 没有的名字与越界下标都是 ECapeInvalidArgument，不是空引用
    {
        CORBA::Any id;
        id <<= "nope";
        EXPECT_THROW(coll->Item(id), ce::ECapeInvalidArgument);
        CORBA::Any zero;
        zero <<= static_cast<CORBA::Long>(0);
        EXPECT_THROW(coll->Item(zero), ce::ECapeInvalidArgument);
    }

    ext->Release();
}

TEST_F(SolverLoopbackTest, CreateSystem_RejectsAReferenceThatIsNotAProblem) {
    CORBA::Object_var sys_obj;
    // 把管理器自己递进去：它是个合法的 CORBA 对象，只是不是 ICapeMINLP
    EXPECT_THROW(manager_->CreateMINLPSystem(manager_.in(), sys_obj.out()),
                 ce::ECapeInvalidArgument);
    EXPECT_THROW(manager_->CreateMINLPSystem(CORBA::Object::_nil(), sys_obj.out()),
                 ce::ECapeInvalidArgument);
}

}  // namespace

#ifndef USE_GTEST_MAIN
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
