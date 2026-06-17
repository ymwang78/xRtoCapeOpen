// ***************************************************************
//  CapeBackendFactory   version:  1.0   -  date:  2026/06/15
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "CapeBackendFactory.h"

#include <algorithm>
#include <cctype>

#include "CapeMINLPModelMock.h"

namespace {

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

}  // namespace

CapeBackendFactory& CapeBackendFactory::instance() {
    static CapeBackendFactory factory;
    return factory;
}

void CapeBackendFactory::registerBackend(const std::string& scheme, Creator creator) {
    creators_[toLower(scheme)] = std::move(creator);
}

std::pair<std::string, std::string> CapeBackendFactory::parse(const std::string& conn) {
    const std::string lower = toLower(conn);
    // 显式 scheme 前缀（注意不要被 Windows 盘符 "C:\" 误伤，故用已知前缀精确匹配）
    if (startsWith(lower, "mock:")) return {"mock", conn.substr(5)};
    if (startsWith(lower, "corba:")) return {"corba", conn.substr(6)};
    if (startsWith(lower, "com:")) return {"com", conn.substr(4)};
    // 无前缀：以 .dll 结尾默认 COM 进程内组件
    if (endsWith(lower, ".dll")) return {"com", conn};
    return {"", conn};
}

std::unique_ptr<ICapeMINLPModel> CapeBackendFactory::create(const std::string& conn,
                                                            std::string& error_out) {
    auto [scheme, target] = parse(conn);
    if (scheme.empty()) {
        error_out = "无法识别的连接串(缺少 com:/corba:/mock: 前缀或 .dll 后缀): " + conn;
        return nullptr;
    }
    // mock 为 core 内置，避免静态库符号被裁剪导致注册丢失
    if (scheme == "mock") {
        return std::make_unique<CapeMINLPModelMock>(target);
    }
    auto it = creators_.find(scheme);
    if (it == creators_.end()) {
        error_out = "未注册的后端 scheme: " + scheme +
                    " (对应后端 DLL 未加载, 或该 DLL 未启用此 scheme)";
        return nullptr;
    }
    return it->second(target);
}
