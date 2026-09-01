// ***************************************************************
//  SplitterModel.cpp
//  -------------------------------------------------------------
//  测试黑箱单元模型：无热力学计算的 1 进 2 出分流器（Splitter）。
//
//  实现平台黑箱模型契约（include/xOpt/xOptModel.h）：
//    - 唯一导出  xOptModel_createModel，宿主分配 xOptModelT 并填 size，
//      本函数填 handle 与全部函数指针（尾部扩展字段按 size 覆盖范围写）。
//    - 返回码约定：<0 失败 / 0 正常 / >0 正常且带含义；
//      错误一律返回负值（XOPTF_ERROR_* 是正数，宿主按 ret<0 判错，禁用）。
//    - getParameters / getFixableVariables / 端口映射 / generateEstimate
//      均为两段式查询：输出指针为 nullptr 时只填 size。
//    - generateEstimate 的固定变量名以 '\0' 分隔打包在连续缓冲里。
//
//  模型方程（N = 组分个数，共 3N+6 个变量、6+4N 个约束）：
//    变量  in_T, in_P, in_fi_Ci | out1_T, out1_P, out1_fi_Ci |
//          out2_T, out2_P, out2_fi_Ci
//    等式  温度/压力直通（4 个）、分流平衡（2N 个）、进料固定（2+N 个）
//    不等式 out1_fi_Ci >= min_product_flow（N 个，供求解器罚函数通道演示）
// ***************************************************************

// 示例 DLL 是导出方：必须在包含 xOptModel.h 之前把 XOPTIF_API 定义为导出，
// 否则头文件里的声明是 __declspec(dllimport)，会与导出定义冲突。
#define XOPTIF_API __declspec(dllexport)

#include "xOpt/xOptModel.h"
#include "xOpt/xOptProblem.h"  // getOptions 需要 xOptProblem::OPTIONS 枚举

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr double kInf = 1e20;         // 流量上界 / 不等式约束上界
constexpr double kDefaultT = 300.0;   // K
constexpr double kDefaultP = 101.325; // kPa
constexpr double kDefaultFlow = 1.0;  // 流量默认初值

class SplitterModel;

// 变量种类：决定边界与默认初值
enum class VarKind { Temperature, Pressure, Flow };

// ***************************************************************
// SplitterProblem：buildProblem 产出的问题对象。
// 常量数据（名字、边界、雅可比结构）在构造时从模型复制一份，
// 运行迭代接口（evaluate*）都要求先调 setX。
// ***************************************************************
class SplitterProblem {
  public:
    explicit SplitterProblem(const SplitterModel& model);

    int n() const { return n_; }
    int m() const { return m_; }

    // 计算全部约束值（等式返回残差，不等式返回函数值本身）
    void evalConstraints(std::vector<double>& cons) const;

    int n_;                 // 变量个数 = 3N+6
    int m_;                 // 约束个数 = 6+4N
    int comp_n_;            // 组分个数 N
    double split_ratio_;    // 分流比 r
    double min_product_flow_;

    std::vector<std::string> var_names_;
    std::vector<std::string> var_descs_;
    std::vector<std::string> cons_names_;
    std::vector<double> xlow_, xupp_, x0_;
    std::vector<double> clow_, cupp_;
    std::vector<int> jac_rows_, jac_cols_;
    std::vector<double> jac_vals_;  // 雅可比为常量

    // 进料固定值（generateEstimate 通道缓存，未固定时取默认值）
    double fixed_t_, fixed_p_;
    std::vector<double> fixed_fi_;

    std::vector<double> x_;
    bool x_set_ = false;

    // ---- 变量/约束下标助手（布局见文件头注释）----
    int inT() const { return 0; }
    int inP() const { return 1; }
    int inFi(int i) const { return 2 + i; }
    int out1T() const { return 2 + comp_n_; }
    int out1P() const { return out1T() + 1; }
    int out1Fi(int i) const { return out1T() + 2 + i; }
    int out2T() const { return out1T() + 2 + comp_n_; }
    int out2P() const { return out2T() + 1; }
    int out2Fi(int i) const { return out2T() + 2 + i; }

