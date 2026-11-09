#pragma once

#include <string>

#include "level.hpp"
#include "player.hpp"
#include "rules.hpp"

namespace bc {

// One level session: the mutable grid, Bobby, the collected totals and the
// clock. Knows nothing about rendering, audio or the platform.
class Game {
public:
    explicit Game(std::string assetRoot = "assets/v1") : root_(std::move(assetRoot)) {}

    bool load(Mode mode, int index);
    void restart();

    // Queue input for the next tick. `held` is a continuously pressed
    // direction, `tap` a discrete press that is remembered until Bobby is
    // free to act on it.
    void setHeld(Dir d) { held_ = d; }
    void queueTap(Dir d) { tap_ = d; }

    void update();

    // --- queries ------------------------------------------------------------
    const Grid& grid() const        { return grid_; }
    const Player& player() const    { return player_; }
    const Level& level() const      { return level_; }
    Mode mode() const               { return mode_; }
    int levelIndex() const          { return index_; }
    const Progress& progress() const{ return progress_; }

    // True once every carrot (or every patch of grass) has been taken, which
    // is what opens the exit.
    bool allCollected() const;
    int remaining() const;
    bool usesCarrots() const        { return level_.carrotTotal > 0; }

    bool levelComplete() const      { return player_.fadeOutFinished(); }
    bool playerDead() const         { return player_.deathFinished(); }

    int elapsedTicks() const        { return elapsedTicks_; }
    int elapsedSeconds() const      { return elapsedTicks_ / kTicksPerSecond; }
    int moveCount() const           { return moveCount_; }

    // Events raised during the last tick, as ev:: bit flags.
    unsigned lastEvents() const     { return lastEvents_; }

private:
    std::string root_;
    Level level_;
    Grid grid_;
    Player player_;
    Progress progress_;
    Mode mode_ = Mode::Normal;
    int index_ = 0;
    int elapsedTicks_ = 0;
    int moveCount_ = 0;
    unsigned lastEvents_ = ev::None;
    Dir held_ = Dir::None;
    Dir tap_  = Dir::None;
    bool wasMoving_ = false;
};

}  // namespace bc
