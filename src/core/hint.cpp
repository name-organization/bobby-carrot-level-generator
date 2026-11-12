#include "hint.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <map>
#include <optional>

#include "sat.hpp"
#include "tiles.hpp"

namespace bc::hint {

namespace {

constexpr int kCells = Grid::kCells;

/** @brief Static puzzle cells read from the current grid. */
struct Model {
    std::array<bool, kCells> walk{};
    std::array<bool, kCells> carrot{};
    std::array<bool, kCells> spike{};
    int exit = -1;
    int carrots = 0;

    /** @brief Enumerate neighbours in fixed U/D/L/R order for deterministic path ties. */
    int neighbours(int c, std::array<int, 4>& out) const {
        const int x = c % Grid::kStride, y = c / Grid::kStride;
        const int nx[4] = {x, x, x - 1, x + 1};
        const int ny[4] = {y - 1, y + 1, y, y};
        int n = 0;
        for (int k = 0; k < 4; ++k) {
            if (!Grid::inStorage(nx[k], ny[k])) continue;
            const int i = ny[k] * Grid::kStride + nx[k];
            if (walk[i]) out[n++] = i;
        }
        return n;
    }
};

/** @brief Player position, collected carrots and collapsed spikes during path replay. */
struct State {
    int pos = 0;
    std::bitset<kCells> got;
    std::bitset<kCells> dead;
    int gotCount = 0;
};

bool isBaseTile(std::uint8_t t) {
    return t < T_FIRST_WALKABLE || t == T_GROUND || t == T_CARROT ||
           t == T_CARROT_TAKEN || t == T_START || t == T_CRACKED || t == T_HOLE ||
           t == T_EXIT;
}

Model buildModel(const Grid& grid) {
    Model m;
    for (int i = 0; i < kCells; ++i) {
        const std::uint8_t t = grid.cells[i];
        m.walk[i] = t >= T_FIRST_WALKABLE && t != T_HOLE;
        m.carrot[i] = t == T_CARROT;
        m.spike[i] = t == T_CRACKED;
        if (t == T_CARROT) ++m.carrots;
        if (t == T_EXIT) m.exit = i;
    }
    return m;
}

bool isWon(const Model& m, const State& s) {
    return s.pos == m.exit && s.gotCount == m.carrots;
}

bool adjacent(int a, int b) {
    const int dx = a % Grid::kStride - b % Grid::kStride, dy = a / Grid::kStride - b / Grid::kStride;
    return (dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1));
}

/** @brief Apply one move; return false without changing state for illegal moves. */
bool step(const Model& m, State& s, int to) {
    if (to < 0 || to >= kCells || !m.walk[to] || s.dead[to] || !adjacent(s.pos, to))
        return false;
    if (m.spike[s.pos]) s.dead[s.pos] = true;
    if (m.carrot[to] && !s.got[to]) {
        s.got[to] = true;
        ++s.gotCount;
    }
    s.pos = to;
    return true;
}

/**
 * @brief Validate a candidate path and trim it to the first winning state.
 *
 * @return Winning prefix, or nullopt for an illegal or non-winning path.
 */
std::optional<std::vector<int>> trimToWin(const Model& m, State s,
                                          const std::vector<int>& path) {
    if (isWon(m, s)) return std::vector<int>{};
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!step(m, s, path[i])) return std::nullopt;
        if (isWon(m, s)) return std::vector<int>(path.begin(), path.begin() + i + 1);
    }
    return std::nullopt;
}

/** @brief Compute BFS distances; unreachable cells retain -1. */
std::array<int, kCells> distances(const Model& m, int src) {
    std::array<int, kCells> dist;
    dist.fill(-1);
    std::array<int, kCells> queue{};
    int head = 0, tail = 0;
    dist[src] = 0;
    queue[tail++] = src;
    std::array<int, 4> nb{};
    while (head < tail) {
        const int c = queue[head++];
        const int n = m.neighbours(c, nb);
        for (int k = 0; k < n; ++k) {
            if (dist[nb[k]] != -1) continue;
            dist[nb[k]] = dist[c] + 1;
            queue[tail++] = nb[k];
        }
    }
    return dist;
}

/**
 * @brief Rejoin the reference without crossing a live spike on the approach.
 *
 * @details A live spike may be the final approach cell, but cannot be crossed.
 * Validate the entire joined path before returning it.
 */
