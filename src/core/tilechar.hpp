#pragma once

#include <cstdint>

#include "tiles.hpp"

namespace bc {

/** @brief Tile characters indexed by ID; keep the README format table in sync. */
inline constexpr char kTileChars[48] = {
    '#',  // 0  solid hedge
    '1',  // 1  edge
    '2',  // 2  edge
    '3',  // 3  edge
    '4',  // 4  edge
    '5',  // 5  corner
    '6',  // 6  corner
    '7',  // 7  corner
    '8',  // 8  corner
    '9',  // 9  corner, rare
    '(',  // 10 corner, rare
    ')',  // 11 corner, rare

    '=',  // 12 fence, horizontal
    '"',  // 13 fence, vertical
    '{',  // 14 fence, horizontal with post
    '}',  // 15 fence, corner
    '[',  // 16 fence, junction
    ']',  // 17 fence, end

    ' ',  // 18 path
    '*',  // 19 carrot
    'o',  // 20 carrot already taken (runtime only, never in a file)
    '0',  // 21 start
    'b',  // 22 red button, armed
    'B',  // 23 red button, spent
    'F',  // 24 rail corner, east-south
    'T',  // 25 rail corner, west-south
    'J',  // 26 rail corner, west-north
    'L',  // 27 rail corner, north-east
    '-',  // 28 rail, horizontal
    '|',  // 29 rail, vertical
    '_',  // 30 spike plate, not yet triggered
    'W',  // 31 spikes, triggered and deadly
    'K',  // 32 grey key
    'G',  // 33 grey lock
    'Y',  // 34 yellow key
    'H',  // 35 yellow lock
    'R',  // 36 red key
    'D',  // 37 red lock
    'n',  // 38 yellow button, armed
    'N',  // 39 yellow button, spent
    '<',  // 40 conveyor, left
    '>',  // 41 conveyor, right
    '^',  // 42 conveyor, up
    'V',  // 43 conveyor, down
    'a',  // 44 exit
    'X',  // 45 grass
    'x',  // 46 grass, already trampled
    '?',  // 47 unused by any original level
};

constexpr int kTileCount = 48;

constexpr char tileToChar(std::uint8_t tile) {
    return tile < kTileCount ? kTileChars[tile] : '?';
}

struct TileAlias {
    char ch;
    std::uint8_t tile;
    const char* note;
};

inline constexpr TileAlias kTileAliases[] = {
    {'.', T_GROUND, "path, written out rather than left blank"},
    {'C', T_GROUND, "an opened lock is path again"},
    {'@', T_EXIT,   "an exit with everything collected is the same tile"},
};

constexpr int kTileAliasCount =
    static_cast<int>(sizeof(kTileAliases) / sizeof(kTileAliases[0]));

/**
 * @brief Decode a tile character or one of its accepted aliases.
 *
 * @return Tile ID, or -1 for an unknown character.
 */
constexpr int charToTile(char c) {
    for (int i = 0; i < kTileCount; ++i) {
        if (kTileChars[i] == c) return i;
    }
    for (int i = 0; i < kTileAliasCount; ++i) {
        if (kTileAliases[i].ch == c) return kTileAliases[i].tile;
    }
    return -1;
}

/** @brief Check that canonical tile characters and aliases do not overlap. */
constexpr bool tileCharsAreUnique() {
    for (int i = 0; i < kTileCount; ++i) {
        for (int j = i + 1; j < kTileCount; ++j) {
            if (kTileChars[i] == kTileChars[j]) return false;
        }
        for (int j = 0; j < kTileAliasCount; ++j) {
            if (kTileChars[i] == kTileAliases[j].ch) return false;
        }
    }
    return true;
}
static_assert(tileCharsAreUnique(), "two tiles share a character");

constexpr char kCommentChar = ';';
static_assert(charToTile(kCommentChar) == -1, "the comment marker is also a tile");

}  // namespace bc
