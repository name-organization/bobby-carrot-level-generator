#include "roomgen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

#include "autotile.hpp"
#include "hint.hpp"
#include "rng.hpp"
#include "wfc.hpp"

namespace bc::roomgen {
namespace {

// Slot neighbours: N, E, S, W.
constexpr int kDX[4] = {0, 1, 0, -1};
constexpr int kDY[4] = {-1, 0, 1, 0};

struct Geometry {
    int r = 0;   // room interior, cells per side
    int t = 0;   // wall thickness between rooms
    int nx = 0;  // slots across
    int ny = 0;  // slots down
    int ox = 0;  // top-left interior cell of slot (0, 0)
    int oy = 0;

    int slotX(int sx) const { return ox + sx * (r + t); }
    int slotY(int sy) const { return oy + sy * (r + t); }
    int index(int sx, int sy) const { return sy * nx + sx; }
    bool inSlots(int sx, int sy) const { return sx >= 0 && sx < nx && sy >= 0 && sy < ny; }
};

/** @brief Count room slots while reserving one hedge cell along each map edge. */
int slotsAcross(int size, int r, int t) { return std::max(0, (size - 2 + t) / (r + t)); }

/**
 * @brief Find a self-avoiding slot path using randomized DFS.
 *
 * @return Requested length, or the longest path found within the search budget.
 */
std::vector<int> randomPath(const Geometry& g, int startSlot, int want, Rng& rng) {
    std::vector<char> used(static_cast<std::size_t>(g.nx * g.ny), 0);
    auto options = [&](int slot) {
        std::vector<int> out;
        const int sx = slot % g.nx, sy = slot / g.nx;
        for (int k = 0; k < 4; ++k) {
            const int nx = sx + kDX[k], ny = sy + kDY[k];
            if (g.inSlots(nx, ny) && !used[g.index(nx, ny)]) out.push_back(g.index(nx, ny));
        }
        rng.shuffle(out);
        return out;
    };

    std::vector<int> path{startSlot};
    std::vector<int> best = path;
    std::vector<std::vector<int>> pending{options(startSlot)};  // one entry per path slot
    used[startSlot] = 1;

    for (int budget = 20000; !pending.empty() && budget > 0; --budget) {
        if (static_cast<int>(path.size()) >= want) return path;
        if (pending.back().empty()) {
            pending.pop_back();
            used[path.back()] = 0;
            path.pop_back();
            continue;
        }
        const int slot = pending.back().back();
        pending.back().pop_back();
        if (used[slot]) continue;
        used[slot] = 1;
        path.push_back(slot);
        if (path.size() > best.size()) best = path;
        pending.push_back(options(slot));
    }
    return best;
}

enum class Pattern { None, Field, InnerField, Rows, Checker, Pillar, Scatter };

Pattern pickPattern(Rng& rng) {
    const int r = rng.range(0, 10);
    if (r < 2) return Pattern::Field;
    if (r < 5) return Pattern::InnerField;
    if (r < 7) return Pattern::Rows;
    if (r < 8) return Pattern::Checker;
    if (r < 10) return Pattern::Pillar;
    return Pattern::Scatter;
}

void carrot(Canvas& c, int x, int y) {
    if (c.at(x, y) == T_GROUND) c.at(x, y) = T_CARROT;
}

/** @brief Check connectivity of all non-hedge cells inside a fixed-pattern room. */
bool roomConnected(const Canvas& c, const Room& room) {
    const int w = room.x1 - room.x0 + 1, h = room.y1 - room.y0 + 1;
    std::vector<char> seen(static_cast<std::size_t>(w * h), 0);
    std::vector<int> stack;
    int open = 0, reached = 0;
    for (int y = room.y0; y <= room.y1; ++y) {
        for (int x = room.x0; x <= room.x1; ++x) {
            if (isHedge(c.at(x, y))) continue;
            ++open;
            if (stack.empty() && reached == 0) {
                stack.push_back((y - room.y0) * w + (x - room.x0));
                seen[stack.back()] = 1;
                reached = 1;
            }
        }
    }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        for (int k = 0; k < 4; ++k) {
            const int x = i % w + kDX[k], y = i / w + kDY[k];
            if (x < 0 || x >= w || y < 0 || y >= h) continue;
            const int j = y * w + x;
            if (seen[j] || isHedge(c.at(room.x0 + x, room.y0 + y))) continue;
            seen[j] = 1;
            ++reached;
            stack.push_back(j);
        }
    }
    return reached == open;
}

/** @brief Fill a room with a fixed pattern while preserving entry anchors. */
void fillRoom(Canvas& c, const Room& room, Pattern pattern, const std::vector<char>& entry,
              Rng& rng) {
    const int w = room.x1 - room.x0 + 1, h = room.y1 - room.y0 + 1;
    auto forEach = [&](auto&& fn) {
        for (int y = room.y0; y <= room.y1; ++y)
            for (int x = room.x0; x <= room.x1; ++x) fn(x, y);
    };

    switch (pattern) {
        case Pattern::None:
            break;
        case Pattern::Field:
            forEach([&](int x, int y) { carrot(c, x, y); });
            break;
        case Pattern::InnerField:
            if (w < 3 || h < 3) {
                forEach([&](int x, int y) { carrot(c, x, y); });
                break;
            }
            forEach([&](int x, int y) {
                if (x > room.x0 && x < room.x1 && y > room.y0 && y < room.y1) carrot(c, x, y);
            });
            break;
        case Pattern::Rows: {
            const bool across = rng.chance(0.5);
            const int phase = rng.range(0, 1);
            forEach([&](int x, int y) {
                if (((across ? y - room.y0 : x - room.x0) + phase) % 2 == 0) carrot(c, x, y);
            });
            break;
        }
        case Pattern::Checker: {
            const int phase = rng.range(0, 1);
            forEach([&](int x, int y) {
                if ((x + y + phase) % 2 == 0) carrot(c, x, y);
            });
            break;
        }
        case Pattern::Pillar: {
            if (w < 3 || h < 3) {
                fillRoom(c, room, Pattern::Field, entry, rng);
                break;
            }

            const int size = (w >= 4 && h >= 4 && rng.chance(0.5)) ? 2 : 1;
            const int px = room.x0 + (w - size) / 2 + ((w - size) % 2 ? rng.range(0, 1) : 0);
            const int py = room.y0 + (h - size) / 2 + ((h - size) % 2 ? rng.range(0, 1) : 0);
            bool placed = true;
            for (int y = py; y < py + size; ++y)
                for (int x = px; x < px + size; ++x)
                    if (entry[static_cast<std::size_t>(y * c.w + x)] || c.at(x, y) != T_GROUND)
                        placed = false;
            if (placed) {
                for (int y = py; y < py + size; ++y)
                    for (int x = px; x < px + size; ++x) c.at(x, y) = T_DEFAULT_GRASS;
                if (!roomConnected(c, room)) {
                    for (int y = py; y < py + size; ++y)
                        for (int x = px; x < px + size; ++x) c.at(x, y) = T_GROUND;
                }
            }
            fillRoom(c, room, rng.chance(0.4) ? Pattern::Field : Pattern::Scatter, entry, rng);
            break;
        }
        case Pattern::Scatter:
            forEach([&](int x, int y) {
                if (rng.chance(0.45)) carrot(c, x, y);
            });
            break;
    }
}

constexpr int kWholeMapTries = 2;

// Reject candidates exceeding this conflict budget; generation invokes SAT repeatedly.
constexpr long long kGenerationConflicts = 20000;

// Count reroutes instead of timing them to preserve seeded results across machines.
constexpr int kReroutes = 4;
constexpr long long kRerouteConflicts = 5000;

/**
 * @brief Check a newly placed spike against the current solution, then bounded SAT.
 *
 * @param g Grid containing the candidate spike.
 * @param start Starting cell of the current solution.
 * @param[in,out] reference Updated to the winning route on success.
 * @param[in,out] reroutes Remaining SAT reroutes; cheap reference checks do not consume it.
 * @param[out] out Winning moves on success.
 */
bool stillWinnable(const Grid& g, Coord start, std::vector<Coord>& reference, int& reroutes,
                   hint::Result& out) {
    hint::Options cheap;
    cheap.useSolver = false;  // flood fill and the reference only
    hint::Result h = hint::compute(g, start, reference, cheap);
    if (h.verdict == hint::Verdict::Unknown && reroutes > 0) {
        --reroutes;
        hint::Options reroute;
        reroute.conflictBudget = kRerouteConflicts;
        h = hint::compute(g, start, reference, reroute);
    }
    if (h.verdict != hint::Verdict::Winnable) return false;
    out = std::move(h);
    reference = hint::cellsAlong(start, out.moves);
    return true;
}

wfc::Symbol lowestSymbol(wfc::Mask m) {
    for (int s = 0; s < wfc::kSymbols; ++s)
        if (m & wfc::bit(static_cast<wfc::Symbol>(s))) return static_cast<wfc::Symbol>(s);
    return wfc::Hedge;
}

/** @brief Check whether all walkable interior cells connect to the entry anchors. */
bool regionConnected(const wfc::Region& region, const std::vector<char>& anchor, int x0, int y0,
                     int x1, int y1) {
    const int w = region.w;
    auto walkable = [&](int i) { return (region.cells[static_cast<std::size_t>(i)] & wfc::kWalkable) != 0; };
    auto inside = [&](int x, int y) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; };
    std::vector<char> seen(region.cells.size(), 0);
    std::vector<int> stack;
    for (std::size_t i = 0; i < anchor.size() && stack.empty(); ++i)
        if (anchor[i]) stack.push_back(static_cast<int>(i));
    if (stack.empty()) return true;
    seen[static_cast<std::size_t>(stack.back())] = 1;
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        for (int k = 0; k < 4; ++k) {
            const int x = i % w + kDX[k], y = i / w + kDY[k];
            const int j = y * w + x;
            if (!inside(x, y) || seen[static_cast<std::size_t>(j)] || !walkable(j)) continue;
            seen[static_cast<std::size_t>(j)] = 1;
            stack.push_back(j);
        }
    }
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (walkable(y * w + x) && !seen[static_cast<std::size_t>(y * w + x)]) return false;
    return true;
}