    int cOut1T() const { return 0; }
    int cOut2T() const { return 1; }
    int cOut1P() const { return 2; }
    int cOut2P() const { return 3; }
    int cOut1Fi(int i) const { return 4 + i; }
    int cOut2Fi(int i) const { return 4 + comp_n_ + i; }
    int cInT() const { return 4 + 2 * comp_n_; }
    int cInP() const { return cInT() + 1; }
    int cInFi(int i) const { return cInT() + 2 + i; }
    int cIneq(int i) const { return 6 + 3 * comp_n_ + i; }
};

// ***************************************************************
// SplitterModel：模型对象本体，xOptModelT.handle 指向它。
// ***************************************************************
class SplitterModel {
  public:
    SplitterModel() = default;

    // ---- slate / 端口 ----
    int setSlate(int slate_index, const xOptSlate* slate) {
        if (slate_index != 0 || slate == nullptr) return -1;
        components_.clear();
        for (int i = 0; i < XOPT_MAX_COMPONENTS && slate->components[i] != nullptr; ++i) {
            components_.emplace_back(slate->components[i]);
        }
        if (components_.empty()) return -1;
        slate_set_ = true;
        rebuildPortMaps();
        return 0;
    }

    int compN() const { return static_cast<int>(components_.size()); }
    const std::vector<std::string>& components() const { return components_; }
    bool slateSet() const { return slate_set_; }
    double splitRatio() const { return split_ratio_; }
    double minProductFlow() const { return min_product_flow_; }

    // ---- 参数（数值通道）----
    static constexpr int kParamCount = 2;

    int getParameters(const char* names[], double values[], int& size) const {
        if (names == nullptr && values == nullptr) {  // 第一段：只报个数
            size = kParamCount;
            return 0;
        }
        if (size < kParamCount) return -1;
        names[0] = "split_ratio";
        names[1] = "min_product_flow";
        values[0] = split_ratio_;
        values[1] = min_product_flow_;
        size = kParamCount;
        return 0;
    }

    int setParameters(const char* names[], const double values[], int size) {
        for (int i = 0; i < size; ++i) {
            if (names[i] == nullptr) return -1;
            const std::string name(names[i]);
            if (name == "split_ratio") {
                split_ratio_ = values[i];
            } else if (name == "min_product_flow") {
                min_product_flow_ = values[i];
            } else {
                return -1;  // 不认识的参数：拒绝
            }
        }
        return 0;
    }

    // ---- fixable 机制：可固定的进料状态变量 ----
    int getFixableVariables(const char* names[], double values[], int& size) const {
        if (!slate_set_) return -1;
        const int count = 2 + compN();
        if (names == nullptr && values == nullptr) {  // 两段式查询
            size = count;
            return 0;
        }
        if (size < count) return -1;
        int k = 0;
        names[k] = "in_T";
        values[k] = fixedValue("in_T", kDefaultT);
        ++k;
        names[k] = "in_P";
        values[k] = fixedValue("in_P", kDefaultP);
        ++k;
        for (int i = 0; i < compN(); ++i) {
            names[k] = fixableFlowName(i).c_str();
            values[k] = fixedValue(fixableFlowName(i), kDefaultFlow);
            ++k;
        }
        size = count;
        return 0;
    }

    // generateEstimate：两段式；固定变量名以 '\0' 分隔打包。
    // 解析固定值并缓存，随后按固定值填初值（未固定变量给默认初值）。
    // （定义在 SplitterProblem 之后，因其初值逻辑复用问题对象。）
    int generateEstimate(double initx[], int& size, const char fixed_var_names[],
                         const double fixed_var_values[], int fixed_var_size);

    // ---- 端口映射（返回的字符串必须是模型成员，活得比调用久）----
    int inPortMap(int port, const char* stream_names[], const char* var_names[],
                  int& size) const {
        if (port != 0) return -1;
        return portMap(in_stream_names_, in_var_names_, stream_names, var_names, size);
    }

