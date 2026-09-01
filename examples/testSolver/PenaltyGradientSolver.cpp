// ***************************************************************
//  PenaltyGradientSolver.cpp
//  -------------------------------------------------------------
//  罚函数梯度下降求解器的算法实现（算法说明见同名头文件）。
//
//  主流程（solve）：
//    1. 校验问题（OPTIONS_MAGIC == 'X'），读取维度/边界/初值/雅可比结构；
//    2. 外层循环：罚参数 mu 从 mu0 起步；
//    3. 内层：对增广目标 Phi 做带 Armijo 回溯线搜索的梯度下降；
//    4. 若最大约束违背仍大于 feas_tol，则 mu *= 10（上限 1e10）再来一轮；
//    5. 收敛（梯度无穷范数 < tol 且最大违背 < feas_tol）即返回。
// ***************************************************************
#include "PenaltyGradientSolver.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace {
constexpr double kMuMax = 1e10;   // 罚参数上限
constexpr double kArmijoC = 1e-4; // Armijo 条件常数
constexpr double kArmijoRho = 0.5;  // 步长收缩率
constexpr int kMaxMuRounds = 8;   // 外层罚参数轮数上限（100 -> 1e9）
constexpr int kMaxLineSearch = 60;
}  // namespace

PenaltyGradientSolver::PenaltyGradientSolver(const char* name, xOptProblem* problem,
                                             xOptLogFunc log_func)
    : problem_(problem), name_(name != nullptr ? name : ""), log_func_(log_func) {}

// ***************************************************************
// 增广目标与梯度
// ***************************************************************
// 纯求值：增广目标函数（不含梯度计算，供线搜索与差分兜底复用）
double PenaltyGradientSolver::evalPenaltyPhi(const Eigen::VectorXd& x) const {
    problem_->setX(x.data(), n_);
    double obj = 0.0;
    problem_->evaluateObjective(obj);
    double penalty = 0.0;
    if (m_ > 0) {
        std::vector<double> cons(m_, 0.0);
        problem_->evaluateConstraints(cons.data(), m_);
        for (int i = 0; i < m_; ++i) {
            const double target = std::clamp(cons[i], clow_[i], cupp_[i]);
            const double v = cons[i] - target;
            penalty += v * v;
        }
    }
    const Eigen::VectorXd bound_viol_lo = (xlow_ - x).cwiseMax(0.0);
    const Eigen::VectorXd bound_viol_up = (x - xupp_).cwiseMax(0.0);
    penalty += bound_viol_lo.squaredNorm() + bound_viol_up.squaredNorm();
    return obj + mu_ * penalty;
}

double PenaltyGradientSolver::phiAt(const Eigen::VectorXd& x) const {
    return evalPenaltyPhi(x);
}

void PenaltyGradientSolver::phiAndGrad(const Eigen::VectorXd& x, double& phi,
                                       Eigen::VectorXd& grad) const {
    // 求值前必须先 setX（平台运行迭代接口约定）
    problem_->setX(x.data(), n_);

    double obj = 0.0;
    problem_->evaluateObjective(obj);
    std::vector<double> cons(m_, 0.0);
    if (m_ > 0) problem_->evaluateConstraints(cons.data(), m_);

    // 约束违背：等式即 clow==cupp，统一用"到区间的距离"
    Eigen::VectorXd weight(m_);
    double penalty = 0.0;
    for (int i = 0; i < m_; ++i) {
        const double target = std::clamp(cons[i], clow_[i], cupp_[i]);
        const double v = cons[i] - target;
        weight[i] = 2.0 * mu_ * v;
        penalty += v * v;
    }

    // 边界违背
    const Eigen::VectorXd bound_viol_lo = (xlow_ - x).cwiseMax(0.0);
    const Eigen::VectorXd bound_viol_up = (x - xupp_).cwiseMax(0.0);
    penalty += bound_viol_lo.squaredNorm() + bound_viol_up.squaredNorm();

    phi = obj + mu_ * penalty;

    // ---- 梯度 ----
    grad.setZero(n_);
    if (!has_jacobian_ && m_ > 0) {
        // 兜底：问题未提供雅可比时，对 Phi 做中心差分（含边界罚项）
        for (int k = 0; k < n_; ++k) {
            const double h = 1e-6 * std::max(1.0, std::abs(x[k]));
            Eigen::VectorXd xp = x, xm = x;
            xp[k] += h;
            xm[k] -= h;
            grad[k] = (evalPenaltyPhi(xp) - evalPenaltyPhi(xm)) / (2.0 * h);
        }
        return;
    }

    // 目标梯度：结构大小 > 0 时取问题提供值，否则视为零
    if (obj_grad_nnz_ > 0) {
        std::vector<double> gvals(obj_grad_nnz_, 0.0);
        if (problem_->evaluateObjectiveGradient(gvals.data(), obj_grad_nnz_) >= 0) {
            for (int k = 0; k < obj_grad_nnz_; ++k) grad[obj_cols_[k]] += gvals[k];
        }
    }

    // 约束罚项梯度：Jᵀw（雅可比为常量结构 + 当前值）
    if (m_ > 0) {
        std::vector<double> jac_vals(jac_rows_.size(), 0.0);
        if (problem_->evaluateConstraintsJacobianValues(jac_vals.data(),
                                                        (int)jac_vals.size()) >= 0) {
            for (size_t k = 0; k < jac_vals.size(); ++k) {
                grad[jac_cols_[k]] += jac_vals[k] * weight[jac_rows_[k]];
            }
        }
    }

    // 边界罚项梯度（解析）：d/dx mu*(x-lo)^2 = 2mu*(x-lo)
    grad += 2.0 * mu_ * ((x - xupp_).cwiseMax(0.0) - (xlow_ - x).cwiseMax(0.0));
}