std::optional<std::vector<int>> backToReference(const Model& m, const State& s,
                                                const std::vector<int>& reference) {
    std::array<int, kCells> parent;
    parent.fill(-2);                       // -2 unseen, -1 root
    std::array<int, kCells> queue{};
    int head = 0, tail = 0;
    parent[s.pos] = -1;
    queue[tail++] = s.pos;
    std::array<int, 4> nb{};
    while (head < tail) {
        const int c = queue[head++];
        if (m.spike[c] && c != s.pos) continue;
        const int n = m.neighbours(c, nb);
        for (int k = 0; k < n; ++k) {
            if (parent[nb[k]] != -2) continue;
            parent[nb[k]] = c;
            queue[tail++] = nb[k];
        }
    }

    std::optional<std::vector<int>> best;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        int cell = reference[i];
        if (cell < 0 || cell >= kCells || parent[cell] == -2) continue;
        std::vector<int> candidate;
        for (; cell != s.pos; cell = parent[cell]) candidate.push_back(cell);
        std::reverse(candidate.begin(), candidate.end());
        candidate.insert(candidate.end(), reference.begin() + i + 1, reference.end());
        auto trimmed = trimToWin(m, s, candidate);
        if (trimmed && (!best || trimmed->size() < best->size())) best = std::move(trimmed);
    }
    return best;
}

/**
 * @brief Collapse safe connected cells into reusable regions and spikes into single-use nodes.
 *
 * @details A winning abstract walk visits every carrot region and reaches the exit
 * without repeating a spike. It has at most 2 * spikes + 1 nodes, giving a complete
 * SAT horizon. Expansion collects every carrot in each visited region.
 */
struct Abstraction {
    const Model& m;
    std::array<int, kCells> nodeOf{};          // cell -> node, -1 for walls
    int regions = 0;                           // nodes [0, regions) are regions
    std::vector<int> spikeCell;                // node - regions -> grid index
    std::vector<std::vector<int>> adj;         // node -> neighbouring nodes
    std::vector<int> carrotRegions;            // regions holding a carrot

    explicit Abstraction(const Model& model) : m(model) {
        nodeOf.fill(-1);
        std::array<int, 4> nb{};
        std::vector<int> stack;
        for (int i = 0; i < kCells; ++i) {
            if (!m.walk[i] || m.spike[i] || nodeOf[i] != -1) continue;
            bool hasCarrot = false;
            nodeOf[i] = regions;
            stack.push_back(i);
            while (!stack.empty()) {
                const int c = stack.back();
                stack.pop_back();
                hasCarrot = hasCarrot || m.carrot[c];
                const int n = m.neighbours(c, nb);
                for (int k = 0; k < n; ++k) {
                    if (m.spike[nb[k]] || nodeOf[nb[k]] != -1) continue;
                    nodeOf[nb[k]] = regions;
                    stack.push_back(nb[k]);
                }
            }
            if (hasCarrot) carrotRegions.push_back(regions);
            ++regions;
        }
        for (int i = 0; i < kCells; ++i) {
            if (!m.spike[i]) continue;
            nodeOf[i] = regions + static_cast<int>(spikeCell.size());
            spikeCell.push_back(i);
        }

        adj.resize(nodeCount());
        for (int cell : spikeCell) {
            const int n = m.neighbours(cell, nb);
            for (int k = 0; k < n; ++k) {
                const int a = nodeOf[cell], b = nodeOf[nb[k]];
                if (std::find(adj[a].begin(), adj[a].end(), b) != adj[a].end()) continue;
                adj[a].push_back(b);
                if (!isSpike(b)) adj[b].push_back(a);   // spike-spike is added from both ends
            }
        }
    }

    int nodeCount() const { return regions + static_cast<int>(spikeCell.size()); }
    bool isSpike(int node) const { return node >= regions; }

