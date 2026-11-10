#pragma once

#include "level.hpp"
#include "tiles.hpp"
#include <random>

namespace bc {

class LevelGenerator {
public:
    // Генерирует новый случайный проходимый уровень
    static Level generateRandom(unsigned int seed = 0, int pathLength = 200);
};

} // namespace bc