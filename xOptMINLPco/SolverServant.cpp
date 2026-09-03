// ***************************************************************
//  SolverServant   version:  1.0   -  date:  2026/09/03
//  -------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen (xOptMINLPco).
//  Copyright (C) 2026 - All Rights Reserved
// ***************************************************************
#include "SolverServant.h"  // 首个包含：XOPTINTERFACE_EXPORTS + xOpt 头

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "CapeMINLPProblemCore.h"
#include "backend/corba/CapeCorbaMarshal.h"
#include "backend/corba/CapeMINLPModelCorba.h"

namespace {

namespace ct = ::CAPEOPEN100::Common::Types;
namespace ce = ::CAPEOPEN100::Common::Error;
namespace mi = ::CAPEOPEN100::Business::Numeric::Minlp;
namespace cp = ::CAPEOPEN100::Common::Parameter;
namespace cc = ::CAPEOPEN100::Common::Collection;

// 异常体按 Error Common Interface.pdf §5.2.1 的 CORBA 映射平摊 ECapeUser 的
// 六个成员，没有 name；与 MINLPServant / UnitServant 同一套写法。
ce::ECapeUnknown unknown(const char* iface, const char* op, const std::string& why) {
    ce::ECapeUnknown e;
    e.code = -1;
    e.description = CORBA::string_dup(why.c_str());
    e.scope = CORBA::string_dup("xOptMINLPco");
    e.interfaceName = CORBA::string_dup(iface);
    e.operation = CORBA::string_dup(op);
    e.moreInfo = CORBA::string_dup("");
    return e;
}

ce::ECapeInvalidArgument invalidArgument(const char* iface, const char* op, const std::string& why,
                                        CORBA::Short pos) {
    ce::ECapeInvalidArgument e;
    e.code = -1;
    e.description = CORBA::string_dup(why.c_str());
    e.scope = CORBA::string_dup("xOptMINLPco");
    e.interfaceName = CORBA::string_dup(iface);
    e.operation = CORBA::string_dup(op);
    e.moreInfo = CORBA::string_dup("");
    e.position = pos;
    return e;
}

// Solve 的失败用规范为它专设的 ECapeSolvingError（ICapeMINLPSystem::Solve 的
// raises 子句里就有它），而不是笼统的 ECapeUnknown：第三方 CO 客户端按它区分
// "求解器说不行"与"调用本身坏了"。
ce::ECapeSolvingError solvingError(const char* op, const std::string& why) {
    ce::ECapeSolvingError e;
    e.code = -1;
    e.description = CORBA::string_dup(why.c_str());
    e.scope = CORBA::string_dup("xOptMINLPco");
    e.interfaceName = CORBA::string_dup("ICapeMINLPSystem");
    e.operation = CORBA::string_dup(op);
    e.moreInfo = CORBA::string_dup("");
    return e;
}

const char* kSystemIface = "IXOptMINLPSystemExtension";

const char* resultName(int r) {
    switch (r) {
        case xOptSolver::RESULT_OPTIMAL: return "RESULT_OPTIMAL";
        case xOptSolver::RESULT_FEASIBLE: return "RESULT_FEASIBLE";
        case xOptSolver::RESULT_UNKNOWN: return "RESULT_UNKNOWN";
        case xOptSolver::RESULT_INFEASIBLE: return "RESULT_INFEASIBLE";
        case xOptSolver::RESULT_UNBOUNDED: return "RESULT_UNBOUNDED";
        case xOptSolver::RESULT_ITER_LIMIT: return "RESULT_ITER_LIMIT";
        case xOptSolver::RESULT_INVALID_SETTINGS: return "RESULT_INVALID_SETTINGS";
        case xOptSolver::RESULT_NUMERICAL_ISSUES: return "RESULT_NUMERICAL_ISSUES";
        case xOptSolver::RESULT_INVALID_PROBLEM: return "RESULT_INVALID_PROBLEM";
        case xOptSolver::RESULT_USER_PAUSE: return "RESULT_USER_PAUSE";
        default: return "unknown code";
    }
}

std::vector<std::string> fromStrings(const ct::CapeArrayString& s) {
    std::vector<std::string> out;
    out.reserve(s.length());
    for (CORBA::ULong i = 0; i < s.length(); ++i) {
        out.emplace_back(s[i].in() != nullptr ? s[i].in() : "");
    }
    return out;
}

std::vector<const char*> cstrs(const std::vector<std::string>& v) {
    std::vector<const char*> out;
    out.reserve(v.size());
    for (const std::string& s : v) out.push_back(s.c_str());
    return out;
}

ct::CapeArrayBoolean* toAccepted(const std::vector<xOptSolver::boolean>& res) {
    ct::CapeArrayBoolean_var out = new ct::CapeArrayBoolean();
    out->length(static_cast<CORBA::ULong>(res.size()));
    for (CORBA::ULong i = 0; i < out->length(); ++i) out[i] = (res[i] != 0);
    return out._retn();
}

// 统一的“无量纲”答案：空的指数数组。规范把量纲表示成 SI 基本量的指数序列，
// 求解器的 max_iter / tol 之类没有量纲。
CORBA::Any* dimensionless() {
    CORBA::Any_var a = new CORBA::Any();
    ct::CapeArrayDouble dims;
    (*a) <<= dims;
    return a._retn();
}

// ===========================================================================
//  参数对象：ICapeParameter + 类型化的 spec
//  （GetParameters 的返回物；求解器的可调参数按 CAPE-OPEN 参数发布）
// ===========================================================================

class RealSpecServant : public POA_XOPTCO::IXOptRealParameterSpec {
  public:
    RealSpecServant(std::string name, double default_value)
        : name_(std::move(name)), default_(default_value) {}