/**
 * @brief Fill rooms and their wall rings with WFC, falling back to fixed patterns.
 *
 * @details Pin doors, entry anchors, start, exit and merged walls. A wall ring
 * committed by one room constrains the next. Context beyond the ring is read-only.
 * The byPattern output marks rooms requiring fixed-pattern content.
 */
void fillWithWfc(const wfc::Model& model, const std::array<int, wfc::kSymbols>& percent,
                 bool whole, int context, int tries, int nodeBudget, Canvas& c,
                 const std::vector<Room>& rooms, bool fenceWalls, int wallThickness,
                 Coord start, Coord exit,
                 const std::vector<char>& entry, const std::vector<char>& widened,
                 std::vector<char>& byPattern, Rng& rng) {
    using namespace wfc;
    const std::size_t n = c.cells.size();
    auto index = [&](int x, int y) { return static_cast<std::size_t>(y * c.w + x); };

    std::vector<int> roomAt(n, -1);
    for (std::size_t i = 0; i < rooms.size(); ++i)
        for (int y = rooms[i].y0; y <= rooms[i].y1; ++y)
            for (int x = rooms[i].x0; x <= rooms[i].x1; ++x) roomAt[index(x, y)] = static_cast<int>(i);
    auto roomOf = [&](int x, int y) { return c.inBounds(x, y) ? roomAt[index(x, y)] : -1; };

    std::vector<Mask> dom(n);
    for (std::size_t i = 0; i < n; ++i)
        dom[i] = isHedge(c.cells[i]) ? bit(Hedge) : bit(symbolOf(c.cells[i]));
    for (std::size_t i = 0; i < n; ++i) {
        if (roomAt[i] >= 0) dom[i] = entry[i] ? (bit(Path) | bit(Carrot)) : kAny;
        if (widened[i]) dom[i] = bit(Path) | bit(Carrot);
    }
    dom[index(start.x, start.y)] = bit(Path);
    dom[index(exit.x, exit.y)] = bit(Path);

    if (fenceWalls && wallThickness == 1) {
        std::vector<char> between(n, 0);
        for (int y = 0; y < c.h; ++y) {
            for (int x = 0; x < c.w; ++x) {
                if (dom[index(x, y)] != bit(Hedge)) continue;
                const int w = roomOf(x - 1, y), e = roomOf(x + 1, y);
                const int nn = roomOf(x, y - 1), s = roomOf(x, y + 1);
                if ((w >= 0 && e >= 0 && w != e) || (nn >= 0 && s >= 0 && nn != s)) between[index(x, y)] = 1;
            }
        }
        for (int y = 0; y < c.h; ++y) {
            for (int x = 0; x < c.w; ++x) {
                if (dom[index(x, y)] != bit(Hedge)) continue;
                int walls = 0;
                for (int k = 0; k < 4; ++k)
                    if (c.inBounds(x + kDX[k], y + kDY[k]) && between[index(x + kDX[k], y + kDY[k])]) ++walls;
                if (between[index(x, y)] || walls >= 2) dom[index(x, y)] = kWall;
            }
        }
    }

    // Whole-map search chooses shared walls jointly; fall back to bounded per-room runs.
    bool wholeDone = false;
    if (whole) {
        for (int attempt = 0; attempt < std::min(tries, kWholeMapTries) && !wholeDone; ++attempt) {
            Region region{c.w, c.h, dom, percent};
            if (!solve(model, region, rng, nodeBudget)) continue;
            bool connected = true;
            for (std::size_t r = 0; r < rooms.size() && connected; ++r) {
                std::vector<char> anchor(n, 0);
                for (std::size_t i = 0; i < n; ++i)
                    anchor[i] = roomAt[i] == static_cast<int>(r) && entry[i];
                connected = regionConnected(region, anchor, rooms[r].x0, rooms[r].y0, rooms[r].x1,
                                            rooms[r].y1);
            }
            if (!connected) continue;
            dom = region.cells;
            std::fill(byPattern.begin(), byPattern.end(), 0);
            wholeDone = true;
        }
    }

    for (std::size_t r = 0; r < rooms.size() && !wholeDone; ++r) {
        const Room& room = rooms[r];
        // Only the room and its wall ring are written back; extra context constrains patterns.
        const int rx0 = std::max(0, room.x0 - context), ry0 = std::max(0, room.y0 - context);
        const int rx1 = std::min(c.w - 1, room.x1 + context);
        const int ry1 = std::min(c.h - 1, room.y1 + context);
        Region base{rx1 - rx0 + 1, ry1 - ry0 + 1, {}, percent};
        std::vector<char> anchor;
        for (int y = ry0; y <= ry1; ++y) {
            for (int x = rx0; x <= rx1; ++x) {
                base.cells.push_back(dom[index(x, y)]);
                anchor.push_back(roomAt[index(x, y)] == static_cast<int>(r) && entry[index(x, y)]);
            }
        }
        const int ix0 = room.x0 - rx0, iy0 = room.y0 - ry0;  // the room inside the region
        const int ix1 = room.x1 - rx0, iy1 = room.y1 - ry0;
        bool done = false;
#ifdef BC_WFC_DEBUG
        int unsolved = 0, cut = 0;
#endif
        for (int attempt = 0; attempt < tries && !done; ++attempt) {
            Region region = base;
            const bool solved = solve(model, region, rng, nodeBudget);
            const bool connected = solved && regionConnected(region, anchor, ix0, iy0, ix1, iy1);
#ifdef BC_WFC_DEBUG
            unsolved += !solved;
            cut += solved && !connected;
#endif
            if (!connected) continue;
            for (int y = iy0 - 1; y <= iy1 + 1; ++y)
                for (int x = ix0 - 1; x <= ix1 + 1; ++x)
                    dom[index(rx0 + x, ry0 + y)] = region.cells[static_cast<std::size_t>(y * base.w + x)];
            done = true;
        }
        byPattern[r] = !done;
#ifdef BC_WFC_DEBUG
        if (!done) {
            std::printf("WFC failed: unsolved %d, disconnected %d\n", unsolved, cut);
            for (int y = 0; y < base.h; ++y) {
                for (int x = 0; x < base.w; ++x) {
                    const Mask m = base.cells[static_cast<std::size_t>(y * base.w + x)];
                    const bool in = x >= ix0 && x <= ix1 && y >= iy0 && y <= iy1;
                    char ch = '?';
                    if (m == bit(Hedge)) ch = '#';
                    else if (m == bit(Fence)) ch = '=';
                    else if (m == bit(Path)) ch = '.';
                    else if (m == bit(Carrot)) ch = '*';
                    else if (m == bit(Spike)) ch = '_';
                    else if (m == kWall) ch = 'w';
                    else if (m == (bit(Path) | bit(Carrot))) ch = 'p';
                    else if (m == kAny) ch = in ? ' ' : '?';
                    std::putchar(ch);
                }
                std::putchar('\n');
            }
        }
#endif
        if (!done)
            for (int y = room.y0; y <= room.y1; ++y)
                for (int x = room.x0; x <= room.x1; ++x) dom[index(x, y)] = bit(Path);
    }

    // Resolve unassigned fallback rings, preferring hedge where the mask allows it.
    for (std::size_t i = 0; i < n; ++i) {
        const Mask m = dom[i];
        const Symbol s = (m & (m - 1)) == 0 ? lowestSymbol(m) : ((m & bit(Hedge)) ? Hedge : lowestSymbol(m));
        c.cells[i] = tileOf(s);
    }
}

