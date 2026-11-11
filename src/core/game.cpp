#include "game.hpp"
#include "canvas.hpp"
#include "generator.hpp"
#include "roomgen.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

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

const wfc::Model* Game::wfcModel() {
    if (!wfcLearned_) {
        wfcLearned_ = true;
        std::vector<Canvas> samples;
        for (int i = 0; i < Level::levelCount(Mode::Normal); ++i) {
            Level original;
            if (original.loadFromFile(Level::pathFor(Mode::Normal, i, root_)))
                samples.push_back(Canvas::fromLevel(original));
        }
        wfcModel_ = wfc::learn(samples);
        std::printf("[gen] WFC: %zu patterns from %zu levels\n", wfcModel_.patterns.size(),
                    samples.size());
    }
    return wfcModel_.empty() ? nullptr : &wfcModel_;
}

bool Game::loadProcedural(unsigned int seed, int width, int height) {
    if (seed == 0) seed = std::random_device{}();
    roomgen::Params params = roomgen::Params::forSize(width, height);
    params.model = wfcModel();
    const auto started = std::chrono::steady_clock::now();
    const roomgen::Result gen = roomgen::generate(params, seed);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started).count();
    Level next;
    if (!gen.canvas.toLevel(next, "generated " + std::to_string(seed))) {
        // the room generator gave nothing playable: fall back to the old one
        std::fprintf(stderr, "[gen] seed %u: room generator failed, using the random walk\n", seed);
        loadLevel(LevelGenerator::generateRandom(seed));
        return true;
    }
    std::printf("[gen] seed %u, %dx%d: %zu rooms (%d by WFC), %s walls, %s after %d attempt(s), "
                "%zu moves, %.1f ms\n",
                seed, width, height, gen.rooms.size(), gen.wfcRooms, roomgen::wallsName(gen.walls),
                gen.verified ? "verified" : "NOT verified", gen.attempts, gen.solution.size(), ms);
    loadLevel(next, gen.solution);
    return true;
}

void Game::loadLevel(const Level& level, const std::vector<Dir>& reference) {
    level_ = level;
    mode_ = Mode::Normal;
    index_ = -1;  // not one of the shipped levels
    levelReference_ = reference.empty() ? std::vector<Coord>{}
                                        : hint::cellsAlong(level_.start, reference);
    restart();
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

// `rejoinOnly`: Bobby stepped off a shown path. Only the cheap stages run, so
// he is led back to the remembered route at no cost; the solver is left for
// an explicit hint request.
void Game::answerHint(bool rejoinOnly) {
    hintRequested_ = false;
    const Coord pos = player_.cell();

    hint::Options options;
    options.useSolver = !rejoinOnly;
    const auto started = std::chrono::steady_clock::now();
    hint::Result result = hint::compute(grid_, pos, reference_, options);
    if (rejoinOnly && result.verdict == hint::Verdict::Unknown) {
        // The remembered route is broken -- typically Bobby stepped on a spike
        // the route still needed, or the way back to it crosses one. Ask the
        // solver now instead of hiding the hint: it finds a new route or says
        // the level is lost (the board turns red). The budget is kept small so
        // that a step never stalls; past it the hint hides as before.
        hint::Options reroute;
        reroute.conflictBudget = kRerouteConflicts;
        result = hint::compute(grid_, pos, reference_, reroute);
    }
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started).count();

    std::printf("hint%s: %s via %s, %zu moves, %.2f ms\n", rejoinOnly ? " (off path)" : "",
                hint::verdictName(result.verdict), hint::stageName(result.stage),
                result.moves.size(), ms);

    if (rejoinOnly && result.verdict == hint::Verdict::Unknown) {
        // not even the solver could tell in time: hide the hint until H is pressed
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
