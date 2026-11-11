#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "types.hpp"

namespace bc {

// The playfield: `w` x `h` cells, up to kMaxW x kMaxH. Mutable: collecting a
// carrot, rotating a corner or trampling grass all rewrite cells in place.
//
// Storage is fixed: kMaxH rows of kStride cells, the level in the top-left
// corner, everything outside it solid hedge (tile 0). Code that walks all of
// `cells` therefore sees the level surrounded by hedge and needs no size
// checks of its own; code that draws or prints uses `w` and `h`.
struct Grid {
    static constexpr int kStride = kGridStride;
    static constexpr int kMaxW = kGridStride;
    static constexpr int kMaxH = 32;
    static constexpr int kCells = kStride * kMaxH;  // storage, not the level size
    // The size of the original levels.
    static constexpr int kClassic = 16;

    int w = kClassic;
    int h = kClassic;
    std::array<std::uint8_t, kCells> cells{};

    std::uint8_t& at(int x, int y)             { return cells[y * kStride + x]; }
    std::uint8_t  at(int x, int y) const       { return cells[y * kStride + x]; }
    std::uint8_t& at(Coord c)                  { return cells[c.index()]; }
    std::uint8_t  at(Coord c) const            { return cells[c.index()]; }

    // Inside the level.
    bool inBounds(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < h; }
    // Inside the storage: safe to index, hedge where it is outside the level.
    static bool inStorage(int x, int y) {
        return x >= 0 && x < kMaxW && y >= 0 && y < kMaxH;
    }
};

// A level as it exists on disk, plus the totals derived from one scan.
//
// Levels are plain text, one character per cell, as described in
// src/core/tilechars.hpp: as many rows as the level is high, the longest row
// as wide as the level, up to Grid::kMaxW x Grid::kMaxH; a shorter row is
// padded with path. Lines starting with ';' are comments, so a file can carry
// a title and notes; a blank line is a row of path. This replaced
// the original .blm container (a 4-byte big-endian length followed by 256 raw
// tile bytes), which was compact but unreadable and unverifiable by eye.
struct Level {
    Grid grid;
    Coord start{};
    Coord exit{};
    int carrotTotal = 0;
    int eggTotal    = 0;
    std::string name;  // from a leading ';' comment, when the file has one
    //std::vector<Coord> successPath = {};

    // `error` receives a message naming the line and column when parsing fails.
    bool loadFromText(std::string_view text, std::string* error = nullptr);
    bool loadFromFile(const std::string& path, std::string* error = nullptr);

    // Renders the grid back to the text format. `title` becomes a leading
    // comment line. Used by the level generator and by the converter.
    std::string toText(std::string_view title = {}) const;
    bool saveToFile(const std::string& path, std::string_view title = {}) const;

    // Recomputes start, exit and the pickup totals from the grid. Call this
    // after building a grid by hand, or the HUD and the win condition will
    // disagree with what is actually on the board.
    void rescan();

    static std::string pathFor(Mode mode, int index, const std::string& root = "assets/v1");
    static int levelCount(Mode mode) { return mode == Mode::Normal ? 30 : 20; }
};

}  // namespace bc
