#pragma once
// ***************************************************************
//  CapeBackendFactory   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  按连接串 scheme 选择并构造 ICapeMINLPModel 后端（design §3.2）。
//    com:{CLSID|ProgId} / *.dll   -> COM 后端
//    corba:IOR / corba:corbaname  -> CORBA 后端
//    mock:<name>                  -> 内置 Mock 后端（core 自带）
//  COM/CORBA 后端在各自 DLL 内通过 registerBackend 注册；mock 为内置。
// ***************************************************************
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "CapeMINLPModel.h"

class CapeBackendFactory {
  public:
    using Creator = std::function<std::unique_ptr<ICapeMINLPModel>(const std::string& target)>;

    static CapeBackendFactory& instance();

    // 注册一个 scheme 的后端构造器（com / corba 在其 DLL 加载时调用）。
    void registerBackend(const std::string& scheme, Creator creator);

    // 解析连接串并构造后端；失败返回 nullptr 并填 error_out。
    std::unique_ptr<ICapeMINLPModel> create(const std::string& conn, std::string& error_out);

    // 解析连接串为 {scheme, target}。scheme 为小写；无法识别时 scheme 为空。
    static std::pair<std::string, std::string> parse(const std::string& conn);

  private:
    CapeBackendFactory() = default;
    std::map<std::string, Creator> creators_;
};