/** @brief Build one layout; return false when geometry cannot connect its rooms. */
bool build(const Params& p, Walls walls, Rng& rng, Result& out) {
    Geometry g;
    g.r = p.roomSize;
    g.t = walls == Walls::Hedge ? 2 : 1;
    g.nx = slotsAcross(p.width, g.r, g.t);
    g.ny = slotsAcross(p.height, g.r, g.t);
    if (g.nx < 1 || g.ny < 1) return false;
    const int usedW = g.nx * g.r + (g.nx - 1) * g.t;
    const int usedH = g.ny * g.r + (g.ny - 1) * g.t;
    g.ox = rng.range(1, p.width - usedW - 1);
    g.oy = rng.range(1, p.height - usedH - 1);
    out.slotsX = g.nx;
    out.slotsY = g.ny;

    const int slots = g.nx * g.ny;

    const int total =
        slots <= 4 && p.loopChance > 0
            ? slots
            : std::clamp(static_cast<int>(std::lround(p.fill * slots)), std::min(3, slots), slots);
    const int mainLen =
        std::clamp(static_cast<int>(std::lround(total * p.mainShare)), std::min(2, total), total);

    // the originals mostly start at the bottom
    const int startX = rng.range(0, g.nx - 1);
    const int startY = rng.chance(0.5) ? g.ny - 1 : rng.range(0, g.ny - 1);
    const std::vector<int> path = randomPath(g, g.index(startX, startY), mainLen, rng);

    std::vector<int> slotRoom(static_cast<std::size_t>(slots), -1);
    std::vector<Room>& rooms = out.rooms;
    rooms.clear();
    for (std::size_t i = 0; i < path.size(); ++i) {
        Room room;
        room.sx = path[i] % g.nx;
        room.sy = path[i] / g.nx;
        room.kind = i == 0 ? RoomKind::Start
                           : (i + 1 == path.size() ? RoomKind::Exit : RoomKind::Main);
        room.parent = static_cast<int>(i) - 1;
        slotRoom[path[i]] = static_cast<int>(i);
        rooms.push_back(room);
    }
    while (static_cast<int>(rooms.size()) < total) {
        std::vector<std::pair<int, int>> grow;  // (room, free neighbouring slot)
        for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
            for (int k = 0; k < 4; ++k) {
                const int sx = rooms[i].sx + kDX[k], sy = rooms[i].sy + kDY[k];
                if (g.inSlots(sx, sy) && slotRoom[g.index(sx, sy)] == -1)
                    grow.emplace_back(i, g.index(sx, sy));
            }
        }
        if (grow.empty()) break;
        const auto [from, slot] = grow[rng.range(0, static_cast<int>(grow.size()) - 1)];
        Room room;
        room.sx = slot % g.nx;
        room.sy = slot / g.nx;
        room.kind = RoomKind::Side;
        room.parent = from;
        slotRoom[slot] = static_cast<int>(rooms.size());
        rooms.push_back(room);
    }
    const int exitRoom = static_cast<int>(path.size()) - 1;  // the start room when alone

    auto usedAt = [&](int sx, int sy) {
        return g.inSlots(sx, sy) && slotRoom[g.index(sx, sy)] != -1;
    };

    // Only thick hedge walls allow shrinking a room toward its neighbour.
    for (Room& room : rooms) {
        room.x0 = g.slotX(room.sx);
        room.y0 = g.slotY(room.sy);
        room.x1 = room.x0 + g.r - 1;
        room.y1 = room.y0 + g.r - 1;
        if (g.r < 4 || !rng.chance(p.insetChance)) continue;
        std::vector<int> sides;
        for (int k = 0; k < 4; ++k)
            if (walls == Walls::Hedge || !usedAt(room.sx + kDX[k], room.sy + kDY[k]))
                sides.push_back(k);
        if (sides.empty()) continue;
        switch (sides[rng.range(0, static_cast<int>(sides.size()) - 1)]) {
            case 0: ++room.y0; break;
            case 1: --room.x1; break;
            case 2: --room.y1; break;
            default: ++room.x0; break;
        }
    }

    Canvas& c = out.canvas;
    c = Canvas(p.width, p.height, T_DEFAULT_GRASS);
    for (const Room& room : rooms)
        for (int y = room.y0; y <= room.y1; ++y)
            for (int x = room.x0; x <= room.x1; ++x) c.at(x, y) = T_GROUND;

    const bool useWfc = p.model && !p.model->empty();
    if (walls == Walls::Fence && !useWfc) {  // with WFC the walls are decided per room
        // a fence between every two neighbouring rooms, door or not
        for (const Room& a : rooms) {
            for (int k : {1, 2}) {
                const int sx = a.sx + kDX[k], sy = a.sy + kDY[k];
                if (!usedAt(sx, sy)) continue;
                const Room& b = rooms[slotRoom[g.index(sx, sy)]];
                if (k == 1) {
                    for (int y = std::max(a.y0, b.y0); y <= std::min(a.y1, b.y1); ++y)
                        c.at(a.x1 + 1, y) = T_FENCE_VV;
                } else {
                    for (int x = std::max(a.x0, b.x0); x <= std::min(a.x1, b.x1); ++x)
                        c.at(x, a.y1 + 1) = T_FENCE_VV;
                }
            }
        }
        // where fences meet, the crossing cell joins them
        for (int sy = 0; sy + 1 < g.ny; ++sy) {
            for (int sx = 0; sx + 1 < g.nx; ++sx) {
                const int x = g.slotX(sx) + g.r, y = g.slotY(sy) + g.r;
                int fences = 0;
                for (int k = 0; k < 4; ++k)
                    if (c.inBounds(x + kDX[k], y + kDY[k]) && isFence(c.at(x + kDX[k], y + kDY[k])))
                        ++fences;
                if (fences >= 2) c.at(x, y) = T_FENCE_VV;
            }
        }
    }

    // Tree doors guarantee connectivity; extra doors close cycles.
    std::vector<Door>& doors = out.doors;
    doors.clear();
    for (std::size_t i = 1; i < rooms.size(); ++i) {
        Door d;
        d.a = rooms[i].parent;
        d.b = static_cast<int>(i);
        doors.push_back(d);
    }
    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
        for (int k : {1, 2}) {  // east and south: every pair once
            const int sx = rooms[i].sx + kDX[k], sy = rooms[i].sy + kDY[k];
            if (!usedAt(sx, sy)) continue;
            const int j = slotRoom[g.index(sx, sy)];
            if (rooms[j].parent == i || rooms[i].parent == j) continue;  // already a tree door
            if (!rng.chance(p.loopChance)) continue;
            Door d;
            d.a = i;
            d.b = j;
            d.loop = true;
            doors.push_back(d);
        }
    }

    std::vector<char> entry(static_cast<std::size_t>(c.w * c.h), 0);
    std::vector<char> widened(static_cast<std::size_t>(c.w * c.h), 0);  // cells of merged walls
    std::vector<Coord> entryOf(rooms.size(), Coord{-1, -1});  // where the tree door comes in
    for (Door& door : doors) {
        const Room& a = rooms[static_cast<std::size_t>(door.a)];
        const Room& b = rooms[static_cast<std::size_t>(door.b)];
        door.spike = rng.chance(p.spikeDoorChance);
        door.merged = !door.spike && rng.chance(p.mergeChance);

        const bool horizontal = a.sy == b.sy;
        const bool aFirst = horizontal ? a.sx < b.sx : a.sy < b.sy;
        const Room& lo = aFirst ? a : b;
        const Room& hi = aFirst ? b : a;
        // the span both interiors share across the wall, and the corridor through it
        const int s0 = horizontal ? std::max(lo.y0, hi.y0) : std::max(lo.x0, hi.x0);
        const int s1 = horizontal ? std::min(lo.y1, hi.y1) : std::min(lo.x1, hi.x1);
        const int c0 = horizontal ? lo.x1 + 1 : lo.y1 + 1;
        const int c1 = horizontal ? hi.x0 - 1 : hi.y0 - 1;
        if (s0 > s1) return false;

        const int at = rng.range(s0, s1);
        const int from = door.merged ? s0 : at;
        const int to = door.merged ? s1 : at;
        const int len = c1 - c0 + 1;
        for (int s = from; s <= to; ++s) {
            for (int q = c0; q <= c1; ++q) {
                // a short door is all spikes, a long one has a single spike midway
                const bool spike = door.spike && (len <= 2 || q == c0 + len / 2);
                const std::uint8_t tile = spike ? T_CRACKED : T_GROUND;
                const int x = horizontal ? q : s, y = horizontal ? s : q;
                c.at(x, y) = tile;
                door.cells.push_back({x, y});
                if (door.merged) widened[static_cast<std::size_t>(y * c.w + x)] = 1;
            }
        }
        const Coord loCell = horizontal ? Coord{c0 - 1, at} : Coord{at, c0 - 1};
        const Coord hiCell = horizontal ? Coord{c1 + 1, at} : Coord{at, c1 + 1};
        entry[static_cast<std::size_t>(loCell.y * c.w + loCell.x)] = 1;
        entry[static_cast<std::size_t>(hiCell.y * c.w + hiCell.x)] = 1;
        if (!door.loop) entryOf[static_cast<std::size_t>(door.b)] = aFirst ? hiCell : loCell;
    }

    auto cellsOf = [](const Room& room) {
        std::vector<Coord> out;
        for (int y = room.y0; y <= room.y1; ++y)
            for (int x = room.x0; x <= room.x1; ++x) out.push_back({x, y});
        return out;
    };

    {
        std::vector<Coord> cells = cellsOf(rooms[0]);
        std::vector<Coord> free;
        for (Coord cell : cells)
            if (!entry[static_cast<std::size_t>(cell.y * c.w + cell.x)]) free.push_back(cell);
        const std::vector<Coord>& pool = free.empty() ? cells : free;
        out.start = pool[rng.range(0, static_cast<int>(pool.size()) - 1)];
    }
    {
        // the exit goes far from where Bobby comes in
        const Coord from = exitRoom == 0 ? out.start : entryOf[exitRoom];
        std::vector<Coord> cells = cellsOf(rooms[exitRoom]);
        int far = -1;
        for (Coord cell : cells) {
            if (cell == out.start) continue;
            far = std::max(far, std::abs(cell.x - from.x) + std::abs(cell.y - from.y));
        }
        std::vector<Coord> pool;
        for (Coord cell : cells) {
            if (cell == out.start) continue;
            if (std::abs(cell.x - from.x) + std::abs(cell.y - from.y) >= far - 1) pool.push_back(cell);
        }
        if (pool.empty()) return false;  // a one-cell map
        out.exit = pool[rng.range(0, static_cast<int>(pool.size()) - 1)];
    }
    entry[static_cast<std::size_t>(out.start.y * c.w + out.start.x)] = 1;
    entry[static_cast<std::size_t>(out.exit.y * c.w + out.exit.x)] = 1;

    std::vector<char> byPattern(rooms.size(), 1);  // rooms left to the fixed patterns
    if (useWfc) {
        const std::array<int, wfc::kSymbols> percent{p.wfcHedgePercent, 100, 100,
                                                     p.wfcCarrotPercent, p.wfcSpikePercent};
        const bool whole = p.wfcWholeMap && p.width * p.height <= p.wfcWholeMapCells;
        fillWithWfc(*p.model, percent, whole, std::max(1, p.wfcContext), std::max(1, p.wfcTries),
                    std::max(1, p.wfcNodeBudget), c, rooms, walls == Walls::Fence, g.t, out.start,
                    out.exit, entry, widened, byPattern, rng);
        for (char left : byPattern) left ? ++out.fallbackRooms : ++out.wfcRooms;
    }
    c.at(out.start.x, out.start.y) = T_START;
    c.at(out.exit.x, out.exit.y) = T_EXIT;

    for (std::size_t i = 0; i < rooms.size(); ++i) {
        if (!byPattern[i]) continue;
        Pattern pattern = Pattern::None;
        switch (rooms[i].kind) {
            case RoomKind::Start: break;  // the originals start in an empty hall
            case RoomKind::Exit: if (rng.chance(0.35)) pattern = pickPattern(rng); break;
            case RoomKind::Main: if (!rng.chance(p.emptyRoomChance)) pattern = pickPattern(rng); break;
            case RoomKind::Side: pattern = pickPattern(rng); break;  // a dead end pays off
        }
        fillRoom(c, rooms[i], pattern, entry, rng);
    }

    // a level without carrots would be an egg level with nothing to collect
    if (std::none_of(c.cells.begin(), c.cells.end(), [](std::uint8_t t) { return t == T_CARROT; })) {
        std::vector<Coord> pool;
        for (const Room& room : rooms)
            for (Coord cell : cellsOf(room))
                if (c.at(cell.x, cell.y) == T_GROUND) pool.push_back(cell);
        if (pool.empty()) return false;
        const Coord cell = pool[rng.range(0, static_cast<int>(pool.size()) - 1)];
        c.at(cell.x, cell.y) = T_CARROT;
    }

    autotile(c);
    return true;
}