    char* GetComponentName() override { return CORBA::string_dup(name_.c_str()); }
    char* GetComponentDescription() override {
        return CORBA::string_dup("real solver option (xOptSolver OPTION_REAL)");
    }
    void SetComponentName(const char*) override {}
    void SetComponentDescription(const char*) override {}

    cp::CapeParamType Type() override { return cp::CAPE_REAL; }
    CORBA::Any* Dimensionality() override { return dimensionless(); }
    ct::CapeDouble DefaultValue() override { return default_; }
    // 求解器契约不报界；"没有界"照实用最宽的浮点范围表示，不编一个出来。
    ct::CapeDouble LowerBound() override { return -DBL_MAX; }
    ct::CapeDouble UpperBound() override { return DBL_MAX; }
    ct::CapeBoolean Validate(ct::CapeDouble, ct::CapeString_out message) override {
        message = CORBA::string_dup("");
        return true;
    }

  private:
    std::string name_;
    double default_;
};

class IntSpecServant : public POA_XOPTCO::IXOptIntegerParameterSpec {
  public:
    IntSpecServant(std::string name, int default_value)
        : name_(std::move(name)), default_(default_value) {}

    char* GetComponentName() override { return CORBA::string_dup(name_.c_str()); }
    char* GetComponentDescription() override {
        return CORBA::string_dup("integer solver option (xOptSolver OPTION_INT)");
    }
    void SetComponentName(const char*) override {}
    void SetComponentDescription(const char*) override {}

    cp::CapeParamType Type() override { return cp::CAPE_INT; }
    CORBA::Any* Dimensionality() override { return dimensionless(); }
    ct::CapeLong DefaultValue() override { return default_; }
    ct::CapeLong LowerBound() override { return INT32_MIN; }
    ct::CapeLong UpperBound() override { return INT32_MAX; }
    ct::CapeBoolean Validate(ct::CapeLong, ct::CapeString_out message) override {
        message = CORBA::string_dup("");
        return true;
    }

  private:
    std::string name_;
    int default_;
};

// 一个可调参数。值直接读写求解器的 options 通道——这里不缓存值，否则经
// IXOptMINLPSystemExtension::Set*Options 改过之后这里答的就是旧的。
class SolverParameterServant : public POA_CAPEOPEN100::Common::Parameter::ICapeParameter {
  public:
    SolverParameterServant(xOptSolver* solver, std::string name, xOptSolver::OPTION_TYPE type,
                           CORBA::Object_ptr spec, double default_value)
        : solver_(solver),
          name_(std::move(name)),
          type_(type),
          spec_(CORBA::Object::_duplicate(spec)),
          default_(default_value) {}

    char* GetComponentName() override { return CORBA::string_dup(name_.c_str()); }
    char* GetComponentDescription() override {
        return CORBA::string_dup("tunable parameter of the wrapped xOpt solver");
    }
    void SetComponentName(const char*) override {}
    void SetComponentDescription(const char*) override {}

    CORBA::Object_ptr Specification() override { return CORBA::Object::_duplicate(spec_.in()); }
    ct::CapeValidationStatus ValStatus() override { return ct::CAPE_VALID; }
    cp::CapeParamMode GetMode() override { return cp::CAPE_INPUT; }
    void SetMode(cp::CapeParamMode mode) override {
        // 求解器选项只能是输入。答应一个 OUTPUT 再悄悄当 INPUT 用，比拒绝难查。
        if (mode != cp::CAPE_INPUT) {
            throw invalidArgument("ICapeParameter", "SetMode",
                                  "solver options are input parameters only", 1);
        }
    }
    ct::CapeBoolean Validate(ct::CapeString_out message) override {
        message = CORBA::string_dup("");
        return true;
    }

    CORBA::Any* GetValue() override {
        CORBA::Any_var a = new CORBA::Any();
        const char* names[1] = {name_.c_str()};
        int size = 1;
        if (type_ == xOptSolver::OPTION_INT) {
            int v[1] = {0};
            if (solver_->getIntOptions(names, v, size) < 0) {
                throw unknown("ICapeParameter", "GetValue", "the solver rejected getIntOptions for '" + name_ + "'");
            }
            (*a) <<= static_cast<CORBA::Long>(v[0]);
        } else {
            double v[1] = {0.0};
            if (solver_->getDoubleOptions(names, v, size) < 0) {
                throw unknown("ICapeParameter", "GetValue", "the solver rejected getDoubleOptions for '" + name_ + "'");
            }
            (*a) <<= static_cast<CORBA::Double>(v[0]);
        }
        return a._retn();
    }

