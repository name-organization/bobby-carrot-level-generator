#pragma once

#include <string>
#include <vector>

#include "hint.hpp"
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
    bool loadProcedural(unsigned int seed = 0);
    bool loadLevel(Level level);
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

    // --- hint ---------------------------------------------------------------
    // Reference solution of the loaded level, as moves from the start. The
    // level generator will ship one with every level; without it the first
    // hint finds a path with the solver and keeps it as the reference.
    void setReference(const std::vector<Dir>& moves);

    // Asks for a hint. Answered on the next tick at which Bobby stands still,
    // so the grid is never read halfway through a step.
    void requestHint() { hintRequested_ = true; }

    // The H key: shows a hint, or hides the one on screen (a pending request
    // counts as shown). Hiding keeps the remembered route, so the next hint
    // is still cheap.
    void toggleHint();
    void hideHint();

    // The last answer, or nullptr if there is none for the current position.
    // Moving along the suggested path shortens it. Moving off it re-routes
    // back to the remembered path without the solver; if that route is
    // broken, the hint disappears until the next requestHint().
    // Its `moves` always start at Bobby's current cell, in step with hintPath().
    const hint::Result* hint() const { return hintShown_ ? &hint_ : nullptr; }
    // Cells still ahead on the suggested path, the next one first.
    const std::vector<Coord>& hintPath() const { return hintPath_; }

private:
    void answerHint(bool rejoinOnly);
    void followHint();

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

    std::vector<Coord> levelReference_;   // from the level start, may be empty
    std::vector<Coord> reference_;        // what the next hint builds on
    hint::Result hint_;
    std::vector<Coord> hintPath_;
    Coord hintCell_{};                    // where Bobby was when hintPath_ was valid
    bool hintShown_ = false;
    bool hintRequested_ = false;
};

}  // namespace bc
