// ***************************************************************
//  mock_xoptmodel_dll   version:  1.0   -  date:  2026/09/02
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco tests).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
//  测试夹具 DLL：导出 C ABI 的 xOptModel_createModel，且**刻意**要求完整的
//  宿主初始化序列（参数 -> setSlate -> validateModel -> 可固定变量 +
//  generateEstimate）才肯 buildProblem。examples 的 Splitter 就是这个形状；
//  这里把它缩到能验证 XOptMINLPAdapter::initModel 的最小规模。
//
//  为什么不直接拿 Splitter 当夹具：Splitter 在 examples/ 下，要 Eigen 和
//  一整套 demo 构建；单测要的是"少了握手就失败、补上握手就成功"这一条，
//  夹具越小，失败时指向的位置越准。
//
//  模型（gain 模型，N = 组分个数）：
//    变量  x0, f_<Ci>                       nv = N + 1
//    约束  f_<Ci> - gain * x0 = 0           N 条
//          x0 - <固定值> = 0                1 条          nc = N + 1
//    目标  恒 0
//  三个部署输入各自能被观察到：组分表 -> nv/nc；gain -> 雅可比值；
//  x0 的固定值 -> 初值。任一步握手被跳过，都会在断言上现形。
// ***************************************************************
// XOPTIF_API 必须解析成 dllexport 而不是 dllimport：本 TU 就是导出方。
// 与 tests/MockXOptProblem.h 同一套做法，须在包含 xOpt 头之前定义。
#ifdef _WIN32
#    ifndef XOPTINTERFACE_EXPORTS
#        define XOPTINTERFACE_EXPORTS
#    endif
#endif

#include <cstddef>
#include <cstring>
#include <map>
#include <utility>
#include <new>
#include <string>
#include <vector>

#include "xOpt/xOptModel.h"
#include "xOpt/xOptProblem.h"  // xOptProblem::OPTIONS

namespace {

const double kBig = 1e20;

class GainModel;

// —— 问题对象（buildProblem 的产物）——
class GainProblem {
  public:
    explicit GainProblem(const GainModel& model);

    int n() const { return static_cast<int>(var_names_.size()); }
    int m() const { return static_cast<int>(con_names_.size()); }

    std::vector<std::string> var_names_;
    std::vector<std::string> con_names_;
    std::vector<double> x0_;
    std::vector<double> x_;
    double gain_ = 1.0;
    double fixed_x0_ = 0.0;
};

// —— 模型对象 ——
class GainModel {
  public:
    int setSlate(int slate_index, const xOptSlate* slate) {
        if (slate_index != 0 || slate == nullptr) return -1;
        components_.clear();
        for (int i = 0; i < XOPT_MAX_COMPONENTS && slate->components[i] != nullptr; ++i) {
            components_.emplace_back(slate->components[i]);
        }
        if (components_.empty()) return -1;
        // 测试钩子：含 "NOPE" 的组分表一律拒绝。回滚路径需要一个**确定**会失败
        // 的输入才测得了，而这个模型对任何非空组分表都乐意接受。
        for (const std::string& c : components_) {
            if (c == "NOPE") {
                components_.clear();
                return -1;
            }
        }
        slate_name_ = (slate->name != nullptr) ? slate->name : "";
        slate_set_ = true;
        return 0;
    }

    int getParameters(const char* names[], double values[], int& size) const {
        if (names == nullptr && values == nullptr) {  // 两段式
            size = 1;
            return 0;
        }
        if (size < 1) return -1;
        names[0] = "gain";
        values[0] = gain_;
        return 1;
    }

    int setParameters(const char* names[], double values[], int size) {
        for (int i = 0; i < size; ++i) {
            if (names[i] == nullptr) return -1;
            if (std::strcmp(names[i], "gain") == 0) {
                gain_ = values[i];
            } else {
                return -1;  // 不认识的参数：拒绝，不静默吞掉
            }
        }
        params_set_ = true;
        return 0;
    }

    int getFixableVariables(const char* names[], double values[], int& size) const {
        if (!slate_set_) return -1;  // 组分表定了才谈得上变量
        if (names == nullptr && values == nullptr) {
            size = 1;
            return 0;
        }
        if (size < 1) return -1;
        names[0] = "x0";
        values[0] = fixedX0();
        return 1;
    }

    int generateEstimate(double initx[], int& size, const char fixed_var_names[],
                         const double fixed_var_values[], int fixed_var_size) {
        if (!slate_set_) return -1;
        const int n = compN() + 1;
        if (initx == nullptr) {
            size = n;
            return 0;
        }
        if (size != n) return -1;
        // 解包 "name1\0name2\0..."，与 fixed_var_values 一一对应
        const char* p = fixed_var_names;
        for (int i = 0; i < fixed_var_size; ++i) {
            if (p == nullptr) return -1;
            const std::string name(p);
            if (name != "x0") return -1;  // 固定了不允许固定的变量
            fixed_values_[name] = fixed_var_values[i];
            p += name.size() + 1;
        }
        estimate_done_ = true;
        initx[0] = fixedX0();
        for (int i = 0; i < compN(); ++i) initx[1 + i] = gain_ * initx[0];
        return 0;
    }