    /** @brief Solve for an abstract walk, suppressing consecutive repeats in the result. */
    sat::Status solve(int startNode, long long& budget, std::vector<int>& walk) const {
        using sat::neg;
        using sat::pos;
        const int nodes = nodeCount();
        const int spikes = static_cast<int>(spikeCell.size());
        const int needed = static_cast<int>(carrotRegions.size());
        const int horizon = 2 * spikes + 1;
        int bits = 0;
        while ((1 << bits) < nodes) ++bits;
        const int frame = nodes + spikes + needed + bits;

        auto at = [&](int t, int node) { return t * frame + node; };
        auto used = [&](int t, int k) { return t * frame + nodes + k; };
        auto seen = [&](int t, int j) { return t * frame + nodes + spikes + j; };
        auto bit = [&](int t, int b) { return t * frame + nodes + spikes + needed + b; };

        sat::Solver s;
        for (int i = 0; i < (horizon + 1) * frame; ++i) s.newVar();

        s.addClause(pos(at(0, startNode)));
        for (int k = 0; k < spikes; ++k) s.addClause(neg(used(0, k)));
        for (int j = 0; j < needed; ++j)
            s.addClause(carrotRegions[j] == startNode ? pos(seen(0, j)) : neg(seen(0, j)));

        for (int t = 0; t <= horizon; ++t) {
            // Each selected node fixes a binary ID, enforcing at most one node per frame.
            std::vector<sat::Lit> any;
            for (int n = 0; n < nodes; ++n) {
                any.push_back(pos(at(t, n)));
                for (int b = 0; b < bits; ++b)
                    s.addClause(neg(at(t, n)), ((n >> b) & 1) ? pos(bit(t, b)) : neg(bit(t, b)));
            }
            s.addClause(any);
            if (t == horizon) break;

            for (int n = 0; n < nodes; ++n) {
                // Regions may wait; spikes must be left and cannot be visited twice.
                std::vector<sat::Lit> next{neg(at(t, n))};
                for (int to : adj[n]) next.push_back(pos(at(t + 1, to)));
                if (!isSpike(n)) next.push_back(pos(at(t + 1, n)));
                s.addClause(next);
            }
            for (int k = 0; k < spikes; ++k) {
                const int n = regions + k;
                // used(t + 1) <=> used(t) OR at(t, spike); forbid entry to a used spike.
                s.addClause(neg(at(t + 1, n)), neg(used(t, k)));
                s.addClause(neg(used(t, k)), pos(used(t + 1, k)));
                s.addClause(neg(at(t, n)), pos(used(t + 1, k)));
                s.addClause(neg(used(t + 1, k)), pos(used(t, k)), pos(at(t, n)));
            }
            for (int j = 0; j < needed; ++j) {
                const int n = carrotRegions[j];
                // seen(t + 1) <=> seen(t) OR at(t + 1, carrot region).
                s.addClause(neg(seen(t, j)), pos(seen(t + 1, j)));
                s.addClause(neg(at(t + 1, n)), pos(seen(t + 1, j)));
                s.addClause(neg(seen(t + 1, j)), pos(seen(t, j)), pos(at(t + 1, n)));
            }
        }

        s.addClause(pos(at(horizon, nodeOf[m.exit])));
        for (int j = 0; j < needed; ++j) s.addClause(pos(seen(horizon, j)));

        const sat::Status status = s.solve(budget);
        if (status == sat::Status::Sat) {
            walk.clear();
            for (int t = 0; t <= horizon; ++t)
                for (int n = 0; n < nodes; ++n)
                    if (s.value(at(t, n))) {
                        if (walk.empty() || walk.back() != n) walk.push_back(n);
                        break;
                    }
        }
        return status;
    }

    /**
     * @brief Append a shortest path through a region to the nearest accepted goal.
     *
     * @param from Start cell, possibly a spike adjacent to the region.
     * @return Reached cell, or -1 when no goal is reachable.
     */
    template <class Goal>
    int walkTo(int from, int region, Goal goal, std::vector<int>& path) const {
        if (nodeOf[from] == region && goal(from)) return from;
        std::array<int, kCells> parent;
        parent.fill(-2);
        std::array<int, kCells> queue{};
        int head = 0, tail = 0;
        parent[from] = -1;
        queue[tail++] = from;
        std::array<int, 4> nb{};
        while (head < tail) {
            const int c = queue[head++];
            const int n = m.neighbours(c, nb);
            for (int k = 0; k < n; ++k) {
                const int to = nb[k];
                if (nodeOf[to] != region || parent[to] != -2) continue;
                parent[to] = c;
                if (goal(to)) {
                    std::vector<int> steps;
                    for (int x = to; x != from; x = parent[x]) steps.push_back(x);
                    path.insert(path.end(), steps.rbegin(), steps.rend());
                    return to;
                }
                queue[tail++] = to;
            }
        }
        return -1;
    }

