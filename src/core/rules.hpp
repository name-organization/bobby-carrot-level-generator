#pragma once

#include "level.hpp"
#include "tiles.hpp"
#include "types.hpp"

namespace bc {

/** @brief Inventory and collectible counts for the current playthrough. */
struct Progress {
    Inventory inv;
    int carrotCount = 0;
    int eggCount    = 0;

    /** @return Uncollected carrots, or grass patches when the level has no carrots. */
    int remaining(const Level& level) const {
        return level.carrotTotal > 0 ? level.carrotTotal - carrotCount
                                     : level.eggTotal - eggCount;
    }

    bool allCollected(const Level& level) const { return remaining(level) == 0; }
};

/** @brief Cell-transition events consumed by audio without callbacks into the core. */
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
    Dir forced       = Dir::None;  ///< conveyors keep Bobby moving on their own
    bool reachedExit = false;
    unsigned events  = ev::None;
};

namespace rules {

/**
 * @brief Check movement restrictions on both source and destination tiles.
 *
 * @param g Current mutable playfield.
 * @param from Source cell inside the level.
 * @param to Destination cell.
 * @param d Direction from source to destination.
 * @param inv Keys available before the move.
 * @return Whether the move is allowed; entering a hole is allowed and causes death.
 */
bool canStep(const Grid& g, Coord from, Coord to, Dir d, const Inventory& inv);

/**
 * @brief Apply departure effects: rotate rails, collapse spikes and trample grass.
 *
 * @param[in,out] g Playfield receiving the departure effects.
 * @param c Cell being left.
 * @param[in,out] p Receives the grass count before the destination is entered.
 * @return Cell-transition event flags.
 */
unsigned onLeave(Grid& g, Coord c, Progress& p);

/**
 * @brief Apply pickups, switches, locks and conveyor movement on arrival.
 *
 * @param[in,out] g Playfield receiving the arrival effects.
 * @param c Cell being entered.
 * @param[in,out] p Inventory and collectible counts.
 * @param allCollected Completion state after onLeave() has updated progress.
 * @return Events, forced direction and exit status.
 */
EnterResult onEnter(Grid& g, Coord c, Progress& p, bool allCollected);

}  // namespace rules
}  // namespace bc
