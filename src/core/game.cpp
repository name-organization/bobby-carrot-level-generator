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
    rememberSeed(0, 0, 0);
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

bool Game::loadProcedural(unsigned int seed, int width, int height, Difficulty difficulty,
                          bool rated) {
    // 0 stands for "random", so a drawn 0 is drawn again
    std::random_device device;
    while (seed == 0) seed = device();
    if (!rated) difficulty = Difficulty::Medium;  // the only one there was
    roomgen::Params params = roomgen::Params::forSize(width, height, difficulty);
    params.model = wfcModel();
    const auto started = std::chrono::steady_clock::now();
    const roomgen::Result gen =
        roomgen::generateRated(params, seed, difficulty, rated ? difficulty::kTries : 1, rated);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started).count();
    Level next;
    if (!gen.canvas.toLevel(next, "generated " + std::to_string(seed))) {
        // the room generator gave nothing playable: fall back to the old one
        std::fprintf(stderr, "[gen] seed %u: room generator failed, using the random walk\n", seed);
        loadLevel(LevelGenerator::generateRandom(seed));
        rememberSeed(seed, width, height, difficulty, rated);
        return true;
    }
    std::printf("[gen] seed %u, %dx%d, %s: %zu rooms (%d by WFC), %s walls, %s after %d attempt(s), "
                "%zu moves, score %d (%s; cover %d, spikes %d.%02d per 100 cells, naive leaves "
                "%d.%d%%), %d of %d tries, tuned %+d spikes, %.1f ms\n",
                seed, width, height, rated ? difficulty::name(difficulty) : "medium, unrated",
                gen.rooms.size(), gen.wfcRooms, roomgen::wallsName(gen.walls),
                gen.verified ? "verified" : "NOT verified", gen.attempts, gen.solution.size(),
                gen.rating.score, difficulty::name(difficulty::band(gen.rating.score)),
                gen.rating.cover, gen.rating.spikes / 100, gen.rating.spikes % 100,
                gen.rating.naive / 10, gen.rating.naive % 10, gen.tries, rated ? difficulty::kTries : 1, gen.tuned, ms);
    loadLevel(next, gen.solution);
    rememberSeed(seed, width, height, difficulty, rated);
    genRating_ = gen.rating;
    return true;
}

void Game::rememberSeed(unsigned int seed, int width, int height, Difficulty difficulty,
                        bool rated) {
    seed_ = seed;
    genWidth_ = width;
    genHeight_ = height;
    genDifficulty_ = difficulty;
    genRated_ = rated;
    genRating_ = difficulty::Rating{};
}

bool Game::loadLevel(const Level& level, const std::vector<Dir>& reference) {
    level_ = level;
    mode_ = Mode::Normal;
    index_ = -1;  // not one of the shipped levels
    rememberSeed(0, 0, 0);  // loadProcedural() sets it after this
    // a reference left over from the previous level must not reach this one
    levelReference_ = reference.empty() ? std::vector<Coord>{}
                                        : hint::cellsAlong(level_.start, reference);
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
    findCarrotBlocks();
    pendingBlock_ = -1;
}

// Groups of 2+ carrots next to each other (up, down, left, right), as the
// level starts. Collected carrots stay in their block.
void Game::findCarrotBlocks() {
    carrotBlock_.assign(Grid::kCells, -1);
    int blocks = 0;
    std::vector<Coord> stack, cells;
    for (int y = 0; y < grid_.h; ++y)
        for (int x = 0; x < grid_.w; ++x) {
            const Coord c{x, y};
            if (grid_.at(c) != T_CARROT || carrotBlock_[c.index()] != -1) continue;
            cells.clear();
            stack.assign(1, c);
            carrotBlock_[c.index()] = blocks;
            while (!stack.empty()) {
                const Coord cur = stack.back();
                stack.pop_back();
                cells.push_back(cur);
                for (Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
                    const Coord n{cur.x + dirDX(d), cur.y + dirDY(d)};
                    if (!grid_.inBounds(n.x, n.y) || grid_.at(n) != T_CARROT ||
                        carrotBlock_[n.index()] != -1)
                        continue;
                    carrotBlock_[n.index()] = blocks;
                    stack.push_back(n);
                }
            }
            if (cells.size() < 2) carrotBlock_[c.index()] = -1;   // a lone carrot
            else ++blocks;
        }
}