    void SetValue(const CORBA::Any& value) override {
        // 整数与实数互相接受、按参数类型转换：PME 侧的脚本语言常常分不清 3 和 3.0。
        CORBA::Long l = 0;
        CORBA::Double d = 0.0;
        double numeric = 0.0;
        if (value >>= l) {
            numeric = static_cast<double>(l);
        } else if (value >>= d) {
            numeric = d;
        } else {
            throw invalidArgument("ICapeParameter", "SetValue",
                                  "value must hold a long or a double", 1);
        }
        apply(numeric, "SetValue");
    }

    void Reset() override { apply(default_, "Reset"); }

  private:
    void apply(double numeric, const char* op) {
        const char* names[1] = {name_.c_str()};
        xOptSolver::boolean accepted[1] = {0};
        int rc;
        if (type_ == xOptSolver::OPTION_INT) {
            // 整数参数只收整数：NaN / 超出 int 范围的 double 转 int 是未定义行为，
            // 1.5 之类静默截成 1 也不是调用方的意思。规范给的是 ECapeInvalidArgument。
            if (!std::isfinite(numeric) || numeric < static_cast<double>(INT32_MIN) ||
                numeric > static_cast<double>(INT32_MAX) || std::floor(numeric) != numeric) {
                throw invalidArgument("ICapeParameter", op,
                                      "'" + name_ + "' is an integer option; the value must be "
                                      "a finite integral number within the 32-bit range",
                                      1);
            }
            const int v[1] = {static_cast<int>(numeric)};
            rc = solver_->setIntOptions(accepted, names, v, 1);
        } else {
            if (!std::isfinite(numeric)) {
                throw invalidArgument("ICapeParameter", op,
                                      "'" + name_ + "' must be a finite number", 1);
            }
            const double v[1] = {numeric};
            rc = solver_->setDoubleOptions(accepted, names, v, 1);
        }
        if (rc < 0) throw unknown("ICapeParameter", op, "the solver rejected the option call");
        if (accepted[0] == 0) {
            throw invalidArgument("ICapeParameter", op,
                                  "the solver did not accept this value for '" + name_ + "'", 1);
        }
    }

    xOptSolver* solver_;
    std::string name_;
    xOptSolver::OPTION_TYPE type_;
    CORBA::Object_var spec_;
    double default_;
};

class ParameterCollectionServant : public POA_CAPEOPEN100::Common::Collection::ICapeCollection {
  public:
    ParameterCollectionServant(std::vector<CORBA::Object_var> refs, std::vector<std::string> names)
        : refs_(std::move(refs)), names_(std::move(names)) {}

    char* GetComponentName() override { return CORBA::string_dup("parameters"); }
    char* GetComponentDescription() override {
        return CORBA::string_dup("tunable parameters of the wrapped xOpt solver");
    }
    void SetComponentName(const char*) override {}
    void SetComponentDescription(const char*) override {}

    CORBA::Object_ptr Item(const CORBA::Any& id) override {
        // 下标**从 1 开始**（Collection Common Interface.pdf），与 vids/cids 同一套约定。
        CORBA::Long index = 0;
        if (id >>= index) {
            if (index < 1 || index > static_cast<CORBA::Long>(refs_.size())) {
                throw invalidArgument("ICapeCollection", "Item",
                                      "collection index out of range (1-based)", 1);
            }
            return CORBA::Object::_duplicate(refs_[static_cast<size_t>(index - 1)].in());
        }
        const char* name = nullptr;
        if (id >>= name) {
            for (size_t i = 0; i < names_.size(); ++i) {
                if (names_[i] == name) return CORBA::Object::_duplicate(refs_[i].in());
            }
            throw invalidArgument("ICapeCollection", "Item", "no parameter with that name", 1);
        }
        throw invalidArgument("ICapeCollection", "Item",
                              "id must hold either a long (1-based) or a string", 1);
    }

    ct::CapeLong Count() override { return static_cast<ct::CapeLong>(refs_.size()); }

  private:
    std::vector<CORBA::Object_var> refs_;
    std::vector<std::string> names_;
};

// 激活一个 servant 并交出引用；ServantBase_var 保证所有权移交给 POA 之后本地
// 那一次引用无论正常还是抛出都被释放（MINLPCorbaServer.cpp 同款写法）。
CORBA::Object_ptr activate(PortableServer::POA_ptr poa, PortableServer::ServantBase* servant) {
    PortableServer::ServantBase_var owner(servant);
    PortableServer::ObjectId_var oid = poa->activate_object(servant);
    return poa->id_to_reference(oid.in());
}

void deactivate(PortableServer::POA_ptr poa, CORBA::Object_ptr ref) {
    if (CORBA::is_nil(ref)) return;
    try {
        PortableServer::ObjectId_var oid = poa->reference_to_id(ref);
        poa->deactivate_object(oid.in());
    } catch (const CORBA::Exception&) {
        // 清理路径：已经不在了就算了。
    }
}

}  // namespace

// ===========================================================================
//  XOptSolverLibrary
// ===========================================================================

XOptSolverLibrary::XOptSolverLibrary(std::string dll_path) : dll_path_(std::move(dll_path)) {}

XOptSolverLibrary::~XOptSolverLibrary() { unloadModule(); }

