// ***************************************************************
//  XOptMINLPAdapter   version:  2.0   -  date:  2026/06/17
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "XOptMINLPAdapter.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// 内部：屏蔽 C++ ABI(xOptProblem*) 与 C ABI(xOptProblemT) 差异。
// 方法签名采用 xOptProblem 的 C++ 风格（结构尺寸用 int&，目标值用 double&），
// C ABI 实现内部转换为指针。
struct IXOptProblemView {
    virtual ~IXOptProblemView() = default;
    virtual int initialize() = 0;
    virtual int numVariables() = 0;
    virtual int numConstraints() = 0;
    virtual int getVariableNames(const char* names[], int n) = 0;
    virtual int getConstraintNames(const char* names[], int n) = 0;
    virtual int getVariableBounds(double* lo, double* hi, int n) = 0;
    virtual int getConstraintBounds(double* lo, double* hi, int n) = 0;
    virtual int getInitialX(double* x, int n) = 0;
    virtual int getLinearConstraints(int* r, int* c, double* v, int& size) = 0;
    virtual int getObjectiveGradientStructure(int* col, int& size) = 0;
    virtual int getConstraintJacobianStructure(int* row, int* col, int& nnz) = 0;
    virtual int setX(const double* x, int n) = 0;
    virtual int evaluateObjective(double& obj) = 0;
    virtual int evaluateConstraints(double* cons, int n) = 0;
    virtual int evaluateObjectiveGradient(double* grad, int n) = 0;
    virtual int evaluateConstraintsJacobianValues(double* v, int n) = 0;
};

namespace {

#ifdef _WIN32
void* loadLib(const std::string& path) { return reinterpret_cast<void*>(LoadLibraryA(path.c_str())); }
void* getSym(void* m, const char* n) {
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(m), n));
}
void closeLib(void* m) { FreeLibrary(reinterpret_cast<HMODULE>(m)); }
#else
void* loadLib(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* getSym(void* m, const char* n) { return dlsym(m, n); }
void closeLib(void* m) { dlclose(m); }
#endif

template <class T>
void pick(const std::vector<T>& all, const std::vector<int>& ids, std::vector<T>& out) {
    if (ids.empty()) {
        out = all;
        return;
    }
    out.clear();
    out.reserve(ids.size());
    for (int id : ids) out.push_back((id >= 0 && id < (int)all.size()) ? all[id] : T{});
}

// —— C++ ABI 视图：xOptProblem* ——
class CppProblemView : public IXOptProblemView {
  public:
    CppProblemView(xOptProblem* p, destroyProblemFunc destroy, bool own)
        : p_(p), destroy_(destroy), own_(own) {}
    ~CppProblemView() override {
        if (own_ && destroy_ && p_) destroy_(p_);
    }
    int initialize() override { return p_->initialize(); }
    int numVariables() override { return p_->numVariables(); }
    int numConstraints() override { return p_->numConstraints(); }
    int getVariableNames(const char* names[], int n) override { return p_->getVariableNames(names, n); }
    int getConstraintNames(const char* names[], int n) override {
        return p_->getConstraintNames(names, n);
    }
    int getVariableBounds(double* lo, double* hi, int n) override {
        return p_->getVariableBounds(lo, hi, n);
    }
    int getConstraintBounds(double* lo, double* hi, int n) override {
        return p_->getConstraintBounds(lo, hi, n);
    }
    int getInitialX(double* x, int n) override { return p_->getInitialX(x, n); }
    int getLinearConstraints(int* r, int* c, double* v, int& size) override {
        return p_->getLinearConstraints(r, c, v, size);
    }
    int getObjectiveGradientStructure(int* col, int& size) override {
        return p_->getObjectiveGradientStructure(col, size);
    }
    int getConstraintJacobianStructure(int* row, int* col, int& nnz) override {
        return p_->getConstraintJacobianStructure(row, col, nnz);
    }
    int setX(const double* x, int n) override { return p_->setX(x, n); }
    int evaluateObjective(double& obj) override { return p_->evaluateObjective(obj); }
    int evaluateConstraints(double* cons, int n) override { return p_->evaluateConstraints(cons, n); }
    int evaluateObjectiveGradient(double* grad, int n) override {
        return p_->evaluateObjectiveGradient(grad, n);
    }
    int evaluateConstraintsJacobianValues(double* v, int n) override {
        return p_->evaluateConstraintsJacobianValues(v, n);
    }

  private:
    xOptProblem* p_;
    destroyProblemFunc destroy_;
    bool own_;
};

// —— C ABI 视图：xOptProblemT vtable（+ 可选 xOptModelT 用于回收）——
class CapiProblemView : public IXOptProblemView {
  public:
    CapiProblemView(const xOptProblemT& pt, bool own_problem, const xOptModelT& mt, bool own_model)
        : pt_(pt), own_problem_(own_problem), mt_(mt), own_model_(own_model) {}
    ~CapiProblemView() override {
        if (own_problem_ && pt_.handle && pt_.destroyProblem) pt_.destroyProblem(pt_.handle);
        if (own_model_ && mt_.handle && mt_.destroyModel) mt_.destroyModel(mt_.handle);
    }
    int initialize() override { return 0; }  // C ABI：buildProblem 已完成构造/初始化
    int numVariables() override { return pt_.numVariables(pt_.handle); }
    int numConstraints() override { return pt_.numConstraints(pt_.handle); }
    int getVariableNames(const char* names[], int n) override {
        return pt_.getVariableNames(pt_.handle, names, n);
    }
    int getConstraintNames(const char* names[], int n) override {
        return pt_.getConstraintNames(pt_.handle, names, n);
    }
    int getVariableBounds(double* lo, double* hi, int n) override {
        return pt_.getVariableBounds(pt_.handle, lo, hi, n);
    }
    int getConstraintBounds(double* lo, double* hi, int n) override {
        return pt_.getConstraintBounds(pt_.handle, lo, hi, n);
    }
    int getInitialX(double* x, int n) override { return pt_.getInitialX(pt_.handle, x, n); }
    int getLinearConstraints(int* r, int* c, double* v, int& size) override {
        int s = size;
        int rc = pt_.getLinearConstraints(pt_.handle, r, c, v, &s);
        size = s;
        return rc;
    }
    int getObjectiveGradientStructure(int* col, int& size) override {
        int s = size;
        int rc = pt_.getObjectiveGradientStructure(pt_.handle, col, &s);
        size = s;
        return rc;
    }
    int getConstraintJacobianStructure(int* row, int* col, int& nnz) override {
        int n = nnz;
        int rc = pt_.getConstraintJacobianStructure(pt_.handle, row, col, &n);
        nnz = n;
        return rc;
    }
    int setX(const double* x, int n) override { return pt_.setX(pt_.handle, x, n); }
    int evaluateObjective(double& obj) override {
        double o = 0;
        int rc = pt_.evaluateObjective(pt_.handle, &o);
        obj = o;
        return rc;
    }
    int evaluateConstraints(double* cons, int n) override {
        return pt_.evaluateConstraints(pt_.handle, cons, n);
    }
    int evaluateObjectiveGradient(double* grad, int n) override {
        return pt_.evaluateObjectiveGradient(pt_.handle, grad, n);
    }
    int evaluateConstraintsJacobianValues(double* v, int n) override {
        return pt_.evaluateConstraintsJacobianValues(pt_.handle, v, n);
    }

  private:
    xOptProblemT pt_;
    bool own_problem_;
    xOptModelT mt_;
    bool own_model_;
};

}  // namespace

