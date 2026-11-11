#include "generator.hpp"
#include <vector>
#include <algorithm>
#include <queue>

namespace bc {

Level LevelGenerator::generateRandom(unsigned int seed, int pathLength) {
    Level lv;
    std::mt19937 rng(seed == 0 ? std::random_device{}() : seed);
    std::uniform_int_distribution<int> distDir(0, 3); // четыре направления
    std::uniform_int_distribution<int> distChance(0, 100);

    const int dx[4] = {-1, 1, 0, 0};
    const int dy[4] = {0, 0, -1, 1};

    // Ограничиваемся циклом на случай, если с первого раза сгенерируется слишком короткий путь (< 4 уникальных клеток)
    while (true) {
        lv.grid.cells.fill(T_DEFAULT_GRASS); 

        // 1. Случайная стартовая позиция с отступом от границ
        std::uniform_int_distribution<int> distCoordX(1, Grid::kClassic - 2);
        std::uniform_int_distribution<int> distCoordY(1, Grid::kClassic - 2);

        Coord start{distCoordX(rng), distCoordY(rng)};
        lv.start = start;

        Coord current = start;

        std::array<int, Grid::kCells> visitCounts{};
        std::vector<Coord> successPath = {};
        
        visitCounts[start.index()]++;
        successPath.push_back(start);
        lv.grid.at(start) = T_GROUND;

        int minX = start.x, maxX = start.x;
        int minY = start.y, maxY = start.y;

        // 2. Блуждание с моментумом для извилистости
        int lastDir = -1;
        bool pathFailed = false; // Флаг для отслеживания проблем
        for (int step = 0; step < pathLength; ++step) {
            int d = distDir(rng);
            if (lastDir != -1 && distChance(rng) < 40) {
                d = lastDir;
            }
            
            Coord next{current.x + dx[d], current.y + dy[d]};

            int safety = 0;
            while ((next.x < 1 || next.x >= Grid::kClassic - 1 || next.y < 1 || next.y >= Grid::kClassic - 1) && safety < 4) {
                d = distDir(rng);
                next.x = current.x + dx[d];
                next.y = current.y + dy[d];
                safety++;
            }
            // если вышли за лимит попыток
            if (safety >= 4) {
                pathFailed = true;
                break; // Выходим из цикла for
            }

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

        if (pathFailed) {
            continue; 
        }

        // ПРОВЕРКА 1: Финиш не должен совпадать со стартом, и уникальных ячеек должно быть >= 4
        int uniqueCellsCount = 0;
        for (int i = 0; i < Grid::kCells; ++i) {
            if (visitCounts[i] > 0) uniqueCellsCount++;
        }

        Coord exitCoord = current;
        if (exitCoord == lv.start || uniqueCellsCount < 4) {
            continue; // Перезапускаем генерацию итерации, если условия не сошлись
        }

        lv.exit = exitCoord;
        lv.grid.at(exitCoord) = T_EXIT;
        lv.grid.at(lv.start) = T_START;

        // подсчёт соседей один раз для всех клеток
        std::array<int, Grid::kCells> pathNeighborsMap{};
        for (const Coord& c : successPath) {
            int n_count = 0;
            for (int i = 0; i < 4; ++i) {
                Coord n{c.x + dx[i], c.y + dy[i]};
                if (n.x >= 1 && n.x < Grid::kClassic - 1 && 
                    n.y >= 1 && n.y < Grid::kClassic - 1 && 
                    visitCounts[n.index()] > 0) {
                    n_count++;
                }
            }
            pathNeighborsMap[c.index()] = n_count;
        }

        // Гарантия шипов: Ищем все валидные точки для шипов
        std::vector<Coord> spikeCandidates;
        for (const Coord& c : successPath) {
            if (c == lv.start || c == lv.exit) continue;
            if (visitCounts[c.index()] == 1) {
                // Шипы не в тупиках
                if (pathNeighborsMap[c.index()] > 1) {
                    spikeCandidates.push_back(c);
                }
            }
        }

        // Если на пути вообще нет мест для шипов (редко, но бывает для коротких путей) — перезапуск
        if (spikeCandidates.empty()) {
            continue;
        }

        // Гарантированно ставим хотя бы ОДИН шип в случайном подходящем месте
        std::uniform_int_distribution<size_t> distCandidate(0, spikeCandidates.size() - 1);
        Coord forcedSpike = spikeCandidates[distCandidate(rng)];
        lv.grid.at(forcedSpike) = T_CRACKED;

        // 4. Заполнение внутреннего пространства и расстановка шипов (сохранена ваша проверка соседей)
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
                    // Внутри прямоугольника пути создаем плотные стены/заборы вместо хаоса
                    if (x >= minX && x <= maxX && y >= minY && y <= maxY) {
                        // Создаем сплошные массы заборов с редкими внутренними просветами
                        // Это заставит путь огибать препятствия интересной формы
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

        // 4. Шаг Б (Автотайлинг заборов): Проходим по всем заборам и настраиваем их стыковку по соседям
        for (int y = 0; y < Grid::kClassic; ++y) {
            for (int x = 0; x < Grid::kClassic; ++x) {
                Coord c{x, y};
                std::uint8_t tile = lv.grid.at(c);
                
                // Нас интересуют только клетки-заборы
                if (tile >= T_FIRST_WALKABLE || tile == T_DEFAULT_GRASS || tile == T_SINGLE_GRASS) {
                    continue; 
                }

                // Функция для проверки, является ли клетка частью забора
                auto isFence = [&](int nx, int ny) {
                    if (nx < 0 || nx >= Grid::kClassic || ny < 0 || ny >= Grid::kClassic) return false;
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

                bool hasDiagUpLeft  = isFence(x - 1, y - 1);
                bool hasDiagUpRight = isFence(x + 1, y - 1);

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
            if (lv.grid.at(c) != T_GROUND) continue; 

            // Если это тупик (1 сосед) или одиночный проход с вероятностью
            if (pathNeighborsMap[c.index()] <= 1 || (visitCounts[c.index()] == 1 && distChance(rng) < 70)) {
                lv.grid.at(c) = T_CARROT;
                lv.carrotTotal++;
            }
        }

        if (lv.carrotTotal == 0 && successPath.size() > 2) {
            lv.grid.at(successPath[1]) = T_CARROT;
            lv.carrotTotal = 1;
        }

        lv.eggTotal = 0;
        return lv; // Уровень полностью удовлетворяет всем условиям, возвращаем его
    }
}

} // namespace bc