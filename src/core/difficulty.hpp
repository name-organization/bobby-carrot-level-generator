/**
 * @file
 * @brief Integer difficulty rating shared by native and browser generation.
 *
 * @details Score averages route coverage, spike density and the square root of
 * the naive player's remaining-objective share, using map-size anchors.
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
constexpr int kNaiveTries = 40;
constexpr int kNaiveRandomPercent = 5;  ///< Steps the naive player takes at random.
/** @brief Maximum candidates per request; the first uses the original seed. */
constexpr int kTries = 10;

struct Rating {
    int cover = 0;   ///< moves per 100 walkable cells
    int spikes = 0;  ///< spikes per 100 walkable cells, x100
    int naive = 0;   ///< Unfinished carrots and exit, averaged over naive trials, in permille.
    int score = 0;   ///< 0..100
};

const char* name(Difficulty d);
bool parse(std::string_view text, Difficulty& out);

/** @brief Classify a score into Easy, Medium or Hard. */
Difficulty band(int score);
int bandLow(Difficulty d);
int bandHigh(Difficulty d);
int distanceToBand(int score, Difficulty d);

/**
 * @brief Simulate a player seeking the nearest carrot, then the exit.
 *
 * @details Ties are randomized; kNaiveRandomPercent of steps take a random legal direction.
 * @return Remaining carrots plus the exit in permille of all objectives; zero after a win.
 */
int naiveLeft(const Level& level, Rng& rng);
/** @brief Average unfinished-objective share over kNaiveTries deterministic runs. */
int naiveLeftPermille(const Level& level, std::uint32_t seed);

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