XOptMINLPAdapter::XOptMINLPAdapter(const std::string& dll_path) : dll_path_(dll_path) {}
XOptMINLPAdapter::XOptMINLPAdapter(const std::string& dll_path, const std::string& desc_path)
    : dll_path_(dll_path), desc_path_(desc_path) {}
XOptMINLPAdapter::XOptMINLPAdapter(xOptProblem* injected, bool initialize_problem)
    : inject_cpp_(injected), initialize_injected_(initialize_problem) {}
XOptMINLPAdapter::XOptMINLPAdapter(const xOptProblemT& injected)
    : have_capi_inject_(true), inject_capi_(injected) {}

XOptMINLPAdapter::~XOptMINLPAdapter() { disconnect(); }

int XOptMINLPAdapter::fail(const std::string& msg) const {
    last_error_ = msg;
    return -1;
}

// ***************************************************************
//  initModel —— C ABI 的模型初始化序列（宿主 xOptModelBlackBox 的等价物）
//  -------------------------------------------------------------
//  createModel 与 buildProblem 之间，宿主还要跑这一段：
//      getParameters -> setParameters（回送）
//      setSlate（每个 slate 一次）
//      validateModel
//      getFixableVariables -> generateEstimate（固定值按 '\0' 分隔打包）
//  先前这里是 create 完直接 buildProblem，于是凡是"必须先 setSlate"的模型
//  一律在 buildProblem 上失败，而失败信息只说 buildProblem 失败，指不到病灶。
//
//  每一步都判空跳过：xOptModelT 的函数指针是可选的，模型只实现自己需要的那些。
//  但"描述里配了、模型却用不上"不跳过而是报错——静默丢弃会让
//  "我明明配了 split_ratio=0.7"变成模型照跑默认值，且全程没有一句提示。
// ***************************************************************
int XOptMINLPAdapter::initModel(xOptModelT& model) {
    // ---- 0) 描述 JSON：显式路径优先，否则在 DLL 同目录自动发现 ----
    std::string err;
    std::string path = desc_path_;
    if (path.empty()) {
        if (!XOptModelDesc::discover(dll_path_, path, err)) return fail("connect: " + err);
    }
    if (!path.empty()) {
        if (!XOptModelDesc::load(path, desc_, err)) return fail("connect: " + err);
        desc_source_ = path;
    }
    // 外部推下来的固定变量集合压过描述文件。空表也算数——那是"一个都不固定"。
    if (has_fixed_override_) {
        desc_.fixable_variables = fixed_names_override_;
        desc_.fixed_values.clear();
        for (size_t i = 0; i < fixed_names_override_.size() &&
                           i < fixed_values_override_.size(); ++i) {
            desc_.fixed_values[fixed_names_override_[i]] = fixed_values_override_[i];
        }
    }

    // 外部推下来的组分表压过描述文件。描述文件里的其余内容（参数、可固定
    // 变量的选择）仍然有效——变的只是组分。
    if (!components_override_.empty()) {
        desc_.components = components_override_;
        if (desc_source_.empty()) desc_source_ = "the pushed component list";
    }

    // 报错信息里指认描述来源。下面几处 from 只在"描述里配了什么"时才用得上，
    // 那时 desc_source_ 必然非空；留一个兜底措辞是为了别在将来改动时漏出空引号。
    const std::string from =
        desc_source_.empty() ? std::string("the model description") : "'" + desc_source_ + "'";

    // ---- 1) 参数：两段式查询 -> 用描述覆盖 -> 回送 ----
    // 回送是宿主的行为，不是可选的礼貌：有的模型把 setParameters 当作
    // "参数已定"的信号，不回送就停在未初始化状态。
    if (model.getParameters != nullptr && model.setParameters != nullptr) {
        int n = 0;
        if (model.getParameters(model.handle, nullptr, nullptr, n) < 0) {
            return fail("connect: getParameters(size) failed");
        }
        if (n > 0) {
            std::vector<const char*> names(n, nullptr);
            std::vector<double> values(n, 0.0);
            if (model.getParameters(model.handle, names.data(), values.data(), n) < 0) {
                return fail("connect: getParameters(fill) failed");
            }
            for (const auto& kv : desc_.parameters) {
                bool hit = false;
                for (int i = 0; i < n; ++i) {
                    if (names[i] != nullptr && kv.first == names[i]) {
                        values[i] = kv.second;
                        hit = true;
                        break;
                    }
                }
                if (!hit) {
                    return fail("connect: parameter '" + kv.first + "' from " + from +
                                " is not offered by the model");
                }
            }
            if (model.setParameters(model.handle, names.data(), values.data(), n) < 0) {
                return fail("connect: setParameters failed (rejected by the model)");
            }
        } else if (!desc_.parameters.empty()) {
            return fail("connect: the model offers no parameters, but " + from + " sets " +
                        std::to_string(desc_.parameters.size()));
        }
    } else if (!desc_.parameters.empty()) {
        return fail("connect: the model exports no get/setParameters, but " + from +
                    " sets parameters");
    }

    // ---- 2) slate（组分表）----
    const int slate_count =
        (model.getNumberOfSlate != nullptr) ? model.getNumberOfSlate(model.handle) : 0;
    if (slate_count > 0) {
        if (model.setSlate == nullptr) {
            return fail("connect: the model reports " + std::to_string(slate_count) +
                        " slate(s) but exports no setSlate");
        }
        if (desc_.components.empty()) {
            // 这一条是最常撞上的失败，措辞要能直接指出下一步做什么：
            // 是压根没找到描述文件，还是找到了但推不出组分。
            const std::string why =
                desc_source_.empty()
                    ? "no *_Model.json was found next to '" + dll_path_ + "'"
                    : "'" + desc_source_ + "' yields no components";
            return fail("connect: the model needs a component slate (" +
                        std::to_string(slate_count) + " slate(s)) but " + why +
                        "; give \"components\", or port maps whose stream names are fi_<component>");
        }
        // 上限留一格给 NULL 结尾：xOptSlate::components 是定长 XOPT_MAX_COMPONENTS
        // 数组，靠 NULL 结尾表示"到此为止"（demo 与 SplitterModel::setSlate 都按
        // 这个读）。填满就没法结尾，遇到不带边界扫描的模型会读出界。
        if (static_cast<int>(desc_.components.size()) > XOPT_MAX_COMPONENTS - 1) {
            return fail("connect: " + std::to_string(desc_.components.size()) +
                        " components exceed the slate capacity (" +
                        std::to_string(XOPT_MAX_COMPONENTS - 1) + ", one slot kept for the NULL "
                        "terminator)");
        }
        xOptSlate slate = {};  // 其余项清零 = NULL 结尾
        slate.name = desc_.slate_name.c_str();
        slate.thermo_method = desc_.thermo_method.empty() ? nullptr : desc_.thermo_method.c_str();
        for (size_t i = 0; i < desc_.components.size(); ++i) {
            slate.components[i] = desc_.components[i].c_str();
        }
        for (int si = 0; si < slate_count; ++si) {
            if (model.setSlate(model.handle, si, &slate) < 0) {
                // 把组分表本身带上：C ABI 的 setSlate 只会返回 -1，模型说不出
                // 是哪一个组分不行。被拒时唯一还能提供的线索，就是"推的是这一套"。
                std::string list;
                for (const std::string& c : desc_.components) {
                    list += (list.empty() ? "" : ", ") + c;
                }
                return fail("connect: setSlate(" + std::to_string(si) +
                            ") failed; the model rejected the component list [" + list + "]");
            }
        }
    }

    // ---- 3) 校验 ----
    if (model.validateModel != nullptr && model.validateModel(model.handle) < 0) {
        return fail("connect: validateModel failed; check the parameters/slate (" +
                    (desc_source_.empty() ? std::string("no model description was used")
                                          : "from '" + desc_source_ + "'") +
                    ")");
    }

    // ---- 3.5) 端口接线信息 ----
    // 放在 validateModel 之后：端口映射依赖组分表（Splitter 是在 setSlate 里
    // rebuildPortMaps 的），校验通过才谈得上映射稳定。
    {
        const int n_in = (model.getInPortNum != nullptr) ? model.getInPortNum(model.handle) : 0;
        const int n_out = (model.getOutPortNum != nullptr) ? model.getOutPortNum(model.handle) : 0;
        for (int side = 0; side < 2; ++side) {
            const bool is_input = (side == 0);
            const int count = is_input ? n_in : n_out;
            auto get_map = is_input ? model.getInPortVariableMap : model.getOutPortVariableMap;
            for (int p = 0; p < count; ++p) {
                XOptPortDesc port;
                port.is_input = is_input;
                port.name = (is_input ? "in" : "out") + std::to_string(p);
                if (get_map != nullptr) {
                    int size = 0;
                    if (get_map(model.handle, p, nullptr, nullptr, size) < 0) {
                        return fail("connect: get" + std::string(is_input ? "In" : "Out") +
                                    "PortVariableMap(" + std::to_string(p) + ") size query failed");
                    }
                    if (size > 0) {
                        std::vector<const char*> stream_names(size, nullptr);
                        std::vector<const char*> var_names(size, nullptr);
                        if (get_map(model.handle, p, stream_names.data(), var_names.data(),
                                    size) < 0) {
                            return fail("connect: get" + std::string(is_input ? "In" : "Out") +
                                        "PortVariableMap(" + std::to_string(p) + ") failed");
                        }
                        for (int k = 0; k < size; ++k) {
                            port.variables.emplace_back(
                                stream_names[k] != nullptr ? stream_names[k] : "",
                                var_names[k] != nullptr ? var_names[k] : "");
                        }
                    }
                }
                ports_.push_back(std::move(port));
            }
        }
    }

    // ---- 4) 可固定变量 + 初值估计 ----
    if (model.getFixableVariables != nullptr && model.generateEstimate != nullptr) {
        int n = 0;
        if (model.getFixableVariables(model.handle, nullptr, nullptr, n) < 0) {
            return fail("connect: getFixableVariables(size) failed");
        }
        std::vector<const char*> names(n > 0 ? n : 0, nullptr);
        std::vector<double> defaults(n > 0 ? n : 0, 0.0);
        if (n > 0 &&
            model.getFixableVariables(model.handle, names.data(), defaults.data(), n) < 0) {
            return fail("connect: getFixableVariables(fill) failed");
        }

        // 固定哪些：描述里点了名就按它，否则全固定——与 demo/宿主的默认一致。
        // 顺手把整份可固定变量表留下：CAPE-OPEN 侧要按原样转发给消费端，
        // 而这里是它唯一还拿得到 model 句柄的时刻。
        for (int i = 0; i < n; ++i) {
            fixables_.emplace_back(names[i] != nullptr ? names[i] : "", defaults[i]);
        }

        std::vector<int> chosen;
        if (desc_.fixable_variables.empty() && !has_fixed_override_) {
            for (int i = 0; i < n; ++i) chosen.push_back(i);
        } else if (!desc_.fixable_variables.empty()) {
            for (const auto& want : desc_.fixable_variables) {
                int at = -1;
                for (int i = 0; i < n; ++i) {
                    if (names[i] != nullptr && want == names[i]) {
                        at = i;
                        break;
                    }
                }
                if (at < 0) {
                    // 宽容只给**旧部署描述**里的名字，不给宿主明确推下来的那份。
                    //
                    // 组分表被推过之后，描述文件里按组分写死的名字（in_fi_C1
                    // 之类）**必然**失效——它是为另一套组分写的。对它报错等于
                    // 要求描述文件预知将来会用哪套组分，而那正是下推要解决的
                    // 问题，所以跳过。
                    //
                    // 但 has_fixed_override_ 为真时，desc_.fixable_variables 已经
                    // 被换成宿主刚推下来的那份（见 initModel 开头）——那是本次
                    // 调用的明确意图，对不上就是真的配错了，必须报错。放行的话，
                    // setComponents 之后一个拼错的固定变量会被静默丢掉、
                    // setFixedVariables 还返回 0，用户要固定的条件实际没生效，
                    // 而同一个非法集合在没推过组分时是会被拒绝的——同样的输入
                    // 两种结果，最难查的那类。
                    if (!components_override_.empty() && !has_fixed_override_) continue;
                    return fail("connect: '" + want + "' from " + from +
                                " is not a fixable variable of the model");
                }
                chosen.push_back(at);
            }
        }
        // 描述里点名的那些全被跳过（组分全换了）时，退回"全固定"——与描述
        // 里根本没写 fixable_variables 时的行为一致，而不是一个都不固定。
        // 注意：外部明确推了一份空表时不能退回"全固定"——那正好是它想避免的。
        if (chosen.empty() && !desc_.fixable_variables.empty() && !has_fixed_override_) {
            for (int i = 0; i < n; ++i) chosen.push_back(i);
        }

        // 固定值配给了一个没被固定的变量，同样不能静默丢弃。
        for (const auto& kv : desc_.fixed_values) {
            bool used = false;
            for (int i : chosen) {
                if (names[i] != nullptr && kv.first == names[i]) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                // 与上面同一条判据：宿主推下来的固定值必须校验，旧描述的才宽容。
                if (!components_override_.empty() && !has_fixed_override_) continue;
                return fail("connect: fixed value for '" + kv.first + "' from " + from +
                            " does not name a variable being fixed");
            }
        }

        // 打包：变量名以 '\0' 分隔拼在连续缓冲里，值数组一一对应。
        std::vector<char> packed_names;
        std::vector<double> packed_values;
        for (int i : chosen) {
            const std::string name = (names[i] != nullptr) ? names[i] : "";
            const auto it = desc_.fixed_values.find(name);
            packed_names.insert(packed_names.end(), name.begin(), name.end());
            packed_names.push_back('\0');
            packed_values.push_back(it != desc_.fixed_values.end() ? it->second : defaults[i]);
        }

        int est = 0;
        const int fixed_n = static_cast<int>(packed_values.size());
        if (model.generateEstimate(model.handle, nullptr, est, packed_names.data(),
                                   packed_values.data(), fixed_n) < 0) {
            return fail("connect: generateEstimate(size) failed");
        }
        if (est > 0) {
            estimate_x_.assign(est, 0.0);
            if (model.generateEstimate(model.handle, estimate_x_.data(), est, packed_names.data(),
                                       packed_values.data(), fixed_n) < 0) {
                estimate_x_.clear();
                return fail("connect: generateEstimate(fill) failed");
            }
        }
    } else if (!desc_.fixable_variables.empty() || !desc_.fixed_values.empty()) {
        return fail("connect: the model exports no getFixableVariables/generateEstimate, but " +
                    from + " fixes variables");
    }

    return 0;
}