int XOptSolverLibrary::load() {
    if (loaded()) return 0;
    if (dll_path_.empty()) {
        last_error_ = "no solver DLL path given";
        return -1;
    }
#ifdef _WIN32
    HMODULE m = LoadLibraryA(dll_path_.c_str());
    if (m == nullptr) {
        last_error_ = "LoadLibrary failed for '" + dll_path_ + "' (error " +
                      std::to_string(GetLastError()) + ")";
        return -1;
    }
    module_ = m;
    create_ = reinterpret_cast<CreateSolverFunc>(GetProcAddress(m, "createSolver"));
    destroy_ = reinterpret_cast<DestroySolverFunc>(GetProcAddress(m, "destroySolver"));
#else
    module_ = dlopen(dll_path_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module_ == nullptr) {
        const char* e = dlerror();
        last_error_ = "dlopen failed for '" + dll_path_ + "': " + (e != nullptr ? e : "");
        return -1;
    }
    create_ = reinterpret_cast<CreateSolverFunc>(dlsym(module_, "createSolver"));
    destroy_ = reinterpret_cast<DestroySolverFunc>(dlsym(module_, "destroySolver"));
#endif
    if (create_ == nullptr || destroy_ == nullptr) {
        // 两个都要：只有 createSolver 的 DLL 宿主也不接受（xOpt.cpp::loadSolver）。
        // 模块此刻就放掉：留着的话下一次 load() 会直接覆盖 module_，前一个句柄
        // 就漏了（Windows 上还多一个引用计数）。
        last_error_ = "'" + dll_path_ + "' does not export both createSolver and destroySolver";
        create_ = nullptr;
        destroy_ = nullptr;
        unloadModule();
        return -1;
    }
    return 0;
}

void XOptSolverLibrary::unloadModule() {
    if (module_ == nullptr) return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(module_));
#else
    dlclose(module_);
#endif
    module_ = nullptr;
}

xOptSolver* XOptSolverLibrary::create(const char* name, xOptProblem* problem, xOptLogFunc log) {
    if (!loaded()) return nullptr;
    return create_(name, problem, log);
}

void XOptSolverLibrary::destroy(xOptSolver* solver) {
    if (solver != nullptr && destroy_ != nullptr) destroy_(solver);
}

// ===========================================================================
//  MINLPSystemServant
// ===========================================================================

MINLPSystemServant::MINLPSystemServant(XOptSolverLibrary* library, PortableServer::POA_ptr poa,
                                       mi::ICapeMINLP_ptr problem, std::string name,
                                       xOptLogFunc log)
    : library_(library),
      poa_(PortableServer::POA::_duplicate(poa)),
      remote_(mi::ICapeMINLP::_duplicate(problem)),
      name_(std::move(name)),
      log_(log),
      comp_name_("xOpt MINLP System"),
      comp_desc_("xOpt solver bound to a CAPE-OPEN MINLP problem") {}

MINLPSystemServant::~MINLPSystemServant() { teardown(); }

void MINLPSystemServant::setSelfOid(const PortableServer::ObjectId& oid) {
    self_oid_ = new PortableServer::ObjectId(oid);
    has_self_oid_ = true;
}

int MINLPSystemServant::init() {
    if (library_ == nullptr || !library_->loaded()) {
        init_error_ = "the solver library is not loaded";
        return -1;
    }
    if (CORBA::is_nil(remote_.in())) {
        init_error_ = "no ICapeMINLP reference";
        return -1;
    }

    // 远端问题 -> ICapeMINLPModel -> xOptProblemT -> xOptProblem。
    // CapeMINLPProblemCore::initialize 会把规模/名称/界/结构一次读回来缓存，
    // 那是几次回到客户端的往返；失败多半是客户端那头没法受理回调。
    auto backend = std::make_unique<CapeMINLPModelCorba>(remote_.in());
    CapeMINLPProblemCore* core = new CapeMINLPProblemCore(std::move(backend));
    if (core->initialize() < 0) {
        init_error_ =
            "reading the client's problem failed (GetMINLPSize / names / bounds / structure "
            "over the callback connection); is the client able to serve requests while it waits?";
        delete core;
        return -1;
    }
    xOptProblemT pt = {sizeof(xOptProblemT)};
    core->fillVtable(&pt);
    problem_ = std::make_unique<XOptProblemFromVtable>(pt);  // 从这里起 core 归它管

    num_variables_ = problem_->numVariables();
    num_constraints_ = problem_->numConstraints();
    if (num_variables_ <= 0 || num_constraints_ < 0) {
        init_error_ = "the client's problem reports nv=" + std::to_string(num_variables_) +
                      " nc=" + std::to_string(num_constraints_);
        problem_.reset();
        return -1;
    }

    solver_ = library_->create(name_.c_str(), problem_.get(), log_);
    if (solver_ == nullptr) {
        init_error_ = "createSolver returned null (" + library_->path() + ")";
        problem_.reset();
        return -1;
    }

    try {
        buildParameters();
    } catch (const CORBA::Exception& e) {
        init_error_ = std::string("publishing the parameter collection failed: ") + e._name();
        teardown();
        return -1;
    }
    return 0;
}