    // buildProblem 的前置：三段握手都做过才算就绪。少一段就失败——夹具存在的
    // 意义就是让"漏掉握手"变成一条断言，而不是一个碰巧还能跑的结果。
    int validate() const {
        if (!slate_set_ || !params_set_ || !estimate_done_) return -1;
        if (!(gain_ > 0.0)) return -1;
        return 0;
    }

    // validateModel 在 generateEstimate 之前被调用，那时 estimate_done_ 还是
    // false；所以校验分两层：validateModel 只查它当时该查的。
    int validateModel() const {
        if (!slate_set_ || !params_set_) return -1;
        if (!(gain_ > 0.0)) return -1;
        return 0;
    }

    // ---- 端口：1 进 1 出 ----
    // 进口只带一个 T（映射到 x0），出口带每个组分一条 fi_<组分>。刻意让两个口
    // 的变量个数不同：个数相同的话，消费端把进出口搞混也测不出来。
    // 返回的 const char* 由本对象持有（port_strings_），活到模型销毁为止——
    // 这正是 xOptModelT 端口映射对实现方的要求。
    int getInPortNum() const { return 1; }
    int getOutPortNum() const { return 1; }

    int getPortVariableMap(bool is_input, int port_index, const char* stream_names[],
                           const char* variable_names[], int& size) {
        if (!slate_set_ || port_index != 0) return -1;
        rebuildPortStrings();
        const auto& m = is_input ? in_map_ : out_map_;
        const int count = static_cast<int>(m.size());
        if (stream_names == nullptr && variable_names == nullptr) {
            size = count;
            return 0;
        }
        if (size < count) return -1;
        for (int i = 0; i < count; ++i) {
            if (stream_names != nullptr) stream_names[i] = m[i].first.c_str();
            if (variable_names != nullptr) variable_names[i] = m[i].second.c_str();
        }
        size = count;
        return count;
    }

    int compN() const { return static_cast<int>(components_.size()); }
    const std::vector<std::string>& components() const { return components_; }
    double gain() const { return gain_; }
    double fixedX0() const {
        const auto it = fixed_values_.find("x0");
        return (it != fixed_values_.end()) ? it->second : 0.0;
    }

  private:
    void rebuildPortStrings() {
        if (!in_map_.empty() || !out_map_.empty()) return;
        in_map_.emplace_back("T", "x0");
        for (const std::string& c : components_) {
            out_map_.emplace_back("fi_" + c, "f_" + c);
        }
    }

