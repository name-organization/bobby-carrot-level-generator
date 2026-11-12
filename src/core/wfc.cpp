#include "wfc.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <numeric>
#include <utility>

#include "tiles.hpp"

namespace bc::wfc {

Symbol symbolOf(std::uint8_t tile) {
    if (tile <= 11) return Hedge;
    if (tile <= 17) return Fence;
    if (tile == T_CARROT) return Carrot;
    if (tile == T_CRACKED || tile == T_HOLE) return Spike;
    return Path;  // plain path, start, exit -- and the mechanics the generator does not place yet
}

std::uint8_t tileOf(Symbol s) {
    switch (s) {
        case Hedge: return T_DEFAULT_GRASS;
        case Fence: return T_FENCE_VV;
        case Carrot: return T_CARROT;
        case Spike: return T_CRACKED;
        default: return T_GROUND;
    }
}

namespace {

struct Symbols {
    int w = 0, h = 0;
    std::vector<Symbol> s;
    Symbol at(int x, int y) const { return s[static_cast<std::size_t>(y * w + x)]; }
};

Symbols rotate(const Symbols& g) {
    Symbols r{g.h, g.w, std::vector<Symbol>(g.s.size())};
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) r.s[static_cast<std::size_t>(y * r.w + x)] = g.at(y, g.h - 1 - x);
    return r;
}

Symbols mirror(const Symbols& g) {
    Symbols r{g.w, g.h, std::vector<Symbol>(g.s.size())};
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) r.s[static_cast<std::size_t>(y * r.w + x)] = g.at(g.w - 1 - x, y);
    return r;
}

class Solver {
public:
    Solver(const Model& model, int w, int h, const std::array<int, kSymbols>& percent, Rng& rng,
           int budget)
        : model_(model), w_(w), h_(h), nx_(w - 2), ny_(h - 2), percent_(percent), rng_(rng),
          budget_(budget) {}

    bool run(const std::vector<Mask>& cells, std::vector<Mask>& out) {
        State s;
        s.cells = cells;
        std::vector<int> all(model_.patterns.size());
        std::iota(all.begin(), all.end(), 0);
        s.alive.assign(static_cast<std::size_t>(nx_ * ny_), all);
        std::vector<int> queue(static_cast<std::size_t>(nx_ * ny_));
        std::iota(queue.begin(), queue.end(), 0);
        if (!propagate(s, std::move(queue))) return false;
        return search(s, out);
    }

private:
    struct State {
        std::vector<Mask> cells;
        std::vector<std::vector<int>> alive;  // per window: patterns its cells still allow
    };

    int cellOf(int win, int k) const { return (win / nx_ + k / 3) * w_ + win % nx_ + k % 3; }

    bool fits(const State& s, int win, int p) const {
        const auto& pattern = model_.patterns[static_cast<std::size_t>(p)];
        for (int k = 0; k < 9; ++k)
            if (!(s.cells[static_cast<std::size_t>(cellOf(win, k))] & bit(pattern[k]))) return false;
        return true;
    }

    /** @brief Visit each covering window with the cell offset inside that window. */
    template <class F>
    void forWindowsOf(int cell, F&& fn) const {
        const int cx = cell % w_, cy = cell / w_;
        for (int wy = std::max(0, cy - 2); wy <= std::min(ny_ - 1, cy); ++wy)
            for (int wx = std::max(0, cx - 2); wx <= std::min(nx_ - 1, cx); ++wx)
                fn(wy * nx_ + wx, (cy - wy) * 3 + (cx - wx));
    }

    bool propagate(State& s, std::vector<int> queue) {
        std::vector<char> queued(static_cast<std::size_t>(nx_ * ny_), 0);
        for (int win : queue) queued[static_cast<std::size_t>(win)] = 1;
        while (!queue.empty()) {
            const int win = queue.back();
            queue.pop_back();
            queued[static_cast<std::size_t>(win)] = 0;

            std::vector<int>& alive = s.alive[static_cast<std::size_t>(win)];
            std::size_t keep = 0;
            Mask support[9] = {};
            for (int p : alive) {
                if (!fits(s, win, p)) continue;
                alive[keep++] = p;
                for (int k = 0; k < 9; ++k) support[k] |= bit(model_.patterns[static_cast<std::size_t>(p)][k]);
            }
            alive.resize(keep);
            if (keep == 0) return false;  // no learned window fits here any more

            for (int k = 0; k < 9; ++k) {
                const std::size_t cell = static_cast<std::size_t>(cellOf(win, k));
                const Mask narrowed = s.cells[cell] & support[k];
                if (narrowed == s.cells[cell]) continue;
                s.cells[cell] = narrowed;
                forWindowsOf(static_cast<int>(cell), [&](int other, int) {
                    if (!queued[static_cast<std::size_t>(other)]) {
                        queued[static_cast<std::size_t>(other)] = 1;
                        queue.push_back(other);
                    }
                });
            }
        }
        return true;
    }