void MINLPSystemServant::teardown() {
    destroyParameters();
    if (solver_ != nullptr) {
        library_->destroy(solver_);
        solver_ = nullptr;
    }
    problem_.reset();  // 析构链：XOptProblemFromVtable -> destroyProblem -> core -> 后端断开
    remote_ = mi::ICapeMINLP::_nil();
}

void MINLPSystemServant::requireLive(const char* operation) const {
    if (released_ || solver_ == nullptr || !problem_) {
        throw unknown(kSystemIface, operation, "this system has been released");
    }
}

// 可调参数按 CAPE-OPEN 参数发布：整数 -> IXOptIntegerParameterSpec，实数 ->
// IXOptRealParameterSpec。字符串型可调参数没有发（规范里对应的是
// ICapeOptionParameterSpec，本次没有承载；xOpt 客户端走扩展的字符串通道）。
// 默认值取创建时读到的当前值——求解器契约里没有"默认值"这个概念。
void MINLPSystemServant::buildParameters() {
    std::vector<CORBA::Object_var> refs;
    std::vector<std::string> names;

    int count = 0;
    if (solver_->getTunableParamList(nullptr, nullptr, count) < 0) count = 0;
    std::vector<const char*> raw_names(static_cast<size_t>(count > 0 ? count : 0), nullptr);
    std::vector<xOptSolver::OPTION_TYPE> types(raw_names.size(), xOptSolver::OPTION_REAL);
    int filled = count;
    if (count > 0 && solver_->getTunableParamList(raw_names.data(), types.data(), filled) < 0) {
        filled = 0;
    }

    for (int i = 0; i < filled; ++i) {
        // 名字指针指向求解器自己的存储，立刻拷出来。
        const std::string name(raw_names[static_cast<size_t>(i)] != nullptr
                                   ? raw_names[static_cast<size_t>(i)]
                                   : "");
        if (name.empty()) continue;
        const char* one[1] = {name.c_str()};
        int size = 1;
        CORBA::Object_var spec;
        double default_value = 0.0;
        if (types[static_cast<size_t>(i)] == xOptSolver::OPTION_INT) {
            int v[1] = {0};
            if (solver_->getIntOptions(one, v, size) < 0) continue;  // 报了名却读不出值：不发
            default_value = v[0];
            spec = activate(poa_.in(), new IntSpecServant(name, v[0]));
        } else if (types[static_cast<size_t>(i)] == xOptSolver::OPTION_REAL) {
            double v[1] = {0.0};
            if (solver_->getDoubleOptions(one, v, size) < 0) continue;
            default_value = v[0];
            spec = activate(poa_.in(), new RealSpecServant(name, v[0]));
        } else {
            continue;  // OPTION_STRING：见上
        }
        parameter_refs_.push_back(spec);
        CORBA::Object_var param =
            activate(poa_.in(), new SolverParameterServant(solver_, name,
                                                           types[static_cast<size_t>(i)],
                                                           spec.in(), default_value));
        parameter_refs_.push_back(param);
        refs.push_back(param);
        names.push_back(name);
    }

    parameters_collection_ =
        activate(poa_.in(), new ParameterCollectionServant(std::move(refs), std::move(names)));
}

void MINLPSystemServant::destroyParameters() {
    if (CORBA::is_nil(poa_.in())) return;
    deactivate(poa_.in(), parameters_collection_.in());
    parameters_collection_ = CORBA::Object::_nil();
    for (CORBA::Object_var& r : parameter_refs_) deactivate(poa_.in(), r.in());
    parameter_refs_.clear();
}

// ---- ICapeIdentification ----

char* MINLPSystemServant::GetComponentName() { return CORBA::string_dup(comp_name_.c_str()); }
char* MINLPSystemServant::GetComponentDescription() {
    return CORBA::string_dup(comp_desc_.c_str());
}
void MINLPSystemServant::SetComponentName(const char* name) {
    if (name != nullptr) comp_name_ = name;
}
void MINLPSystemServant::SetComponentDescription(const char* desc) {
    if (desc != nullptr) comp_desc_ = desc;
}

// ---- ICapeMINLPSystem ----

void MINLPSystemServant::Solve() {
    requireLive("Solve");
    int r = xOptSolver::RESULT_UNKNOWN;
    std::string why;
    try {
        r = solver_->solve();
    } catch (const std::exception& e) {
        r = xOptSolver::RESULT_NUMERICAL_ISSUES;
        why = e.what();
    } catch (...) {
        r = xOptSolver::RESULT_NUMERICAL_ISSUES;
        why = "unknown exception";
    }
    std::string write_back_why;
    const bool write_back_ok = recordOutcome(r, write_back_why);

    // 负码即失败（RESULT_USER_PAUSE 例外：那是"停下了"，不是"坏了"）。抛规范
    // 专设的 ECapeSolvingError；xOpt 客户端 catch 之后经 GetSolveResult 拿到原码。
    if (r < 0 && r != xOptSolver::RESULT_USER_PAUSE) {
        std::string desc = "solver '" + name_ + "' returned " + std::to_string(r) + " (" +
                           resultName(r) + ")";
        if (!why.empty()) desc += ": " + why;
        throw solvingError("Solve", desc);
    }
    // 写回失败不能静默：标准客户端靠 GetMINLPVariableValues 取解，回调断了或
    // 客户端拒收时它读到的是旧值，而 Solve 还说成功。结果码与 GetX 照常可用
    // （xOpt 客户端不靠写回取解），但 Solve 本身按失败报。
    if (!write_back_ok) {
        throw solvingError("Solve", "solver '" + name_ + "' finished with " + resultName(r) +
                                        ", but " + write_back_why +
                                        "; the problem's variable values are stale -- read X "
                                        "through the extension");
    }
}

