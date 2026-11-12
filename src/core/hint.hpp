/**
 * @file
 * @brief Winning-route hints for ground, carrots, spikes, start and exit.
 *
 * @details Tests reachability, rejoins a remembered route, then uses SAT if needed;
 * a winning route is then rebuilt with as few spikes as the same carrot order allows.
 * @see docs/smt-formalization.md for the model and completeness argument.
 */
#pragma once

#include <vector>

#include "level.hpp"
#include "types.hpp"

namespace bc::hint {

enum class Verdict { Winnable, DeadEnd, Unknown };
enum class Stage { Won, Flood, Reference, Solver, Unsupported };

struct Result {
    Verdict verdict = Verdict::Unknown;
    Stage stage = Stage::Unsupported;

    std::vector<Dir> moves;  ///< Winning moves from the current cell; empty for other verdicts.
};

struct Options {
    long long conflictBudget = 400000;  ///< Exhaustion returns Verdict::Unknown.

    bool useSolver = true;  ///< False stops at Stage::Reference if cheap checks are inconclusive.

    /// Rebuild the route with as few spikes as the same carrot order allows. Off in the
    /// level generator, whose solutions are part of a level (a seed keeps its level).
    bool spareSpikes = true;
};

/** @brief Check the base tile set and require exactly one exit. */
bool supported(const Grid& grid);

/** @brief Expand moves into visited cells, including the start. */
std::vector<Coord> cellsAlong(Coord start, const std::vector<Dir>& moves);

/**
 * @brief Find a winning route from the current playfield state.
 *
 * @param grid Live grid with collected carrots and collapsed spikes already applied.
 * @param pos Current player cell.
 * @param reference Previously winning cell path, which may start elsewhere.
 * @param options Solver policy and conflict budget.
 * @return Verdict and moves from pos; Unknown covers unsupported maps and budget exhaustion.
 * @note Reuse cellsAlong(pos, result.moves) after a Winnable result.
 */
Result compute(const Grid& grid, Coord pos, const std::vector<Coord>& reference,
               const Options& options = {});

const char* stageName(Stage stage);
const char* verdictName(Verdict verdict);

}  // namespace bc::hint
