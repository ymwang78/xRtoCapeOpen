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
XOptMINLPAdapter::XOptMINLPAdapter(xOptProblem* injected) : inject_cpp_(injected) {}
XOptMINLPAdapter::XOptMINLPAdapter(const xOptProblemT& injected)
    : have_capi_inject_(true), inject_capi_(injected) {}

XOptMINLPAdapter::~XOptMINLPAdapter() { disconnect(); }

int XOptMINLPAdapter::fail(const std::string& msg) const {
    last_error_ = msg;
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
            xOptProblemT pt = {sizeof(xOptProblemT)};
            if (model.buildProblem(model.handle, &pt) < 0 || pt.handle == nullptr) {
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
    if (view_->initialize() < 0) return fail("connect: initialize failed");

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

int XOptMINLPAdapter::getConstraintDerivativeValues(const std::string& type,
                                                    const std::vector<int>& /*cids*/,
                                                    std::vector<double>& values_out) {
    if (!initialized_) return fail("getConstraintDerivativeValues: not connected");
    if (type != cape::kStructJacobian && type != cape::kDerivNonlinear)
        return fail("getConstraintDerivativeValues: unsupported type " + type);
    const int nnz = static_cast<int>(jac_rowidx_.size());
    values_out.assign(nnz, 0.0);
    if (nnz > 0 && view_->evaluateConstraintsJacobianValues(values_out.data(), nnz) < 0)
        return fail("getConstraintDerivativeValues: evaluateConstraintsJacobianValues failed");
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