// 把解写回远端问题。ICapeMINLP 的消费者靠 GetMINLPVariableValues 取解，
// 而 xOptSolver 契约只保证 X() 能读出来，并不要求求解器最后一次 setX 停在
// 解上——不能指望它。这里是 xRto 那头 X() 之外的第二条取解路径。
// Solve 与 ContinueSolve 都走这里：继续求解会把解推进，结果与写回得一起刷新。
bool MINLPSystemServant::recordOutcome(int result, std::string& why_out) {
    result_ = result;
    solved_ = true;
    if (num_variables_ <= 0) return true;
    std::vector<double> x(static_cast<size_t>(num_variables_), 0.0);
    if (solver_->X(x.data(), num_variables_) < 0) {
        why_out = "the solver has no solution vector to report (X() failed)";
        return false;
    }
    if (problem_->setX(x.data(), num_variables_) < 0) {
        why_out = "writing the solution back to the problem (SetMINLPVariableValues) failed";
        return false;
    }
    return true;
}

CORBA::Object_ptr MINLPSystemServant::GetParameters() {
    requireLive("GetParameters");
    return CORBA::Object::_duplicate(parameters_collection_.in());
}

// ---- IXOptMINLPSystemExtension：选项通道 ----

void MINLPSystemServant::GetTunableParameters(ct::CapeArrayString_out names,
                                              ct::CapeArrayLong_out types) {
    requireLive("GetTunableParameters");
    int count = 0;
    if (solver_->getTunableParamList(nullptr, nullptr, count) < 0) {
        throw unknown(kSystemIface, "GetTunableParameters", "the solver rejected getTunableParamList");
    }
    std::vector<const char*> raw(static_cast<size_t>(count > 0 ? count : 0), nullptr);
    std::vector<xOptSolver::OPTION_TYPE> ty(raw.size(), xOptSolver::OPTION_REAL);
    int filled = count;
    if (count > 0 && solver_->getTunableParamList(raw.data(), ty.data(), filled) < 0) {
        throw unknown(kSystemIface, "GetTunableParameters", "the solver rejected getTunableParamList (fill)");
    }
    ct::CapeArrayString_var n = new ct::CapeArrayString();
    ct::CapeArrayLong_var t = new ct::CapeArrayLong();
    n->length(static_cast<CORBA::ULong>(filled > 0 ? filled : 0));
    t->length(n->length());
    for (CORBA::ULong i = 0; i < n->length(); ++i) {
        n[i] = CORBA::string_dup(raw[i] != nullptr ? raw[i] : "");
        t[i] = static_cast<CORBA::Long>(ty[i]);
    }
    names = n._retn();
    types = t._retn();
}

void MINLPSystemServant::SetTunableParameters(const ct::CapeArrayString& names) {
    requireLive("SetTunableParameters");
    const std::vector<std::string> keep = fromStrings(names);
    std::vector<const char*> cs = cstrs(keep);
    if (solver_->setTunableParamList(cs.data(), static_cast<int>(cs.size())) < 0) {
        throw invalidArgument(kSystemIface, "SetTunableParameters",
                              "the solver rejected the tunable-parameter list", 1);
    }
}

void MINLPSystemServant::GetIntOptions(const ct::CapeArrayString& names,
                                       ct::CapeArrayLong_out values) {
    requireLive("GetIntOptions");
    const std::vector<std::string> keep = fromStrings(names);
    std::vector<const char*> cs = cstrs(keep);
    std::vector<int> v(keep.size(), 0);
    int size = static_cast<int>(keep.size());
    if (size > 0 && solver_->getIntOptions(cs.data(), v.data(), size) < 0) {
        throw invalidArgument(kSystemIface, "GetIntOptions",
                              "the solver rejected the request (unknown option name?)", 1);
    }
    values = new ct::CapeArrayLong(cape_corba::toLongSeq(v));
}

void MINLPSystemServant::SetIntOptions(const ct::CapeArrayString& names,
                                       const ct::CapeArrayLong& values,
                                       ct::CapeArrayBoolean_out accepted) {
    requireLive("SetIntOptions");
    if (names.length() != values.length()) {
        throw invalidArgument(kSystemIface, "SetIntOptions", "names and values differ in length", 2);
    }
    const std::vector<std::string> keep = fromStrings(names);
    std::vector<const char*> cs = cstrs(keep);
    std::vector<int> v;
    cape_corba::fromLongSeq(values, v);
    std::vector<xOptSolver::boolean> res(keep.size(), 0);
    if (!keep.empty() &&
        solver_->setIntOptions(res.data(), cs.data(), v.data(), static_cast<int>(keep.size())) < 0) {
        throw unknown(kSystemIface, "SetIntOptions", "the solver rejected setIntOptions");
    }
    accepted = toAccepted(res);
}