    int outPortMap(int port, const char* stream_names[], const char* var_names[],
                   int& size) const {
        const auto& streams = (port == 0) ? out1_stream_names_ : out2_stream_names_;
        const auto& vars = (port == 0) ? out1_var_names_ : out2_var_names_;
        if (port != 0 && port != 1) return -1;
        return portMap(streams, vars, stream_names, var_names, size);
    }

    int getInPortNum() const { return 1; }
    int getOutPortNum() const { return 2; }

    int validate() const {
        // 必须已设组分表，分流比在开区间 (0,1) 内
        if (!slate_set_) return -1;
        if (!(split_ratio_ > 0.0 && split_ratio_ < 1.0)) return -1;
        return 0;
    }

    // 进料固定值：已固定用固定值，否则用默认值
    double fixedValue(const std::string& name, double def) const {
        auto it = fixed_values_.find(name);
        return (it != fixed_values_.end()) ? it->second : def;
    }

  private:
    bool isFixableName(const std::string& name) const {
        if (name == "in_T" || name == "in_P") return true;
        for (int i = 0; i < compN(); ++i) {
            if (name == fixableFlowName(i)) return true;
        }
        return false;
    }

    const std::string& fixableFlowName(int i) const { return in_var_names_[2 + i]; }

    // setSlate 后重建端口映射与可固定变量名（"in_fi_<组分>"）
    void rebuildPortMaps() {
        in_stream_names_.assign({"T", "P"});
        in_var_names_.assign({"in_T", "in_P"});
        out1_stream_names_.assign({"T", "P"});
        out1_var_names_.assign({"out1_T", "out1_P"});
        out2_stream_names_.assign({"T", "P"});
        out2_var_names_.assign({"out2_T", "out2_P"});
        for (const auto& comp : components_) {
            const std::string fi = "fi_" + comp;
            in_stream_names_.push_back(fi);
            in_var_names_.push_back("in_" + fi);
            out1_stream_names_.push_back(fi);
            out1_var_names_.push_back("out1_" + fi);
            out2_stream_names_.push_back(fi);
            out2_var_names_.push_back("out2_" + fi);
        }
    }

    static int portMap(const std::vector<std::string>& streams,
                       const std::vector<std::string>& vars, const char* stream_names[],
                       const char* var_names[], int& size) {
        const int count = static_cast<int>(streams.size());
        if (stream_names == nullptr && var_names == nullptr) {  // 两段式查询
            size = count;
            return 0;
        }
        if (size < count) return -1;
        for (int i = 0; i < count; ++i) {
            stream_names[i] = streams[i].c_str();
            var_names[i] = vars[i].c_str();
        }
        size = count;
        return 0;
    }

    std::vector<std::string> components_;  // slate 组分表
    bool slate_set_ = false;
    double split_ratio_ = 0.5;       // 分流比，要求 0 < r < 1
    double min_product_flow_ = 0.0;  // 最小产品流量（产生不等式约束）
    std::map<std::string, double> fixed_values_;  // generateEstimate 缓存的固定值

    // 端口映射（字符串为成员，保证返回的 c_str() 寿命）
    std::vector<std::string> in_stream_names_, in_var_names_;
    std::vector<std::string> out1_stream_names_, out1_var_names_;
    std::vector<std::string> out2_stream_names_, out2_var_names_;
};