int XOptMINLPAdapter::setComponents(const std::vector<std::string>& components) {
    if (components.empty()) return fail("setComponents: empty component list");
    if (dll_path_.empty()) {
        return fail("setComponents: only a DLL-backed model can be rebuilt "
                    "(an injected problem has no model layer)");
    }
    // 全拆全建，而不是就地改：组分表决定变量集合，buildProblem 之后的问题对象
    // 是按旧组分建的，任何"改一半"的中间状态都是错的。
    const std::vector<std::string> previous = components_override_;
    components_override_ = components;
    disconnect();
    if (connect() >= 0) return 0;

    // 推失败必须回滚。不回滚的话对象就停在"已断开"——模型没了，之后每个调用
    // 都失败，而真正的原因（组分表被拒）已经被冲掉，查起来离病灶极远。
    const std::string why = last_error_;
    components_override_ = previous;
    disconnect();
    if (connect() < 0) {
        last_error_ = "setComponents failed (" + why +
                      "), and restoring the previous component list also failed (" +
                      last_error_ + ")";
        return -1;
    }
    last_error_ = why;  // 报的仍是推失败的原因，而不是回滚成功
    return -1;
}

int XOptMINLPAdapter::setFixedVariables(const std::vector<std::string>& names,
                                       const std::vector<double>& values) {
    if (names.size() != values.size()) {
        return fail("setFixedVariables: names and values have different lengths");
    }
    if (dll_path_.empty()) {
        return fail("setFixedVariables: only a DLL-backed model can be rebuilt");
    }
    const bool had = has_fixed_override_;
    const std::vector<std::string> prev_names = fixed_names_override_;
    const std::vector<double> prev_values = fixed_values_override_;

    has_fixed_override_ = true;
    fixed_names_override_ = names;
    fixed_values_override_ = values;
    disconnect();
    if (connect() >= 0) return 0;

    const std::string why = last_error_;  // 与 setComponents 同样的回滚理由
    has_fixed_override_ = had;
    fixed_names_override_ = prev_names;
    fixed_values_override_ = prev_values;
    disconnect();
    if (connect() < 0) {
        last_error_ = "setFixedVariables failed (" + why +
                      "), and restoring the previous selection also failed (" + last_error_ + ")";
        return -1;
    }
    last_error_ = why;
    return -1;
}