    std::vector<std::pair<std::string, std::string>> in_map_;
    std::vector<std::pair<std::string, std::string>> out_map_;
    std::vector<std::string> components_;
    std::string slate_name_;
    std::map<std::string, double> fixed_values_;
    double gain_ = 1.0;
    bool slate_set_ = false;
    bool params_set_ = false;
    bool estimate_done_ = false;
};

GainProblem::GainProblem(const GainModel& model)
    : gain_(model.gain()), fixed_x0_(model.fixedX0()) {
    var_names_.push_back("x0");
    for (const std::string& c : model.components()) var_names_.push_back("f_" + c);
    for (const std::string& c : model.components()) con_names_.push_back("bal_" + c);
    con_names_.push_back("fix_x0");
    x0_.assign(var_names_.size(), 0.0);
    x0_[0] = fixed_x0_;
    for (size_t i = 1; i < x0_.size(); ++i) x0_[i] = gain_ * fixed_x0_;
    x_ = x0_;
}

GainModel* toModel(xOptModelHandle h) { return reinterpret_cast<GainModel*>(h); }
GainProblem* toProblem(xOptProblemHandle h) { return reinterpret_cast<GainProblem*>(h); }

// ---------------- xOptProblemT 回调 ----------------

int* problemDestroy(xOptProblemHandle handle) {
    delete toProblem(handle);
    return nullptr;
}
int problemNumVariables(xOptProblemHandle h) { return toProblem(h)->n(); }
int problemNumConstraints(xOptProblemHandle h) { return toProblem(h)->m(); }

int problemGetVariableNames(xOptProblemHandle h, const char* names[], int size) {
    auto* p = toProblem(h);
    if (size < p->n()) return -1;
    for (int i = 0; i < p->n(); ++i) names[i] = p->var_names_[i].c_str();
    return p->n();
}
int problemGetVariableDescriptions(xOptProblemHandle h, const char* d[], int size) {
    auto* p = toProblem(h);
    if (size < p->n()) return -1;
    for (int i = 0; i < p->n(); ++i) d[i] = "";
    return p->n();
}
int problemGetConstraintNames(xOptProblemHandle h, const char* names[], int size) {
    auto* p = toProblem(h);
    if (size < p->m()) return -1;
    for (int i = 0; i < p->m(); ++i) names[i] = p->con_names_[i].c_str();
    return p->m();
}
int problemGetOptions(xOptProblemHandle, double* options, int options_size) {
    if (options_size < xOptProblem::OPTIONS_LIMIT) return -1;
    options[xOptProblem::OPTIONS_MAGIC] = 'X';
    options[xOptProblem::HAS_DERIVATIVE] = 1;
    options[xOptProblem::HAS_LINEAR_A] = 0;
    options[xOptProblem::IS_SIMULATION] = 0;
    return 0;
}
int problemGetVariableBounds(xOptProblemHandle h, double* lo, double* hi, int size) {
    auto* p = toProblem(h);
    if (size < p->n()) return -1;
    for (int i = 0; i < p->n(); ++i) {
        lo[i] = -kBig;
        hi[i] = kBig;
    }
    return p->n();
}
int problemGetConstraintBounds(xOptProblemHandle h, double* lo, double* hi, int size) {
    auto* p = toProblem(h);
    if (size < p->m()) return -1;
    for (int i = 0; i < p->m(); ++i) {
        lo[i] = 0.0;
        hi[i] = 0.0;
    }
    return p->m();
}
int problemGetInitialX(xOptProblemHandle h, double* x, int size) {
    auto* p = toProblem(h);
    if (size < p->n()) return -1;
    for (int i = 0; i < p->n(); ++i) x[i] = p->x0_[i];
    return p->n();
}
int problemGetLinearConstraints(xOptProblemHandle, int*, int*, double*, int* size) {
    if (size != nullptr) *size = 0;  // 不走线性通道
    return 0;
}
int problemGetObjectiveGradientStructure(xOptProblemHandle, int*, int* size) {
    if (size != nullptr) *size = 0;  // 目标恒 0，梯度结构为空
    return 0;
}
int problemGetConstraintJacobianStructure(xOptProblemHandle h, int* row, int* col, int* nnz) {
    auto* p = toProblem(h);
    const int n = 2 * (p->n() - 1) + 1;
    if (row == nullptr || col == nullptr) {
        if (nnz != nullptr) *nnz = n;
        return 0;
    }
    if (nnz == nullptr || *nnz < n) return -1;
    int k = 0;
    for (int i = 0; i < p->n() - 1; ++i) {  // bal_<Ci>: d/dx0, d/df_i
        row[k] = i;
        col[k] = 0;
        ++k;
        row[k] = i;
        col[k] = 1 + i;
        ++k;
    }
    row[k] = p->m() - 1;  // fix_x0: d/dx0
    col[k] = 0;
    ++k;
    *nnz = k;
    return 0;
}
int problemSetX(xOptProblemHandle h, const double* x, int size) {
    auto* p = toProblem(h);
    if (size < p->n()) return -1;
    for (int i = 0; i < p->n(); ++i) p->x_[i] = x[i];
    return p->n();
}
int problemRunTimeCheck(xOptProblemHandle) { return 1; }
int problemEvaluateObjective(xOptProblemHandle, double* obj) {
    *obj = 0.0;
    return 0;
}
int problemEvaluateConstraints(xOptProblemHandle h, double* cons, int size) {
    auto* p = toProblem(h);
    if (size < p->m()) return -1;
    for (int i = 0; i < p->n() - 1; ++i) cons[i] = p->x_[1 + i] - p->gain_ * p->x_[0];
    cons[p->m() - 1] = p->x_[0] - p->fixed_x0_;
    return p->m();
}
int problemEvaluateObjectiveGradient(xOptProblemHandle, double*, int) { return 0; }
int problemEvaluateConstraintsJacobianValues(xOptProblemHandle h, double* v, int size) {
    auto* p = toProblem(h);
    const int n = 2 * (p->n() - 1) + 1;
    if (size < n) return -1;
    int k = 0;
    for (int i = 0; i < p->n() - 1; ++i) {
        v[k++] = -p->gain_;  // d bal_i / d x0
        v[k++] = 1.0;        // d bal_i / d f_i
    }
    v[k++] = 1.0;  // d fix_x0 / d x0
    return k;
}

// ---------------- xOptModelT 回调 ----------------

void modelDestroy(xOptModelHandle h) { delete toModel(h); }
const char* modelGetVersion(xOptModelHandle) { return "mock-1.0.0"; }
int modelGetNumberOfSlate(xOptModelHandle) { return 1; }
int modelGetSlateIdOfPort(xOptModelHandle, bool, int) { return 0; }
int modelSetSlate(xOptModelHandle h, int idx, const xOptSlate* s) {
    return toModel(h)->setSlate(idx, s);
}
int modelGetParameters(xOptModelHandle h, const char* names[], double values[], int& size) {
    return toModel(h)->getParameters(names, values, size);
}
int modelSetParameters(xOptModelHandle h, const char* names[], double values[], int size) {
    return toModel(h)->setParameters(names, values, size);
}
int modelGetFixableVariables(xOptModelHandle h, const char* names[], double values[], int& size) {
    return toModel(h)->getFixableVariables(names, values, size);
}
int modelValidate(xOptModelHandle h) { return toModel(h)->validateModel(); }
int modelGenerateEstimate(xOptModelHandle h, double initx[], int& size, const char names[],
                          const double values[], int fixed_size) {
    return toModel(h)->generateEstimate(initx, size, names, values, fixed_size);
}
int modelGetNumberOfThermoBlock(xOptModelHandle) { return 0; }
int modelGetInPortNum(xOptModelHandle h) { return toModel(h)->getInPortNum(); }
int modelGetOutPortNum(xOptModelHandle h) { return toModel(h)->getOutPortNum(); }
int modelGetInPortVariableMap(xOptModelHandle h, int port, const char* sn[], const char* vn[],
                              int& size) {
    return toModel(h)->getPortVariableMap(true, port, sn, vn, size);
}
int modelGetOutPortVariableMap(xOptModelHandle h, int port, const char* sn[], const char* vn[],
                               int& size) {
    return toModel(h)->getPortVariableMap(false, port, sn, vn, size);
}

int modelBuildProblem(xOptModelHandle handle, xOptProblemT* problem) {
    auto* model = toModel(handle);
    if (problem == nullptr || model->validate() < 0) return -1;
    auto* prob = new (std::nothrow) GainProblem(*model);
    if (prob == nullptr) return -1;
    problem->handle = reinterpret_cast<xOptProblemHandle>(prob);
    problem->destroyProblem = &problemDestroy;
    problem->numVariables = &problemNumVariables;
    problem->numConstraints = &problemNumConstraints;
    problem->getVariableNames = &problemGetVariableNames;
    problem->getVariableDescriptions = &problemGetVariableDescriptions;
    problem->getConstraintNames = &problemGetConstraintNames;
    problem->getOptions = &problemGetOptions;
    problem->getVariableBounds = &problemGetVariableBounds;
    problem->getConstraintBounds = &problemGetConstraintBounds;
    problem->getInitialX = &problemGetInitialX;
    problem->getLinearConstraints = &problemGetLinearConstraints;
    problem->getObjectiveGradientStructure = &problemGetObjectiveGradientStructure;
    problem->getConstraintJacobianStructure = &problemGetConstraintJacobianStructure;
    problem->setX = &problemSetX;
    problem->runTimeCheck = &problemRunTimeCheck;
    problem->evaluateObjective = &problemEvaluateObjective;
    problem->evaluateConstraints = &problemEvaluateConstraints;
    problem->evaluateObjectiveGradient = &problemEvaluateObjectiveGradient;
    problem->evaluateConstraintsJacobianValues = &problemEvaluateConstraintsJacobianValues;
    return 0;
}

}  // namespace

