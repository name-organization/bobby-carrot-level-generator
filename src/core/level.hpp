#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "types.hpp"

namespace bc {

/**
 * @brief Mutable playfield with fixed-stride storage.
 *
 * @details The level occupies the top-left w by h cells. Unused storage
 * must remain solid hedge so algorithms scanning all cells see a closed border.
 */
struct Grid {
    static constexpr int kStride = kGridStride;
    static constexpr int kMaxW = kGridStride;
    static constexpr int kMaxH = 32;
    static constexpr int kCells = kStride * kMaxH;  ///< storage, not the level size

    static constexpr int kClassic = 16;

    int w = kClassic;
    int h = kClassic;
    std::array<std::uint8_t, kCells> cells{};

    std::uint8_t& at(int x, int y)             { return cells[y * kStride + x]; }
    std::uint8_t  at(int x, int y) const       { return cells[y * kStride + x]; }
    std::uint8_t& at(Coord c)                  { return cells[c.index()]; }
    std::uint8_t  at(Coord c) const            { return cells[c.index()]; }

    bool inBounds(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < h; }
    /** @brief Check storage bounds, including hedge outside the level. */
    static bool inStorage(int x, int y) {
        return x >= 0 && x < kMaxW && y >= 0 && y < kMaxH;
    }
};

/**
 * @brief Level grid and derived spawn, exit and collectible totals.
 *
 * @details The .lvl format uses one character per cell. Comment lines start
 * with a semicolon; blank lines and missing row suffixes are path.
 * @see tilechar.hpp
 */
struct Level {
    Grid grid;
    Coord start{};
    Coord exit{};
    int carrotTotal = 0;
    int eggTotal    = 0;
    std::string name;  ///< from a leading ';' comment, when the file has one

    /**
     * @brief Parse a .lvl map without changing this level on failure.
     *
     * @param text Rows in LF or CRLF format, at most kMaxW by kMaxH.
     * @param[out] error Optional parse error with line and column where applicable.
     * @return True on success.
     */
    bool loadFromText(std::string_view text, std::string* error = nullptr);
    /** @brief Load .lvl text from disk; optionally report a path-qualified error. */
    bool loadFromFile(const std::string& path, std::string* error = nullptr);

    /**
     * @brief Serialize the grid as .lvl text.
     *
     * @param title Optional leading comment; the stored name is not used implicitly.
     */
    std::string toText(std::string_view title = {}) const;
    bool saveToFile(const std::string& path, std::string_view title = {}) const;

    /** @brief Recompute spawn, exit and collectible totals after editing the grid. */
    void rescan();

    static std::string pathFor(Mode mode, int index, const std::string& root = "assets/v1");
    static int levelCount(Mode mode) { return mode == Mode::Normal ? 30 : 20; }
};

}  // namespace bc
