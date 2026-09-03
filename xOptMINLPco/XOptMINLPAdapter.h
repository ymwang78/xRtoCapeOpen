#pragma once
// ***************************************************************
//  XOptMINLPAdapter   version:  2.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  反向适配：把一个 xOptProblem 暴露成传输无关的 ICapeMINLPModel
//  （复用 capeopen_core 的抽象），供 COM/CORBA 前端发布为 CAPE-OPEN MINLP。
//  详见 docs/xOptMINLPco_design.md §2、§3。
//
//  输入来源（自动探测 DLL 导出，N1=C++ ABI / N5=C ABI）：
//    - C++ ABI：DLL 导出 createProblem()/destroyProblem()（返回 xOptProblem*）
//    - C   ABI：DLL 导出 xOptModel_createModel()（填 xOptModelT，buildProblem 填 xOptProblemT）
//    - 注入：xOptProblem*（C++）或 xOptProblemT（C）—— 用于 in-proc 单测，不拥有
//
//  C ABI 的模型初始化序列（见 initModel）：createModel 与 buildProblem 之间还有
//  一段宿主（xOptModelBlackBox）会跑、而我们先前漏掉的握手——参数、组分表、
//  校验、可固定变量与初值估计。凡是"必须先 setSlate 才能 buildProblem"的模型
//  （examples 的 Splitter 就是）在漏掉这段时会直接 buildProblem 失败。
//  这段握手的输入来自部署描述 JSON（XOptModelDesc）：显式给路径，或由
//  DLL 同目录唯一的 *_Model.json 自动发现。
// ***************************************************************
#include <memory>
#include <string>
#include <vector>

#include "CapeMINLPModel.h"     // ICapeMINLPModel（来自 capeopen_core）
#include "XOptModelDesc.h"      // 部署描述（参数/组分表/固定变量）
#include "xOpt/xOptModel.h"     // xOptModelT / xOptProblemT
#include "xOpt/xOptProblem.h"   // xOptProblem / createProblemFunc

struct IXOptProblemView;  // 内部：屏蔽 C++/C ABI 差异（定义在 .cpp）

// 一个端口的接线信息，从 xOptModelT 的端口 API 抄下来。
//
// name 是**合成的**："in0"/"out0"/...。xOptModelT 没有端口名这个概念，它只有
// 计数加两张映射表；而 CAPE-OPEN 的 ICapeUnitPort 继承 ICapeIdentification，
// 端口必须有名字。合成规则写死在 XOptMINLPAdapter::initModel 里，消费端按
// 同一规则理解，别的地方不要另立一套。
struct XOptPortDesc {
    std::string name;
    bool is_input = false;
    // (流股变量名, 模型内部变量名)。流股侧按平台约定是 "T"/"P"/"fi_<组分>"。
    std::vector<std::pair<std::string, std::string>> variables;
};

class XOptMINLPAdapter : public ICapeMINLPModel {
  public:
    explicit XOptMINLPAdapter(const std::string& dll_path);  // 自动探测 ABI
    // desc_path 为空时在 DLL 同目录自动发现唯一的 *_Model.json（C ABI 才用得上）
    XOptMINLPAdapter(const std::string& dll_path, const std::string& desc_path);
    // 注入 C++ 问题（不拥有）。initialize_problem=false 时 connect() 不再调它的
    // initialize()：宿主交来的问题（xRto 的组合问题）早已初始化过，再调一次会把
    // 它整个重建——求解器桥接 DLL 走的就是这条。
    explicit XOptMINLPAdapter(xOptProblem* injected, bool initialize_problem = true);
    explicit XOptMINLPAdapter(const xOptProblemT& injected); // 注入 C vtable（不拥有）
    ~XOptMINLPAdapter() override;

    int connect() override;
    void disconnect() override;

