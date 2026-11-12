#include "generator.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <vector>

#include "tiles.hpp"

namespace bc {
namespace {

bool insideWalkArea(Coord c) {
    return c.x >= 1 && c.x < Grid::kClassic - 1 &&
           c.y >= 1 && c.y < Grid::kClassic - 1;
}

/** @brief Apply the legacy fence rules without changing seeded level layouts. */
void autotileLegacy(Grid& grid) {
    // Legacy autotiling reads earlier writes; preserve scan order for existing seeds.
    for (int y = 0; y < Grid::kClassic; ++y) {
        for (int x = 0; x < Grid::kClassic; ++x) {
            Coord c{x, y};
            std::uint8_t tile = grid.at(c);

            if (tile >= T_FIRST_WALKABLE || tile == T_DEFAULT_GRASS || tile == T_SINGLE_GRASS) {
                continue;
            }

            auto isFence = [&](int nx, int ny) {
                if (nx < 0 || nx >= Grid::kClassic || ny < 0 || ny >= Grid::kClassic) return false;
                std::uint8_t neighborTile = grid.at(nx, ny);
                return neighborTile < T_FIRST_WALKABLE &&
                       neighborTile != T_DEFAULT_GRASS &&
                       neighborTile != T_SINGLE_GRASS;
            };

            bool hasL = isFence(x - 1, y);
            bool hasR = isFence(x + 1, y);
            bool hasU = isFence(x, y - 1);
            bool hasD = isFence(x, y + 1);

            bool hasDiagUpLeft  = isFence(x - 1, y - 1);
            bool hasDiagUpRight = isFence(x + 1, y - 1);

            if (hasU && (hasDiagUpLeft || hasDiagUpRight)) {
                grid.at(c) = T_SINGLE_GRASS;
            } else {
                if (hasL && hasR) {
                    grid.at(c) = T_FENCE_LVR;
                } else if (hasD && hasR) {
                    grid.at(c) = T_FENCE_VVR;
                } else if (hasL && hasD) {
                    grid.at(c) = T_FENCE_LVV;
                } else if (hasR) {
                    grid.at(c) = T_FENCE_VR;
                } else if (hasL) {
                    grid.at(c) = T_FENCE_LV;
                } else if (hasU || hasD) {
                    grid.at(c) = T_FENCE_VV;
                } else {
                    grid.at(c) = T_SINGLE_GRASS;
                }
            }
        }
    }
}

}  // namespace

Level LevelGenerator::generateRandom(unsigned int seed, int pathLength) {
    Level lv;
    std::mt19937 rng(seed == 0 ? std::random_device{}() : seed);
    std::uniform_int_distribution<int> distDir(0, 3);
    std::uniform_int_distribution<int> distChance(0, 100);

    const int dx[4] = {-1, 1, 0, 0};
    const int dy[4] = {0, 0, -1, 1};

    while (true) {
        lv.grid.cells.fill(T_DEFAULT_GRASS);

        std::uniform_int_distribution<int> distCoordX(1, Grid::kClassic - 2);
        std::uniform_int_distribution<int> distCoordY(1, Grid::kClassic - 2);

        Coord start{distCoordX(rng), distCoordY(rng)};
        lv.start = start;

        Coord current = start;

        // First-visit order drives placement; this vector is not a winning move sequence.
        std::array<int, Grid::kCells> visitCounts{};
        std::vector<Coord> visitedCells;

        visitCounts[start.index()]++;
        visitedCells.push_back(start);
        lv.grid.at(start) = T_GROUND;

        int minX = start.x, maxX = start.x;
        int minY = start.y, maxY = start.y;

        int lastDir = -1;
        bool pathFailed = false;
        for (int step = 0; step < pathLength; ++step) {
            int d = distDir(rng);
            if (lastDir != -1 && distChance(rng) < 40) {
                d = lastDir;
            }

            Coord next{current.x + dx[d], current.y + dy[d]};

            int safety = 0;
            while (!insideWalkArea(next) && safety < 4) {
                d = distDir(rng);
                next.x = current.x + dx[d];
                next.y = current.y + dy[d];
                safety++;
            }

            if (safety >= 4) {
                pathFailed = true;
                break;
            }

            current = next;
            lastDir = d;

            if (visitCounts[current.index()] == 0) {
                visitedCells.push_back(current);
            }
            visitCounts[current.index()]++;
            lv.grid.at(current) = T_GROUND;

            minX = std::min(minX, current.x);
            maxX = std::max(maxX, current.x);
            minY = std::min(minY, current.y);
            maxY = std::max(maxY, current.y);
        }

        if (pathFailed) {
            continue;
        }

        Coord exitCoord = current;
        if (exitCoord == lv.start || visitedCells.size() < 4) {
            continue;
        }

        lv.exit = exitCoord;
        lv.grid.at(exitCoord) = T_EXIT;
        lv.grid.at(lv.start) = T_START;

        std::array<int, Grid::kCells> pathNeighborsMap{};
        for (const Coord& c : visitedCells) {
            int neighborCount = 0;
            for (int i = 0; i < 4; ++i) {
                Coord n{c.x + dx[i], c.y + dy[i]};
                if (insideWalkArea(n) && visitCounts[n.index()] > 0) {
                    neighborCount++;
                }
            }
            pathNeighborsMap[c.index()] = neighborCount;
        }

        // A spike is safe on the original walk only when that cell is crossed once.
        std::vector<Coord> spikeCandidates;
        for (const Coord& c : visitedCells) {
            if (c == lv.start || c == lv.exit) continue;
            if (visitCounts[c.index()] == 1 && pathNeighborsMap[c.index()] > 1) {
                spikeCandidates.push_back(c);
            }
        }

        if (spikeCandidates.empty()) {
            continue;
        }

        std::uniform_int_distribution<std::size_t> distCandidate(0, spikeCandidates.size() - 1);
        Coord forcedSpike = spikeCandidates[distCandidate(rng)];
        lv.grid.at(forcedSpike) = T_CRACKED;

        for (int y = 0; y < Grid::kClassic; ++y) {
            for (int x = 0; x < Grid::kClassic; ++x) {
                Coord c{x, y};
                int visits = visitCounts[c.index()];

                if (visits > 0) {
                    bool isStartOrExit = (c == lv.start) || (c == lv.exit);
                    if (visits == 1 && !isStartOrExit && !(c == forcedSpike)) {
                        if (pathNeighborsMap[c.index()] > 1 && distChance(rng) < 25) {
                            lv.grid.at(c) = T_CRACKED;
                        }
                    }
                } else {
                    if (x >= minX && x <= maxX && y >= minY && y <= maxY) {
                        if (distChance(rng) < 75) {
                            lv.grid.at(c) = T_FENCE_VV;
                        } else {
                            lv.grid.at(c) = T_GROUND;
                        }
                    } else {
                        lv.grid.at(c) = T_DEFAULT_GRASS;
                    }
                }
            }
        }

        autotileLegacy(lv.grid);

        lv.carrotTotal = 0;
        for (const Coord& c : visitedCells) {
            if (c == lv.start || c == lv.exit) continue;
            if (lv.grid.at(c) != T_GROUND) continue;

            if (pathNeighborsMap[c.index()] <= 1 || (visitCounts[c.index()] == 1 && distChance(rng) < 70)) {
                lv.grid.at(c) = T_CARROT;
                lv.carrotTotal++;
            }
        }

        if (lv.carrotTotal == 0 && visitedCells.size() > 2) {
            lv.grid.at(visitedCells[1]) = T_CARROT;
            lv.carrotTotal = 1;
        }

        lv.eggTotal = 0;
        return lv;
    }
}

}  // namespace bc
