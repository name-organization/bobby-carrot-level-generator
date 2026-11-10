#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

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
//
// Levels are plain text: 16 rows of 16 characters, one character per cell, as
// described in src/core/tilechars.hpp. Lines starting with ';' are comments and
// blank lines are skipped, so a file can carry a title and notes. This replaced
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
