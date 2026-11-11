// A map of any size, for the level generator.
//
// The engine plays a Grid of up to Grid::kMaxW x Grid::kMaxH (level.hpp); the
// generator draws on a Canvas, which has no such limit and is stored densely.
// A Canvas that fits converts to a Level with toLevel().
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "level.hpp"
#include "tilechar.hpp"
#include "tiles.hpp"

namespace bc {

struct Canvas {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> cells;

    Canvas() = default;
    Canvas(int width, int height, std::uint8_t fill)
        : w(width), h(height), cells(static_cast<std::size_t>(width * height), fill) {}

    static Canvas fromGrid(const Grid& g) {
        Canvas c(g.w, g.h, 0);
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x) c.at(x, y) = g.at(x, y);
        return c;
    }
    static Canvas fromLevel(const Level& level) { return fromGrid(level.grid); }

    bool inBounds(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < h; }
    std::uint8_t& at(int x, int y)       { return cells[static_cast<std::size_t>(y * w + x)]; }
    std::uint8_t  at(int x, int y) const { return cells[static_cast<std::size_t>(y * w + x)]; }

    // Small enough for the engine to play.
    bool fits() const { return w >= 1 && h >= 1 && w <= Grid::kMaxW && h <= Grid::kMaxH; }

    // Rows of tile characters, as in a .lvl file, without the title line.
    std::string toText() const {
        std::string out;
        out.reserve(static_cast<std::size_t>((w + 1) * h));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) out += tileToChar(at(x, y));
            out += '\n';
        }
        return out;
    }

    // False if the canvas does not fit().
    bool toLevel(Level& level, std::string name = {}) const {
        if (!fits()) return false;
        level = Level{};
        level.grid.w = w;
        level.grid.h = h;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) level.grid.at(x, y) = at(x, y);
        level.name = std::move(name);
        level.rescan();
        return true;
    }
};

}  // namespace bc
