// Rule verification harness.
//
// The tile semantics in src/core were recovered by reverse engineering, so the
// only convincing proof that they are right is that every original level is
// still solvable under them. This drives the real Game class -- not a
// simplified model -- with a depth-first search that prunes on reachability.
//
// A level that becomes unsolvable is a strong signal that some tile rule is
// wrong: a corner rotating the wrong way, grass counted on entry instead of
// exit, a conveyor boardable from the wrong side, the exit checked against a
// stale pickup count.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "../src/core/game.hpp"

using namespace bc;

namespace {

struct Budget {
    std::size_t nodes = 0;
    std::size_t limit = 3'000'000;
    bool exhausted() const { return nodes >= limit; }
};

enum class Outcome { Settled, Died, Won };

// Push one direction into the simulation and let it run until Bobby comes to
// rest again. Conveyor chains make a single key press last many cells, so the
// search operates on "until he stops" rather than on single ticks.
Outcome advance(Game& g, Dir d) {
    g.setHeld(Dir::None);
    g.queueTap(d);

    int idleStreak = 0;
    for (int tick = 0; tick < 20000; ++tick) {
        g.update();
        if (g.playerDead()) return Outcome::Died;
        if (g.levelComplete()) return Outcome::Won;

        const auto st = g.player().state();
        if (st == Player::State::FadeOut || st == Player::State::Death) {
            idleStreak = 0;
            continue;
        }
        idleStreak = g.player().isMoving() ? 0 : idleStreak + 1;
        if (idleStreak >= 3) return Outcome::Settled;
    }
    return Outcome::Settled;
}

void settle(Game& g) {
    for (int i = 0; i < kFadeTicks + 4; ++i) g.update();
}

std::uint64_t hashState(const Game& g) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::uint8_t b) { h ^= b; h *= 1099511628211ull; };
    for (std::uint8_t c : g.grid().cells) mix(c);
    mix(static_cast<std::uint8_t>(g.player().cell().x));
    mix(static_cast<std::uint8_t>(g.player().cell().y));
    mix(static_cast<std::uint8_t>(g.progress().inv.gray));
    mix(static_cast<std::uint8_t>(g.progress().inv.yellow));
    mix(static_cast<std::uint8_t>(g.progress().inv.red));
    return h;
}

bool isPickup(std::uint8_t t) { return t == T_CARROT || t == T_GRASS; }

// Deliberately permissive flood fill: it ignores one-way corners, rails,
// conveyor direction and locks, so it can only ever over-estimate what is
// reachable. That keeps the pruning sound -- a branch is cut only when a
// pickup is provably stranded, which is exactly what happens once grass has
// been trampled into a wall or a cracked tile has collapsed.
bool everythingStillReachable(const Game& g) {
    const Grid& grid = g.grid();
    std::array<bool, Grid::kCells> seen{};
    std::array<int, Grid::kCells> stack{};
    int sp = 0;

    const int startIdx = g.player().cell().index();
    seen[startIdx] = true;
    stack[sp++] = startIdx;

    auto passable = [&](std::uint8_t t) {
        return !isSolidScenery(t) && t != T_GRASS_FLAT && t != T_HOLE;
    };

    while (sp > 0) {
        const int cur = stack[--sp];
        const int cx = cur % Grid::W, cy = cur / Grid::W;
        const int nx[4] = {cx - 1, cx + 1, cx, cx};
        const int ny[4] = {cy, cy, cy - 1, cy + 1};
        for (int k = 0; k < 4; ++k) {
            if (!Grid::inBounds(nx[k], ny[k])) continue;
            const int ni = ny[k] * Grid::W + nx[k];
            if (seen[ni] || !passable(grid.cells[ni])) continue;
            seen[ni] = true;
            stack[sp++] = ni;
        }
    }

    for (int i = 0; i < Grid::kCells; ++i) {
        const std::uint8_t t = grid.cells[i];
        if ((isPickup(t) || t == T_EXIT) && !seen[i]) return false;
    }
    return true;
}

int nearestPickupDistance(const Game& g, Coord from) {
    int best = 1000;
    const Grid& grid = g.grid();
    for (int i = 0; i < Grid::kCells; ++i) {
        const std::uint8_t t = grid.cells[i];
        if (!isPickup(t) && !(t == T_EXIT && g.remaining() == 0)) continue;
        const int d = std::abs(i % Grid::W - from.x) + std::abs(i / Grid::W - from.y);
        best = std::min(best, d);
    }
    return best;
}

// Two search modes. The first memoises every state ever expanded, which is
// fast but incomplete: a state first reached through a dead end is never
// retried from a better path. The second only guards against cycles along the
// current path, which is slower but finds solutions the first mode misses --
// the grass levels are essentially Hamiltonian path problems and need it.
struct Solver {
    Budget budget;
    bool globalMemo = true;
    std::unordered_set<std::uint64_t> seen;
    std::unordered_set<std::uint64_t> path;
    std::vector<Dir> solution;