void MINLPSystemServant::GetDoubleOptions(const ct::CapeArrayString& names,
                                          ct::CapeArrayDouble_out values) {
    requireLive("GetDoubleOptions");
    const std::vector<std::string> keep = fromStrings(names);
    std::vector<const char*> cs = cstrs(keep);
    std::vector<double> v(keep.size(), 0.0);
    int size = static_cast<int>(keep.size());
    if (size > 0 && solver_->getDoubleOptions(cs.data(), v.data(), size) < 0) {
        throw invalidArgument(kSystemIface, "GetDoubleOptions",
                              "the solver rejected the request (unknown option name?)", 1);
    }
    values = new ct::CapeArrayDouble(cape_corba::toDoubleSeq(v));
}

void MINLPSystemServant::SetDoubleOptions(const ct::CapeArrayString& names,
                                          const ct::CapeArrayDouble& values,
                                          ct::CapeArrayBoolean_out accepted) {
    requireLive("SetDoubleOptions");
    if (names.length() != values.length()) {
        throw invalidArgument(kSystemIface, "SetDoubleOptions", "names and values differ in length", 2);
    }
    const std::vector<std::string> keep = fromStrings(names);
    std::vector<const char*> cs = cstrs(keep);
    std::vector<double> v;
    cape_corba::fromDoubleSeq(values, v);
    std::vector<xOptSolver::boolean> res(keep.size(), 0);
    if (!keep.empty() &&
        solver_->setDoubleOptions(res.data(), cs.data(), v.data(), static_cast<int>(keep.size())) < 0) {
        throw unknown(kSystemIface, "SetDoubleOptions", "the solver rejected setDoubleOptions");
    }
    accepted = toAccepted(res);
}

void MINLPSystemServant::GetStringOptions(const ct::CapeArrayString& names,
                                          ct::CapeArrayString_out values) {
    requireLive("GetStringOptions");
    const std::vector<std::string> keep = fromStrings(names);
    std::vector<const char*> cs = cstrs(keep);
    std::vector<const char*> v(keep.size(), nullptr);
    int size = static_cast<int>(keep.size());
    if (size > 0 && solver_->getStringOptions(cs.data(), v.data(), size) < 0) {
        throw invalidArgument(kSystemIface, "GetStringOptions",
                              "the solver rejected the request (unknown option name?)", 1);
    }
    // 值指针指向求解器自己的存储，当场拷进序列。
    ct::CapeArrayString_var out = new ct::CapeArrayString();
    out->length(static_cast<CORBA::ULong>(v.size()));
    for (CORBA::ULong i = 0; i < out->length(); ++i) {
        out[i] = CORBA::string_dup(v[i] != nullptr ? v[i] : "");
    }
    values = out._retn();
}

void MINLPSystemServant::SetStringOptions(const ct::CapeArrayString& names,
                                          const ct::CapeArrayString& values,
                                          ct::CapeArrayBoolean_out accepted) {
    requireLive("SetStringOptions");
    if (names.length() != values.length()) {
        throw invalidArgument(kSystemIface, "SetStringOptions", "names and values differ in length", 2);
    }
    const std::vector<std::string> keep_names = fromStrings(names);
    const std::vector<std::string> keep_values = fromStrings(values);
    std::vector<const char*> cn = cstrs(keep_names);
    std::vector<const char*> cv = cstrs(keep_values);
    std::vector<xOptSolver::boolean> res(keep_names.size(), 0);
    if (!keep_names.empty() &&
        solver_->setStringOptions(res.data(), cn.data(), cv.data(),
                                  static_cast<int>(keep_names.size())) < 0) {
        throw unknown(kSystemIface, "SetStringOptions", "the solver rejected setStringOptions");
    }
    accepted = toAccepted(res);
}

// ---- IXOptMINLPSystemExtension：结果 ----

ct::CapeLong MINLPSystemServant::GetSolveResult() {
    requireLive("GetSolveResult");
    return static_cast<ct::CapeLong>(result_);
}

ct::CapeArrayDouble* MINLPSystemServant::fetchVector(const char* operation, bool from_x,
                                                     bool multipliers) {
    requireLive(operation);
    if (!solved_) throw unknown(kSystemIface, operation, "no Solve has run yet");
    // X / Xmul 按变量数；F / Fmul 按 m+1（xOpt 约定：f[0] 是目标，其后是约束值）。
    const int want = from_x ? num_variables_ : num_constraints_ + 1;
    std::vector<double> buf(static_cast<size_t>(want > 0 ? want : 0), 0.0);
    int rc;
    if (from_x) {
        rc = multipliers ? solver_->Xmul(buf.data(), want) : solver_->X(buf.data(), want);
    } else {
        rc = multipliers ? solver_->Fmul(buf.data(), want) : solver_->F(buf.data(), want);
    }
    if (rc < 0) {
        throw unknown(kSystemIface, operation,
                      std::string("the solver has no ") + (multipliers ? "multipliers" : "values") +
                          " to report");
    }
    // 契约里 >0 是"填了几个"，0 是"成功"（按请求长度算）。
    if (rc > 0 && rc < want) buf.resize(static_cast<size_t>(rc));
    return new ct::CapeArrayDouble(cape_corba::toDoubleSeq(buf));
}

