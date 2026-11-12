/**
 * @file
 * @brief Overlapping WFC using 3x3 patterns of hedge, fence, path, carrot and spike.
 *
 * @details Constraint propagation prunes patterns per window and symbols per cell.
 * Search collapses the most constrained cell and backtracks within a node budget.
 */
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "canvas.hpp"
#include "rng.hpp"

namespace bc::wfc {

enum Symbol : std::uint8_t { Hedge, Fence, Path, Carrot, Spike, kSymbols };

using Mask = std::uint8_t;  ///< one bit per symbol
constexpr Mask bit(Symbol s) { return static_cast<Mask>(1u << s); }
constexpr Mask kAny = static_cast<Mask>((1u << kSymbols) - 1);
constexpr Mask kWall = bit(Hedge) | bit(Fence);
constexpr Mask kWalkable = bit(Path) | bit(Carrot) | bit(Spike);

Symbol symbolOf(std::uint8_t tile);
/** @brief Map a symbol to a base tile; autotile() selects wall sprites later. */
std::uint8_t tileOf(Symbol s);

struct Model {
    std::vector<std::array<Symbol, 9>> patterns;  ///< 3x3, row by row, sorted
    std::vector<std::uint32_t> counts;            ///< how often each occurs
    bool empty() const { return patterns.empty(); }
    bool has(const std::array<Symbol, 9>& pattern) const;
};

/**
 * @brief Count unknown 3x3 windows containing the given cell.
 *
 * @return Count from 0 to 9, excluding windows outside the canvas.
 */
int unknownWindows(const Model& model, const Canvas& c, int x, int y);

Model learn(const std::vector<Canvas>& samples, bool allOrientations = true);

/** @brief Dense w by h symbol masks; a single bit pins a cell. */
struct Region {
    int w = 0;
    int h = 0;
    std::vector<Mask> cells;
    /** @brief Symbol weights as percentages of learned occurrence counts. */
    std::array<int, kSymbols> percent{100, 100, 100, 100, 100};
};

/**
 * @brief Fill a region with learned patterns under its cell masks.
 *
 * @param model Learned patterns and occurrence counts.
 * @param[in,out] region At least 3x3; unchanged on failure, singleton masks on success.
 * @param[in,out] rng Deterministic random stream.
 * @param nodeBudget Maximum search nodes.
 * @return Whether a complete assignment was found.
 */
bool solve(const Model& model, Region& region, Rng& rng, int nodeBudget = 300);

}  // namespace bc::wfc