    bool dfs(const Game& state, int depth) {
        if (budget.exhausted() || depth > 600) return false;
        ++budget.nodes;

        if (!everythingStillReachable(state)) return false;

        struct Cand {
            Dir dir;
            Game game;
            int score;
        };
        std::vector<Cand> cands;
        cands.reserve(4);

        for (Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
            Game next = state;
            const Outcome o = advance(next, d);
            if (o == Outcome::Died) continue;
            if (o == Outcome::Won) {
                solution.push_back(d);
                return true;
            }
            if (next.player().cell() == state.player().cell() &&
                next.progress().carrotCount == state.progress().carrotCount &&
                next.progress().eggCount == state.progress().eggCount) {
                continue;  // the move was refused, nothing changed
            }
            const std::uint64_t h = hashState(next);
            if (globalMemo) {
                if (!seen.insert(h).second) continue;
            } else {
                if (path.count(h)) continue;
            }

            int score = next.remaining() * 50 +
                        nearestPickupDistance(next, next.player().cell());
            cands.push_back(Cand{d, std::move(next), score});
        }

        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b) { return a.score < b.score; });

        for (auto& c : cands) {
            const std::uint64_t h = hashState(c.game);
            if (!globalMemo) path.insert(h);
            const bool won = dfs(c.game, depth + 1);
            if (!globalMemo) path.erase(h);
            if (won) {
                solution.push_back(c.dir);
                return true;
            }
        }
        return false;
    }
};

struct Result {
    bool solved = false;
    int moves = 0;
    std::size_t nodes = 0;
    double seconds = 0.0;
};

Result solve(Mode mode, int index, const std::string& root, std::size_t limit) {
    Result res;
    const auto t0 = std::chrono::steady_clock::now();

    Game start(root);
    if (!start.load(mode, index)) {
        std::printf("  !! cannot load %s\n", Level::pathFor(mode, index, root).c_str());
        return res;
    }
    settle(start);

    Solver s;
    s.budget.limit = limit;
    s.seen.reserve(1 << 16);
    s.seen.insert(hashState(start));

    res.solved  = s.dfs(start, 0);
    res.moves   = static_cast<int>(s.solution.size());
    res.nodes   = s.budget.nodes;
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

// Replay a found solution through a fresh Game to make sure the search did not
// win through some state the simulation would not actually reach.
bool verifyReplay(Mode mode, int index, const std::string& root,
                  const std::vector<Dir>& movesReversed) {
    Game g(root);
    if (!g.load(mode, index)) return false;
    settle(g);
    for (auto it = movesReversed.rbegin(); it != movesReversed.rend(); ++it) {
        const Outcome o = advance(g, *it);
        if (o == Outcome::Died) return false;
        if (o == Outcome::Won) return std::next(it) == movesReversed.rend();
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : "assets/v1";
    const std::size_t limit = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 3'000'000;

    int solved = 0, total = 0, replayed = 0;
    double worst = 0.0;

    for (Mode mode : {Mode::Normal, Mode::Egg}) {
        const char* label = (mode == Mode::Normal) ? "normal" : "egg";
        std::printf("\n== %s ==\n", label);
        for (int i = 0; i < Level::levelCount(mode); ++i) {
            const auto t0 = std::chrono::steady_clock::now();

            Game start(root);
            if (!start.load(mode, i)) {
                std::printf("  %s%02d  LOAD FAILED\n", label, i + 1);
                ++total;
                continue;
            }
            settle(start);

            Solver s;
            s.budget.limit = limit;
            s.seen.reserve(1 << 16);
            s.seen.insert(hashState(start));
            bool ok = s.dfs(start, 0);
            std::size_t nodes = s.budget.nodes;

            if (!ok) {
                Solver s2;
                s2.globalMemo = false;
                s2.budget.limit = limit;
                s2.path.insert(hashState(start));
                ok = s2.dfs(start, 0);
                nodes += s2.budget.nodes;
                if (ok) s.solution = s2.solution;
            }
            s.budget.nodes = nodes;

            const double secs =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

            ++total;
            bool replayOk = false;
            if (ok) {
                ++solved;
                replayOk = verifyReplay(mode, i, root, s.solution);
                replayed += replayOk ? 1 : 0;
            }
            worst = std::max(worst, secs);

            std::printf("  %s%02d  %-7s moves=%-4zu nodes=%-9zu %5.2fs %s\n", label, i + 1,
                        ok ? "SOLVED" : "FAILED", s.solution.size(), s.budget.nodes, secs,
                        ok ? (replayOk ? "replay ok" : "REPLAY MISMATCH") : "");
            std::fflush(stdout);
        }
    }

    std::printf("\n%d/%d levels solvable, %d/%d replays verified (slowest %.2fs)\n", solved,
                total, replayed, solved, worst);
    return (solved == total && replayed == solved) ? 0 : 1;
}