    /** @brief Expand an abstract walk into cells, collecting region carrots nearest-first. */
    bool expand(int startCell, const std::vector<int>& walk, std::vector<int>& path) const {
        path.clear();
        int cur = startCell;
        std::size_t marked = 0;
        std::bitset<kCells> taken;
        auto mark = [&] {
            for (; marked < path.size(); ++marked) taken[path[marked]] = true;
        };

        for (std::size_t i = 0; i < walk.size(); ++i) {
            const int node = walk[i];
            const bool last = i + 1 == walk.size();
            if (isSpike(node)) {
                if (last) return false;
                if (isSpike(walk[i + 1])) path.push_back(cur = spikeCell[walk[i + 1] - regions]);
                continue;
            }
            while (true) {
                const int got = walkTo(cur, node,
                                       [&](int c) { return m.carrot[c] && !taken[c]; }, path);
                if (got == -1) break;
                cur = got;
                taken[cur] = true;
                mark();
            }
            if (last) {
                cur = walkTo(cur, node, [&](int c) { return c == m.exit; }, path);
            } else {
                const int spike = spikeCell[walk[i + 1] - regions];
                cur = walkTo(cur, node, [&](int c) { return adjacent(c, spike); }, path);
                if (cur != -1) path.push_back(cur = spike);
            }
            if (cur == -1) return false;
            mark();
        }
        return true;
    }
};

/**
 * @brief Rebuild a winning path with as few live spikes as the same carrot order allows.
 *
 * @details Neither stage avoids spikes: the generator's reference keeps any spike its
 * solution crosses once, and the solver accepts any region walk. The path is cut at its
 * milestones (each carrot taken for the first time, and the end); each piece is rebuilt
 * between the same two cells with the fewest spikes, then the fewest moves, using only
 * the live spikes the old piece used. Pieces never take a spike from each other, so the
 * result is as legal as the input, keeps the carrot order and adds no spike; it is longer
 * only when it has fewer spikes.
 *
 * @param m Static puzzle.
 * @param start State the path starts from.
 * @param path Legal winning path as cells after start.pos.
 * @return The rebuilt path, or @p path unchanged if any piece cannot be rebuilt.
 */
std::vector<int> spareSpikes(const Model& m, const State& start, const std::vector<int>& path) {
    // the milestones: indices into `path`
    std::vector<std::size_t> milestones;
    {
        State s = start;
        for (std::size_t i = 0; i < path.size(); ++i) {
            const bool fresh = m.carrot[path[i]] && !s.got[path[i]];
            if (!step(m, s, path[i])) return path;
            if (fresh || i + 1 == path.size()) milestones.push_back(i);
        }
    }

    State s = start;
    std::vector<int> out;
    std::size_t from = 0;  // first index of the current piece
    std::bitset<kCells> allowed;
    std::array<int, kCells> cost, parent;
    std::array<int, 4> nb{};
    // cost of a route: spikes x (kCells + 1) + moves, so fewer spikes always win
    constexpr int kSpikeCost = kCells + 1;
    for (std::size_t end : milestones) {
        const int a = s.pos, b = path[end];
        allowed.reset();
        for (std::size_t i = from; i <= end; ++i)
            if (m.spike[path[i]] && !s.dead[path[i]]) allowed[path[i]] = true;

        // Dijkstra over the costs; std::map keeps the order the same everywhere
        cost.fill(-1);
        parent.fill(-1);
        std::map<int, std::vector<int>> open;
        cost[a] = 0;
        open[0].push_back(a);
        // until nothing left in the queue is cheaper than the way to b found so far
        while (!open.empty() && (cost[b] == -1 || open.begin()->first < cost[b])) {
            auto first = open.begin();
            const int c0 = first->first;
            std::vector<int> cells = std::move(first->second);
            open.erase(first);
            for (int c : cells) {
                if (cost[c] != c0) continue;  // reached cheaper since
                const int n = m.neighbours(c, nb);
                for (int k = 0; k < n; ++k) {
                    const int to = nb[k];
                    if (s.dead[to] || (m.spike[to] && !allowed[to])) continue;
                    const int c1 = c0 + 1 + (m.spike[to] ? kSpikeCost : 0);
                    if (cost[to] != -1 && cost[to] <= c1) continue;
                    cost[to] = c1;
                    parent[to] = c;
                    open[c1].push_back(to);
                }
            }
        }
        if (cost[b] == -1) return path;  // cannot happen: the old piece is a route

        std::vector<int> piece;
        for (int c = b; c != a; c = parent[c]) piece.push_back(c);
        for (auto it = piece.rbegin(); it != piece.rend(); ++it) {
            if (!step(m, s, *it)) return path;
            out.push_back(*it);
        }
        from = end + 1;
    }
    return out;
}