    bool search(const State& s, std::vector<Mask>& out) {
        int fewest = kSymbols + 1;
        std::vector<int> ties;
        for (int i = 0; i < w_ * h_; ++i) {
            const int n = std::popcount(s.cells[static_cast<std::size_t>(i)]);
            if (n <= 1) continue;
            if (n < fewest) {
                fewest = n;
                ties.clear();
            }
            if (n == fewest) ties.push_back(i);
        }
        if (ties.empty()) {
            out = s.cells;
            return true;
        }
        const int cell = ties[static_cast<std::size_t>(rng_.range(0, static_cast<int>(ties.size()) - 1))];

        // Prefer centered-window frequencies; summing every covering window overweights shore hedge.
        // Boundary cells have no centered window and use all covering windows instead.
        std::uint64_t weight[kSymbols] = {};
        const int cx = cell % w_, cy = cell / w_;
        const bool centred = cx >= 1 && cx < w_ - 1 && cy >= 1 && cy < h_ - 1;
        if (centred) {
            const int win = (cy - 1) * nx_ + (cx - 1);
            for (int p : s.alive[static_cast<std::size_t>(win)])
                weight[model_.patterns[static_cast<std::size_t>(p)][4]] +=
                    model_.counts[static_cast<std::size_t>(p)];
        } else {
            forWindowsOf(cell, [&](int win, int k) {
                for (int p : s.alive[static_cast<std::size_t>(win)])
                    weight[model_.patterns[static_cast<std::size_t>(p)][k]] +=
                        model_.counts[static_cast<std::size_t>(p)];
            });
        }

        for (int v = 0; v < kSymbols; ++v)
            weight[v] = weight[v] *
                        static_cast<std::uint64_t>(std::max(0, percent_[static_cast<std::size_t>(v)])) / 100;

        Mask left = s.cells[static_cast<std::size_t>(cell)];
        while (left) {
            if (--budget_ < 0) return false;
            std::uint64_t total = 0;
            for (int v = 0; v < kSymbols; ++v)
                if (left & bit(static_cast<Symbol>(v))) total += weight[v];
            int pick = -1;
            if (total > 0) {
                std::uint64_t r = rng_.next64() % total;
                for (int v = 0; v < kSymbols && pick < 0; ++v) {
                    if (!(left & bit(static_cast<Symbol>(v)))) continue;
                    if (r < weight[v]) pick = v;
                    else r -= weight[v];
                }
            }
            if (pick < 0) {
                // Zero-weight fallback tries non-hedge symbols before hedge.
                std::vector<int> options;
                for (int v = 0; v < kSymbols; ++v)
                    if (v != Hedge && (left & bit(static_cast<Symbol>(v)))) options.push_back(v);
                pick = options.empty() ? Hedge
                                       : options[static_cast<std::size_t>(
                                             rng_.range(0, static_cast<int>(options.size()) - 1))];
            }
            left &= static_cast<Mask>(~bit(static_cast<Symbol>(pick)));

            State next = s;
            next.cells[static_cast<std::size_t>(cell)] = bit(static_cast<Symbol>(pick));
            std::vector<int> queue;
            forWindowsOf(cell, [&](int win, int) { queue.push_back(win); });
            if (propagate(next, std::move(queue)) && search(next, out)) return true;
            if (budget_ < 0) return false;
        }
        return false;
    }

    const Model& model_;
    int w_, h_, nx_, ny_;
    std::array<int, kSymbols> percent_;
    Rng& rng_;
    int budget_;
};

}  // namespace

Model learn(const std::vector<Canvas>& samples, bool allOrientations) {
    std::map<std::array<Symbol, 9>, std::uint32_t> counts;  // ordered: same model everywhere
    for (const Canvas& c : samples) {
        Symbols g{c.w, c.h, {}};
        for (std::uint8_t t : c.cells) g.s.push_back(symbolOf(t));
        for (int m = 0; m < (allOrientations ? 2 : 1); ++m) {
            Symbols turned = m ? mirror(g) : g;
            for (int r = 0; r < (allOrientations ? 4 : 1); ++r) {
                for (int y = 0; y + 3 <= turned.h; ++y) {
                    for (int x = 0; x + 3 <= turned.w; ++x) {
                        std::array<Symbol, 9> key{};
                        for (int k = 0; k < 9; ++k) key[static_cast<std::size_t>(k)] = turned.at(x + k % 3, y + k / 3);
                        ++counts[key];
                    }
                }
                turned = rotate(turned);
            }
        }
    }
    Model model;
    for (const auto& [pattern, n] : counts) {
        model.patterns.push_back(pattern);
        model.counts.push_back(n);
    }
    return model;
}

bool Model::has(const std::array<Symbol, 9>& pattern) const {
    return std::binary_search(patterns.begin(), patterns.end(), pattern);
}

int unknownWindows(const Model& model, const Canvas& c, int x, int y) {
    int unknown = 0;
    for (int wy = std::max(0, y - 2); wy <= std::min(c.h - 3, y); ++wy) {
        for (int wx = std::max(0, x - 2); wx <= std::min(c.w - 3, x); ++wx) {
            std::array<Symbol, 9> key{};
            for (int k = 0; k < 9; ++k)
                key[static_cast<std::size_t>(k)] = symbolOf(c.at(wx + k % 3, wy + k / 3));
            if (!model.has(key)) ++unknown;
        }
    }
    return unknown;
}

bool solve(const Model& model, Region& region, Rng& rng, int nodeBudget) {
    if (model.empty() || region.w < 3 || region.h < 3) return false;
    if (region.cells.size() != static_cast<std::size_t>(region.w * region.h)) return false;
    Solver solver(model, region.w, region.h, region.percent, rng, nodeBudget);
    std::vector<Mask> out;
    if (!solver.run(region.cells, out)) return false;
    region.cells = std::move(out);
    return true;
}

}  // namespace bc::wfc