int XOptMINLPAdapter::connect() {
    // 1) 建立 view_（注入 / 加载 DLL 并自动探测 ABI）
    if (inject_cpp_ != nullptr) {
        view_.reset(new CppProblemView(inject_cpp_, nullptr, /*own*/ false));
    } else if (have_capi_inject_) {
        xOptModelT none{};
        view_.reset(new CapiProblemView(inject_capi_, /*own_problem*/ false, none, /*own_model*/ false));
    } else {
        if (dll_path_.empty()) return fail("connect: no dll path and no injected problem");
        module_ = loadLib(dll_path_);
        if (module_ == nullptr) return fail("connect: failed to load '" + dll_path_ + "'");

        if (getSym(module_, "xOptModel_createModel")) {
            // C ABI：xOptModel_createModel 填 xOptModelT，buildProblem 填 xOptProblemT
            using CreateModelFn = int (*)(xOptModelT*, xOptPlatformT*, const char*);
            auto create = reinterpret_cast<CreateModelFn>(getSym(module_, "xOptModel_createModel"));
            xOptModelT model = {sizeof(xOptModelT)};
            if (create(&model, nullptr, "") < 0 || model.handle == nullptr ||
                model.buildProblem == nullptr) {
                return fail("connect: xOptModel_createModel/buildProblem unavailable");
            }
            // 宿主在这里还有一段初始化序列（参数/slate/校验/固定变量），
            // 跳过它 buildProblem 对很多模型是必然失败的。
            if (initModel(model) < 0) {  // 已填 last_error_
                if (model.destroyModel != nullptr) model.destroyModel(model.handle);
                return -1;
            }
            xOptProblemT pt = {sizeof(xOptProblemT)};
            if (model.buildProblem(model.handle, &pt) < 0 || pt.handle == nullptr) {
                // view_ 还没接手，模型此刻只有这里能回收。
                if (model.destroyModel != nullptr) model.destroyModel(model.handle);
                return fail("connect: buildProblem failed");
            }
            view_.reset(new CapiProblemView(pt, /*own_problem*/ true, model, /*own_model*/ true));
        } else if (getSym(module_, "createProblem")) {
            // C++ ABI：createProblem()/destroyProblem()
            auto create = reinterpret_cast<createProblemFunc>(getSym(module_, "createProblem"));
            auto destroy = reinterpret_cast<destroyProblemFunc>(getSym(module_, "destroyProblem"));
            if (destroy == nullptr) return fail("connect: destroyProblem not exported");
            xOptProblem* p = create();
            if (p == nullptr) return fail("connect: createProblem returned null");
            view_.reset(new CppProblemView(p, destroy, /*own*/ true));
        } else {
            return fail("connect: '" + dll_path_ +
                        "' exports neither createProblem nor xOptModel_createModel");
        }
    }

    // 2) initialize + 缓存规模/名称/界/结构（经 view_，ABI 无关）
    // 注入的、且宿主声明已初始化过的问题不再 initialize（见头文件注入构造的说明）。
    const bool skip_init = (inject_cpp_ != nullptr && !initialize_injected_);
    if (!skip_init && view_->initialize() < 0) return fail("connect: initialize failed");

    size_ = CapeMINLPSize{};
    const int nv = view_->numVariables();
    const int nc = view_->numConstraints();
    if (nv < 0 || nc < 0) return fail("connect: numVariables/numConstraints failed");
    size_.num_variables = nv;
    size_.num_constraints = nc;

    {
        std::vector<const char*> tmp(nv > 0 ? nv : 0, nullptr);
        if (nv > 0 && view_->getVariableNames(tmp.data(), nv) < 0)
            return fail("connect: getVariableNames failed");
        var_names_.resize(nv);
        for (int i = 0; i < nv; ++i) var_names_[i] = tmp[i] ? tmp[i] : "";
    }
    {
        std::vector<const char*> tmp(nc > 0 ? nc : 0, nullptr);
        if (nc > 0 && view_->getConstraintNames(tmp.data(), nc) < 0)
            return fail("connect: getConstraintNames failed");
        con_names_.resize(nc);
        for (int i = 0; i < nc; ++i) con_names_[i] = tmp[i] ? tmp[i] : "";
    }

    var_lower_.assign(nv, 0.0);
    var_upper_.assign(nv, 0.0);
    if (nv > 0 && view_->getVariableBounds(var_lower_.data(), var_upper_.data(), nv) < 0)
        return fail("connect: getVariableBounds failed");
    con_lower_.assign(nc, 0.0);
    con_upper_.assign(nc, 0.0);
    if (nc > 0 && view_->getConstraintBounds(con_lower_.data(), con_upper_.data(), nc) < 0)
        return fail("connect: getConstraintBounds failed");

    initial_x_.assign(nv, 0.0);
    if (nv > 0 && view_->getInitialX(initial_x_.data(), nv) < 0)
        return fail("connect: getInitialX failed");
    // 模型给过 generateEstimate 的初值估计就用它：固定进料之类的效果体现在
    // 那份向量里，而问题自己的 getInitialX 未必反映——宿主也是这么取的。
    // 维数对不上说明两者说的不是同一个问题，那就不动，用问题自己报的。
    if (nv > 0 && static_cast<int>(estimate_x_.size()) == nv) initial_x_ = estimate_x_;

    {
        int nnz = 0;
        if (view_->getConstraintJacobianStructure(nullptr, nullptr, nnz) < 0)
            return fail("connect: jacobian structure(size) failed");
        jac_rowidx_.assign(nnz, 0);
        jac_colidx_.assign(nnz, 0);
        if (nnz > 0 && view_->getConstraintJacobianStructure(jac_rowidx_.data(), jac_colidx_.data(),
                                                             nnz) < 0)
            return fail("connect: jacobian structure(fill) failed");
        size_.num_nonlinear_jacobian_nz = static_cast<int>(jac_rowidx_.size());
    }
    {
        int gsz = 0;
        if (view_->getObjectiveGradientStructure(nullptr, gsz) < 0)
            return fail("connect: objgrad structure(size) failed");
        objgrad_colidx_.assign(gsz, 0);
        if (gsz > 0 && view_->getObjectiveGradientStructure(objgrad_colidx_.data(), gsz) < 0)
            return fail("connect: objgrad structure(fill) failed");
        size_.num_nonlinear_objgrad_nz = static_cast<int>(objgrad_colidx_.size());
    }
    {
        int lsz = 0;
        if (view_->getLinearConstraints(nullptr, nullptr, nullptr, lsz) >= 0)
            size_.num_linear_jacobian_nz = lsz;
    }

    current_x_ = initial_x_;
    if (nv > 0 && view_->setX(current_x_.data(), nv) < 0) return fail("connect: setX(initial) failed");

    initialized_ = true;
    return 0;
}

