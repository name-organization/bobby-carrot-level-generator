#include "difficulty.hpp"

#include <algorithm>

#include "tiles.hpp"

namespace bc::difficulty {
namespace {

// Where a measure starts counting (easy) and where it is all in (hard), in the
// units of Rating. Per map size, from the medium levels of that size: about
// their mean minus and plus a spread, so that a typical medium level scores
// near 50 on every size (measured over 100 medium levels per size).
struct Anchor {
    int easy, hard;
};
struct Anchors {
    Anchor cover, spikes, naive;
};
constexpr Anchors kSmall{{31, 101}, {514, 1114}, {0, 80}};    // 16 x 16
constexpr Anchors kMiddle{{45, 105}, {486, 986}, {20, 100}};  // 24 x 24
constexpr Anchors kLarge{{63, 113}, {415, 655}, {35, 115}};   // 32 x 32
constexpr int kCoverWeight = 1, kSpikesWeight = 1, kNaiveWeight = 2;

// The same size classes as roomgen::Params::forSize.
const Anchors& anchorsFor(int width, int height) {
    const int side = std::max(width, height);
    return side <= 18 ? kSmall : (side <= 26 ? kMiddle : kLarge);
}

// 0..1000 between the anchors.
int scale(int value, Anchor a) {
    if (a.hard == a.easy) return 0;
    const long long v = 1000LL * (value - a.easy) / (a.hard - a.easy);
    return static_cast<int>(std::clamp<long long>(v, 0, 1000));
}

}  // namespace

const char* name(Difficulty d) {
    switch (d) {
        case Difficulty::Easy: return "easy";
        case Difficulty::Medium: return "medium";
        case Difficulty::Hard: return "hard";
    }
    return "?";
}

bool parse(std::string_view text, Difficulty& out) {
    for (Difficulty d : {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard}) {
        if (text == name(d)) {
            out = d;
            return true;
        }
    }
    return false;
}

Difficulty band(int score) {
    if (score >= kHardFrom) return Difficulty::Hard;
    if (score >= kMediumFrom) return Difficulty::Medium;
    return Difficulty::Easy;
}

int distanceToBand(int score, Difficulty d) {
    const int lo = d == Difficulty::Easy ? 0 : (d == Difficulty::Medium ? kMediumFrom : kHardFrom);
    const int hi = d == Difficulty::Easy ? kMediumFrom - 1
                                         : (d == Difficulty::Medium ? kHardFrom - 1 : 100);
    return score < lo ? lo - score : (score > hi ? score - hi : 0);
}

bool naiveWins(const Level& level, Rng& rng) {
    Grid g = level.grid;
    Coord pos = level.start;
    int carrots = level.carrotTotal;
    auto open = [&](Coord c) {
        return g.inBounds(c.x, c.y) && g.at(c) >= T_FIRST_WALKABLE && g.at(c) != T_HOLE;
    };
    std::vector<Dir> dirs{Dir::Up, Dir::Down, Dir::Left, Dir::Right};
    std::vector<int> dist(Grid::kCells);
    std::vector<Coord> first(Grid::kCells);
    std::vector<Coord> queue;
    for (int step = 0; step < 4 * g.w * g.h; ++step) {
        if (carrots == 0 && pos == level.exit) return true;
        rng.shuffle(dirs);
        // breadth-first from Bobby: the first step towards each cell
        std::fill(dist.begin(), dist.end(), -1);
        queue.assign(1, pos);
        dist[pos.index()] = 0;
        Coord target{-1, -1};
        for (std::size_t head = 0; head < queue.size() && target.x < 0; ++head) {
            const Coord c = queue[head];
            for (Dir d : dirs) {
                const Coord n{c.x + dirDX(d), c.y + dirDY(d)};
                if (!open(n) || dist[n.index()] != -1) continue;
                dist[n.index()] = dist[c.index()] + 1;
                first[n.index()] = c == pos ? n : first[c.index()];
                queue.push_back(n);
                const bool wanted = carrots > 0 ? g.at(n) == T_CARROT : n == level.exit;
                if (wanted) {
                    target = n;
                    break;
                }
            }
        }
        if (target.x < 0) return false;  // cut off
        Coord next = first[target.index()];
        if (rng.chance(0.2)) {
            for (Dir d : dirs) {
                const Coord n{pos.x + dirDX(d), pos.y + dirDY(d)};
                if (open(n)) {
                    next = n;
                    break;
                }
            }
        }
        if (g.at(pos) == T_CRACKED) g.at(pos) = T_HOLE;  // a spike fires once left
        pos = next;
        if (g.at(pos) == T_CARROT) {
            g.at(pos) = T_CARROT_TAKEN;
            --carrots;
        }
    }
    return false;
}

int naiveLossPercent(const Level& level, std::uint32_t seed) {
    Rng rng(seed * 7919u + 1u);
    int lost = 0;
    for (int i = 0; i < kNaiveTries; ++i) lost += !naiveWins(level, rng);
    return lost * 100 / kNaiveTries;
}

Rating rate(const Level& level, const std::vector<Dir>& moves, std::uint32_t seed) {
    const Grid& g = level.grid;
    int area = 0, spikes = 0;
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            const std::uint8_t t = g.at(x, y);
            if (t < T_FIRST_WALKABLE) continue;
            ++area;
            spikes += t == T_CRACKED;
        }
    }
    Rating r;
    if (area == 0) return r;
    r.cover = static_cast<int>(100LL * static_cast<long long>(moves.size()) / area);
    r.spikes = spikes * 10000 / area;
    r.naive = naiveLossPercent(level, seed);

    const Anchors& a = anchorsFor(g.w, g.h);
    const long long sum = static_cast<long long>(scale(r.cover, a.cover)) * kCoverWeight +
                          static_cast<long long>(scale(r.spikes, a.spikes)) * kSpikesWeight +
                          static_cast<long long>(scale(r.naive, a.naive)) * kNaiveWeight;
    const int weights = kCoverWeight + kSpikesWeight + kNaiveWeight;
    // 0..1000 per measure, so the mean over 10 is 0..100, rounded to nearest
    r.score = static_cast<int>((sum + 5LL * weights) / (10LL * weights));
    return r;
}

std::uint32_t trySeed(std::uint32_t seed, int k) {
    if (k == 0) return seed;
    // a different, well-mixed seed per try; never 0, which means "random"
    std::uint32_t s = seed + 0x9E3779B9u * static_cast<std::uint32_t>(k);
    s ^= s >> 16;
    s *= 0x85EBCA6Bu;
    s ^= s >> 13;
    s *= 0xC2B2AE35u;
    s ^= s >> 16;
    return s == 0 ? 1 : s;
}

}  // namespace bc::difficulty