// ***************************************************************
// SplitterProblem 实现
// ***************************************************************
SplitterProblem::SplitterProblem(const SplitterModel& model) {
    comp_n_ = model.compN();
    split_ratio_ = model.splitRatio();
    min_product_flow_ = model.minProductFlow();
    n_ = 3 * comp_n_ + 6;
    m_ = 6 + 4 * comp_n_;

    // 进料固定值：未固定取默认初值
    fixed_t_ = model.fixedValue("in_T", kDefaultT);
    fixed_p_ = model.fixedValue("in_P", kDefaultP);
    fixed_fi_.resize(comp_n_);
    for (int i = 0; i < comp_n_; ++i) {
        fixed_fi_[i] = model.fixedValue("in_fi_" + model.components()[i], kDefaultFlow);
    }

    // ---- 变量表 ----
    auto addVar = [&](const std::string& name, const std::string& desc, VarKind kind) {
        var_names_.push_back(name);
        var_descs_.push_back(desc);
        switch (kind) {
            case VarKind::Temperature:
                xlow_.push_back(100.0);
                xupp_.push_back(1000.0);
                x0_.push_back(kDefaultT);
                break;
            case VarKind::Pressure:
                xlow_.push_back(1.0);
                xupp_.push_back(10000.0);
                x0_.push_back(kDefaultP);
                break;
            case VarKind::Flow:
                xlow_.push_back(0.0);
                xupp_.push_back(kInf);
                x0_.push_back(kDefaultFlow);
                break;
        }
    };
    addVar("in_T", "进料温度 [K]", VarKind::Temperature);
    addVar("in_P", "进料压力 [kPa]", VarKind::Pressure);
    for (int i = 0; i < comp_n_; ++i)
        addVar("in_fi_" + model.components()[i], "进料组分流量", VarKind::Flow);
    addVar("out1_T", "出料1温度 [K]", VarKind::Temperature);
    addVar("out1_P", "出料1压力 [kPa]", VarKind::Pressure);
    for (int i = 0; i < comp_n_; ++i)
        addVar("out1_fi_" + model.components()[i], "出料1组分流量", VarKind::Flow);
    addVar("out2_T", "出料2温度 [K]", VarKind::Temperature);
    addVar("out2_P", "出料2压力 [kPa]", VarKind::Pressure);
    for (int i = 0; i < comp_n_; ++i)
        addVar("out2_fi_" + model.components()[i], "出料2组分流量", VarKind::Flow);

    // 初值覆盖：被固定的进料变量用固定值
    x0_[inT()] = fixed_t_;
    x0_[inP()] = fixed_p_;
    for (int i = 0; i < comp_n_; ++i) x0_[inFi(i)] = fixed_fi_[i];

    // ---- 约束表 ----
    auto addEq = [&](const std::string& name) {
        cons_names_.push_back(name);
        clow_.push_back(0.0);
        cupp_.push_back(0.0);
    };
    addEq("out1_T - in_T");
    addEq("out2_T - in_T");
    addEq("out1_P - in_P");
    addEq("out2_P - in_P");
    for (int i = 0; i < comp_n_; ++i)
        addEq("out1_fi - r*in_fi [" + std::to_string(i) + "]");
    for (int i = 0; i < comp_n_; ++i)
        addEq("out2_fi - (1-r)*in_fi [" + std::to_string(i) + "]");
    addEq("in_T fixed");
    addEq("in_P fixed");
    for (int i = 0; i < comp_n_; ++i)
        addEq("in_fi fixed [" + std::to_string(i) + "]");
    for (int i = 0; i < comp_n_; ++i) {  // 不等式：out1_fi >= min_product_flow
        cons_names_.push_back("out1_fi >= mpf [" + std::to_string(i) + "]");
        clow_.push_back(min_product_flow_);
        cupp_.push_back(kInf);
    }

    // ---- 雅可比（常量）：同时作为线性约束 A 矩阵 ----
    auto addJac = [&](int row, int col, double val) {
        jac_rows_.push_back(row);
        jac_cols_.push_back(col);
        jac_vals_.push_back(val);
    };
    addJac(cOut1T(), out1T(), 1.0);
    addJac(cOut1T(), inT(), -1.0);
    addJac(cOut2T(), out2T(), 1.0);
    addJac(cOut2T(), inT(), -1.0);
    addJac(cOut1P(), out1P(), 1.0);
    addJac(cOut1P(), inP(), -1.0);
    addJac(cOut2P(), out2P(), 1.0);
    addJac(cOut2P(), inP(), -1.0);
    for (int i = 0; i < comp_n_; ++i) {
        addJac(cOut1Fi(i), out1Fi(i), 1.0);
        addJac(cOut1Fi(i), inFi(i), -split_ratio_);
        addJac(cOut2Fi(i), out2Fi(i), 1.0);
        addJac(cOut2Fi(i), inFi(i), -(1.0 - split_ratio_));
    }
    addJac(cInT(), inT(), 1.0);
    addJac(cInP(), inP(), 1.0);
    for (int i = 0; i < comp_n_; ++i) {
        addJac(cInFi(i), inFi(i), 1.0);
        addJac(cIneq(i), out1Fi(i), 1.0);
    }

    x_.assign(n_, 0.0);
}

