// ***************************************************************
//  test_xoptminlpco_register   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  N3 注册 + COM 激活冒烟：
//    1) 设 XRTO_XOPT_PROBLEM_DLL = mock_xoptproblem.dll
//    2) 加载 xOptMINLPco.dll，调 DllRegisterServer（HKCU\Software\Classes）
//    3) CoCreateInstance(CLSID_XOptMINLP) → QI ICapeMINLP / ICapeIdentification → 驱动对拍
//    4) DllUnregisterServer 清理
//  注册失败（如权限）则 GTEST_SKIP。仅 Windows。自带 main。
//
//  xOptMINLPco.dll 与 mock_xoptproblem.dll 与本 exe 同目录（Ninja 单配置）。
// ***************************************************************
#ifdef _WIN32

#include <windows.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "backend/com/CapeOpenComInterfaces.h"
#include "backend/com/CapeVariantMarshal.h"
#include "xOptMINLPcoClsid.h"

extern "C" const GUID IID_ICapeMINLP;
extern "C" const GUID IID_ICapeIdentification;

using cape_com::makeLongArray;
using cape_com::makeDoubleArray;
using cape_com::readDoubleArray;
using cape_com::bstrToUtf8;

namespace {

typedef HRESULT(__stdcall* DllRegFn)();

// 本 exe 所在目录（末尾含分隔符）。
std::wstring exeDir() {
    wchar_t p[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}

TEST(XOptMINLPcoRegister, ActivateViaCoCreateInstance) {
    const std::wstring dir = exeDir();
    // 指向被包装的 xOptProblem DLL（mock，与本 exe 同目录）
    _wputenv_s(L"XRTO_XOPT_PROBLEM_DLL", (dir + L"mock_xoptproblem.dll").c_str());

    HMODULE dll = LoadLibraryW((dir + L"xOptMINLPco.dll").c_str());
    ASSERT_NE(dll, nullptr) << "load xOptMINLPco.dll failed";

    auto reg = reinterpret_cast<DllRegFn>(GetProcAddress(dll, "DllRegisterServer"));
    auto unreg = reinterpret_cast<DllRegFn>(GetProcAddress(dll, "DllUnregisterServer"));
    ASSERT_NE(reg, nullptr);
    ASSERT_NE(unreg, nullptr);

    if (FAILED(reg())) {
        FreeLibrary(dll);
        GTEST_SKIP() << "DllRegisterServer failed (registry permission?) — skipping activation";
    }

    ASSERT_EQ(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), S_OK);

    // 经注册表激活（CoCreateInstance），而非注入 —— 验证类厂/导出/生产 ctor 全路径
    ICapeMINLP* minlp = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_XOptMINLP, nullptr, CLSCTX_INPROC_SERVER, IID_ICapeMINLP,
                                  reinterpret_cast<void**>(&minlp));
    ASSERT_EQ(hr, S_OK) << "CoCreateInstance hr=0x" << std::hex << hr;
    ASSERT_NE(minlp, nullptr);

    long nv = 0, niv, nlv, nliv, nc = 0, nlc, nlz, nnz, nlzof, nnzof;
    ASSERT_EQ(minlp->GetMINLPSize(&nv, &niv, &nlv, &nliv, &nc, &nlc, &nlz, &nnz, &nlzof, &nnzof),
              S_OK);
    EXPECT_EQ(nv, 2);
    EXPECT_EQ(nc, 1);

    // setX(3,4) 后目标应为 25
    VARIANT vids = makeLongArray({0, 1});
    VARIANT vals = makeDoubleArray({3.0, 4.0});
    EXPECT_EQ(minlp->SetMINLPVariableValues(vids, vals), S_OK);
    VariantClear(&vids);
    VariantClear(&vals);
    double obj = 0;
    EXPECT_EQ(minlp->GetMINLPNonlinearObjectiveFunctionValue(&obj), S_OK);
    EXPECT_DOUBLE_EQ(obj, 25.0);

    // QI ICapeIdentification
    ICapeIdentification* ident = nullptr;
    ASSERT_EQ(minlp->QueryInterface(IID_ICapeIdentification, reinterpret_cast<void**>(&ident)), S_OK);
    BSTR name = nullptr;
    ASSERT_EQ(ident->get_ComponentName(&name), S_OK);
    EXPECT_FALSE(bstrToUtf8(name).empty());
    SysFreeString(name);
    ident->Release();

    minlp->Release();
    CoUninitialize();
    unreg();
    FreeLibrary(dll);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#endif  // _WIN32