    int getSize(CapeMINLPSize& size_out) override;
    int getStructure(const std::string& type, std::vector<int>& row_index,
                     std::vector<int>& col_index, std::vector<int>& obj_index) override;
    int getVariableNames(const std::vector<int>& vids, std::vector<std::string>& names_out) override;
    int getVariableBounds(const std::vector<int>& vids, std::vector<double>& lower_out,
                          std::vector<double>& upper_out) override;
    int getVariableValues(const std::vector<int>& vids, std::vector<double>& values_out) override;
    int setVariableValues(const std::vector<int>& vids, const std::vector<double>& values) override;
    int getConstraintNames(const std::vector<int>& cids,
                           std::vector<std::string>& names_out) override;
    int getConstraintBounds(const std::vector<int>& cids, std::vector<double>& lower_out,
                            std::vector<double>& upper_out) override;
    int getNonlinearConstraintValues(const std::vector<int>& cids,
                                     std::vector<double>& values_out) override;
    int getConstraintDerivativeValues(const std::string& type, const std::vector<int>& cids,
                                      std::vector<double>& values_out) override;
    int getObjectiveValue(double& value_out) override;
    int getObjectiveDerivativeValues(const std::string& type,
                                     std::vector<double>& values_out) override;
    std::string lastError() const override;

    // —— 接线信息（C ABI 输入时由 initModel 填；C++ ABI 输入时恒为空）——
    //
    // 为什么在 connect 时一次抓完而不是按需去问模型：buildProblem 之后模型的
    // 端口/组分表就定了，而 CAPE-OPEN 侧的 servant 是被远端随时调用的，
    // 每次回头去碰 xOptModelT 意味着要考虑线程安全——那是白给自己找的麻烦。
    const std::vector<std::string>& components() const { return desc_.components; }

    // 换一套组分表并**整体重建**：断开、按新组分重跑 initModel + buildProblem。
    // 端口映射、可固定变量、问题的规模全都会变——组分表本来就决定这些。
    // 只对 C-ABI 模型输入有意义（C++ ABI 的 createProblem 没有模型这一层）。
    int setComponents(const std::vector<std::string>& components);

    // 换一组"要固定的变量"并整体重建。空表是**有效答案**（一个都不固定），
    // 与"没指定、用描述文件里那份"是两回事——接进流程图时进料由上游决定，
    // 一个都不该固定，而描述文件里通常列着全部。
    int setFixedVariables(const std::vector<std::string>& names,
                          const std::vector<double>& values);

    // 最近一次重建后的初值向量（generateEstimate 的产物；没有则退回问题的
    // getInitialX）。长度等于变量个数。
    const std::vector<double>& initialX() const { return initial_x_; }
    const std::vector<XOptPortDesc>& ports() const { return ports_; }
    const std::vector<std::pair<std::string, double>>& fixableVariables() const {
        return fixables_;
    }

  private:
    int fail(const std::string& msg) const;

    // C ABI：在 createModel 与 buildProblem 之间重放宿主的模型初始化序列。
    // 失败时已填 last_error_，调用方直接返回 -1。
    int initModel(xOptModelT& model);

    // 输入来源（connect 时据此建 view_）
    std::string dll_path_;
    std::string desc_path_;    // 显式给的描述路径；空 = 自动发现
    std::string desc_source_;  // 实际用上的那一份（报错信息要指得出是谁）
    XOptModelDesc desc_;
    xOptProblem* inject_cpp_ = nullptr;
    bool initialize_injected_ = true;  // 见注入构造的说明
    bool have_capi_inject_ = false;
    xOptProblemT inject_capi_{};

    void* module_ = nullptr;  // HMODULE / dlopen handle
    std::unique_ptr<IXOptProblemView> view_;
    bool initialized_ = false;

    CapeMINLPSize size_{};
    std::vector<std::string> var_names_;
    std::vector<std::string> con_names_;
    std::vector<double> var_lower_;
    std::vector<double> var_upper_;
    std::vector<double> con_lower_;
    std::vector<double> con_upper_;
    std::vector<double> initial_x_;
    std::vector<double> current_x_;
    std::vector<double> estimate_x_;  // generateEstimate 的初值估计（有则盖过 getInitialX）
    std::vector<XOptPortDesc> ports_;
    std::vector<std::pair<std::string, double>> fixables_;
    // 覆盖描述文件里的组分表。**刻意不在 disconnect 里清掉**：它是外部的意志，
    // 而 disconnect 清的是本次连接的产物。清掉的话重连就会退回描述文件的值，
    // 等于把刚推下来的组分表悄悄丢了。
    std::vector<std::string> components_override_;
    // 同理不在 disconnect 里清。has_ 标志把"指定了空表"和"没指定"分开。
    bool has_fixed_override_ = false;
    std::vector<std::string> fixed_names_override_;
    std::vector<double> fixed_values_override_;
    std::vector<int> jac_rowidx_;
    std::vector<int> jac_colidx_;
    std::vector<int> objgrad_colidx_;

    mutable std::string last_error_;
};
