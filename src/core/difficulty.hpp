/**
 * @file
 * @brief Integer difficulty rating shared by native and browser generation.
 *
 * @details Score combines route coverage, spike density and naive-player losses
 * using map-size anchors. Naive losses have twice the weight of either other measure.
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "level.hpp"
#include "rng.hpp"
#include "types.hpp"

namespace bc {

enum class Difficulty : std::uint8_t { Easy, Medium, Hard };

namespace difficulty {

constexpr int kMediumFrom = 38;  ///< score where Medium starts
constexpr int kHardFrom = 62;    ///< score where Hard starts
constexpr int kNaiveTries = 20;
/** @brief Maximum candidates per request; the first uses the original seed. */
constexpr int kTries = 3;

struct Rating {
    int cover = 0;   ///< moves per 100 walkable cells
    int spikes = 0;  ///< spikes per 100 walkable cells, x100
    int naive = 0;   ///< naive tries lost, percent
    int score = 0;   ///< 0..100
};

const char* name(Difficulty d);
bool parse(std::string_view text, Difficulty& out);

/** @brief Classify a score into Easy, Medium or Hard. */
Difficulty band(int score);
int distanceToBand(int score, Difficulty d);

/**
 * @brief Simulate a player seeking the nearest carrot, then the exit.
 *
 * @details Ties are randomized; one step in five takes a random legal direction.
 * @return False when required cells become unreachable or the step budget expires.
 */
bool naiveWins(const Level& level, Rng& rng);
/** @brief Measure naive-player losses in percent over kNaiveTries deterministic runs. */
int naiveLossPercent(const Level& level, std::uint32_t seed);

/**
 * @brief Score a level using a known winning solution.
 *
 * @param level Level with derived totals already computed.
 * @param moves Winning moves from the level start.
 * @param seed Random seed for the naive-player trials.
 */
Rating rate(const Level& level, const std::vector<Dir>& moves, std::uint32_t seed);

/** @brief Derive a deterministic candidate seed; attempt zero keeps the input seed. */
std::uint32_t trySeed(std::uint32_t seed, int k);

}  // namespace difficulty
}  // namespace bc
