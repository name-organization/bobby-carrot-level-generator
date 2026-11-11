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

// Cells per stored row of a Grid: the widest map the engine plays. A level
// narrower than that uses the left part of each row.
constexpr int kGridStride = 32;

struct Coord {
    int x = 0;
    int y = 0;
    constexpr int index() const { return y * kGridStride + x; }
    friend constexpr bool operator==(Coord a, Coord b) {
        return a.x == b.x && a.y == b.y;
    }
};

// Which collectible a level is built around. The original ships two separate
// level sets and shows a different HUD icon for each.
enum class Mode : std::uint8_t { Normal = 0, Egg = 1 };

struct Inventory {
    int gray = 0;
    int yellow = 0;
    int red = 0;
};

// The simulation runs at a fixed rate so that movement speed never depends on
// the display refresh rate. The original moved Bobby two pixels per frame on a
// 60 Hz handset, i.e. sixteen frames to cross one 32 px cell.
constexpr int kTicksPerSecond  = 60;
constexpr int kTileSize        = 32;
constexpr int kStepPixels      = 2;
constexpr int kTicksPerStep    = kTileSize / kStepPixels;  // 16
constexpr int kFadeTicks       = 24;   // 9 frames at 22.5 fps
constexpr int kDeathTicks      = 54;   // 8 frames at 22.5 fps + held last frame
constexpr int kIdleAfterTicks  = 4 * kTicksPerSecond;

}  // namespace bc