double PenaltyGradientSolver::maxViolation(const Eigen::VectorXd& x) const {
    problem_->setX(x.data(), n_);
    std::vector<double> cons(m_, 0.0);
    if (m_ > 0) problem_->evaluateConstraints(cons.data(), m_);
    double viol = 0.0;
    for (int i = 0; i < m_; ++i) {
        const double target = std::clamp(cons[i], clow_[i], cupp_[i]);
        viol = std::max(viol, std::abs(cons[i] - target));
    }
    for (int k = 0; k < n_; ++k) {
        viol = std::max(viol, std::max(xlow_[k] - x[k], x[k] - xupp_[k]));
    }
    return viol;
}

void PenaltyGradientSolver::log(ZLOG_LEVEL level, const char* format, ...) const {
    if (log_func_ == nullptr || print_level_ <= 0) return;
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    log_func_(level, "%s", buf);
}

// ***************************************************************
// 主求解流程
// ***************************************************************
int PenaltyGradientSolver::solve() {
    try {
        // ---- 1. 问题校验与数据读取 ----
        result_ = RESULT_INVALID_PROBLEM;
        if (problem_ == nullptr) {
            log(ZLOG_ERROR, "[%s] solve failed: problem is null", name_.c_str());
            return result_;
        }
        double options[xOptProblem::OPTIONS_LIMIT] = {0.0};
        if (problem_->getOptions(options, xOptProblem::OPTIONS_LIMIT) < 0 ||
            static_cast<int>(options[xOptProblem::OPTIONS_MAGIC]) != 'X') {
            log(ZLOG_ERROR, "[%s] solve failed: invalid problem (magic check)", name_.c_str());
            return result_;
        }

        n_ = problem_->numVariables();
        m_ = problem_->numConstraints();
        if (n_ <= 0 || m_ < 0) {
            log(ZLOG_ERROR, "[%s] solve failed: bad dimensions n=%d m=%d", name_.c_str(), n_,
                m_);
            return result_;
        }

        xlow_.resize(n_);
        xupp_.resize(n_);
        clow_.resize(m_);
        cupp_.resize(m_);
        Eigen::VectorXd x(n_);
        if (problem_->getVariableBounds(xlow_.data(), xupp_.data(), n_) < 0 ||
            problem_->getConstraintBounds(clow_.data(), cupp_.data(), m_) < 0 ||
            problem_->getInitialX(x.data(), n_) < 0) {
            log(ZLOG_ERROR, "[%s] solve failed: cannot read bounds/initial x", name_.c_str());
            result_ = RESULT_NUMERICAL_ISSUES;
            return result_;
        }

        // 雅可比结构（两段式：先报个数再填数据）
        int jac_nnz = 0;
        if (problem_->getConstraintJacobianStructure(nullptr, nullptr, jac_nnz) < 0)
            jac_nnz = 0;
        has_jacobian_ = (jac_nnz > 0);
        jac_rows_.assign(jac_nnz, 0);
        jac_cols_.assign(jac_nnz, 0);
        if (has_jacobian_) {
            int fill_nnz = jac_nnz;
            if (problem_->getConstraintJacobianStructure(jac_rows_.data(), jac_cols_.data(),
                                                         fill_nnz) < 0) {
                has_jacobian_ = false;
            }
        }

        // 目标梯度结构（两段式）
        obj_grad_nnz_ = 0;
        if (problem_->getObjectiveGradientStructure(nullptr, obj_grad_nnz_) < 0)
            obj_grad_nnz_ = 0;
        obj_cols_.assign(obj_grad_nnz_, 0);
        if (obj_grad_nnz_ > 0) {
            int fill = obj_grad_nnz_;
            if (problem_->getObjectiveGradientStructure(obj_cols_.data(), fill) < 0)
                obj_grad_nnz_ = 0;
        }

        log(ZLOG_INFOR, "[%s] problem loaded: n=%d, m=%d, jacobian nnz=%d, mu0=%.1f",
            name_.c_str(), n_, m_, jac_nnz, mu0_);

        // ---- 2. 外层：罚参数自适应 ----
        mu_ = mu0_;
        result_ = RESULT_ITER_LIMIT;
        double grad_inf = 0.0;
        double viol = 0.0;
        for (int round = 0; round < kMaxMuRounds; ++round) {
            log(ZLOG_INFOR, "[%s] outer round %d: mu = %.3e", name_.c_str(), round + 1, mu_);

            // ---- 3. 内层：梯度下降 + Armijo 回溯线搜索 ----
            bool inner_converged = false;
            for (int iter = 0; iter < max_iter_; ++iter) {
                double phi = 0.0;
                Eigen::VectorXd grad(n_);
                phiAndGrad(x, phi, grad);
                grad_inf = grad.lpNorm<Eigen::Infinity>();
                viol = maxViolation(x);
                if (grad_inf < tol_ && viol < feas_tol_) {
                    inner_converged = true;
                    log(ZLOG_INFOR,
                        "[%s] converged at inner iter %d: Phi=%.6e, |grad|inf=%.3e, "
                        "max_viol=%.3e",
                        name_.c_str(), iter, phi, grad_inf, viol);
                    break;
                }

                // 方向：最速下降；步长：Armijo 回溯
                const Eigen::VectorXd dir = -grad;
                const double dir_deriv = grad.dot(dir);  // = -|grad|^2
                double alpha = 1.0;
                for (int ls = 0; ls < kMaxLineSearch; ++ls) {
                    const Eigen::VectorXd x_new = x + alpha * dir;
                    const double phi_new = phiAt(x_new);
                    if (std::isfinite(phi_new) &&
                        phi_new <= phi + kArmijoC * alpha * dir_deriv) {
                        break;
                    }
                    alpha *= kArmijoRho;
                }
                x += alpha * dir;

                if (print_level_ >= 2 && iter % 200 == 0) {
                    log(ZLOG_INFOR, "[%s]   iter %d: Phi=%.6e, |grad|inf=%.3e, viol=%.3e",
                        name_.c_str(), iter, phi, grad_inf, viol);
                }
            }

            if (inner_converged) {
                result_ = (obj_grad_nnz_ == 0) ? RESULT_FEASIBLE : RESULT_OPTIMAL;
                break;
            }
            viol = maxViolation(x);
            if (viol < feas_tol_) {  // 已可行，仅梯度条件未达：接受为可行解
                result_ = RESULT_FEASIBLE;
                break;
            }
            if (mu_ >= kMuMax) break;
            mu_ = std::min(mu_ * 10.0, kMuMax);
        }

        // ---- 4. 缓存结果：X 与 F（f[0]=目标，f[1..m]=约束值）----
        x_ = x;
        problem_->setX(x_.data(), n_);
        f_.resize(m_ + 1);
        double obj = 0.0;
        problem_->evaluateObjective(obj);
        f_[0] = obj;
        if (m_ > 0) {
            std::vector<double> cons(m_, 0.0);
            problem_->evaluateConstraints(cons.data(), m_);
            for (int i = 0; i < m_; ++i) f_[1 + i] = cons[i];
        }

        log(ZLOG_INFOR, "[%s] solve finished: result=%d, objective=%.6e, max_viol=%.3e",
            name_.c_str(), result_, f_[0], maxViolation(x_));
        return result_;
    } catch (const std::exception& e) {
        log(ZLOG_ERROR, "[%s] solve exception: %s", name_.c_str(), e.what());
        result_ = RESULT_NUMERICAL_ISSUES;
        return result_;
    } catch (...) {
        log(ZLOG_ERROR, "[%s] solve unknown exception", name_.c_str());
        result_ = RESULT_NUMERICAL_ISSUES;
        return result_;
    }
}

