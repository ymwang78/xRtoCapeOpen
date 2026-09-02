#pragma once
// ***************************************************************
//  XOptModelDesc   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  黑箱模型的接入描述（部署形态：UnitModel/<名称>/ 下 DLL + <名称>_Model.json）。
//
//  为什么需要它：C-ABI 的黑箱模型不是"createModel 完就能 buildProblem"。宿主
//  （xOptModelBlackBox）在两者之间还要跑一段初始化序列——参数、组分表(slate)、
//  校验、可固定变量与初值估计。这些信息不在 DLL 里，DLL 只报"我需要几个 slate、
//  我有哪些参数"，具体给什么由部署决定。描述 JSON 就是那份部署输入。
//  XOptMINLPAdapter 据此把同一段序列在 CAPE-OPEN 生产端重放一遍。
//
//  识别的键（与 examples/testModel/Splitter_Model.json 兼容）：
//    "parameters"        {名: 数值}      —— 覆盖 getParameters 报出的默认值
//    "fixable_variables" [名, ...]       —— 要固定的变量子集；缺省 = 全部固定
//    "fixed_values"      {名: 数值}      —— 固定值；缺省 = 用模型报的默认值
//    "inports"/"outports" [{内部变量名: 流股变量名}, ...]
//                                        —— 组分表由流股变量名里的 fi_<组分> 推出
//    "slate"             {"name":…, "thermo_method":…, "components":[…]}
//    "components"        [名, ...]       —— 显式组分表；给了就不再从端口推
//  其余键（端口映射本身等）在本通路用不到，解析后忽略——它们服务的是宿主的
//  流股连接，而 CAPE-OPEN 生产端发布的是已经建好的单个问题。
// ***************************************************************
#include <map>
#include <string>
#include <utility>
#include <vector>

struct XOptModelDesc {
    // slate（组分表）。name/thermo_method 有默认值，components 无默认——
    // 模型报了需要 slate 而这里为空，是要报错的情形，不是可以静默跳过的情形。
    std::string slate_name = "StreamPort";
    std::string thermo_method;  // 空 = 不指定（xOptSlate::thermo_method 置 NULL）
    std::vector<std::string> components;

    // 参数覆盖值。用有序 vector 而非 map：报错信息里按书写顺序提名字，
    // 定位 JSON 里那一行比字典序快。
    std::vector<std::pair<std::string, double>> parameters;

    // 要固定的变量子集（空 = 把模型报出的可固定变量全部固定，与 demo/宿主一致）。
    std::vector<std::string> fixable_variables;
    // 显式固定值（缺席的用 getFixableVariables 报出的默认值）。
    std::map<std::string, double> fixed_values;

    bool empty() const {
        return components.empty() && parameters.empty() && fixable_variables.empty() &&
               fixed_values.empty();
    }

    // 解析 JSON 文本。失败返回 false 并填 error（含出错位置）。
    static bool parse(const std::string& json_text, XOptModelDesc& out, std::string& error);

    // 读文件并解析。
    static bool load(const std::string& path, XOptModelDesc& out, std::string& error);

    // 在 DLL 同目录找唯一的 *_Model.json。
    //   找到 1 个 -> 返回 true，path_out 为该路径
    //   找到 0 个 -> 返回 true，path_out 为空（没有描述不是错误）
    //   找到多个 -> 返回 false 并填 error（歧义必须由调用方显式指定，
    //               随便挑一个会让"换了个 json 却没生效"变成静默行为）
    static bool discover(const std::string& dll_path, std::string& path_out, std::string& error);
};
