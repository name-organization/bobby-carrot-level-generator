/**
 * @file
 * @brief CaDiCaL adapter isolating the hint implementation from solver-specific APIs.
 */
#pragma once

#include <memory>
#include <vector>

namespace CaDiCaL {
class Solver;
}

namespace bc::sat {

/** @brief Zero-based encoding: 2 * variable is positive, 2 * variable + 1 is negative. */
using Lit = int;

constexpr Lit pos(int var) { return var * 2; }
constexpr Lit neg(int var) { return var * 2 + 1; }

enum class Status { Sat, Unsat, Unknown };

class Solver {
public:
    Solver();
    ~Solver();
    Solver(const Solver&) = delete;
    Solver& operator=(const Solver&) = delete;

    int newVar() { return vars_++; }
    int varCount() const { return vars_; }

    void addClause(const std::vector<Lit>& lits);
    void addClause(Lit a)               { addClause(std::vector<Lit>{a}); }
    void addClause(Lit a, Lit b)        { addClause(std::vector<Lit>{a, b}); }
    void addClause(Lit a, Lit b, Lit c) { addClause(std::vector<Lit>{a, b, c}); }

    /**
     * @brief Solve with a caller-owned conflict budget.
     *
     * @param[in,out] conflictBudget Remaining conflicts; set to zero on Unknown.
     * @return Sat, Unsat or Unknown when the budget is exhausted.
     */
    Status solve(long long& conflictBudget);

    /**
     * @brief Read a variable from the last satisfiable model.
     *
     * @pre The last solve() returned Status::Sat.
     */
    bool value(int var) const;

private:
    /** @brief Reserve new variables before CaDiCaL preprocessing can reuse their IDs. */
    void declare();

    std::unique_ptr<CaDiCaL::Solver> solver_;
    int vars_ = 0;
    int declared_ = 0;
};

}  // namespace bc::sat
