#pragma once

#include "level.hpp"

namespace bc {

class LevelGenerator {
public:
    /**
     * @brief Generate a classic-size level with the legacy random-walk algorithm.
     *
     * @param seed Random seed; zero draws one from std::random_device.
     * @param pathLength Number of steps per attempted walk.
     * @return A classic-size map with carrots and at least one spike.
     * @note The default walk length is used by the room generator fallback.
     */
    static Level generateRandom(unsigned int seed = 0, int pathLength = 200);
};

}  // namespace bc
