// Thin wrapper around the CaDiCaL SAT solver (src/third_party/cadical).
//
// The hint code talks to this interface only, so the solver behind it can be
// swapped without touching hint.cpp.
#pragma once

#include <memory>
#include <vector>

namespace CaDiCaL {
class Solver;
}

namespace bc::sat {

// A literal is `2 * var` for the variable and `2 * var + 1` for its negation.
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

    // Searches until an answer is found or `conflictBudget` conflicts have been
    // spent. On Unknown the budget is used up (set to zero).
    Status solve(long long& conflictBudget);

    // Value of a variable in the model found by the last Sat answer.
    bool value(int var) const;

private:
    // Declares variables created since the last call, so CaDiCaL never
    // hands one of ours out to its own preprocessing.
    void declare();

    std::unique_ptr<CaDiCaL::Solver> solver_;
    int vars_ = 0;
    int declared_ = 0;
};

}  // namespace bc::sat
