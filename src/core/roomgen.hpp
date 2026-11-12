// Room-based level generator (the Spelunky approach).
//
// The map is cut into a grid of equal room slots separated by walls:
//
//   1. Layout   a random self-avoiding path over the slots from the start room
//               to the exit room, plus side rooms hanging off it (dead ends).
//               Unused slots stay hedge, which gives the island its outline.
//   2. Doors    every room is joined to the one it was reached from, and more
//               doors between neighbouring rooms close cycles, so that there is
//               more than one way through. Any door may be spikes: crossed once,
//               it can cut Bobby off from what lies behind it if he picks the
//               wrong moment -- the solver makes sure some order still wins.
//   3. Content  wave function collapse over 3x3 patterns of the originals
//               (wfc.hpp), room by room together with its walls; or the fixed
//               patterns (carrot fields, rows, pillars...) without a model.
//   4. Autotile hedge and fence pieces from their neighbours (autotile.hpp).
//   5. Verify   the hint solver must find a win. Its moves are the reference
//               solution the game hands to the hint. If the spikes leave no way
//               to win, all are taken out and put back one at a time, doors
//               first, each kept only if the solver still wins.
//   6. Spikes   extra spikes up to a target, each one kept only if the solver
//               still finds a win with it.
//
// Without spikes the layout is winnable by construction (every room is
// reachable and nothing closes); the spikes are what the solver checks, and
// step 5 is what makes "verified" a fact.
//
// Everything works on a Canvas of any size. A canvas the engine can play (up
// to Grid::kMaxW x Grid::kMaxH) is verified; a bigger one is only drawn.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "canvas.hpp"
#include "difficulty.hpp"
#include "types.hpp"

namespace bc::wfc {
struct Model;
}

namespace bc::roomgen {

enum class Walls : std::uint8_t {
    Auto,       // pick one of the below per level
    Fence,      // 1-cell wooden fences between rooms: "{===}" pens
    ThinHedge,  // 1-cell hedge between rooms
    Hedge,      // 2-cell hedge between rooms, like normal07
};

enum class RoomKind : std::uint8_t { Start, Main, Exit, Side };

struct Params {
    int width    = Grid::kClassic;
    int height   = Grid::kClassic;
    int roomSize = 4;            // cells inside one room slot, per side
    Walls walls  = Walls::Auto;
    double fill            = 0.75;  // share of the room slots the level uses (3 rooms at least)
    double mainShare       = 0.65;  // share of used rooms on the start-exit path
    // Per pair of neighbouring rooms the layout tree leaves apart: a door that
    // closes a cycle, so that most rooms open into 2-4 others and there is
    // more than one way through.
    double loopChance      = 0.8;
    // Per door, any door: laid as spikes. With cycles a spike door is a real
    // choice -- cross it at the wrong time and something is cut off -- so the
    // solver keeps only the spikes the level can still be won with.
    double spikeDoorChance = 0.5;
    double mergeChance     = 0.15;  // plain door widened to the whole wall
    double insetChance     = 0.35;  // room shrunk by a cell on one side
    double emptyRoomChance = 0.25;  // path room without carrots (fixed patterns only)
    // Spikes the level should end up with, doors included (the originals have
    // 6.8 on average). Missing ones are added after the layout: cells are tried
    // in random order (corridor-like cells first) and a spike stays only if the
    // solver still finds a win. Playable sizes only, since it needs the solver.
    int spikeTarget = 6;
    // With a WFC model, an added spike may make at most this many of the nine
    // 3x3 windows around it unlike anything in the originals. Every added
    // spike costs some likeness: on 300 levels, scored against originals WFC
    // did not learn from, no added spikes gives 2.1 spikes and 57% of known
    // windows, at most 2 unknown 4.7 and 48%, no limit 6.0 and 47%.
    int spikeUnknownWindows = 2;
    int attempts = 40;              // layouts tried until one passes the solver
    // Learned 3x3 patterns (wfc.hpp). With a model the rooms and the walls
    // between them are filled by wave function collapse; without one, or in a
    // room WFC cannot fill, by the fixed patterns (fields, rows, pillars...).
    const wfc::Model* model = nullptr;
    // Symbol weights inside a room for WFC, in percent of what the originals
    // give them (wfc::Region::percent). Hedge: 0 leaves it only where the
    // patterns demand it, like rounded room corners; 10 and up narrows rooms.
    // Carrots: at 100 the walls crowd them out (a 4x4 room is nearly all
    // edge, and the originals keep a path along their walls); 700 comes
    // closest to the originals' carrot count, see tests/gen_bench.cpp.
    int wfcHedgePercent = 5;
    int wfcCarrotPercent = 700;
    // Spikes: raising it barely adds any (they are rare in the patterns) and
    // only makes more levels need the spike repair.
    int wfcSpikePercent = 100;
    // How far around a room WFC looks: 1 is the room and its walls; 2 also
    // takes in the next cell beyond each wall, so that what a neighbouring
    // room already has across the wall constrains this one -- which makes WFC
    // fail in more rooms (84% filled against 97%), so 1 is the default.
    int wfcContext = 1;
    // Try the whole map in one WFC run first, so that each wall is chosen
    // knowing both rooms it separates; room by room only if that fails.
    bool wfcWholeMap = true;
    // ...but only on maps up to this many cells. A search node copies the
    // state of every window of the map: at 32x32 the whole-map run took 79 ms
    // a level against 17 room by room, and filled no more rooms (94% both).
    int wfcWholeMapCells = 600;
    int wfcTries = 4;         // WFC runs per room before the fixed patterns take over
    int wfcNodeBudget = 300;  // search nodes per run

