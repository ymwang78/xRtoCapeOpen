// ***************************************************************
//  test_xoptminlpco_corba   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  N4 回环（CORBA 方向，同时验证两个方向）：
//    xOptProblem(Mock) → XOptMINLPAdapter(ICapeMINLPModel)
//      → MINLPServant(POA SqpSolver::ICapeMINLP)         ← xOptMINLPco 生产者
//        → collocated → capeopen_core CapeMINLPModelCorba(注入)  ← capeopen_core 消费者
//          → 读回，与解析值对拍。
//  自带 main。
// ***************************************************************
#include "MockXOptProblem.h"  // 首个包含：XOPTINTERFACE_EXPORTS + xOptProblem

#include <gtest/gtest.h>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <vector>

#include "MINLPServant.h"
#include "XOptMINLPAdapter.h"
#include "backend/corba/CapeMINLPModelCorba.h"

namespace {

TEST(XOptMINLPcoCorbaLoopback, RoundTrip) {
    MockXOptProblem mock;
    XOptMINLPAdapter adapter(&mock);
    ASSERT_EQ(adapter.connect(), 0) << adapter.lastError();

    int argc = 0;
    CORBA::ORB_ptr orb = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
    CORBA::Object_var p = orb->resolve_initial_references("RootPOA");
    PortableServer::POA_var poa = PortableServer::POA::_narrow(p.in());
    poa->the_POAManager()->activate();

    // 生产者：把 adapter 发布为 CORBA ICapeMINLP servant
    MINLPServant* servant = new MINLPServant(&adapter);
    PortableServer::ObjectId_var oid = poa->activate_object(servant);
    CORBA::Object_var obj = poa->id_to_reference(oid.in());
    SqpSolver::ICapeMINLP_var ref = SqpSolver::ICapeMINLP::_narrow(obj.in());

    // 消费者：capeopen_core 的 CORBA 后端，注入上面的引用
    CapeMINLPModelCorba consumer(ref.in());
    ASSERT_EQ(consumer.connect(), 0) << consumer.lastError();

    CapeMINLPSize s;
    ASSERT_EQ(consumer.getSize(s), 0) << consumer.lastError();
    EXPECT_EQ(s.num_variables, 2);
    EXPECT_EQ(s.num_constraints, 1);
    EXPECT_EQ(s.num_nonlinear_jacobian_nz, 2);

    std::vector<std::string> names;
    ASSERT_EQ(consumer.getVariableNames({0, 1}, names), 0);
    EXPECT_EQ(names, (std::vector<std::string>{"x0", "x1"}));

    std::vector<double> lb, ub;
    ASSERT_EQ(consumer.getVariableBounds({0, 1}, lb, ub), 0);
    EXPECT_DOUBLE_EQ(lb[0], -10.0);
    EXPECT_DOUBLE_EQ(ub[1], 10.0);

    std::vector<int> r, c, o;
    ASSERT_EQ(consumer.getStructure("Jacobian", r, c, o), 0);
    EXPECT_EQ(r, (std::vector<int>{0, 0}));
    EXPECT_EQ(c, (std::vector<int>{0, 1}));

    ASSERT_EQ(consumer.setVariableValues({0, 1}, {3.0, 4.0}), 0);
    double objv = 0;
    ASSERT_EQ(consumer.getObjectiveValue(objv), 0);
    EXPECT_DOUBLE_EQ(objv, 25.0);

    std::vector<double> cons;
    ASSERT_EQ(consumer.getNonlinearConstraintValues({0}, cons), 0);
    EXPECT_DOUBLE_EQ(cons[0], 4.0);

    std::vector<double> grad;
    ASSERT_EQ(consumer.getObjectiveDerivativeValues("Nonlinear", grad), 0);
    EXPECT_EQ(grad, (std::vector<double>{6.0, 8.0}));

    std::vector<double> jac;
    ASSERT_EQ(consumer.getConstraintDerivativeValues("Jacobian", {0}, jac), 0);
    EXPECT_EQ(jac, (std::vector<double>{1.0, 1.0}));
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
