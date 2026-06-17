// ***************************************************************
//  CapeRegisterComBackend   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  把 COM 后端注册到 CapeBackendFactory 的 "com" scheme（design §3.2）。
//  由 xOptModelCapeOpen.cpp 的 ensureBackendsRegistered() 调用。仅 Windows。
// ***************************************************************
#ifdef _WIN32

#include <memory>

#include "../../CapeBackendFactory.h"
#include "CapeMINLPModelCom.h"

void CapeRegisterComBackend() {
    CapeBackendFactory::instance().registerBackend("com", [](const std::string& target) {
        return std::unique_ptr<ICapeMINLPModel>(new CapeMINLPModelCom(target));
    });
}

#endif  // _WIN32
