// ***************************************************************
//  test_capeopen_corba   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  CORBA 后端测试：CapeMINLPModelCorba 经 collocated 的参考服务端
//  RefCapeMINLPServant（POA）做全链路（sequence<->vector）对拍（design §5.3）。
//  自带 main（本 exe 独立于 COM/core 测试 exe）。
// ***************************************************************
#include <gtest/gtest.h>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <memory>
#include <vector>

#include "backend/corba/CapeMINLPModelCorba.h"
#include "backend/corba/RefCapeMINLPServant.h"
#include "CapeMINLPProblemCore.h"
#include "xOpt/xOptModel.h"

namespace {

// 懒创建一个 collocated 的 ICapeMINLP 引用（ORB/POA/servant 故意泄漏，测试进程退出回收）。
SqpSolver::ICapeMINLP_ptr getRef() {
    static SqpSolver::ICapeMINLP_ptr ref = nullptr;
    if (ref) return ref;
    int argc = 0;
    CORBA::ORB_ptr orb = CORBA::ORB_init(argc, static_cast<char**>(nullptr));
    CORBA::Object_var p = orb->resolve_initial_references("RootPOA");
    PortableServer::POA_var poa = PortableServer::POA::_narrow(p.in());
    poa->the_POAManager()->activate();
    RefCapeMINLPServant* servant = new RefCapeMINLPServant();
    PortableServer::ObjectId_var oid = poa->activate_object(servant);
    CORBA::Object_var obj = poa->id_to_reference(oid.in());
    ref = SqpSolver::ICapeMINLP::_narrow(obj.in());
    return ref;
}

TEST(CapeCorbaBackendTest, RoundTripCollocated) {
    CapeMINLPModelCorba m(getRef());
    ASSERT_EQ(m.connect(), 0) << m.lastError();

    CapeMINLPSize s;
    ASSERT_EQ(m.getSize(s), 0) << m.lastError();
    EXPECT_EQ(s.num_variables, 2);
    EXPECT_EQ(s.num_constraints, 1);
    EXPECT_EQ(s.num_nonlinear_jacobian_nz, 2);

    std::vector<std::string> names;
    ASSERT_EQ(m.getVariableNames({0, 1}, names), 0) << m.lastError();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x0");
    EXPECT_EQ(names[1], "x1");

    std::vector<double> lb, ub;
    ASSERT_EQ(m.getVariableBounds({0, 1}, lb, ub), 0) << m.lastError();
    EXPECT_DOUBLE_EQ(lb[0], -10.0);
    EXPECT_DOUBLE_EQ(ub[1], 10.0);

    std::vector<int> r, c, o;
    ASSERT_EQ(m.getStructure("Jacobian", r, c, o), 0) << m.lastError();
    EXPECT_EQ(r, (std::vector<int>{0, 0}));
    EXPECT_EQ(c, (std::vector<int>{0, 1}));

    ASSERT_EQ(m.setVariableValues({0, 1}, {3.0, 4.0}), 0) << m.lastError();
    double obj = 0;
    ASSERT_EQ(m.getObjectiveValue(obj), 0) << m.lastError();
    EXPECT_DOUBLE_EQ(obj, 25.0);

    std::vector<double> cons;
    ASSERT_EQ(m.getNonlinearConstraintValues({0}, cons), 0) << m.lastError();
    ASSERT_EQ(cons.size(), 1u);
    EXPECT_DOUBLE_EQ(cons[0], 4.0);

    std::vector<double> grad;
    ASSERT_EQ(m.getObjectiveDerivativeValues("Nonlinear", grad), 0) << m.lastError();
    EXPECT_EQ(grad, (std::vector<double>{6.0, 8.0}));

    std::vector<double> jac;
    ASSERT_EQ(m.getConstraintDerivativeValues("Jacobian", {0}, jac), 0) << m.lastError();
    EXPECT_EQ(jac, (std::vector<double>{1.0, 1.0}));
}

TEST(CapeCorbaBackendTest, FullStackThroughProblemCore) {
    auto backend = std::make_unique<CapeMINLPModelCorba>(getRef());
    auto* core = new CapeMINLPProblemCore(std::move(backend));
    ASSERT_GE(core->initialize(), 0);

    xOptProblemT pt{sizeof(xOptProblemT)};
    core->fillVtable(&pt);
    EXPECT_EQ(pt.numVariables(pt.handle), 2);

    const double x[2] = {3.0, 4.0};
    ASSERT_EQ(pt.setX(pt.handle, x, 2), 2);
    double obj = 0;
    ASSERT_EQ(pt.evaluateObjective(pt.handle, &obj), 0);
    EXPECT_DOUBLE_EQ(obj, 25.0);

    pt.destroyProblem(pt.handle);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