int countTiles(const Grid& g, std::uint8_t tile) {
    return static_cast<int>(std::count(g.cells.begin(), g.cells.end(), tile));
}

/**
 * @brief Reinsert initial spikes one at a time, prioritizing doors.
 *
 * @details The spike-free layout supplies a winning reference. Keep each spike
 * only if reference replay or a bounded reroute still wins; avoid repeated
 * unsatisfiability proofs for the original spike arrangement.
 */
hint::Result placeSpikes(Level& level, const std::vector<Door>& doors, Rng& rng, int& dropped,
                         int& reroutes) {
    Grid& g = level.grid;
    std::vector<char> onDoor(Grid::kCells, 0);
    for (const Door& door : doors)
        for (Coord cell : door.cells) onDoor[cell.index()] = 1;
    std::vector<Coord> doorSpikes, roomSpikes;
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            if (g.at(x, y) != T_CRACKED) continue;
            (onDoor[Coord{x, y}.index()] ? doorSpikes : roomSpikes).push_back({x, y});
            g.at(x, y) = T_GROUND;
        }
    }
    rng.shuffle(doorSpikes);
    rng.shuffle(roomSpikes);

    hint::Options options;
    options.conflictBudget = kGenerationConflicts;
    hint::Result best = hint::compute(g, level.start, {}, options);
    dropped = 0;
    if (best.verdict != hint::Verdict::Winnable) return best;
    std::vector<Coord> reference = hint::cellsAlong(level.start, best.moves);
    for (const std::vector<Coord>* group : {&doorSpikes, &roomSpikes}) {
        for (Coord cell : *group) {
            g.at(cell) = T_CRACKED;
            if (!stillWinnable(g, level.start, reference, reroutes, best)) {
                g.at(cell) = T_GROUND;
                ++dropped;
            }
        }
    }
    return best;
}

