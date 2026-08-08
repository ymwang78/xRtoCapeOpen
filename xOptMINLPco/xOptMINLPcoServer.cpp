// ***************************************************************
//  xOptMINLPcoServer   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  COM inproc server 入口：类厂 + DllGetClassObject/DllCanUnloadNow/
//  DllRegisterServer/DllUnregisterServer，发布 CoMINLP 为 CAPE-OPEN MINLP 组件
//  （产物 xOptMINLPco.dll）。详见 docs/xOptMINLPco_design.md（N2/N3）。仅 Windows。
//
//  自铸标识：
//    CLSID  {7B2C9E10-5A3D-4C8E-9F21-0A1B2C3D4E5F}
//    ProgID "xOpt.MINLP.1"
//  注册/CoCreateInstance 冒烟见 N3。
// ***************************************************************
#ifdef _WIN32

#include <windows.h>
#include <olectl.h>  // SELFREG_E_CLASS

#include <string>

#include "CoMINLP.h"
#include "xOptMINLPcoClsid.h"

static const wchar_t* kProgId = XOPTMINLPCO_PROGID;
static const wchar_t* kFriendly = XOPTMINLPCO_FRIENDLY;
// 注册到 HKCU\Software\Classes（无需管理员；CoCreateInstance 经 HKCR 合并视图可见）。
static const wchar_t* kClassesRoot = L"Software\\Classes\\";

static HMODULE g_module = nullptr;
static LONG g_lock_count = 0;

namespace {

class ClassFactory : public IClassFactory {
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }   // 单例，永驻
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer != nullptr) return CLASS_E_NOAGGREGATION;
        CoMINLP* obj = new (std::nothrow) CoMINLP();  // 生产模式：读 env XRTO_XOPT_PROBLEM_DLL
        if (obj == nullptr) return E_OUTOFMEMORY;
        HRESULT hr = obj->QueryInterface(riid, ppv);
        obj->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_lock_count);
        else InterlockedDecrement(&g_lock_count);
        return S_OK;
    }
};

ClassFactory g_factory;

bool setRegValue(HKEY root, const std::wstring& subkey, const wchar_t* name,
                 const std::wstring& value) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    LONG rc = RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                             static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return rc == ERROR_SUCCESS;
}

std::wstring clsidString() {
    wchar_t buf[64] = {0};
    StringFromGUID2(CLSID_XOptMINLP, buf, 64);
    return buf;
}

// CAPE-OPEN 的「Implemented Categories」是 PME 发现组件的入口（issue #5）。
//
// 这里直接写注册表键，而不是走 ICatRegister：`ICatRegister::RegisterClassImplCategories`
// 只往 **HKCR**（实为 HKLM）写，需要管理员权限；而本组件其余部分刻意注册在
// HKCU\Software\Classes 以便免管理员安装。两者混用会得到一个「类注册在 HKCU、
// 类别注册在 HKLM」的半吊子状态，卸载时还清不干净。
//
// 写的键与 Methods&Tools 文档 Figure 13 的 .reg 示例逐字一致：
//   CLSID\{…}\Implemented Categories\{类别 GUID}
// PME 经 HKCR 合并视图能读到 HKCU 这一份。
//
// GUID 转字符串用 StringFromGUID2 而非 StringFromCLSID，与上面的 clsidString() 一致。
// 不只是省一次 CoTaskMemFree：DllRegisterServer 会在 **COM 尚未初始化**时被调用
// （regsvr32 如此，本项目的注册测试也是先 reg() 再 CoInitializeEx），而
// StringFromCLSID 走 COM 任务分配器。StringFromGUID2 只写调用方缓冲，没有这层依赖。
bool registerCategory(const std::wstring& clsidKey, const CATID& catid) {
    wchar_t buf[64] = {0};
    if (StringFromGUID2(catid, buf, 64) == 0) return false;
    return setRegValue(HKEY_CURRENT_USER, clsidKey + L"\\Implemented Categories\\" + buf, nullptr,
                       L"");
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (IsEqualCLSID(rclsid, CLSID_XOptMINLP)) {
        return g_factory.QueryInterface(riid, ppv);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return g_lock_count == 0 ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllRegisterServer() {
    wchar_t path[MAX_PATH] = {0};
    if (GetModuleFileNameW(g_module, path, MAX_PATH) == 0) return SELFREG_E_CLASS;
    const std::wstring clsid = clsidString();
    const std::wstring base = kClassesRoot;
    const std::wstring clsidKey = base + L"CLSID\\" + clsid;
    const std::wstring progKey = base + kProgId;

    bool ok = true;
    ok &= setRegValue(HKEY_CURRENT_USER, clsidKey, nullptr, kFriendly);
    ok &= setRegValue(HKEY_CURRENT_USER, clsidKey + L"\\InprocServer32", nullptr, path);
    ok &= setRegValue(HKEY_CURRENT_USER, clsidKey + L"\\InprocServer32", L"ThreadingModel",
                      L"Apartment");
    ok &= setRegValue(HKEY_CURRENT_USER, clsidKey + L"\\ProgID", nullptr, kProgId);
    ok &= setRegValue(HKEY_CURRENT_USER, progKey, nullptr, kFriendly);
    ok &= setRegValue(HKEY_CURRENT_USER, progKey + L"\\CLSID", nullptr, clsid);

    // 类别：只注册通用的「CAPE-OPEN Component」。规范文档集里没有 MINLP 专用类别，
    // 详见 xOptMINLPcoClsid.h 顶部的出处说明——不臆造 GUID。
    ok &= registerCategory(clsidKey, CATID_CapeOpenComponent);

    // CapeDescription：PME 用它展示组件信息（Figure 13）。
    const std::wstring desc = clsidKey + L"\\CapeDescription";
    // Name 是 PME 列表里给人看的显示名（Figure 13 的示例值就是显示名，不是 ProgID）；
    // ProgID 另有自己的注册表键，不必再占这个字段。
    ok &= setRegValue(HKEY_CURRENT_USER, desc, L"Name", XOPTMINLPCO_NAME);
    ok &= setRegValue(HKEY_CURRENT_USER, desc, L"Description", kFriendly);
    ok &= setRegValue(HKEY_CURRENT_USER, desc, L"CapeVersion", XOPTMINLPCO_CAPE_VERSION);
    ok &= setRegValue(HKEY_CURRENT_USER, desc, L"ComponentVersion", XOPTMINLPCO_COMPONENT_VERSION);
    ok &= setRegValue(HKEY_CURRENT_USER, desc, L"About", XOPTMINLPCO_ABOUT);

    return ok ? S_OK : SELFREG_E_CLASS;
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
    const std::wstring clsid = clsidString();
    const std::wstring base = kClassesRoot;
    // RegDeleteTreeW 递归删除，Implemented Categories / CapeDescription 一并清掉。
    RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"CLSID\\" + clsid).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, (base + kProgId).c_str());
    return S_OK;
}

#endif  // _WIN32
