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
#include <comcat.h>  // ICatInformation：按类别枚举组件，即 PME 的发现路径

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

    // setX(3,4) 后目标应为 25。
    // vids 用 **1-based**：本用例扮演的是 CAPE-OPEN 客户端，规范要求变量从 1 编号
    // （issue #2）。这里原先写 {0, 1}，在 COM 侧换基之前一直"能过"——那恰恰是
    // 缺陷的一部分，不是测试的便利写法。
    VARIANT vids = makeLongArray({1, 2});
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

    // —— 类别注册：PME 发现组件走的就是这条路（issue #5）——
    // 断言的不是「注册表里有那个键」，而是**按类别枚举能不能找到本组件**，
    // 那才是 PME 实际做的事。
    {
        ICatInformation* cat = nullptr;
        HRESULT chr = CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_ICatInformation, reinterpret_cast<void**>(&cat));
        ASSERT_EQ(chr, S_OK) << "拿不到组件类别管理器 hr=0x" << std::hex << chr;

        CATID want = CATID_CapeOpenComponent;
        IEnumCLSID* e = nullptr;
        ASSERT_EQ(cat->EnumClassesOfCategories(1, &want, 0, nullptr, &e), S_OK);

        bool found = false;
        CLSID got;
        ULONG fetched = 0;
        while (e->Next(1, &got, &fetched) == S_OK && fetched == 1) {
            if (IsEqualCLSID(got, CLSID_XOptMINLP)) { found = true; break; }
        }
        e->Release();
        cat->Release();
        EXPECT_TRUE(found) << "按 CAPE-OPEN Component 类别枚举不到本组件——PME 也就发现不了它";
    }

    CoUninitialize();
    unreg();

    // 注销后必须枚举不到，否则会留下指向已卸载组件的陈旧条目。
    {
        ASSERT_EQ(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), S_OK);
        ICatInformation* cat = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_ICatInformation,
                                       reinterpret_cast<void**>(&cat)))) {
            CATID want = CATID_CapeOpenComponent;
            IEnumCLSID* e = nullptr;
            if (SUCCEEDED(cat->EnumClassesOfCategories(1, &want, 0, nullptr, &e))) {
                bool still = false;
                CLSID got;
                ULONG fetched = 0;
                while (e->Next(1, &got, &fetched) == S_OK && fetched == 1) {
                    if (IsEqualCLSID(got, CLSID_XOptMINLP)) { still = true; break; }
                }
                e->Release();
                EXPECT_FALSE(still) << "注销后仍能枚举到——留下了陈旧的类别条目";
            }
            cat->Release();
        }
        CoUninitialize();
    }

    FreeLibrary(dll);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#endif  // _WIN32
