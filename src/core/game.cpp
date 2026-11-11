#include "game.hpp"
#include "generator.hpp"

#include <algorithm>
#include <chrono>
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
    levelReference_.clear();
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

bool Game::loadLevel(Level level) {
    level_ = std::move(level);
    mode_ = Mode::Normal;
    index_ = -1;
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

    reference_ = levelReference_;
    hint_ = hint::Result{};
    hintPath_.clear();
    hintShown_ = false;
    hintRequested_ = false;
}

void Game::setReference(const std::vector<Dir>& moves) {
    levelReference_ = hint::cellsAlong(level_.start, moves);
    reference_ = levelReference_;
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

    // Bobby stays in Walk for a few seconds after he stops (the idle animation
    // only starts later), so "standing still" is !isMoving() in either state.
    const Player::State now = player_.state();
    const bool inPlay = now == Player::State::Idle || now == Player::State::Walk;
    followHint();
    if (hintRequested_ && inPlay && !player_.isMoving()) answerHint(false);
}

void Game::toggleHint() {
    if (hintShown_ || hintRequested_) hideHint();
    else requestHint();
}

void Game::hideHint() {
    hintShown_ = false;
    hintRequested_ = false;
    hintPath_.clear();
}

// `rejoinOnly`: Bobby stepped off a shown path. He is led back to the
// remembered route at no cost; the solver runs only when that route is broken
// (e.g. he stood on a spike the route still needs).
void Game::answerHint(bool rejoinOnly) {
    hintRequested_ = false;
    const Coord pos = player_.cell();

    const auto started = std::chrono::steady_clock::now();
    const hint::Result result = hint::compute(grid_, pos, reference_);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started).count();

    std::printf("hint%s: %s via %s, %zu moves, %.2f ms\n", rejoinOnly ? " (off path)" : "",
                hint::verdictName(result.verdict), hint::stageName(result.stage),
                result.moves.size(), ms);

    if (rejoinOnly && result.verdict == hint::Verdict::Unknown) {
        // the solver ran out of budget: hide the hint until H is pressed
        hintShown_ = false;
        hintPath_.clear();
        return;
    }

    hint_ = result;
    hintPath_ = hint::cellsAlong(pos, hint_.moves);
    hintCell_ = pos;
    hintShown_ = true;
    if (hint_.verdict == hint::Verdict::Winnable && !hint_.moves.empty()) {
        reference_ = hintPath_;
        if (levelReference_.empty() && pos == level_.start && moveCount_ == 0)
            levelReference_ = hintPath_;
    }
    hintPath_.erase(hintPath_.begin());    // drop Bobby's own cell
}

// Keeps the shown hint in step with Bobby: a step along the path shortens it,
// a step off it leads him back to the remembered route.
void Game::followHint() {
    if (!hintShown_ || player_.isMoving()) return;
    const Coord pos = player_.cell();
    if (pos == hintCell_) return;
    if (!hintPath_.empty() && hintPath_.front() == pos) {
        hintPath_.erase(hintPath_.begin());
        if (!hint_.moves.empty()) hint_.moves.erase(hint_.moves.begin());
        hintCell_ = pos;
        return;
    }
    if (hint_.verdict != hint::Verdict::Winnable) {
        hintShown_ = false;
        hintPath_.clear();
        return;
    }
    answerHint(true);
}

}  // namespace bc