ct::CapeArrayDouble* MINLPSystemServant::GetX() { return fetchVector("GetX", true, false); }
ct::CapeArrayDouble* MINLPSystemServant::GetF() { return fetchVector("GetF", false, false); }
ct::CapeArrayDouble* MINLPSystemServant::GetXmul() { return fetchVector("GetXmul", true, true); }
ct::CapeArrayDouble* MINLPSystemServant::GetFmul() { return fetchVector("GetFmul", false, true); }

ct::CapeLong MINLPSystemServant::PauseSolve() {
    requireLive("PauseSolve");
    return static_cast<ct::CapeLong>(solver_->pauseSolve());
}

ct::CapeLong MINLPSystemServant::ContinueSolve() {
    requireLive("ContinueSolve");
    int rc = xOptSolver::RESULT_UNKNOWN;
    try {
        rc = solver_->continueSolve();
    } catch (const std::exception& e) {
        throw unknown(kSystemIface, "ContinueSolve", std::string("continueSolve threw: ") + e.what());
    } catch (...) {
        throw unknown(kSystemIface, "ContinueSolve", "continueSolve threw an unknown exception");
    }
    // RESULT_UNKNOWN(-1) 是"不支持/没动"的约定值（examples 的罚函数求解器就这么答），
    // 上一次的结果与写回保持原样。其余返回码意味着求解器真的往前走了——Ipopt
    // 那类后端在这里同步 ReOptimize——结果码与远端问题里的解都得跟着刷新，否则
    // GetSolveResult 停在 RESULT_USER_PAUSE、GetMINLPVariableValues 停在暂停点。
    if (rc != xOptSolver::RESULT_UNKNOWN) {
        std::string why;
        if (!recordOutcome(rc, why)) {
            throw unknown(kSystemIface, "ContinueSolve",
                          "continueSolve returned " + std::to_string(rc) + " (" + resultName(rc) +
                              "), but " + why);
        }
    }
    return static_cast<ct::CapeLong>(rc);
}

void MINLPSystemServant::Release() {
    if (released_) return;  // 幂等：客户端的析构路径可能重复走到这里
    released_ = true;
    teardown();
    // 注销自己。在自己的上行调用里 deactivate 是允许的：POA 等这次调用结束
    // 才真正回收 servant（本对象随最后一个引用释放而析构）。
    if (has_self_oid_ && !CORBA::is_nil(poa_.in())) {
        try {
            poa_->deactivate_object(self_oid_.in());
        } catch (const CORBA::Exception&) {
        }
    }
}

// ===========================================================================
//  SolverManagerServant
// ===========================================================================

SolverManagerServant::SolverManagerServant(XOptSolverLibrary* library, PortableServer::POA_ptr poa,
                                           xOptLogFunc log)
    : library_(library),
      poa_(PortableServer::POA::_duplicate(poa)),
      log_(log),
      comp_name_("xOpt MINLP Solver Manager"),
      comp_desc_("xOpt solver DLL published as a CAPE-OPEN MINLP solver manager") {
    if (library_ != nullptr && !library_->path().empty()) {
        comp_desc_ += " (" + library_->path() + ")";
    }
}

char* SolverManagerServant::GetComponentName() { return CORBA::string_dup(comp_name_.c_str()); }
char* SolverManagerServant::GetComponentDescription() {
    return CORBA::string_dup(comp_desc_.c_str());
}
void SolverManagerServant::SetComponentName(const char* name) {
    if (name != nullptr) comp_name_ = name;
}
void SolverManagerServant::SetComponentDescription(const char* desc) {
    if (desc != nullptr) comp_desc_ = desc;
}

void SolverManagerServant::CreateMINLPSystem(CORBA::Object_ptr theMINLP,
                                             CORBA::Object_out theMINLPSystem) {
    if (library_ == nullptr || !library_->loaded()) {
        throw unknown("ICapeMINLPSolverManager", "CreateMINLPSystem",
                      "the solver library is not loaded");
    }
    mi::ICapeMINLP_var problem = mi::ICapeMINLP::_narrow(theMINLP);
    if (CORBA::is_nil(problem.in())) {
        throw invalidArgument("ICapeMINLPSolverManager", "CreateMINLPSystem",
                              "theMINLP is nil or is not an ICapeMINLP", 1);
    }
    ++systems_created_;
    const std::string name = "xOptMINLPSystem#" + std::to_string(systems_created_);

    MINLPSystemServant* system = new MINLPSystemServant(library_, poa_.in(), problem.in(), name, log_);
    PortableServer::ServantBase_var owner(system);  // 出作用域 _remove_ref
    if (system->init() < 0) {
        // 把原话透出去：客户端那头收到的这句就是唯一线索。
        throw unknown("ICapeMINLPSolverManager", "CreateMINLPSystem", system->initError());
    }
    PortableServer::ObjectId_var oid = poa_->activate_object(system);
    system->setSelfOid(oid.in());
    theMINLPSystem = poa_->id_to_reference(oid.in());
}