bool Game::blockDone(int block) const {
    for (int i = 0; i < Grid::kCells; ++i)
        if (carrotBlock_[i] == block && grid_.cells[i] == T_CARROT) return false;
    return true;
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
    pendingBlock_ = -1;
}

// `rejoinOnly`: Bobby stepped off a shown path. He is led back to the
// remembered route at no cost; the solver runs only when that route is broken
// (e.g. he stood on a spike the route still needs).
// `tookCarrot`: carrots were taken ahead of the route (`block`, or the one
// Bobby stands on if -1), so the route is rebuilt not to come back to the
// carrots already taken.
void Game::answerHint(bool rejoinOnly, bool tookCarrot, int block) {
    hintRequested_ = false;
    if (!rejoinOnly) pendingBlock_ = -1;
    const Coord pos = player_.cell();

    hint::Options options;
    options.useSolver = !rejoinOnly;
    const auto started = std::chrono::steady_clock::now();
    hint::Result result;
    if (tookCarrot) result = routeAroundCarrots(pos, block);
    if (result.verdict != hint::Verdict::Winnable)
        result = hint::compute(grid_, pos, reference_, options);
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
//
// A carrot taken off the path rebuilds the route around the taken carrots. In
// a block of carrots that waits until the block is collected or Bobby leaves
// it, so the route does not jump on every carrot; until then only the cheap
// way back to the route runs.
void Game::followHint() {
    if (!hintShown_ || player_.isMoving()) return;
    const Coord pos = player_.cell();
    if (pos == hintCell_) return;
    const bool offPath = hintPath_.empty() || !(hintPath_.front() == pos);
    const int block = carrotBlock_[pos.index()];
    int around = pendingBlock_;
    bool rebuild = pendingBlock_ >= 0 && (block != pendingBlock_ || blockDone(pendingBlock_));
    if (offPath && (lastEvents_ & ev::Carrot)) {
        around = block;
        rebuild = block < 0 || blockDone(block);
        if (!rebuild) pendingBlock_ = block;
    }
    if (rebuild) {
        pendingBlock_ = -1;
        if (hint_.verdict == hint::Verdict::Winnable) {
            answerHint(true, true, around);
            return;
        }
    }
    if (!offPath) {
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

// A route that never comes back to a taken carrot: around all of them if it
// can, else around those of `block` (or just the one Bobby stands on, if -1).
// Taken carrots become walls, and Bobby's own cell, if one, a spike, so he may
// leave it but not return. Any route found there is also legal on the real
// grid. Same small budget as a re-route, so a step never stalls.
hint::Result Game::routeAroundCarrots(Coord pos, int block) const {
    Grid all = grid_, last = grid_;
    for (int i = 0; i < Grid::kCells; ++i) {
        if (grid_.cells[i] != T_CARROT_TAKEN) continue;
        all.cells[i] = T_DEFAULT_GRASS;
        if (i == pos.index() || (block >= 0 && carrotBlock_[i] == block))
            last.cells[i] = T_DEFAULT_GRASS;
    }
    if (grid_.at(pos) == T_CARROT_TAKEN) all.at(pos) = last.at(pos) = T_CRACKED;
    hint::Options options;
    options.conflictBudget = kRerouteConflicts;
    for (const Grid* g : {&all, &last}) {
        const hint::Result r = hint::compute(*g, pos, {}, options);
        if (r.verdict == hint::Verdict::Winnable) return r;
    }
    return {};
}

}  // namespace bc