void SplitterProblem::evalConstraints(std::vector<double>& cons) const {
    cons.assign(m_, 0.0);
    // 温度/压力直通：出料 = 进料（残差形式）
    cons[cOut1T()] = x_[out1T()] - x_[inT()];
    cons[cOut2T()] = x_[out2T()] - x_[inT()];
    cons[cOut1P()] = x_[out1P()] - x_[inP()];
    cons[cOut2P()] = x_[out2P()] - x_[inP()];
    // 分流物料平衡
    for (int i = 0; i < comp_n_; ++i) {
        cons[cOut1Fi(i)] = x_[out1Fi(i)] - split_ratio_ * x_[inFi(i)];
        cons[cOut2Fi(i)] = x_[out2Fi(i)] - (1.0 - split_ratio_) * x_[inFi(i)];
    }
    // 进料固定（等价于宿主 xOptModelFixVars 追加的等式）
    cons[cInT()] = x_[inT()] - fixed_t_;
    cons[cInP()] = x_[inP()] - fixed_p_;
    for (int i = 0; i < comp_n_; ++i) cons[cInFi(i)] = x_[inFi(i)] - fixed_fi_[i];
    // 不等式：返回约束函数值本身，由 [clow, cupp] 判定
    for (int i = 0; i < comp_n_; ++i) cons[cIneq(i)] = x_[out1Fi(i)];
}

int SplitterModel::generateEstimate(double initx[], int& size, const char fixed_var_names[],
                                    const double fixed_var_values[], int fixed_var_size) {
    if (!slate_set_) return -1;
    const int n = 3 * compN() + 6;
    if (initx == nullptr) {  // 第一段：只报个数
        size = n;
        return 0;
    }
    if (size != n) return -1;
    // 解包 "name1\0name2\0..."，与 fixed_var_values 一一对应
    const char* p = fixed_var_names;
    for (int i = 0; i < fixed_var_size; ++i) {
        if (p == nullptr) return -1;
        const std::string name(p);
        if (!isFixableName(name)) return -1;  // 固定了不允许的变量
        fixed_values_[name] = fixed_var_values[i];
        p += name.size() + 1;
    }
    // 按固定值 + 默认初值填 initx（复用问题的初值逻辑）
    const SplitterProblem probe(*this);
    for (int j = 0; j < n; ++j) initx[j] = probe.x0_[j];
    return 0;
}

// ***************************************************************
// xOptProblemT 回调（静态函数，handle 指向 SplitterProblem）
// 注意：结构查询均为两段式；evaluate* 之前必须先 setX。
// ***************************************************************
SplitterProblem* toProblem(xOptProblemHandle handle) {
    return reinterpret_cast<SplitterProblem*>(handle);
}

int* problemDestroy(xOptProblemHandle handle) {
    delete toProblem(handle);
    return nullptr;
}

int problemNumVariables(xOptProblemHandle handle) { return toProblem(handle)->n(); }

int problemNumConstraints(xOptProblemHandle handle) { return toProblem(handle)->m(); }

int problemGetVariableNames(xOptProblemHandle handle, const char* names[], int names_size) {
    auto* p = toProblem(handle);
    const int count = std::min(names_size, p->n());
    for (int i = 0; i < count; ++i) names[i] = p->var_names_[i].c_str();
    return count;  // 返回填充个数
}

int problemGetVariableDescriptions(xOptProblemHandle handle, const char* descriptions[],
                                   int descriptions_size) {
    auto* p = toProblem(handle);
    const int count = std::min(descriptions_size, p->n());
    for (int i = 0; i < count; ++i) descriptions[i] = p->var_descs_[i].c_str();
    return count;
}

int problemGetConstraintNames(xOptProblemHandle handle, const char* names[], int names_size) {
    auto* p = toProblem(handle);
    const int count = std::min(names_size, p->m());
    for (int i = 0; i < count; ++i) names[i] = p->cons_names_[i].c_str();
    return count;
}

