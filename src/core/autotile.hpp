// Picks the hedge and fence pieces from their neighbours.
//
// The generator only says "hedge here" (any tile 0..11) or "fence here" (any
// tile 12..17); autotile() then chooses the piece the original levels use for
// that neighbourhood. Both tables were read off all 50 original levels: for
// every hedge cell, which of its four neighbours are not hedge; for every fence
// cell, which of its four neighbours are fence. Diagonals never matter -- a
// hedge cell open only diagonally is always the solid '#'.
#pragma once

#include "canvas.hpp"

namespace bc {

constexpr bool isHedge(std::uint8_t t) { return t <= 11; }
constexpr bool isFence(std::uint8_t t) { return t >= 12 && t <= 17; }

// Rewrites every hedge and fence cell of the canvas; other cells are kept.
void autotile(Canvas& canvas);

}  // namespace bc