/** @brief Count live spike cells on a path. */
int spikesOn(const Model& m, const std::vector<int>& path) {
    int n = 0;
    for (int c : path) n += m.spike[c];
    return n;
}

/**
 * @brief Return the path to hand out: spike-spared when enabled and still winning.
 *
 * @details Falls back to @p path if the rebuilt one has more spikes or no longer wins.
 */
std::vector<int> finish(const Model& m, const State& s, std::vector<int> path,
                        const Options& options) {
    if (!options.spareSpikes) return path;
    std::vector<int> spared = spareSpikes(m, s, path);
    if (spikesOn(m, spared) > spikesOn(m, path)) return path;
    auto won = trimToWin(m, s, spared);
    return won ? std::move(*won) : path;
}

Dir direction(int from, int to) {
    if (to == from - Grid::kStride) return Dir::Up;
    if (to == from + Grid::kStride) return Dir::Down;
    if (to == from - 1) return Dir::Left;
    return Dir::Right;
}

Result winnable(Stage stage, int pos, const std::vector<int>& path) {
    Result r{Verdict::Winnable, stage, {}};
    for (int c : path) {
        r.moves.push_back(direction(pos, c));
        pos = c;
    }
    return r;
}

}  // namespace

bool supported(const Grid& grid) {
    int exits = 0;
    for (std::uint8_t t : grid.cells) {
        if (!isBaseTile(t)) return false;
        if (t == T_EXIT) ++exits;
    }
    return exits == 1;
}

std::vector<Coord> cellsAlong(Coord start, const std::vector<Dir>& moves) {
    std::vector<Coord> out{start};
    for (Dir d : moves) {
        start.x += dirDX(d);
        start.y += dirDY(d);
        out.push_back(start);
    }
    return out;
}

Result compute(const Grid& grid, Coord pos, const std::vector<Coord>& reference,
               const Options& options) {
    if (!supported(grid) || !grid.inBounds(pos.x, pos.y)) return {};
    const Model m = buildModel(grid);
    State state;
    state.pos = pos.index();
    if (!m.walk[state.pos]) return {};

    if (isWon(m, state)) return {Verdict::Winnable, Stage::Won, {}};

    const auto fromPos = distances(m, state.pos);
    bool reachable = fromPos[m.exit] != -1;
    for (int i = 0; i < kCells && reachable; ++i)
        if (m.carrot[i] && fromPos[i] == -1) reachable = false;
    if (!reachable) return {Verdict::DeadEnd, Stage::Flood, {}};

    std::vector<int> refCells;
    for (Coord c : reference)
        refCells.push_back(grid.inBounds(c.x, c.y) ? c.index() : -1);
    if (auto path = backToReference(m, state, refCells))
        return winnable(Stage::Reference, state.pos, finish(m, state, std::move(*path), options));
    if (!options.useSolver) return {Verdict::Unknown, Stage::Reference, {}};

    const Abstraction abstraction(m);
    long long budget = options.conflictBudget;
    std::vector<int> walk, path;
    const sat::Status status = abstraction.solve(abstraction.nodeOf[state.pos], budget, walk);
    if (status == sat::Status::Unsat) return {Verdict::DeadEnd, Stage::Solver, {}};
    if (status == sat::Status::Unknown || !abstraction.expand(state.pos, walk, path))
        return {Verdict::Unknown, Stage::Solver, {}};
    auto trimmed = trimToWin(m, state, path);
    if (!trimmed) return {Verdict::Unknown, Stage::Solver, {}};
    return winnable(Stage::Solver, state.pos, finish(m, state, std::move(*trimmed), options));
}

const char* stageName(Stage stage) {
    switch (stage) {
        case Stage::Won:         return "won";
        case Stage::Flood:       return "flood";
        case Stage::Reference:   return "reference";
        case Stage::Solver:      return "solver";
        case Stage::Unsupported: return "unsupported";
    }
    return "unsupported";
}

const char* verdictName(Verdict verdict) {
    switch (verdict) {
        case Verdict::Winnable: return "winnable";
        case Verdict::DeadEnd:  return "dead_end";
        case Verdict::Unknown:  return "unknown";
    }
    return "unknown";
}

}  // namespace bc::hint