void markSpikeDoors(Result& res) {
    for (Door& door : res.doors) {
        door.spike = false;
        for (Coord cell : door.cells)
            if (res.canvas.inBounds(cell.x, cell.y) && res.canvas.at(cell.x, cell.y) == T_CRACKED)
                door.spike = true;
    }
}

/**
 * @brief Add spikes toward the target while preserving a verified winning route.
 *
 * @details Corridor/corner/junction/open-floor weights are 15/7/8/6, inferred
 * from original levels. Dead ends are excluded; WFC likeness limits may reject spikes.
 */
void addSpikes(Result& res, int target, const wfc::Model* model, int maxUnknown, int& reroutes,
               Rng& rng) {
    Level level;
    if (!res.canvas.toLevel(level)) return;
    Grid& g = level.grid;
    const int want = target - countTiles(g, T_CRACKED);
    if (want <= 0) return;
    auto walkable = [&](int x, int y) {
        return g.inBounds(x, y) && g.at(x, y) >= T_FIRST_WALKABLE;
    };

    // Repeated entries plus shuffle give a weighted order without platform-dependent rounding.
    std::vector<Coord> pool;
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            const std::uint8_t t = g.at(x, y);
            if (t != T_GROUND && t != T_CARROT) continue;
            const bool n = walkable(x, y - 1), s = walkable(x, y + 1);
            const bool e = walkable(x + 1, y), w = walkable(x - 1, y);
            const int open = n + s + e + w;
            if (open <= 1) continue;  // a dead end
            const bool corridor = open == 2 && ((n && s) || (e && w));
            const int weight = corridor ? 15 : (open == 2 ? 7 : (open == 3 ? 8 : 6));
            for (int k = 0; k < weight; ++k) pool.push_back(Coord{x, y});
        }
    }
    rng.shuffle(pool);

    std::vector<char> tried(Grid::kCells, 0);
    std::vector<Coord> reference = hint::cellsAlong(level.start, res.solution);
    int carrots = level.carrotTotal;
    int placed = 0, tries = 0;
    for (Coord cell : pool) {
        if (placed >= want || tries >= want * 4) break;
        if (tried[cell.index()]) continue;
        tried[cell.index()] = 1;
        const std::uint8_t old = g.at(cell);
        if (old == T_CARROT && carrots <= 1) continue;  // keep it a carrot level
        g.at(cell) = T_CRACKED;
        if (model) {
            // Reject spikes that introduce too many unknown learned-pattern windows.
            Canvas view = Canvas::fromLevel(level);
            if (wfc::unknownWindows(*model, view, cell.x, cell.y) > maxUnknown) {
                g.at(cell) = old;
                continue;
            }
        }
        ++tries;
        hint::Result h;
        if (stillWinnable(g, level.start, reference, reroutes, h)) {
            res.solution = h.moves;
            ++placed;
            if (old == T_CARROT) --carrots;
        } else {
            g.at(cell) = old;
        }
    }
    res.canvas = Canvas::fromGrid(g);
}

}  // namespace

