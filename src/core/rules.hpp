#pragma once

#include "level.hpp"
#include "tiles.hpp"
#include "types.hpp"

namespace bc {

// Everything the player accumulates while solving a level.
struct Progress {
    Inventory inv;
    int carrotCount = 0;
    int eggCount    = 0;
};

// Bit flags describing what happened during a cell transition, consumed by the
// audio layer. Kept as flags so the core stays free of callbacks.
namespace ev {
enum : unsigned {
    None         = 0,
    Carrot       = 1u << 0,
    Egg          = 1u << 1,
    KeyPickup    = 1u << 2,
    LockOpened   = 1u << 3,
    SwitchRed    = 1u << 4,
    SwitchYellow = 1u << 5,
    Cracked      = 1u << 6,
    Exit         = 1u << 7,
    Death        = 1u << 8,
    Blocked      = 1u << 9,
};
}  // namespace ev

struct EnterResult {
    Dir forced       = Dir::None;  // conveyors keep Bobby moving on their own
    bool reachedExit = false;
    unsigned events  = ev::None;
};

namespace rules {

// Can Bobby move from `from` to `to` heading `d`?
//
// Both the source and the destination tile matter: a corner piece restricts
// which way you may leave it just as much as which way you may enter it. This
// is what makes the rail puzzles work.
bool canStep(const Grid& g, Coord from, Coord to, Dir d, const Inventory& inv);

// Applied to the cell Bobby just left. Corners rotate, rails flip, cracked
// tiles collapse and grass gets trampled -- all of it on the way out, never on
// the way in.
unsigned onLeave(Grid& g, Coord c, Progress& p);

// Applied to the cell Bobby just reached.
EnterResult onEnter(Grid& g, Coord c, Progress& p, bool allCollected);

}  // namespace rules
}  // namespace bc
