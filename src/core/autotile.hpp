/**
 * @file
 * @brief Select hedge and fence sprites by their four cardinal neighbours.
 *
 * @details Tables are inferred from the original levels; diagonals do not participate.
 */
#pragma once

#include "canvas.hpp"

namespace bc {

constexpr bool isHedge(std::uint8_t t) { return t <= 11; }
constexpr bool isFence(std::uint8_t t) { return t >= 12 && t <= 17; }

/**
 * @brief Rewrite wall sprites from a snapshot, preserving all non-wall tiles.
 * */
void autotile(Canvas& canvas);

}  // namespace bc
