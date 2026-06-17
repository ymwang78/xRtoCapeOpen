#pragma once
// ***************************************************************
//  xOptMINLPcoClsid   version:  1.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  xOptMINLPco COM 组件的自铸标识（CLSID / ProgID），由服务端与测试共享。
// ***************************************************************
#ifdef _WIN32

#include <windows.h>

// {7B2C9E10-5A3D-4C8E-9F21-0A1B2C3D4E5F}
static const CLSID CLSID_XOptMINLP = {
    0x7B2C9E10, 0x5A3D, 0x4C8E, {0x9F, 0x21, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F}};

#define XOPTMINLPCO_PROGID L"xOpt.MINLP.1"
#define XOPTMINLPCO_FRIENDLY L"xOpt problem published as CAPE-OPEN MINLP"

#endif  // _WIN32