extern "C" XOPTIF_API int xOptModel_createModel(xOptModelT* model, xOptPlatformT*, const char*) {
    if (model == nullptr || model->size < offsetof(xOptModelT, handle) + sizeof(model->handle)) {
        return -1;
    }
    auto* impl = new (std::nothrow) GainModel();
    if (impl == nullptr) return -1;
    model->handle = reinterpret_cast<xOptModelHandle>(impl);
    model->destroyModel = &modelDestroy;
    model->getNumberOfSlate = &modelGetNumberOfSlate;
    model->getSlateIdOfPort = &modelGetSlateIdOfPort;
    model->setSlate = &modelSetSlate;
    model->getParameters = &modelGetParameters;
    model->setParameters = &modelSetParameters;
    model->getFixableVariables = &modelGetFixableVariables;
    model->validateModel = &modelValidate;
    model->buildProblem = &modelBuildProblem;
    model->generateEstimate = &modelGenerateEstimate;
    model->getNumberOfThermoBlock = &modelGetNumberOfThermoBlock;
    model->getInPortNum = &modelGetInPortNum;
    model->getOutPortNum = &modelGetOutPortNum;
    model->getInPortVariableMap = &modelGetInPortVariableMap;
    model->getOutPortVariableMap = &modelGetOutPortVariableMap;
    // 尾部扩展按 size 覆盖范围写（宿主可能用旧头编译，结构比我们的短）。
    if (model->size >= offsetof(xOptModelT, getVersion) + sizeof(model->getVersion)) {
        model->getVersion = &modelGetVersion;
    }
    return 0;
}