    // Defaults for a map of the given size and difficulty, measured with
    // tests/gen_bench.cpp over 200 levels each (carrots aimed at the
    // originals' 27% of the walkable area; "naive loses" is how often a player
    // who always goes for the nearest carrot cuts himself off -- 59% on the
    // originals 1-7). Medium is what the generator made before it had
    // difficulties, so links without one still give the same levels:
    //
    //   map    room  carrot  spikes  doors/room  cycles  naive loses  likeness  native
    //   16x16  4x4   700%     6      2.1         1.2     43%          49%        7 ms
    //   24x24  5x5   320%    12      2.1         1.5     59%          51%       30 ms
    //   32x32  6x6   300%    20      2.5         3.8     77%          45%       35 ms
    //
    // Bigger rooms for bigger maps keep the number of rooms sensible; their
    // price is likeness, since the originals' rooms are small (5x5 rooms on
    // 32x32 score 46%). The spike target stays under the originals' density
    // on big maps because every spike is one more solver run.
    //
    // Easy and Hard change the levers that move the difficulty score
    // (difficulty.hpp) the most: the share of doors laid as spikes, the spike
    // target and how much of the map the rooms take; Hard also closes fewer
    // cycles, so that a burnt spike more often cuts something off.
    //
    //              fill  spike doors  spike target    cycles
    //   easy       0.55  0.1          0.4x medium     0.8
    //   medium     0.75  0.5          6 / 12 / 20     0.8
    //   hard       0.85  1.0          2x medium       0.3
    //
    // What comes out with generateRated() -- 3 tries, then the tuning into
    // the band -- 200 levels each (seeds 1-200; "in band" is the share of
    // levels whose score lands in the band asked for, without the tuning in
    // brackets; "tuned" the share the tuning changed; native ms on one core,
    // 50 levels, the tuning included):
    //
    //   map    difficulty  score       in band     tries  tuned  spikes  naive loses  native
    //   16x16  easy        15 (0-37)   100% (100)  1.1     0%     2.0     4%           24 ms
    //   16x16  medium      47 (33-70)   96%  (60)  2.4    40%     6.1    33%           63 ms
    //   16x16  hard        83 (55-100)  98%  (96)  1.4     3%    10.1    78%           37 ms
    //   24x24  easy        10 (0-37)   100% (100)  1.1     0%     3.7     8%           75 ms
    //   24x24  medium      49 (25-69)   94%  (56)  2.4    43%    10.9    59%          302 ms
    //   24x24  hard        80 (41-100)  99%  (97)  1.4     2%    17.3    90%          139 ms
    //   32x32  easy        11 (0-37)   100% (100)  1.3     0%     7.8    22%          105 ms
    //   32x32  medium      52 (25-66)   99%  (76)  2.0    25%    18.5    78%          298 ms
    //   32x32  hard        73 (62-91)  100%  (98)  1.2     2%    31.3    98%          163 ms
    //
    // Medium levels spread the widest, so Medium needs the most tries and the
    // most tuning: 1.9-3.4 spikes added or taken out per tuned level. The
    // tuning costs likeness (tests/gen_bench.cpp, "patterns") at most a point,
    // and the time it takes is within the noise of the measurement.
    static Params forSize(int width, int height, Difficulty difficulty = Difficulty::Medium);
};

struct Room {
    int sx = 0, sy = 0;                // slot on the room grid
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // interior, inclusive
    RoomKind kind = RoomKind::Main;
    int parent = -1;                   // room it was reached from in the layout, -1 for the start
};

// A door between two neighbouring rooms.
struct Door {
    int a = 0, b = 0;          // the rooms; for a door of the layout tree, `a` is the parent
    bool loop = false;         // not needed to reach every room: it closes a cycle
    bool spike = false;        // has a spike in the finished level
    bool merged = false;       // the whole wall between the rooms is gone
    std::vector<Coord> cells;  // the cells through the wall
};

struct Result {
    Canvas canvas;
    Coord start{};
    Coord exit{};
    std::vector<Room> rooms;
    std::vector<Door> doors;
    int slotsX = 0, slotsY = 0;
    Walls walls = Walls::Auto;  // the style actually used
    // Winning moves from the start. Filled only when `verified`.
    std::vector<Dir> solution;
    bool verified = false;  // the hint solver found a win (playable sizes only)
    int attempts = 0;       // layouts built, including the returned one
    std::uint32_t seed = 0;
    int wfcRooms = 0;       // rooms filled by WFC
    int fallbackRooms = 0;  // rooms filled by the fixed patterns
    bool spikesRepaired = false;  // spikes had to be thinned out to win
    // Set by generateRated(): the score of this level, how many levels were
    // built to find it, and the spikes the tuning added (+) or took out (-).
    difficulty::Rating rating;
    int tries = 1;
    int tuned = 0;
};

// Same seed and params give the same level on every platform.
Result generate(const Params& params, std::uint32_t seed);

// A level of the given difficulty (made with Params::forSize(..., difficulty)).
// Builds up to `tries` levels, try k from difficulty::trySeed(seed, k), and
// returns the first whose score falls into the difficulty's band, or the one
// closest to it. If none falls into the band and `tune` is set, that closest
// one is then moved into the band in place: spikes are added below the band
// and taken out above it, one at a time, each added one kept only if the
// solver still wins, until the score lands in the band or a fixed number of
// steps runs out (roomgen.cpp, "tuning"). A level that is in the band from the
// start is never touched. With `tries` 1 and `tune` false the first level is
// taken as it is, which is what generate() gives.
Result generateRated(const Params& params, std::uint32_t seed, Difficulty difficulty,
                     int tries = difficulty::kTries, bool tune = true);

const char* wallsName(Walls walls);
bool parseWalls(std::string_view text, Walls& out);
const char* kindName(RoomKind kind);

}  // namespace bc::roomgen