int problemGetOptions(xOptProblemHandle handle, double* options, int options_size) {
    (void)handle;
    if (options_size < xOptProblem::OPTIONS_LIMIT) return -1;
    options[xOptProblem::OPTIONS_MAGIC] = 'X';
    options[xOptProblem::HAS_DERIVATIVE] = 1;  // 手工提供常量雅可比
    options[xOptProblem::HAS_LINEAR_A] = 1;    // 全部约束都是线性，提供完整 A
    options[xOptProblem::IS_SIMULATION] = 0;
    return 0;
}

int problemGetVariableBounds(xOptProblemHandle handle, double* xlow, double* xupp,
                             int x_size) {
    auto* p = toProblem(handle);
    if (x_size < p->n()) return -1;
    for (int i = 0; i < p->n(); ++i) {
        xlow[i] = p->xlow_[i];
        xupp[i] = p->xupp_[i];
    }
    return p->n();
}

int problemGetConstraintBounds(xOptProblemHandle handle, double* clow, double* cupp,
                               int c_size) {
    auto* p = toProblem(handle);
    if (c_size < p->m()) return -1;
    for (int i = 0; i < p->m(); ++i) {
        clow[i] = p->clow_[i];
        cupp[i] = p->cupp_[i];
    }
    return p->m();
}

int problemGetInitialX(xOptProblemHandle handle, double* x0, int x0_size) {
    auto* p = toProblem(handle);
    if (x0_size < p->n()) return -1;
    for (int i = 0; i < p->n(); ++i) x0[i] = p->x0_[i];
    return p->n();
}

// 线性约束 A 矩阵：本模型全部约束均为线性，A 即完整雅可比。
// 行下标是全局约束下标（含不等式行）。两段式：任一输出指针为空只报个数。
int problemGetLinearConstraints(xOptProblemHandle handle, int* lcons_rowidx,
                                int* lcons_colidx, double* values, int* lcons_size) {
    auto* p = toProblem(handle);
    const int nnz = static_cast<int>(p->jac_vals_.size());
    if (lcons_rowidx == nullptr || lcons_colidx == nullptr || values == nullptr) {
        *lcons_size = nnz;
        return 0;
    }
    if (*lcons_size < nnz) return -1;
    for (int i = 0; i < nnz; ++i) {
        lcons_rowidx[i] = p->jac_rows_[i];
        lcons_colidx[i] = p->jac_cols_[i];
        values[i] = p->jac_vals_[i];
    }
    *lcons_size = nnz;
    return 0;
}

// 目标恒为 0：梯度结构为空
int problemGetObjectiveGradientStructure(xOptProblemHandle handle, int* obj_colidx,
                                         int* obj_colidx_size) {
    (void)handle;
    (void)obj_colidx;
    *obj_colidx_size = 0;
    return 0;
}

int problemGetConstraintJacobianStructure(xOptProblemHandle handle, int* cons_rowidx,
                                          int* cons_colidx, int* nnz) {
    auto* p = toProblem(handle);
    const int count = static_cast<int>(p->jac_rows_.size());
    if (cons_rowidx == nullptr || cons_colidx == nullptr) {  // 两段式查询
        *nnz = count;
        return 0;
    }
    if (*nnz < count) return -1;
    for (int i = 0; i < count; ++i) {
        cons_rowidx[i] = p->jac_rows_[i];
        cons_colidx[i] = p->jac_cols_[i];
    }
    *nnz = count;
    return 0;
}

int problemSetX(xOptProblemHandle handle, const double* x, int x_size) {
    auto* p = toProblem(handle);
    if (x_size < p->n()) return -1;
    for (int i = 0; i < p->n(); ++i) p->x_[i] = x[i];
    p->x_set_ = true;
    return 0;
}

int problemRunTimeCheck(xOptProblemHandle handle) {
    return toProblem(handle)->x_set_ ? 0 : -1;  // 未 setX 的点无效
}

int problemEvaluateObjective(xOptProblemHandle handle, double* obj) {
    *obj = 0.0;  // 可行性问题
    (void)handle;
    return 0;
}

