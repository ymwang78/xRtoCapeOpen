#pragma once
// ***************************************************************
//  CapeUnitCorba   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  消费端：从一个 CORBA 目标读回**接线信息**（端口、组分表、可固定变量），
//  供 xOptModelCapeOpen.cpp 填 xOptModelT 的端口回调，让 xRto 能画线。
//
//  为什么与 CapeMINLPModelCorba 分开而不是塞进去：
//  两者的生命周期不一样。问题对象（ICapeMINLPModel）在 buildProblem 里被
//  移交给 CapeMINLPProblemCore 拥有；而端口信息在那之前就要被宿主问到
//  （xOptModelBlackBox 的 prepareRuntime 早于 buildProblem），之后还可能再问。
//  一个"连上、读完、断开"的短命对象最省事——读回来的都是值，没有任何东西
//  的有效期依赖那条连接。
//
//  连接目标可以是：
//    - 一个 ICapeUnit / IXOptUnitExtension 引用（有端口）
//    - 一个 ICapeMINLP 引用（没有端口；isUnit() 返回 false，不是错误）
//  后者是本项目原有的通路，保持能连是为了不把既有部署打断。
// ***************************************************************
#ifdef CAPEOPEN_WITH_CORBA

#include <string>
#include <utility>
#include <vector>

// 一个端口的接线信息。与生产端 XOptPortDesc 同构，但刻意是两份定义：
// 它们分处 CORBA 线的两侧，共用一个头文件只会让 core 依赖 xOptMINLPco。
struct CapeUnitPort {
    std::string name;
    bool is_input = false;
    // (流股变量名, 模型内部变量名)，流股侧按平台约定是 "T"/"P"/"fi_<组分>"。
    std::vector<std::pair<std::string, std::string>> variables;
};

class CapeUnitCorba {
  public:
    // 连接并把接线信息全部读回来。返回 >=0 成功。
    // 目标不是单元（narrow 到 IXOptUnitExtension 失败）时**也返回成功**，
    // 只是 isUnit() 为 false、ports() 为空——"这个组件没有端口"是合法状态，
    // 用失败表达会逼调用方把它和"连不上"混为一谈。
    int read(const std::string& target);

    // 把组分表推给远端并**重读全部接线信息**——组分变了，端口映射、可固定
    // 变量、问题规模都跟着变。必须先 read() 过一次（要有目标地址）。
    // 目标不是单元时返回 -1：那时根本没有可推的对象。
    int setComponents(const std::vector<std::string>& components);

    // 推固定变量集合并取回初值向量，随后重读接线信息。空表合法（一个都不固定）。
    int generateEstimate(const std::vector<std::string>& fixed_names,
                         const std::vector<double>& fixed_values,
                         std::vector<double>& initial_x_out);

    bool isUnit() const { return is_unit_; }
    const std::vector<std::string>& components() const { return components_; }
    const std::vector<CapeUnitPort>& ports() const { return ports_; }
    const std::vector<std::pair<std::string, double>>& fixableVariables() const {
        return fixables_;
    }
    const std::string& lastError() const { return last_error_; }

  private:
    int fail(const std::string& msg);

    std::string target_;
    bool is_unit_ = false;
    std::vector<std::string> components_;
    std::vector<CapeUnitPort> ports_;
    std::vector<std::pair<std::string, double>> fixables_;
    std::string last_error_;
};

#endif  // CAPEOPEN_WITH_CORBA