// ***************************************************************
// 结果查询：X 返回解；F 返回 m+1 个值（平台 printF 约定）
// ***************************************************************
int PenaltyGradientSolver::X(double* x, int x_size) const {
    if (x_.size() == 0 || x_size < x_.size()) return -1;
    for (int i = 0; i < x_.size(); ++i) x[i] = x_[i];
    return static_cast<int>(x_.size());
}

int PenaltyGradientSolver::F(double* x, int x_size) const {
    if (f_.size() == 0 || x_size < f_.size()) return -1;
    for (int i = 0; i < f_.size(); ++i) x[i] = f_[i];
    return static_cast<int>(f_.size());
}

// ***************************************************************
// 可调参数：两段式查询（p_name == nullptr 时只报个数）
// ***************************************************************
int PenaltyGradientSolver::getTunableParamList(const char* p_name[], OPTION_TYPE p_type[],
                                               int& p_size) const {
    constexpr int kCount = 4;
    if (p_name == nullptr && p_type == nullptr) {
        p_size = kCount;
        return 0;
    }
    if (p_size < kCount) return -1;
    p_name[0] = "max_iter";
    p_type[0] = OPTION_INT;
    p_name[1] = "tol";
    p_type[1] = OPTION_REAL;
    p_name[2] = "feas_tol";
    p_type[2] = OPTION_REAL;
    p_name[3] = "mu0";
    p_type[3] = OPTION_REAL;
    p_size = kCount;
    return 0;
}