void XOptMINLPAdapter::disconnect() {
    view_.reset();  // 先回收 problem/model
    if (module_) {
        closeLib(module_);
        module_ = nullptr;
    }
    // 描述与初值估计是本次 connect 的产物，一并清掉：留着的话，一次失败的
    // 重连会拿上一次的估计去盖 getInitialX，而那份估计属于已经销毁的模型。
    desc_ = XOptModelDesc{};
    desc_source_.clear();
    estimate_x_.clear();
    ports_.clear();
    fixables_.clear();
    initialized_ = false;
}

int XOptMINLPAdapter::getSize(CapeMINLPSize& size_out) {
    if (!initialized_) return fail("getSize: not connected");
    size_out = size_;
    return 0;
}

int XOptMINLPAdapter::getStructure(const std::string& type, std::vector<int>& row_index,
                                   std::vector<int>& col_index, std::vector<int>& obj_index) {
    if (!initialized_) return fail("getStructure: not connected");
    row_index.clear();
    col_index.clear();
    obj_index.clear();
    if (type == cape::kStructJacobian) {
        row_index = jac_rowidx_;
        col_index = jac_colidx_;
        return 0;
    }
    if (type == cape::kStructObjectiveGradient) {
        obj_index = objgrad_colidx_;
        return 0;
    }
    return fail("getStructure: unknown type " + type);
}