int problemEvaluateConstraints(xOptProblemHandle handle, double* cons, int cons_size) {
    auto* p = toProblem(handle);
    if (!p->x_set_ || cons_size < p->m()) return -1;
    std::vector<double> tmp;
    p->evalConstraints(tmp);
    for (int i = 0; i < p->m(); ++i) cons[i] = tmp[i];
    return p->m();
}

int problemEvaluateObjectiveGradient(xOptProblemHandle handle, double* grad, int grad_size) {
    (void)handle;
    if (grad_size != 0) return -1;  // 结构大小为 0，无梯度可填
    (void)grad;
    return 0;
}

int problemEvaluateConstraintsJacobianValues(xOptProblemHandle handle, double* values,
                                             int values_size) {
    auto* p = toProblem(handle);
    const int nnz = static_cast<int>(p->jac_vals_.size());
    if (values_size < nnz) return -1;
    for (int i = 0; i < nnz; ++i) values[i] = p->jac_vals_[i];  // 常量
    return nnz;
}

// ***************************************************************
// xOptModelT 回调（静态函数，handle 指向 SplitterModel）
// ***************************************************************
SplitterModel* toModel(xOptModelHandle handle) {
    return reinterpret_cast<SplitterModel*>(handle);
}

void modelDestroy(xOptModelHandle handle) { delete toModel(handle); }

const char* modelGetVersion(xOptModelHandle handle) {
    (void)handle;
    return "v1.0.0";
}

int modelSetLanguage(xOptModelHandle handle, const char* language_code) {
    (void)handle;
    (void)language_code;
    return 0;  // 示例不区分语言
}

int modelGetNumberOfSlate(xOptModelHandle handle) {
    (void)handle;
    return 1;  // 三端口共用同一 slate
}

int modelGetSlateIdOfPort(xOptModelHandle handle, bool is_input_port, int port_index) {
    (void)handle;
    (void)is_input_port;
    (void)port_index;
    return 0;
}

int modelSetSlate(xOptModelHandle handle, int slate_index, const xOptSlate* slate) {
    return toModel(handle)->setSlate(slate_index, slate);
}

int modelGetParameters(xOptModelHandle handle, const char* names[], double values[],
                       int& size) {
    return toModel(handle)->getParameters(names, values, size);
}

int modelSetParameters(xOptModelHandle handle, const char* names[], double values[],
                       int size) {
    return toModel(handle)->setParameters(names, values, size);
}

int modelSetProblemType(xOptModelHandle handle, XOPTF_PROBLEM_TYPE type) {
    (void)handle;
    (void)type;
    return 0;
}

int modelGetFixableVariables(xOptModelHandle handle, const char* names[],
                             double initial_values[], int& size) {
    return toModel(handle)->getFixableVariables(names, initial_values, size);
}

int modelGetInPortNum(xOptModelHandle handle) { return toModel(handle)->getInPortNum(); }

int modelGetOutPortNum(xOptModelHandle handle) { return toModel(handle)->getOutPortNum(); }

int modelGetInPortVariableMap(xOptModelHandle handle, int port_index,
                              const char* stream_names[], const char* variable_names[],
                              int& size) {
    return toModel(handle)->inPortMap(port_index, stream_names, variable_names, size);
}

int modelGetOutPortVariableMap(xOptModelHandle handle, int port_index,
                               const char* stream_names[], const char* variable_names[],
                               int& size) {
    return toModel(handle)->outPortMap(port_index, stream_names, variable_names, size);
}

int modelValidateModel(xOptModelHandle handle) { return toModel(handle)->validate(); }

