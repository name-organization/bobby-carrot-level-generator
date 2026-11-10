#include "sat.hpp"

#include <algorithm>
#include <climits>

#include "../third_party/cadical/cadical.hpp"

namespace bc::sat {

namespace {

// CaDiCaL numbers variables from 1 and negates a literal by its sign.
int toCadical(Lit l) {
    const int var = (l >> 1) + 1;
    return (l & 1) ? -var : var;
}

}  // namespace

Solver::Solver() : solver_(std::make_unique<CaDiCaL::Solver>()) {}

Solver::~Solver() = default;

void Solver::declare() {
    if (declared_ < vars_) solver_->declare_more_variables(vars_ - declared_);
    declared_ = vars_;
}

void Solver::addClause(const std::vector<Lit>& lits) {
    declare();
    for (Lit l : lits) solver_->add(toCadical(l));
    solver_->add(0);
}

Status Solver::solve(long long& conflictBudget) {
    if (conflictBudget <= 0) return Status::Unknown;
    declare();
    solver_->limit("conflicts", static_cast<int>(std::min<long long>(conflictBudget, INT_MAX)));
    switch (solver_->solve()) {
        case 10: return Status::Sat;
        case 20: return Status::Unsat;
        default:
            conflictBudget = 0;
            return Status::Unknown;
    }
}

bool Solver::value(int var) const {
    return solver_->val(var + 1) > 0;
}

}  // namespace bc::sat