int XOptMINLPAdapter::getVariableNames(const std::vector<int>& vids,
                                       std::vector<std::string>& names_out) {
    if (!initialized_) return fail("getVariableNames: not connected");
    pick(var_names_, vids, names_out);
    return 0;
}

int XOptMINLPAdapter::getVariableBounds(const std::vector<int>& vids, std::vector<double>& lower_out,
                                        std::vector<double>& upper_out) {
    if (!initialized_) return fail("getVariableBounds: not connected");
    pick(var_lower_, vids, lower_out);
    pick(var_upper_, vids, upper_out);
    return 0;
}

int XOptMINLPAdapter::getVariableValues(const std::vector<int>& vids,
                                        std::vector<double>& values_out) {
    if (!initialized_) return fail("getVariableValues: not connected");
    pick(current_x_, vids, values_out);
    return 0;
}

int XOptMINLPAdapter::setVariableValues(const std::vector<int>& vids,
                                        const std::vector<double>& values) {
    if (!initialized_) return fail("setVariableValues: not connected");
    if (vids.empty()) {
        if (values.size() != current_x_.size()) return fail("setVariableValues: size mismatch");
        current_x_ = values;
    } else {
        if (vids.size() != values.size()) return fail("setVariableValues: vids/values mismatch");
        for (size_t i = 0; i < vids.size(); ++i) {
            int id = vids[i];
            if (id < 0 || id >= static_cast<int>(current_x_.size()))
                return fail("setVariableValues: vid out of range");
            current_x_[id] = values[i];
        }
    }
    if (view_->setX(current_x_.data(), static_cast<int>(current_x_.size())) < 0)
        return fail("setVariableValues: setX failed");
    return 0;
}

