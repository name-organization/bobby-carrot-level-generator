#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "types.hpp"

namespace bc {

// The 16x16 playfield. Mutable: collecting a carrot, rotating a corner or
// trampling grass all rewrite cells in place.
struct Grid {
    static constexpr int W = 16;
    static constexpr int H = 16;
    static constexpr int kCells = W * H;

    std::array<std::uint8_t, kCells> cells{};

    std::uint8_t& at(int x, int y)             { return cells[y * W + x]; }
    std::uint8_t  at(int x, int y) const       { return cells[y * W + x]; }
    std::uint8_t& at(Coord c)                  { return cells[c.index()]; }
    std::uint8_t  at(Coord c) const            { return cells[c.index()]; }

    static bool inBounds(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }
};

// A level as it exists on disk, plus the totals derived from one scan.
struct Level {
    Grid grid;
    Coord start{};
    Coord exit{};
    int carrotTotal = 0;
    int eggTotal    = 0;

    // .blm layout: 4-byte big-endian payload length (always 256) followed by a
    // 16x16 row-major grid of tile bytes. Exactly 260 bytes, no compression.
    static constexpr std::size_t kFileSize   = 260;
    static constexpr std::size_t kHeaderSize = 4;

    bool loadFromMemory(const std::uint8_t* data, std::size_t size);
    bool loadFromFile(const std::string& path);

    // Path of an original level file for the given mode and zero-based index.
    static std::string pathFor(Mode mode, int index, const std::string& root = "assets/v1");
    static int levelCount(Mode mode) { return mode == Mode::Normal ? 30 : 20; }
};

}  // namespace bc
