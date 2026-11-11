#include "autotile.hpp"

namespace bc {
namespace {

// Neighbour bits: N = 1, E = 2, S = 4, W = 8.
constexpr int kDX[4] = {0, 1, 0, -1};
constexpr int kDY[4] = {-1, 0, 1, 0};

// Hedge piece by its open sides (neighbours that are not hedge). The counts in
// the comments are how often the originals use that piece for that mask; the
// rare disagreements are hand-drawn exceptions.
constexpr std::uint8_t kHedge[16] = {
    0,   // -      '#'  4576
    4,   // N      '4'   579  top edge of the hedge below an open cell
    1,   // E      '1'   511
    8,   // NE     '8'    92
    3,   // S      '3'   558
    10,  // NS     '('    19  thin hedge between two open rows
    5,   // ES     '5'   102
    10,  // NES    '('     9
    2,   // W      '2'   513
    7,   // NW     '7'    92
    11,  // EW     ')'     8  thin hedge between two open columns
    11,  // NEW    ')'     4
    6,   // SW     '6'    88
    10,  // NSW    '('    10
    11,  // ESW    ')'     2
    9,   // NESW   '9'    16  lone bush
};

// Fence piece by its fence neighbours.
constexpr std::uint8_t kFence[16] = {
    13,  // -      '"'     8
    13,  // N      '"'    78
    16,  // E      '['    89  left end
    16,  // NE     '['    40  bottom-left corner
    13,  // S      '"'    95
    13,  // NS     '"'   280
    14,  // ES     '{'    33  top-left corner
    13,  // NES    '"'     6
    17,  // W      ']'    84  right end
    17,  // NW     ']'    40  bottom-right corner
    12,  // EW     '='   223
    12,  // NEW    '='    11
    15,  // SW     '}'    32  top-right corner
    15,  // NSW    '}'     7
    12,  // ESW    '='     7
    12,  // NESW   '='     1
};

}  // namespace

void autotile(Canvas& canvas) {
    const Canvas src = canvas;  // decide from the input, not from half-rewritten cells
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            const std::uint8_t t = src.at(x, y);
            if (!isHedge(t) && !isFence(t)) continue;
            int mask = 0;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + kDX[k], ny = y + kDY[k];
                // Off the map counts as hedge: the island never touches the edge.
                const std::uint8_t n = src.inBounds(nx, ny) ? src.at(nx, ny) : 0;
                const bool bit = isHedge(t) ? !isHedge(n) : isFence(n);
                if (bit) mask |= 1 << k;
            }
            canvas.at(x, y) = isHedge(t) ? kHedge[mask] : kFence[mask];
        }
    }
}

}  // namespace bc
