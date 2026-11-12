#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "canvas.hpp"
#include "difficulty.hpp"
#include "types.hpp"

namespace bc::wfc {
struct Model;
}

namespace bc::roomgen {

/**
 * @brief Wall style between room interiors.
 * @details Auto selects a style per layout; Hedge uses two-cell walls,
 * while Fence and ThinHedge use one-cell walls.
 */
enum class Walls : std::uint8_t { Auto, Fence, ThinHedge, Hedge };

enum class RoomKind : std::uint8_t { Start, Main, Exit, Side };

/** @brief Layout, WFC and verification limits for room-based generation. */
struct Params {
    int width    = Grid::kClassic;
    int height   = Grid::kClassic;
    int roomSize = 4;               ///< Interior cells per room side.
    Walls walls  = Walls::Auto;
    double fill            = 0.75;  ///< Fraction of room slots used, with at least three when possible.
    double mainShare       = 0.65;  ///< Fraction of rooms on the start-to-exit path.
    double loopChance      = 0.8;   ///< Extra door probability per neighbouring pair outside the layout tree.
    double spikeDoorChance = 0.5;   ///< Probability of initially placing spikes in a door.
    double mergeChance     = 0.15;  ///< Probability of widening a non-spike door to the entire wall.
    double insetChance     = 0.35;  ///< Probability of shrinking a room by one cell on one side.
    double emptyRoomChance = 0.25;  ///< Probability of a carrot-free main room with fixed patterns.
    int spikeTarget = 6;            ///< Desired total spikes, including doors; playable canvases only.
    int spikeUnknownWindows = 2;    ///< Maximum unknown 3x3 windows introduced by an extra spike.
    int attempts = 40;              ///< Layout attempts before returning the last result.

    /** @brief Optional learned patterns; unsolved rooms fall back to fixed patterns. */
    const wfc::Model* model = nullptr;
    int wfcHedgePercent = 5;         ///< Hedge weight relative to learned occurrences.
    int wfcCarrotPercent = 700;      ///< Carrot weight, increased to compensate for small room borders.
    int wfcSpikePercent = 100;       ///< Spike weight relative to learned occurrences.
    int wfcContext = 1;             ///< Cells around a room included as WFC context; minimum one.
    bool wfcWholeMap = true;        ///< Try the whole map before solving rooms separately.
    int wfcWholeMapCells = 600;     ///< Cell limit for whole-map search, whose nodes copy all windows.
    int wfcTries = 4;               ///< WFC attempts per room before using fixed patterns.
    int wfcNodeBudget = 300;        ///< Search nodes per WFC attempt.

    /**
     * @brief Select room size, content weights and spike target for a map size.
     * @param width Requested map width.
     * @param height Requested map height.
     * @param difficulty Controls fill, spike doors, spike target, cycle probability and, on
     * Hard, the unknown 3x3 windows an added spike may cause.
     * @return Parameters for the requested size and difficulty.
     * @note Medium defaults preserve the levels produced by legacy seed links.
     */
    static Params forSize(int width, int height, Difficulty difficulty = Difficulty::Medium);
};

struct Room {
    int sx = 0, sy = 0;                  ///< Slot on the room grid.
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  ///< Inclusive interior bounds.
    RoomKind kind = RoomKind::Main;
    int parent = -1;                    ///< Parent in the layout tree; -1 for the start room.
};

/** @brief Door joining two neighbouring rooms. */
struct Door {
    int a = 0, b = 0;          ///< Room indices; a is the parent for layout-tree doors.
    bool loop = false;        ///< Extra door closing a cycle outside the layout tree.
    bool spike = false;       ///< At least one door cell is a spike in the final map.
    bool merged = false;      ///< The entire separating wall was opened.
    std::vector<Coord> cells;  ///< Corridor cells through the separating wall.
};

struct Result {
    Canvas canvas;
    Coord start{};
    Coord exit{};
    std::vector<Room> rooms;
    std::vector<Door> doors;
    int slotsX = 0, slotsY = 0;
    Walls walls = Walls::Auto;  ///< Wall style actually selected.
    std::vector<Dir> solution;  ///< Winning moves from start; populated only if verified.
    bool verified = false;     ///< Solver found a win; only available for playable sizes.
    int attempts = 0;          ///< Layouts built, including the returned one.
    std::uint32_t seed = 0;
    int wfcRooms = 0;
    int fallbackRooms = 0;
    bool spikesRepaired = false;  ///< Initial spikes had to be removed to preserve a win.
    difficulty::Rating rating;    ///< Populated by generateRated().
    int tries = 1;                ///< Candidates built by generateRated().
    int tuned = 0;                ///< Spikes added (+) or removed (-) while tuning difficulty.
};

/**
 * @brief Check that a map shows all seven base cell types.
 * @details Path, hedge, fence, carrot, spikes, start and exit each occur at least once.
 */
bool hasAllCellTypes(const Grid& grid);

/**
 * @brief Check whether a spike at the cell would be part of a row of spikes along a wall.
 * @details Two side-by-side spikes hugging the same wall (wall on one side, open floor on the
 * other) line the edge of a room: they are walked around and only imitate the wall. Door and
 * corridor spikes, walled on both sides, and diagonal spike patterns never count.
 */
bool inSpikeRow(const Grid& grid, Coord cell);

/** @brief Count spikes that are part of a row along a wall. */
int spikeRowCells(const Grid& grid);

/**
 * @brief Generate connected rooms, fill content and verify spikes incrementally.
 * @details Every verified level shows all seven cell types (a wall between two rooms turns
 * into a fence when there is none, and a spike-free level gets one spike) and no row of spikes
 * along a wall (inSpikeRow()); a layout that cannot meet this is replaced by the next attempt.
 * @param params Layout and search configuration.
 * @param seed Deterministic seed; the same parameters and seed reproduce the level.
 * @return Last attempted layout, with verified and solution set when a win is found.
 * @note Canvases exceeding the engine dimensions are drawn without verification.
 */
Result generate(const Params& params, std::uint32_t seed);

/**
 * @brief Select a candidate by its measured difficulty.
 * @param params Parameters normally obtained from Params::forSize().
 * @param seed Request seed; later candidates use difficulty::trySeed().
 * @param difficulty Desired score band.
 * @param tries Maximum candidates; one with tune=false preserves generate().
 * @param tune Adjust spikes in a candidate that misses the requested band.
 * @return First candidate in the band, or the closest candidate after optional tuning.
 * @details The first kUntunedTries candidates are only rated; then the closest one is tuned;
 * if it still misses, every further candidate is tuned until one lands in the band.
 */
Result generateRated(const Params& params, std::uint32_t seed, Difficulty difficulty,
                     int tries = difficulty::kTries, bool tune = true);

const char* wallsName(Walls walls);
bool parseWalls(std::string_view text, Walls& out);
const char* kindName(RoomKind kind);

}  // namespace bc::roomgen
