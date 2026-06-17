// ***************************************************************
//  CapeRegisterCorbaBackend   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  把 CORBA 后端注册到 CapeBackendFactory 的 "corba" scheme（design §3.2）。
// ***************************************************************
#include <memory>

#include "../../CapeBackendFactory.h"
#include "CapeMINLPModelCorba.h"

void CapeRegisterCorbaBackend() {
    CapeBackendFactory::instance().registerBackend("corba", [](const std::string& target) {
        return std::unique_ptr<ICapeMINLPModel>(new CapeMINLPModelCorba(target));
    });
}
