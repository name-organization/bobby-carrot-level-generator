// Room-based level generator (the Spelunky approach).
//
// The map is cut into a grid of equal room slots separated by walls:
//
//   1. Layout   a random self-avoiding path over the slots from the start room
//               to the exit room, plus side rooms hanging off it (dead ends).
//               Unused slots stay hedge, which gives the island its outline.
//   2. Doors    every room is joined to the one it was reached from. A door on
//               the start-exit path may be spikes: it can be crossed once, and
//               nothing behind it is ever needed again. Side rooms always get
//               plain doors, since Bobby has to come back out.
//   3. Content  wave function collapse over 3x3 patterns of the originals
//               (wfc.hpp), room by room together with its walls; or the fixed
//               patterns (carrot fields, rows, pillars...) without a model.
//   4. Autotile hedge and fence pieces from their neighbours (autotile.hpp).
//   5. Verify   the hint solver must find a win. Its moves are the reference
//               solution the game hands to the hint. If WFC spikes block the
//               way, they are thinned out until the solver wins.
//   6. Spikes   extra spikes up to a target, each one kept only if the solver
//               still finds a win with it.
//
// The layout is winnable by construction (visit side rooms, then cross the
// next door); step 5 is the safety net and is what makes "verified" a fact.
//
// Everything works on a Canvas of any size. Only a Grid-sized canvas can be
// verified and played until the engine learns bigger maps.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "canvas.hpp"
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
    int width    = Grid::W;
    int height   = Grid::H;
    int roomSize = 4;            // cells inside one room slot, per side
    Walls walls  = Walls::Auto;
    double fill            = 0.75;  // share of the room slots the level uses (3 rooms at least)
    double mainShare       = 0.65;  // share of used rooms on the start-exit path
    double spikeDoorChance = 0.5;   // per door on the start-exit path
    double mergeChance     = 0.15;  // plain door widened to the whole wall
    double insetChance     = 0.35;  // room shrunk by a cell on one side
    double emptyRoomChance = 0.25;  // path room without carrots (fixed patterns only)
    // Spikes the level should end up with, doors included (the originals have
    // 6.8 on average). Missing ones are added after the layout: cells are tried
    // in random order (corridor-like cells first) and a spike stays only if the
    // solver still finds a win. Grid-sized maps only, since it needs the solver.
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
    int wfcTries = 4;         // WFC runs per room before the fixed patterns take over
    int wfcNodeBudget = 300;  // search nodes per run
};

struct Room {
    int sx = 0, sy = 0;                // slot on the room grid
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // interior, inclusive
    RoomKind kind = RoomKind::Main;
    int parent = -1;                   // room it is entered from, -1 for the start
    bool spikeDoor = false;            // the door from the parent is spikes
    bool merged = false;               // the wall to the parent is gone
};

struct Result {
    Canvas canvas;
    Coord start{};
    Coord exit{};
    std::vector<Room> rooms;
    int slotsX = 0, slotsY = 0;
    Walls walls = Walls::Auto;  // the style actually used
    // Winning moves from the start. Filled only when `verified`.
    std::vector<Dir> solution;
    bool verified = false;  // the hint solver found a win (Grid-sized maps only)
    int attempts = 0;       // layouts built, including the returned one
    std::uint32_t seed = 0;
    int wfcRooms = 0;       // rooms filled by WFC
    int fallbackRooms = 0;  // rooms filled by the fixed patterns
    bool spikesRepaired = false;  // WFC spikes had to be thinned out to win
};

// Same seed and params give the same level on every platform.
Result generate(const Params& params, std::uint32_t seed);

const char* wallsName(Walls walls);
bool parseWalls(std::string_view text, Walls& out);
const char* kindName(RoomKind kind);

}  // namespace bc::roomgen
