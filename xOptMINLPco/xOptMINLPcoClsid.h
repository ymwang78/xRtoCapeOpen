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
#include <comcat.h>  // CATID（<windows.h> 不带）

// {7B2C9E10-5A3D-4C8E-9F21-0A1B2C3D4E5F}
static const CLSID CLSID_XOptMINLP = {
    0x7B2C9E10, 0x5A3D, 0x4C8E, {0x9F, 0x21, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F}};

#define XOPTMINLPCO_PROGID L"xOpt.MINLP.1"
// 显示名与描述：注册表的 CapeDescription 与运行时 ICapeIdentification 用**同一对**
// 常量，两者不会漂移——PME 在组件列表里看到的，和它 CoCreateInstance 之后
// 问出来的，必须是同一个东西。
#define XOPTMINLPCO_NAME L"xOpt MINLP"
#define XOPTMINLPCO_FRIENDLY L"xOpt problem published as CAPE-OPEN MINLP"

// ---- CAPE-OPEN 组件类别（CATID）----
//
// PME 靠枚举 CAPE-OPEN 类别来发现可用组件。没有类别注册，用户在 PME 的组件列表里
// **看不到**这个组件，只能手工填 CLSID/ProgID——对最终用户等于不可用（issue #5）。
//
// 出处：`docs/.../Methods&Tools_Integrated_Guidelines.pdf` Figure 14
// “GUIDs for CAPE-OPEN Component categories”（p.47），全表 6 项：
//   CAPE-OPEN Component                 {678c09a1-7d66-11D2-a67d-00105a42887f}
//   CAPE-OPEN Thermo Routine            {678c09a2-…}
//   CAPE-OPEN Thermo Property System    {678c09a3-…}
//   CAPE-OPEN Thermo Property Package   {678c09a4-…}
//   CAPE-OPEN Unit Operation            {678c09a5-…}
//   CAPE-OPEN Thermo Equilibrium Server {678c09a6-…}
//
// **该表里没有 MINLP / Solvers / Numerics 类别**，Optimisation 规范也未定义任何
// 类别——已对 docs/ 下 1.0 与 1.1 两套文档全文扫描确认（只有 Methods&Tools 的这张表
// 和 Unit Operations 自引的 a5）。因此这里**只注册通用的「CAPE-OPEN Component」**，
// 不臆造一个 MINLP 专用 GUID：编一个出来没有任何 PME 会去找它，还可能与将来 CO-LaN
// 真定的那个撞号。
//
// 注：较新的规范确实会各自定义 CATID（如 Monitoring 用 {7BA1AF89-B2E4-493d-BD80-
// 2970BF4CBE99}，与 678c09ax 不同族）。若 CO-LaN 后来为 MINLP 定过一个，应在此补上
// ——这属于 issue #3「取得官方权威材料」的范围。
//
// {678C09A1-7D66-11D2-A67D-00105A42887F}
static const CATID CATID_CapeOpenComponent = {
    0x678C09A1, 0x7D66, 0x11D2, {0xA6, 0x7D, 0x00, 0x10, 0x5A, 0x42, 0x88, 0x7F}};

// PME 读 CapeDescription 子键来展示组件信息。
// 出处：同上文档 Figure 13“Sample COM Registration entries”。
#define XOPTMINLPCO_CAPE_VERSION L"1.0"
#define XOPTMINLPCO_COMPONENT_VERSION L"1.0.0"
#define XOPTMINLPCO_ABOUT L"xOpt problem exposed through the CAPE-OPEN MINLP interface."

#endif  // _WIN32