Result generate(const Params& params, std::uint32_t seed) {
    Params p = params;
    p.width = std::max(p.width, 5);
    p.height = std::max(p.height, 5);
    p.roomSize = std::clamp(p.roomSize, 2, std::min(p.width, p.height) - 2);
    p.attempts = std::max(1, p.attempts);

    Rng rng(seed);
    Result last;
    last.seed = seed;
    for (int attempt = 1; attempt <= p.attempts; ++attempt) {
        Walls walls = p.walls;
        if (walls == Walls::Auto) {
            const int r = rng.range(0, 9);
            walls = r < 5 ? Walls::Fence : (r < 9 ? Walls::Hedge : Walls::ThinHedge);
        }
        Result res;
        res.seed = seed;
        res.walls = walls;
        res.attempts = attempt;
        if (!build(p, walls, rng, res)) {
            last = std::move(res);
            continue;
        }

        Level level;
        if (!res.canvas.toLevel(level)) return res;  // bigger than the engine plays
        int dropped = 0, reroutes = kReroutes;
        const hint::Result h = placeSpikes(level, res.doors, rng, dropped, reroutes);
        res.spikesRepaired = dropped > 0;
        res.canvas = Canvas::fromGrid(level.grid);
        if (h.verdict == hint::Verdict::Winnable) {
            res.solution = h.moves;
            res.verified = true;
            addSpikes(res, p.spikeTarget, p.model && !p.model->empty() ? p.model : nullptr,
                      p.spikeUnknownWindows, reroutes, rng);
            markSpikeDoors(res);
            return res;
        }
        last = std::move(res);
    }
    return last;
}

