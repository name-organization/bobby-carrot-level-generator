#include "rules.hpp"

namespace bc::rules {

bool canStep(const Grid& g, Coord from, Coord to, Dir d, const Inventory& inv) {
    if (!g.inBounds(to.x, to.y)) return false;

    const std::uint8_t nw = g.at(to);
    const std::uint8_t od = g.at(from);

    // --- destination tile ---------------------------------------------------
    if (isSolidScenery(nw)) return false;
    if (nw == T_GRASS_FLAT) return false;  // already trampled

    if (nw == T_LOCK_GRAY   && inv.gray   == 0) return false;
    if (nw == T_LOCK_YELLOW && inv.yellow == 0) return false;
    if (nw == T_LOCK_RED    && inv.red    == 0) return false;

    // Corners accept entry only through the two edges they connect.
    if (nw == T_CORNER_ES && (d == Dir::Right || d == Dir::Down)) return false;
    if (nw == T_CORNER_WS && (d == Dir::Left  || d == Dir::Down)) return false;
    if (nw == T_CORNER_WN && (d == Dir::Left  || d == Dir::Up))   return false;
    if (nw == T_CORNER_NE && (d == Dir::Right || d == Dir::Up))   return false;

    if ((nw == T_RAIL_H || nw == T_CONVEYOR_L || nw == T_CONVEYOR_R) && isVertical(d))
        return false;
    if ((nw == T_RAIL_V || nw == T_CONVEYOR_U || nw == T_CONVEYOR_D) && isHorizontal(d))
        return false;

    // A conveyor may only be boarded from behind, never head-on.
    if (nw == T_CONVEYOR_L && d == Dir::Right) return false;
    if (nw == T_CONVEYOR_R && d == Dir::Left)  return false;
    if (nw == T_CONVEYOR_U && d == Dir::Down)  return false;
    if (nw == T_CONVEYOR_D && d == Dir::Up)    return false;

    // --- source tile --------------------------------------------------------
    if (od == T_CORNER_ES && (d == Dir::Left  || d == Dir::Up))   return false;
    if (od == T_CORNER_WS && (d == Dir::Right || d == Dir::Up))   return false;
    if (od == T_CORNER_WN && (d == Dir::Right || d == Dir::Down)) return false;
    if (od == T_CORNER_NE && (d == Dir::Left  || d == Dir::Down)) return false;

    if ((od == T_RAIL_H || od == T_CONVEYOR_L || od == T_CONVEYOR_R) && isVertical(d))
        return false;
    if ((od == T_RAIL_V || od == T_CONVEYOR_U || od == T_CONVEYOR_D) && isHorizontal(d))
        return false;

    if (od == T_CONVEYOR_L && d == Dir::Right) return false;
    if (od == T_CONVEYOR_R && d == Dir::Left)  return false;
    if (od == T_CONVEYOR_U && d == Dir::Down)  return false;
    if (od == T_CONVEYOR_D && d == Dir::Up)    return false;

    return true;
}

unsigned onLeave(Grid& g, Coord c, Progress& p) {
    std::uint8_t& cell = g.at(c);
    unsigned events = ev::None;

    switch (cell) {
        case T_CORNER_ES: cell = T_CORNER_WS; break;
        case T_CORNER_WS: cell = T_CORNER_WN; break;
        case T_CORNER_WN: cell = T_CORNER_NE; break;
        case T_CORNER_NE: cell = T_CORNER_ES; break;

        case T_RAIL_H:    cell = T_RAIL_V;    break;
        case T_RAIL_V:    cell = T_RAIL_H;    break;

        case T_CRACKED:   cell = T_HOLE; events |= ev::Cracked; break;

        case T_GRASS:
            cell = T_GRASS_FLAT;
            ++p.eggCount;
            events |= ev::Egg;
            break;

        default: break;
    }
    return events;
}

namespace {

void toggleRedNetwork(Grid& g) {
    for (auto& cell : g.cells) {
        switch (cell) {
            case T_SWITCH_RED_ON:  cell = T_SWITCH_RED_OFF; break;
            case T_SWITCH_RED_OFF: cell = T_SWITCH_RED_ON;  break;

            case T_CORNER_ES: cell = T_CORNER_WS; break;
            case T_CORNER_WS: cell = T_CORNER_WN; break;
            case T_CORNER_WN: cell = T_CORNER_NE; break;
            case T_CORNER_NE: cell = T_CORNER_ES; break;

            case T_RAIL_H:    cell = T_RAIL_V;    break;
            case T_RAIL_V:    cell = T_RAIL_H;    break;
            default: break;
        }
    }
}

void toggleYellowNetwork(Grid& g) {
    for (auto& cell : g.cells) {
        switch (cell) {
            case T_SWITCH_YELLOW_ON:  cell = T_SWITCH_YELLOW_OFF; break;
            case T_SWITCH_YELLOW_OFF: cell = T_SWITCH_YELLOW_ON;  break;

            case T_CONVEYOR_L: cell = T_CONVEYOR_R; break;
            case T_CONVEYOR_R: cell = T_CONVEYOR_L; break;
            case T_CONVEYOR_U: cell = T_CONVEYOR_D; break;
            case T_CONVEYOR_D: cell = T_CONVEYOR_U; break;
            default: break;
        }
    }
}

}  // namespace

EnterResult onEnter(Grid& g, Coord c, Progress& p, bool allCollected) {
    EnterResult r;
    std::uint8_t& cell = g.at(c);

    switch (cell) {
        case T_CARROT:
            cell = T_CARROT_TAKEN;
            ++p.carrotCount;
            r.events |= ev::Carrot;
            break;

        case T_SWITCH_RED_ON:
            toggleRedNetwork(g);
            r.events |= ev::SwitchRed;
            break;

        case T_KEY_GRAY:   ++p.inv.gray;   cell = T_GROUND; r.events |= ev::KeyPickup;  break;
        case T_KEY_YELLOW: ++p.inv.yellow; cell = T_GROUND; r.events |= ev::KeyPickup;  break;
        case T_KEY_RED:    ++p.inv.red;    cell = T_GROUND; r.events |= ev::KeyPickup;  break;

        case T_LOCK_GRAY:
            if (p.inv.gray > 0)   { --p.inv.gray;   cell = T_GROUND; r.events |= ev::LockOpened; }
            break;
        case T_LOCK_YELLOW:
            if (p.inv.yellow > 0) { --p.inv.yellow; cell = T_GROUND; r.events |= ev::LockOpened; }
            break;
        case T_LOCK_RED:
            if (p.inv.red > 0)    { --p.inv.red;    cell = T_GROUND; r.events |= ev::LockOpened; }
            break;

        case T_SWITCH_YELLOW_ON:
            toggleYellowNetwork(g);
            r.events |= ev::SwitchYellow;
            break;

        case T_CONVEYOR_L: r.forced = Dir::Left;  break;
        case T_CONVEYOR_R: r.forced = Dir::Right; break;
        case T_CONVEYOR_U: r.forced = Dir::Up;    break;
        case T_CONVEYOR_D: r.forced = Dir::Down;  break;

        case T_EXIT:
            if (allCollected) {
                r.reachedExit = true;
                r.events |= ev::Exit;
            }
            break;

        default: break;
    }
    return r;
}

}  // namespace bc::rules
