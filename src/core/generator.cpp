#include "generator.hpp"
#include <vector>
#include <algorithm>
#include <queue>

namespace bc {

Level LevelGenerator::generateRandom(unsigned int seed, int pathLength) {
    Level lv;
    // Изначально заполняем всю карту сплошными препятствиями/стенами
    lv.grid.cells.fill(T_DEFAULT_GRASS); 

    std::mt19937 rng(seed == 0 ? std::random_device{}() : seed);
    std::uniform_int_distribution<int> distDir(0, 3); // четыре направления

    // 1. Случайная стартовая позиция с отступом от границ
    std::uniform_int_distribution<int> distCoordX(1, Grid::W - 2);
    std::uniform_int_distribution<int> distCoordY(1, Grid::H - 2);

    Coord start{distCoordX(rng), distCoordY(rng)};
    lv.start = start;

    Coord current = start;
    // int pathLength = std::uniform_int_distribution<int>(45, 75)(rng); // метрика сложности

    std::array<int, Grid::kCells> visitCounts{};
    std::vector<Coord> successPath = {};
    
    visitCounts[start.index()]++;
    successPath.push_back(start);
    lv.grid.at(start) = T_GROUND;

    const int dx[4] = {-1, 1, 0, 0};
    const int dy[4] = {0, 0, -1, 1};

    int minX = Grid::W, maxX = -1;
    int minY = Grid::H, maxY = -1;

    std::uniform_int_distribution<int> distChance(0, 100);

    // 2. Улучшенное блуждание с сохранением направления (моментумом) для извилистости
    int lastDir = -1;
    for (int step = 0; step < pathLength; ++step) {
        int d = distDir(rng);
        // С вероятностью 40% продолжаем идти в ту же сторону (создает длинные прямые коридоры и повороты, а не кашу)
        if (lastDir != -1 && distChance(rng) < 40) {
            d = lastDir;
        }
        
        Coord next{current.x + dx[d], current.y + dy[d]};

        // Если выходим за безопасные рамки, принудительно ищем валидное направление
        int safety = 0;
        while ((next.x < 1 || next.x >= Grid::W - 1 || next.y < 1 || next.y >= Grid::H - 1) && safety < 4) {
            d = distDir(rng);
            next.x = current.x + dx[d];
            next.y = current.y + dy[d];
            safety++;
        }
        if (safety >= 4) break;

        current = next;
        lastDir = d;

        if (visitCounts[current.index()] == 0) {
            successPath.push_back(current);
        }
        visitCounts[current.index()]++;
        lv.grid.at(current) = T_GROUND; 

        minX = std::min(minX, current.x);
        maxX = std::max(maxX, current.x);
        minY = std::min(minY, current.y);
        maxY = std::max(maxY, current.y);
    }

    // 3. Финиш там, где остановился генератор
    Coord exitCoord = current;
    lv.exit = exitCoord;
    lv.grid.at(exitCoord) = T_EXIT;
    lv.grid.at(lv.start) = T_START;

    // 4. Заполнение внутреннего пространства структурированными блоками заборов
    for (int y = 0; y < Grid::H; ++y) {
        for (int x = 0; x < Grid::W; ++x) {
            Coord c{x, y};
            int visits = visitCounts[c.index()];

            if (visits > 0) {
                // Правила для пути: шипы только на одиночных участках (visits == 1) вдали от старта/финиша
                bool isStartOrExit = (c == lv.start) || (c == lv.exit);
                if (visits == 1 && !isStartOrExit) {
                    int pathNeighbors = 0;
                    for (int i = 0; i < 4; ++i) {
                        Coord n{c.x + dx[i], c.y + dy[i]};
                        if (n.x >= 1 && n.x < Grid::W - 1 && 
                            n.y >= 1 && n.y < Grid::H - 1) {
                            pathNeighbors++;
                        }
                    }
                    if (pathNeighbors > 1 && distChance(rng) < 25) { // Аккуратное количество шипов
                        lv.grid.at(c) = T_CRACKED; 
                    }
                }
            } else {
                // Внутри прямоугольника пути создаем плотные стены/заборы вместо хаоса
                if (x >= minX && x <= maxX && y >= minY && y <= maxY) {
                    // Создаем сплошные массы заборов с редкими внутренними просветами
                    // Это заставит путь огибать препятствия интересной форме
                    if (distChance(rng) < 75) {
                        lv.grid.at(c) = T_FENCE_VV; // Временный базовый забор для автотайлинга
                    } else {
                        lv.grid.at(c) = T_GROUND; // Небольшие внутренние полянки
                    }
                } else {
                    lv.grid.at(c) = T_DEFAULT_GRASS;
                }
            }
        }
    }

    // 4. Шаг Б (Автотайлинг): Проходим по всем заборам и настраиваем их стыковку по соседям
    for (int y = 0; y < Grid::H; ++y) {
        for (int x = 0; x < Grid::W; ++x) {
            Coord c{x, y};
            std::uint8_t tile = lv.grid.at(c);
            
            // Нас интересуют только клетки-заборы
            if (tile >= T_FIRST_WALKABLE || tile == T_DEFAULT_GRASS || tile == T_SINGLE_GRASS) {
                continue; 
            }

            // Функция для проверки, является ли клетка частью забора
            auto isFence = [&](int nx, int ny) {
                if (nx < 0 || nx >= Grid::W || ny < 0 || ny >= Grid::H) return false;
                std::uint8_t neighborTile = lv.grid.at(nx, ny);
                return neighborTile < T_FIRST_WALKABLE && 
                       neighborTile != T_DEFAULT_GRASS && 
                       neighborTile != T_SINGLE_GRASS;
            };

            // Прямые соседи (по 4 сторонам)
            bool hasL = isFence(x - 1, y);
            bool hasR = isFence(x + 1, y);
            bool hasU = isFence(x, y - 1);
            bool hasD = isFence(x, y + 1);

            // Диагональные соседи сверху ("вверх наискосок")
            bool hasDiagUpLeft  = isFence(x - 1, y - 1); // вверх наискосок влево
            bool hasDiagUpRight = isFence(x + 1, y - 1); // вверх наискосок вправо

            // ВАШЕ ПРАВИЛО: если есть забор сверху И при этом хотя бы один забор сверху наискосок
            if (hasU && (hasDiagUpLeft || hasDiagUpRight)) {
                lv.grid.at(c) = T_SINGLE_GRASS;
            } 
            else {
                // Стандартный автотайлинг по прямым соседям
                if (hasL && hasR) {
                    lv.grid.at(c) = T_FENCE_LVR; 
                } else if (hasD && hasR) {
                    lv.grid.at(c) = T_FENCE_VVR; 
                } else if (hasL && hasD) {
                    lv.grid.at(c) = T_FENCE_LVV; 
                } else if (hasR) {
                    lv.grid.at(c) = T_FENCE_VR;  
                } else if (hasL) {
                    lv.grid.at(c) = T_FENCE_LV;  
                } else if (hasU || hasD) {
                    lv.grid.at(c) = T_FENCE_VV;  
                } else {
                    lv.grid.at(c) = T_SINGLE_GRASS; 
                }
            }
        }
    }

    // 5. Расстановка морковок (в тупиках и на одиночных ветках пути для мотивации обойти всё)
    lv.carrotTotal = 0;
    for (const Coord& c : successPath) {
        if (c == lv.start || c == lv.exit) continue;
        if (lv.grid.at(c) != T_GROUND) continue; // Не ставим морковки поверх шипов

        int pathNeighbors = 0;
        for (int i = 0; i < 4; ++i) {
            Coord n{c.x + dx[i], c.y + dy[i]};
            if (n.x >= 1 && n.x < Grid::W - 1 && 
                n.y >= 1 && n.y < Grid::H - 1 && 
                visitCounts[n.index()] > 0) {
                pathNeighbors++;
            }
        }

        // Если это тупик (1 сосед) или одиночный проход с вероятностью
        if (pathNeighbors <= 1 || (visitCounts[c.index()] == 1 && distChance(rng) < 70)) {
            lv.grid.at(c) = T_CARROT;
            lv.carrotTotal++;
        }
    }

    if (lv.carrotTotal == 0 && successPath.size() > 2) {
        lv.grid.at(successPath[1]) = T_CARROT;
        lv.carrotTotal = 1;
    }

    lv.eggTotal = 0;
    return lv;
}

} // namespace bc