// Overlapping wave function collapse on a five-symbol alphabet.
//
// Model: every 3x3 window of the original carrot levels, written in symbols
// (hedge, fence, path, carrot, spike) instead of tiles and taken in all eight
// orientations. The symbols do not care which way they face -- autotile()
// picks the hedge and fence pieces afterwards -- so turning the windows is
// free extra data: 30 levels become 240.
//
// solve() fills a region so that every 3x3 window inside it is one of the
// learned windows, under a mask per cell (a door stays path, a wall stays
// hedge or fence, ...). It propagates constraints between the windows (a
// window keeps the patterns its cells still allow, a cell keeps the symbols
// its windows still allow), collapses the most constrained cell to a symbol
// drawn by how often the originals have it there, and backtracks depth-first
// within a node budget.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "canvas.hpp"
#include "rng.hpp"

namespace bc::wfc {

enum Symbol : std::uint8_t { Hedge, Fence, Path, Carrot, Spike, kSymbols };

using Mask = std::uint8_t;  // one bit per symbol
constexpr Mask bit(Symbol s) { return static_cast<Mask>(1u << s); }
constexpr Mask kAny = static_cast<Mask>((1u << kSymbols) - 1);
constexpr Mask kWall = bit(Hedge) | bit(Fence);
constexpr Mask kWalkable = bit(Path) | bit(Carrot) | bit(Spike);

Symbol symbolOf(std::uint8_t tile);
// A stand-in tile; hedge and fence get their real pieces from autotile().
std::uint8_t tileOf(Symbol s);

struct Model {
    std::vector<std::array<Symbol, 9>> patterns;  // 3x3, row by row, sorted
    std::vector<std::uint32_t> counts;            // how often each occurs
    bool empty() const { return patterns.empty(); }
    bool has(const std::array<Symbol, 9>& pattern) const;
};

// How many of the 3x3 windows of `c` that cover (x, y) and lie inside it do
// not occur in the model (0 to 9)?
int unknownWindows(const Model& model, const Canvas& c, int x, int y);

Model learn(const std::vector<Canvas>& samples, bool allOrientations = true);

// w x h cells, one mask each; a cell with a single bit is fixed.
struct Region {
    int w = 0;
    int h = 0;
    std::vector<Mask> cells;
    // Weight of each symbol, in percent of what the originals give it, when a
    // cell is collapsed. The originals are mostly
    // hedge, so at 100 WFC overgrows a room; and in a small room nearly every
    // cell touches a wall, where the originals mostly have path, so carrots
    // come out rarer than in the originals unless they weigh more.
    std::array<int, kSymbols> percent{100, 100, 100, 100, 100};
};

// On success every cell holds exactly one bit. On failure the region is left
// as it was. Needs a region of at least 3x3.
bool solve(const Model& model, Region& region, Rng& rng, int nodeBudget = 300);

}  // namespace bc::wfc
