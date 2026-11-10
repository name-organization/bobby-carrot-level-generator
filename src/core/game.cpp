#include "game.hpp"
#include "generator.hpp"

#include <cstdio>

namespace bc {

bool Game::load(Mode mode, int index) {
    if (index < 0 || index >= Level::levelCount(mode)) return false;

    Level next;
    std::string error;
    if (!next.loadFromFile(Level::pathFor(mode, index, root_), &error)) {
        // A text format can say exactly what is wrong and where, which the
        // binary one never could -- so pass it on instead of a bare false.
        std::fprintf(stderr, "[level] %s\n", error.c_str());
        return false;
    }

    level_ = next;
    mode_  = mode;
    index_ = index;
    restart();
    return true;
}

bool Game::loadProcedural(unsigned int seed) {
    Level next = LevelGenerator::generateRandom(seed);
    level_ = next;
    mode_ = Mode::Normal;
    index_ = -1; // Обозначает пользовательский/процедурный уровень
    restart();
    return true;
}

void Game::restart() {
    grid_     = level_.grid;
    progress_ = Progress{};
    elapsedTicks_ = 0;
    moveCount_ = 0;
    lastEvents_ = ev::None;
    held_ = Dir::None;
    tap_  = Dir::None;
    wasMoving_ = false;
    player_.spawn(level_.start);
}

bool Game::allCollected() const {
    return level_.carrotTotal > 0 ? progress_.carrotCount == level_.carrotTotal
                                  : progress_.eggCount == level_.eggTotal;
}

int Game::remaining() const {
    return level_.carrotTotal > 0 ? level_.carrotTotal - progress_.carrotCount
                                  : level_.eggTotal - progress_.eggCount;
}

void Game::update() {
    // The clock runs while Bobby is actually in play: not during the spawn
    // fade, not while he is dying, not after he has stepped into the exit.
    const Player::State st = player_.state();
    if (st == Player::State::Idle || st == Player::State::Walk) ++elapsedTicks_;

    lastEvents_ = player_.update(grid_, progress_, level_, held_, tap_);

    if (player_.consumedBuffered()) tap_ = Dir::None;

    if (!wasMoving_ && player_.isMoving()) ++moveCount_;
    wasMoving_ = player_.isMoving();
}

}  // namespace bc
