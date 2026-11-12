#pragma once

#include <cstdint>

namespace bc {

enum class Dir : std::uint8_t { None = 0, Left, Right, Up, Down };

constexpr int dirDX(Dir d) {
    return d == Dir::Left ? -1 : d == Dir::Right ? 1 : 0;
}
constexpr int dirDY(Dir d) {
    return d == Dir::Up ? -1 : d == Dir::Down ? 1 : 0;
}

constexpr bool isHorizontal(Dir d) { return d == Dir::Left || d == Dir::Right; }
constexpr bool isVertical(Dir d)   { return d == Dir::Up   || d == Dir::Down;  }

/** @brief Stored row width; narrower levels occupy the leftmost cells. */
constexpr int kGridStride = 32;

struct Coord {
    int x = 0;
    int y = 0;
    constexpr int index() const { return y * kGridStride + x; }
    friend constexpr bool operator==(Coord a, Coord b) {
        return a.x == b.x && a.y == b.y;
    }
};

/** @brief Collectible set used by the original level files and HUD. */
enum class Mode : std::uint8_t { Normal = 0, Egg = 1 };

struct Inventory {
    int gray = 0;
    int yellow = 0;
    int red = 0;
};

/** @brief Fixed simulation rate; rendering never controls movement speed. */
constexpr int kTicksPerSecond  = 60;
constexpr int kTileSize        = 32;
constexpr int kStepPixels      = 2;
constexpr int kTicksPerStep    = kTileSize / kStepPixels;  ///< 16
constexpr int kFadeTicks       = 24;   ///< 9 frames at 22.5 fps
constexpr int kDeathTicks      = 54;   ///< 8 frames at 22.5 fps + held last frame
constexpr int kIdleAfterTicks  = 4 * kTicksPerSecond;

}  // namespace bc