int modelBuildProblem(xOptModelHandle handle, xOptProblemT* problem) {
    auto* model = toModel(handle);
    if (problem == nullptr || model->validate() < 0) return -1;
    auto* prob = new (std::nothrow) SplitterProblem(*model);
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

int modelGenerateEstimate(xOptModelHandle handle, double initx[], int& size,
                          const char fixed_var_names[], const double fixed_var_values[],
                          int fixed_var_size) {
    return toModel(handle)->generateEstimate(initx, size, fixed_var_names,
                                             fixed_var_values, fixed_var_size);
}

int modelGetReportMetaAbstracts(xOptModelHandle handle, const char* names[],
                                const char* titles[], const char* descriptions[],
                                const char* preferred_display_types[], int dim_size[],
                                int& size) {
    (void)handle;
    (void)names;
    (void)titles;
    (void)descriptions;
    (void)preferred_display_types;
    (void)dim_size;
    size = 0;  // 示例无报表输出，返回 0 项合法
    return 0;
}

int modelGetReportMetaDims(xOptModelHandle handle, const char* dim_names[],
                           const char* dim_units[], const char* name, int dim_size) {
    (void)handle;
    (void)dim_names;
    (void)dim_units;
    (void)name;
    (void)dim_size;
    return -1;  // 无报表元数据，不支持
}

int modelGetReportData(xOptModelHandle handle, double data[], int shape[],
                       const char* name, int& data_size, int& shape_size) {
    (void)handle;
    (void)data;
    (void)shape;
    (void)name;
    (void)data_size;
    (void)shape_size;
    return -1;  // 无报表数据，不支持
}

int modelGetNumberOfThermoBlock(xOptModelHandle handle) {
    (void)handle;
    return 0;  // 无热力学
}

int modelGetThermoBlocks(xOptModelHandle handle, int count, xOptThermoBlock blocks[]) {
    (void)handle;
    (void)count;
    (void)blocks;
    return 0;
}

// 尾部扩展字段：只有宿主分配的 size 覆盖到该字段才写入，
// 否则会越过宿主的分配写坏内存。
#define FILL_TAIL_FIELD(model, field, fn)                                          \
    do {                                                                           \
        if ((model)->size >= offsetof(xOptModelT, field) + sizeof((model)->field)) \
            (model)->field = (fn);                                                 \
    } while (0)

}  // namespace

// ***************************************************************
// 唯一导出：宿主只 GetProcAddress("xOptModel_createModel")。
// 宿主分配 xOptModelT 并已填 size = sizeof(xOptModelT)。
// ***************************************************************
extern "C" __declspec(dllexport) int xOptModel_createModel(xOptModelT* model,
                                                           xOptPlatformT* platform,
                                                           const char* name) {
    (void)platform;  // 本模型不需要热力学服务
    (void)name;
    if (model == nullptr || model->size < offsetof(xOptModelT, handle) + sizeof(model->handle))
        return -1;

    model->handle = reinterpret_cast<xOptModelHandle>(new (std::nothrow) SplitterModel());
    if (model->handle == nullptr) return -1;

    model->destroyModel = &modelDestroy;
    model->setLanguage = &modelSetLanguage;
    model->getNumberOfSlate = &modelGetNumberOfSlate;
    model->getSlateIdOfPort = &modelGetSlateIdOfPort;
    model->setSlate = &modelSetSlate;
    model->getParameters = &modelGetParameters;
    model->setParameters = &modelSetParameters;
    model->setProblemType = &modelSetProblemType;
    model->getFixableVariables = &modelGetFixableVariables;
    model->getInPortNum = &modelGetInPortNum;
    model->getOutPortNum = &modelGetOutPortNum;
    model->getInPortVariableMap = &modelGetInPortVariableMap;
    model->getOutPortVariableMap = &modelGetOutPortVariableMap;
    model->validateModel = &modelValidateModel;
    model->buildProblem = &modelBuildProblem;
    model->generateEstimate = &modelGenerateEstimate;
    model->getReportMetaAbstracts = &modelGetReportMetaAbstracts;
    model->getReportMetaDims = &modelGetReportMetaDims;
    model->getReportData = &modelGetReportData;
    model->getNumberOfThermoBlock = &modelGetNumberOfThermoBlock;
    model->getThermoBlocks = &modelGetThermoBlocks;
    FILL_TAIL_FIELD(model, getVersion, &modelGetVersion);
    // JSON 参数通道成对置 NULL：走数值通道，同时验证宿主的判空回退路径
    FILL_TAIL_FIELD(model, setParametersJson, nullptr);
    FILL_TAIL_FIELD(model, getParametersJson, nullptr);
    return 0;
}
