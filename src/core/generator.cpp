#include "generator.hpp"
#include <vector>
#include <algorithm>
#include <queue>

namespace bc {

Level LevelGenerator::generateRandom(unsigned int seed) {
    Level lv;
    // Изначально заполняем всю карту сплошными препятствиями/стенами (< T_FIRST_WALKABLE, например 0)
    lv.grid.cells.fill(0); 

    std::mt19937 rng(seed == 0 ? std::random_device{}() : seed);
    std::uniform_int_distribution<int> distDir(0, 3);

    // 1. Стартовая позиция
    Coord start{2, 2};
    lv.start = start;

    Coord cur = start;
    int pathLength = std::uniform_int_distribution<int>(45, 75)(rng); // метрика сложности

    std::array<int, Grid::kCells> visitCounts{};
    std::vector<Coord> pathCells;
    
    visitCounts[start.index()]++;
    pathCells.push_back(start);
    lv.grid.at(start) = T_GROUND;

    const int dx[4] = {-1, 1, 0, 0};
    const int dy[4] = {0, 0, -1, 1};

    // 2. Генерация блуждания (пути)
    for (int step = 0; step < pathLength; ++step) {
        int d = distDir(rng);
        Coord next{cur.x + dx[d], cur.y + dy[d]};

        // Держим отступ от краев карты (например, в пределах 1..W-2)
        if (next.x >= 1 && next.x < Grid::W - 1 && next.y >= 1 && next.y < Grid::H - 1) {
            cur = next;
            if (visitCounts[cur.index()] == 0) {
                pathCells.push_back(cur);
            }
            visitCounts[cur.index()]++;
            lv.grid.at(cur) = T_GROUND; // На пройденном пути всегда земля
        }
    }

    // 3. Финиш там, где остановился генератор
    Coord exitCoord = cur;
    lv.exit = exitCoord;
    lv.grid.at(exitCoord) = T_EXIT;
    lv.grid.at(lv.start) = T_START;

    // 4. Расстановка препятствий по вашим правилам:
    std::uniform_int_distribution<int> distChance(0, 100);

    for (int y = 0; y < Grid::H; ++y) {
        for (int x = 0; x < Grid::W; ++x) {
            Coord c{x, y};
            int visits = visitCounts[c.index()];

            if (visits > 0) {
                // ПРАВИЛО 2: На клетках, по которым путь прошёл ХОТЯ БЫ ОДИН РАЗ, 
                // не может быть заборов и травы (там должна оставаться земля или интерактивные объекты).
                // Но мы можем расставить шипы/ловушки, НО ТОЛЬКО если путь здесь ПРОШЕЛ РОВНО 1 РАЗ:
                // ПРАВИЛО 1: На клетках с посещениями > 1 шипов быть НЕ МОЖЕТ.
                if (visits == 1 && (c.x != lv.start.x || c.y != lv.start.y) && (c.x != lv.exit.x || c.y != lv.exit.y)) {
                    // С некоторой вероятностью превращаем одиночный тайл в шипы (например, T_CRACKED)
                    if (distChance(rng) < 50) { // 12% шанс на одиночных участках
                        lv.grid.at(c) = T_CRACKED; 
                    }
                }
            } else {
                // Если путь СОВСЕМ НЕ ПРОХОДИЛ через эту клетку (visits == 0),
                // здесь можно разместить глухие заборы/стены/декорации (оставляем 0 или ставим блокировку).
                // По умолчанию у нас уже стоит 0 (solid scenery).
            }
        }
    }

    // 5. Расстановка морковок (в тупиках и на одиночных ветках пути для мотивации обойти всё)
    lv.carrotTotal = 0;
    for (const Coord& c : pathCells) {
        if (c == lv.start || c == lv.exit) continue;
        if (lv.grid.at(c) != T_GROUND) continue; // Не ставим морковки поверх шипов

        int pathNeighbors = 0;
        for (int i = 0; i < 4; ++i) {
            Coord n{c.x + dx[i], c.y + dy[i]};
            if (Grid::inBounds(n.x, n.y) && visitCounts[n.index()] > 0) {
                pathNeighbors++;
            }
        }

        // Если это тупик (1 сосед) или одиночный проход с вероятностью
        if (pathNeighbors <= 1 || (visitCounts[c.index()] == 1 && distChance(rng) < 50)) {
            lv.grid.at(c) = T_CARROT;
            lv.carrotTotal++;
        }
    }

    if (lv.carrotTotal == 0 && pathCells.size() > 2) {
        lv.grid.at(pathCells[1]) = T_CARROT;
        lv.carrotTotal = 1;
    }

    lv.eggTotal = 0;
    return lv;
}

} // namespace bc