Result generateRated(const Params& params, std::uint32_t seed, Difficulty difficulty,
                     int tries) {
    Result best;
    int bestOff = -1;  // how far the best score is from the band, -1 before the first
    int built = 0;
    for (int k = 0; k < std::max(1, tries); ++k) {
        ++built;
        Result res = generate(params, difficulty::trySeed(seed, k));
        Level level;
        int off = 1000;  // a level the engine cannot play or the solver did not win
        if (res.verified && res.canvas.toLevel(level)) {
            res.rating = difficulty::rate(level, res.solution, res.seed);
            off = difficulty::distanceToBand(res.rating.score, difficulty);
        }
        if (bestOff < 0 || off < bestOff) {
            best = std::move(res);
            bestOff = off;
        }
        if (bestOff == 0) break;
    }
    best.tries = built;
    return best;
}

Params Params::forSize(int width, int height, Difficulty difficulty) {
    Params p;
    p.width = width;
    p.height = height;
    const int side = std::max(width, height);
    if (side <= 18) {
        p.roomSize = 4;
        p.wfcCarrotPercent = 700;
        p.spikeTarget = 6;
    } else if (side <= 26) {
        p.roomSize = 5;
        p.wfcCarrotPercent = 320;
        p.spikeTarget = 12;
    } else {
        p.roomSize = 6;
        p.wfcCarrotPercent = 300;
        p.spikeTarget = 20;
    }
    switch (difficulty) {
        case Difficulty::Easy:
            p.fill = 0.55;
            p.spikeDoorChance = 0.1;
            p.spikeTarget = p.spikeTarget * 2 / 5;
            break;
        case Difficulty::Medium:
            break;
        case Difficulty::Hard:
            p.fill = 0.85;
            p.spikeDoorChance = 1.0;
            p.loopChance = 0.3;
            p.spikeTarget *= 2;
            break;
    }
    return p;
}

const char* wallsName(Walls walls) {
    switch (walls) {
        case Walls::Auto: return "auto";
        case Walls::Fence: return "fence";
        case Walls::ThinHedge: return "thin";
        case Walls::Hedge: return "hedge";
    }
    return "?";
}

bool parseWalls(std::string_view text, Walls& out) {
    for (Walls w : {Walls::Auto, Walls::Fence, Walls::ThinHedge, Walls::Hedge}) {
        if (text == wallsName(w)) {
            out = w;
            return true;
        }
    }
    return false;
}

const char* kindName(RoomKind kind) {
    switch (kind) {
        case RoomKind::Start: return "start";
        case RoomKind::Main: return "main";
        case RoomKind::Exit: return "exit";
        case RoomKind::Side: return "side";
    }
    return "?";
}

}  // namespace bc::roomgen
