// How hard a generated level is, and the three difficulties a level can be
// asked for.
//
// A level gets a score from 0 to 100 out of three measures:
//
//   cover   moves of the reference solution (the winning moves the generator
//           verified the level with) per 100 walkable cells. A move is one
//           cell and the path is one cell wide, so moves and area are both
//           counts of cells: the ratio is how many times over Bobby walks the
//           island, and it barely changes with the size of the map. (Moves
//           over the square root of the area, a length over a length, grows
//           with the map instead: 6.2 / 8.8 / 16.3 on 16 / 24 / 32 cells,
//           because the route has to visit every room, not cross the map.)
//   spikes  spikes per 100 walkable cells, x100: every spike is a cell that
//           can be crossed once.
//   naive   how much a player who always goes for the nearest carrot leaves
//           behind when a spike cuts him off: the carrots (and the exit) he
//           can no longer reach, in permille of all of them, averaged over
//           kNaiveTries tries. A share rather than "lost or not", and few
//           random steps, so that one spike more or less moves it a little
//           instead of flipping it (see "smoothness" below).
//
// Each measure is scaled to 0..1000 between an "easy" and a "hard" anchor --
// naive by its square root, which spreads the many levels where the player
// leaves little and squeezes the few where he leaves a lot -- and the score
// is their mean, rounded to 0..100. The anchors depend on the map size (16,
// 24, 32): they are what is typical of a level of that size, so that the
// score says how hard a level is for its size, and Easy / Medium / Hard mean
// the same on every map.
//
// Easy is a score below kMediumFrom, Hard one of kHardFrom and up, Medium what
// lies between. All of it is integer arithmetic, so that a seed gives the same
// level natively and in the browser.
//
// Smoothness: taking one spike out of a medium level used to move its score
// by more than 24 points -- the whole width of Medium -- in 4.0 / 2.9 / 1.4%
// of cases on 16 / 24 / 32 maps, so tuning a level into its band
// (roomgen.cpp) could jump over it. With the share, the square root and
// naive weighing once instead of twice: 3.0 / 0.2 / 0.1% (100 medium levels
// per size, every spike taken out in turn).
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

constexpr int kMediumFrom = 38;  // score where Medium starts
constexpr int kHardFrom = 62;    // score where Hard starts
constexpr int kNaiveTries = 40;
constexpr int kNaiveRandomPercent = 5;  // steps the naive player takes at random
// Levels built for one request: the first one in the band wins, else the one
// closest to it. The first try uses the seed itself.
constexpr int kTries = 3;

struct Rating {
    int cover = 0;   // moves per 100 walkable cells
    int spikes = 0;  // spikes per 100 walkable cells, x100
    int naive = 0;   // what the naive player leaves behind, permille
    int score = 0;   // 0..100
};

const char* name(Difficulty d);
bool parse(std::string_view text, Difficulty& out);

// The band a score falls into, the lowest and highest score of a band, and how
// far a score is from a band (0 inside).
Difficulty band(int score);
int bandLow(Difficulty d);
int bandHigh(Difficulty d);
int distanceToBand(int score, Difficulty d);

// Walks to the nearest carrot left (to the exit once there are none), never
// onto a hole, breaking ties at random and taking a random step
// kNaiveRandomPercent of the time, until nothing it needs can be reached any
// more (a spike it burnt cut it off) or it has won. Returns what is left when it stops: 0 once it reaches the exit with every
// carrot, else the carrots it can no longer reach plus the exit, in permille
// of the carrots plus the exit.
int naiveLeft(const Level& level, Rng& rng);
// naiveLeft averaged over kNaiveTries tries; the same seed, the same answer.
int naiveLeftPermille(const Level& level, std::uint32_t seed);

// `moves` wins the level from its start; `seed` drives the naive player.
Rating rate(const Level& level, const std::vector<Dir>& moves, std::uint32_t seed);

// The seed of try `k` of a request with `seed`; try 0 is the seed itself.
std::uint32_t trySeed(std::uint32_t seed, int k);

}  // namespace difficulty
}  // namespace bc
