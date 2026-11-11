// A map of any size, for the level generator.
//
// The engine plays a fixed Grid (level.hpp); the generator draws on a Canvas so
// that it already works for the bigger maps planned later. A Canvas the size of
// the Grid converts to a Level with toLevel().
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

    static Canvas fromLevel(const Level& level) {
        Canvas c(Grid::W, Grid::H, 0);
        c.cells.assign(level.grid.cells.begin(), level.grid.cells.end());
        return c;
    }

    bool inBounds(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < h; }
    std::uint8_t& at(int x, int y)       { return cells[static_cast<std::size_t>(y * w + x)]; }
    std::uint8_t  at(int x, int y) const { return cells[static_cast<std::size_t>(y * w + x)]; }

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

    // False unless the canvas is exactly the size of the playable Grid.
    bool toLevel(Level& level, std::string name = {}) const {
        if (w != Grid::W || h != Grid::H) return false;
        level = Level{};
        for (int i = 0; i < Grid::kCells; ++i) level.grid.cells[i] = cells[i];
        level.name = std::move(name);
        level.rescan();
        return true;
    }
};

}  // namespace bc