int XOptMINLPAdapter::getConstraintNames(const std::vector<int>& cids,
                                         std::vector<std::string>& names_out) {
    if (!initialized_) return fail("getConstraintNames: not connected");
    pick(con_names_, cids, names_out);
    return 0;
}

int XOptMINLPAdapter::getConstraintBounds(const std::vector<int>& cids, std::vector<double>& lower_out,
                                          std::vector<double>& upper_out) {
    if (!initialized_) return fail("getConstraintBounds: not connected");
    pick(con_lower_, cids, lower_out);
    pick(con_upper_, cids, upper_out);
    return 0;
}

int XOptMINLPAdapter::getNonlinearConstraintValues(const std::vector<int>& cids,
                                                   std::vector<double>& values_out) {
    if (!initialized_) return fail("getNonlinearConstraintValues: not connected");
    std::vector<double> all(size_.num_constraints, 0.0);
    if (size_.num_constraints > 0 && view_->evaluateConstraints(all.data(), size_.num_constraints) < 0)
        return fail("getNonlinearConstraintValues: evaluateConstraints failed");
    pick(all, cids, values_out);
    return 0;
}

// 规范：「Obtain the values of the first partial derivatives of **a subset** of the
// MINLP constraints (corresponding to a specified list of indices, cids)」，且
// 「The row and column indices of the elements of the vector can be obtained from
// method GetMINLPStructure」。
//
// 原先这里整个忽略 cids，永远返回整表 nnz：客户端要第 3 个约束的导数，拿到的是
// 全部约束的导数，长度和含义都不是它要的，且**毫无征兆**。这是本 PR 一直在清理
// 的那类「静默地给出错误答案」。
//
// 现在按 cids 过滤：保留行下标落在子集里的条目，顺序与 getStructure 返回的结构
// 一致，客户端用同样的方式过滤 rowindex/columnindex 就能对上。
// 空 cids 依约定表示「全部」。
int XOptMINLPAdapter::getConstraintDerivativeValues(const std::string& type,
                                                    const std::vector<int>& cids,
                                                    std::vector<double>& values_out) {
    if (!initialized_) return fail("getConstraintDerivativeValues: not connected");
    if (type != cape::kStructJacobian && type != cape::kDerivNonlinear)
        return fail("getConstraintDerivativeValues: unsupported type " + type);

    const int nnz = static_cast<int>(jac_rowidx_.size());
    std::vector<double> all(nnz, 0.0);
    if (nnz > 0 && view_->evaluateConstraintsJacobianValues(all.data(), nnz) < 0)
        return fail("getConstraintDerivativeValues: evaluateConstraintsJacobianValues failed");

    if (cids.empty()) {
        values_out = all;
        return 0;
    }

    std::vector<char> wanted(size_.num_constraints > 0 ? size_.num_constraints : 0, 0);
    for (int id : cids) {
        if (id < 0 || id >= size_.num_constraints)
            return fail("getConstraintDerivativeValues: cid out of range");
        wanted[id] = 1;
    }
    values_out.clear();
    values_out.reserve(all.size());
    for (int k = 0; k < nnz; ++k) {
        const int row = jac_rowidx_[k];
        if (row >= 0 && row < static_cast<int>(wanted.size()) && wanted[row])
            values_out.push_back(all[k]);
    }
    return 0;
}

int XOptMINLPAdapter::getObjectiveValue(double& value_out) {
    if (!initialized_) return fail("getObjectiveValue: not connected");
    if (view_->evaluateObjective(value_out) < 0)
        return fail("getObjectiveValue: evaluateObjective failed");
    return 0;
}

int XOptMINLPAdapter::getObjectiveDerivativeValues(const std::string& /*type*/,
                                                   std::vector<double>& values_out) {
    if (!initialized_) return fail("getObjectiveDerivativeValues: not connected");
    const int gsz = static_cast<int>(objgrad_colidx_.size());
    values_out.assign(gsz, 0.0);
    if (gsz > 0 && view_->evaluateObjectiveGradient(values_out.data(), gsz) < 0)
        return fail("getObjectiveDerivativeValues: evaluateObjectiveGradient failed");
    return 0;
}

std::string XOptMINLPAdapter::lastError() const { return last_error_; }
