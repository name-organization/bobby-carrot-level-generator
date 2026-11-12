/**
 * @file
 * @brief Tile IDs shared by gameplay rules and tileset.png.
 *
 * @details Sprite coordinates are (id % 8 * 32, id / 8 * 32).
 * @see tilechar.hpp for the text representation.
 */
#pragma once

#include <cstdint>

namespace bc {

enum Tile : std::uint8_t {
    T_DEFAULT_GRASS    = 0,
    T_SINGLE_GRASS     = 9,
    T_FENCE_LVR        = 12,
    T_FENCE_VV         = 13,
    T_FENCE_VVR        = 14,
    T_FENCE_LVV        = 15,
    T_FENCE_VR         = 16,
    T_FENCE_LV         = 17,

    T_FIRST_WALKABLE   = 18,

    T_GROUND           = 18,  ///< plain dirt path
    T_CARROT           = 19,  ///< collectible; becomes T_CARROT_TAKEN on enter
    T_CARROT_TAKEN     = 20,  ///< runtime only, never present in files
    T_START            = 21,  ///< spawn point, otherwise plain ground

    T_SWITCH_RED_ON    = 22,  ///< stepping on it rotates every corner + rail
    T_SWITCH_RED_OFF   = 23,

    T_CORNER_ES        = 24,  ///< corner connecting East and South edges
    T_CORNER_WS        = 25,  ///< West  + South
    T_CORNER_WN        = 26,  ///< West  + North
    T_CORNER_NE        = 27,  ///< North + East
    T_RAIL_H           = 28,  ///< straight horizontal rail (West + East)
    T_RAIL_V           = 29,  ///< straight vertical rail (North + South)

    T_CRACKED          = 30,  ///< turns into T_HOLE once you step off it
    T_HOLE             = 31,  ///< entering it kills Bobby

    T_KEY_GRAY         = 32,
    T_LOCK_GRAY        = 33,
    T_KEY_YELLOW       = 34,
    T_LOCK_YELLOW      = 35,
    T_KEY_RED          = 36,
    T_LOCK_RED         = 37,

    T_SWITCH_YELLOW_ON = 38,  ///< stepping on it reverses every conveyor
    T_SWITCH_YELLOW_OFF= 39,

    T_CONVEYOR_L       = 40,
    T_CONVEYOR_R       = 41,
    T_CONVEYOR_U       = 42,
    T_CONVEYOR_D       = 43,

    T_EXIT             = 44,  ///< animated once every pickup is collected
    T_GRASS            = 45,  ///< "egg" tile: counted when you step OFF it
    T_GRASS_FLAT       = 46,  ///< trampled grass, cannot be entered again
};

constexpr bool isSolidScenery(std::uint8_t t) { return t < T_FIRST_WALKABLE; }

constexpr bool isConveyor(std::uint8_t t) {
    return t >= T_CONVEYOR_L && t <= T_CONVEYOR_D;
}

constexpr bool isCorner(std::uint8_t t) {
    return t >= T_CORNER_ES && t <= T_CORNER_NE;
}

constexpr bool isRail(std::uint8_t t) { return t == T_RAIL_H || t == T_RAIL_V; }

}  // namespace bc