// 接口只携带参数名列表（值经 options 通道设置）：校验名字是否认识
int PenaltyGradientSolver::setTunableParamList(const char* p_name[], int p_size) {
    for (int i = 0; i < p_size; ++i) {
        const std::string name(p_name[i] != nullptr ? p_name[i] : "");
        if (name != "max_iter" && name != "tol" && name != "feas_tol" && name != "mu0") {
            log(ZLOG_WARNI, "[%s] unknown tunable parameter: %s", name_.c_str(), name.c_str());
            return -1;
        }
    }
    return 0;
}

// ***************************************************************
// options 通道：最小实现——字符串只读暴露求解器名；
// int 支持 print_level / max_iter；double 支持 tol / feas_tol / mu0
// ***************************************************************
int PenaltyGradientSolver::getStringOptions(const char* option_names[],
                                            const char* option_values[],
                                            int& options_size) const {
    if (option_names == nullptr && option_values == nullptr) {
        options_size = 1;  // 只有 "solver_name"
        return 0;
    }
    for (int i = 0; i < options_size; ++i) {
        if (std::string(option_names[i] != nullptr ? option_names[i] : "") == "solver_name") {
            option_values[i] = name_.c_str();
        } else {
            return -1;
        }
    }
    return 0;
}

int PenaltyGradientSolver::setStringOptions(boolean option_results[],
                                            const char* option_names[],
                                            const char* option_values[], int options_size) {
    for (int i = 0; i < options_size; ++i) {
        option_results[i] = 0;  // 字符串选项只读，一律拒绝
        (void)option_names;
        (void)option_values;
    }
    return 0;
}

int PenaltyGradientSolver::getIntOptions(const char* option_names[], int option_values[],
                                         int& options_size) const {
    if (option_names == nullptr && option_values == nullptr) {
        options_size = 2;  // print_level, max_iter
        return 0;
    }
    for (int i = 0; i < options_size; ++i) {
        const std::string name(option_names[i] != nullptr ? option_names[i] : "");
        if (name == "print_level") {
            option_values[i] = print_level_;
        } else if (name == "max_iter") {
            option_values[i] = max_iter_;
        } else {
            return -1;
        }
    }
    return 0;
}

int PenaltyGradientSolver::setIntOptions(boolean option_results[], const char* option_names[],
                                         const int option_values[], int options_size) {
    for (int i = 0; i < options_size; ++i) {
        const std::string name(option_names[i] != nullptr ? option_names[i] : "");
        if (name == "print_level") {
            print_level_ = option_values[i];
            option_results[i] = 1;
        } else if (name == "max_iter" && option_values[i] > 0) {
            max_iter_ = option_values[i];
            option_results[i] = 1;
        } else {
            option_results[i] = 0;  // 不支持的选项
        }
    }
    return 0;
}

int PenaltyGradientSolver::getDoubleOptions(const char* option_names[],
                                            double option_values[], int& options_size) const {
    if (option_names == nullptr && option_values == nullptr) {
        options_size = 3;  // tol, feas_tol, mu0
        return 0;
    }
    for (int i = 0; i < options_size; ++i) {
        const std::string name(option_names[i] != nullptr ? option_names[i] : "");
        if (name == "tol") {
            option_values[i] = tol_;
        } else if (name == "feas_tol") {
            option_values[i] = feas_tol_;
        } else if (name == "mu0") {
            option_values[i] = mu0_;
        } else {
            return -1;
        }
    }
    return 0;
}

int PenaltyGradientSolver::setDoubleOptions(boolean option_results[],
                                            const char* option_names[],
                                            const double option_values[], int options_size) {
    for (int i = 0; i < options_size; ++i) {
        const std::string name(option_names[i] != nullptr ? option_names[i] : "");
        if (name == "tol" && option_values[i] > 0.0) {
            tol_ = option_values[i];
            option_results[i] = 1;
        } else if (name == "feas_tol" && option_values[i] > 0.0) {
            feas_tol_ = option_values[i];
            option_results[i] = 1;
        } else if (name == "mu0" && option_values[i] > 0.0) {
            mu0_ = option_values[i];
            option_results[i] = 1;
        } else {
            option_results[i] = 0;  // 不支持的选项
        }
    }
    return 0;
}
