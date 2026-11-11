// Level generator bench. Generates levels, checks each one with the hint
// solver, plays the solution through the real Game class and compares simple
// metrics with the original levels. No graphics, no SDL.
//
//   gen_bench [options]
//
//   --count N      levels to generate (10)
//   --seed S       seed of the first level; the next ones count up (1)
//   --algo A       rooms | walk -- walk is the old random-walk generator (rooms)
//   --size WxH     map size (16x16); a bigger map is printed but not verified,
//                  the engine plays 16x16 only
//   --room R       room interior, cells per side (4)
//   --walls W      auto | fence | thin | hedge (auto)
//   --fill F  --main F  --spikes F  --merge F  --inset F  --empty F
//                  the chances in roomgen::Params
//   --spike-target N   spikes a level should end up with (roomgen::Params::spikeTarget)
//   --content C    wfc | patterns -- how rooms are filled (wfc)
//   --hedge P      weight of hedge inside rooms for WFC, percent (5)
//   --carrot P     weight of carrots inside rooms for WFC, percent (300)
//   --holdout      WFC learns from the odd originals only and "patterns" is
//                  measured against the even ones, so that the score is not
//                  just WFC reproducing its own training data
//   --assets DIR   asset root with the original levels (assets/v1)
//   --out DIR      also write every 16x16 level to DIR/gen_<seed>.lvl
//   -q             summary only
//
// "patterns" is the share of 3x3 windows of a map that also occur somewhere in
// the original carrot levels: 100% means every local detail has a precedent.
// For the originals it is measured leave-one-out, against the other 29.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../src/core/autotile.hpp"
#include "../src/core/canvas.hpp"
#include "../src/core/game.hpp"
#include "../src/core/generator.hpp"
#include "../src/core/hint.hpp"
#include "../src/core/roomgen.hpp"
#include "../src/core/wfc.hpp"

using namespace bc;

namespace {

struct Options {
    int count = 10;
    std::uint32_t seed = 1;
    std::string algo = "rooms";
    roomgen::Params params;
    std::string assets = "assets/v1";
    std::string out;
    bool quiet = false;
    bool wfc = true;
    bool holdout = false;
};

[[noreturn]] void usage(const char* error) {
    std::fprintf(stderr, "gen_bench: %s\nsee the comment at the top of tests/gen_bench.cpp\n",
                 error);
    std::exit(2);
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage(("missing value for " + a).c_str());
            return argv[++i];
        };
        auto number = [&]() { return std::atof(value().c_str()); };
        if (a == "--count") o.count = std::atoi(value().c_str());
        else if (a == "--seed") o.seed = static_cast<std::uint32_t>(std::strtoul(value().c_str(), nullptr, 10));
        else if (a == "--algo") o.algo = value();
        else if (a == "--size") {
            const std::string v = value();
            if (std::sscanf(v.c_str(), "%dx%d", &o.params.width, &o.params.height) != 2)
                usage("--size wants WxH, e.g. 16x16");
        }
        else if (a == "--room") o.params.roomSize = std::atoi(value().c_str());
        else if (a == "--walls") {
            if (!roomgen::parseWalls(value(), o.params.walls)) usage("--walls: auto|fence|thin|hedge");
        }
        else if (a == "--fill") o.params.fill = number();
        else if (a == "--main") o.params.mainShare = number();
        else if (a == "--spikes") o.params.spikeDoorChance = number();
        else if (a == "--merge") o.params.mergeChance = number();
        else if (a == "--inset") o.params.insetChance = number();
        else if (a == "--empty") o.params.emptyRoomChance = number();
        else if (a == "--hedge") o.params.wfcHedgePercent = std::atoi(value().c_str());
        else if (a == "--carrot") o.params.wfcCarrotPercent = std::atoi(value().c_str());
        else if (a == "--spike-target") o.params.spikeTarget = std::atoi(value().c_str());
        else if (a == "--content") {
            const std::string v = value();
            if (v != "wfc" && v != "patterns") usage("--content: wfc|patterns");
            o.wfc = v == "wfc";
        }
        else if (a == "--holdout") o.holdout = true;
        else if (a == "--assets") o.assets = value();
        else if (a == "--out") o.out = value();
        else if (a == "-q") o.quiet = true;
        else usage(("unknown option " + a).c_str());
    }
    if (o.algo != "rooms" && o.algo != "walk") usage("--algo: rooms|walk");
    if (o.algo == "walk" && (o.params.width != Grid::W || o.params.height != Grid::H))
        usage("the walk generator makes 16x16 maps only");
    return o;
}

Canvas fromLevel(const Level& level) { return Canvas::fromLevel(level); }

// --- similarity with the originals ------------------------------------------

// Mechanics the generator does not place yet count as plain path, so that a
// window is not "new" just for lacking a conveyor.
char normalise(char ch) {
    if (ch != '\0' && std::strchr("#123456789()=\"{}[]*_ ", ch)) return ch;
    return ' ';
}

std::vector<std::string> windows(const Canvas& c) {
    std::vector<std::string> out;
    for (int y = 0; y + 3 <= c.h; ++y) {
        for (int x = 0; x + 3 <= c.w; ++x) {
            std::string key;
            for (int dy = 0; dy < 3; ++dy)
                for (int dx = 0; dx < 3; ++dx) key += normalise(tileToChar(c.at(x + dx, y + dy)));
            if (key != "#########") out.push_back(key);  // solid hedge tells nothing
        }
    }
    return out;
}

struct Corpus {
    std::vector<Canvas> levels;
    std::map<std::string, int> levelsWith;  // window -> how many originals have it
};

// `parity`: -1 every original, 0 the even ones (normal02, 04...), 1 the odd ones.
Corpus loadCorpus(const std::string& root, int parity = -1) {
    Corpus corpus;
    for (int i = 0; i < Level::levelCount(Mode::Normal); ++i) {
        if (parity >= 0 && (i + 1) % 2 != parity) continue;
        Level level;
        if (!level.loadFromFile(Level::pathFor(Mode::Normal, i, root))) continue;
        corpus.levels.push_back(fromLevel(level));
        const std::vector<std::string> w = windows(corpus.levels.back());
        for (const std::string& key : std::set<std::string>(w.begin(), w.end()))
            ++corpus.levelsWith[key];
    }
    return corpus;
}

// `self`: the canvas is one of the originals, so its own windows do not count.
double patternShare(const Canvas& c, const Corpus& corpus, bool self) {
    const std::vector<std::string> w = windows(c);
    if (w.empty() || corpus.levelsWith.empty()) return 0;
    int hits = 0;
    for (const std::string& key : w) {
        const auto it = corpus.levelsWith.find(key);
        if (it != corpus.levelsWith.end() && it->second > (self ? 1 : 0)) ++hits;
    }
    return static_cast<double>(hits) / static_cast<double>(w.size());
}

// --- metrics -----------------------------------------------------------------

struct Metrics {
    double area = 0;     // walkable cells
    double carrots = 0;
    double spikes = 0;
    double islandW = 0;  // bounding box of the walkable cells
    double islandH = 0;
    double patterns = 0;
};

Metrics measure(const Canvas& c, const Corpus& corpus, bool self) {
    Metrics m;
    int x0 = c.w, y0 = c.h, x1 = -1, y1 = -1;
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            const std::uint8_t t = c.at(x, y);
            if (t < T_FIRST_WALKABLE) continue;
            ++m.area;
            if (t == T_CARROT) ++m.carrots;
            if (t == T_CRACKED) ++m.spikes;
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
    }
    m.islandW = x1 >= x0 ? x1 - x0 + 1 : 0;
    m.islandH = y1 >= y0 ? y1 - y0 + 1 : 0;
    m.patterns = patternShare(c, corpus, self);
    return m;
}

struct Stat {
    double sum = 0, lo = 1e18, hi = -1e18;
    int n = 0;
    void add(double v) { sum += v; lo = std::min(lo, v); hi = std::max(hi, v); ++n; }
    std::string show(bool percent = false) const {
        if (n == 0) return "-";
        char buf[64];
        if (percent)
            std::snprintf(buf, sizeof(buf), "%3.0f%% (%.0f-%.0f)", 100 * sum / n, 100 * lo, 100 * hi);
        else
            std::snprintf(buf, sizeof(buf), "%5.1f (%.0f-%.0f)", sum / n, lo, hi);
        return buf;
    }
};

struct Stats {
    Stat area, carrots, spikes, islandW, islandH, patterns;
    void add(const Metrics& m) {
        area.add(m.area); carrots.add(m.carrots); spikes.add(m.spikes);
        islandW.add(m.islandW); islandH.add(m.islandH); patterns.add(m.patterns);
    }
    void print(const char* label) const {
        std::printf("%-12s %-16s %-16s %-14s %-16s %-16s %s\n", label, area.show().c_str(),
                    carrots.show().c_str(), spikes.show().c_str(), islandW.show().c_str(),
                    islandH.show().c_str(), patterns.show(true).c_str());
    }
};

// --- replay through the real Game ---------------------------------------------

// Same as tests/hint_game.cpp: tick until Bobby stands still again.
void settle(Game& g) {
    for (int i = 0; i < 400; ++i) {
        g.update();
        if (g.levelComplete() || g.playerDead()) return;
        const auto st = g.player().state();
        if ((st == Player::State::Idle || st == Player::State::Walk) && !g.player().isMoving())
            return;
    }
}

bool replay(const Level& level, const std::vector<Dir>& moves) {
    Game g;
    g.loadLevel(level, moves);
    settle(g);
    for (Dir d : moves) {
        if (g.levelComplete() || g.playerDead()) break;
        g.queueTap(d);
        g.update();
        settle(g);
    }
    for (int i = 0; i < 300 && !g.levelComplete() && !g.playerDead(); ++i) g.update();
    return g.levelComplete();
}

// --- autotile against the originals --------------------------------------------

// Strips every hedge and fence piece of the originals down to "hedge" or
// "fence" and lets autotile() pick them again.
void checkAutotile(const std::string& root) {
    int cells = 0, same = 0;
    for (Mode mode : {Mode::Normal, Mode::Egg}) {
        for (int i = 0; i < Level::levelCount(mode); ++i) {
            Level level;
            if (!level.loadFromFile(Level::pathFor(mode, i, root))) continue;
            const Canvas original = fromLevel(level);
            Canvas c = original;
            for (std::uint8_t& t : c.cells) {
                if (isHedge(t)) t = T_DEFAULT_GRASS;
                else if (isFence(t)) t = T_FENCE_VV;
            }
            autotile(c);
            for (std::size_t k = 0; k < c.cells.size(); ++k) {
                if (!isHedge(original.cells[k]) && !isFence(original.cells[k])) continue;
                ++cells;
                if (c.cells[k] == original.cells[k]) ++same;
            }
        }
    }
    if (cells)
        std::printf("autotile picks the original piece for %.1f%% of %d hedge and fence cells\n",
                    100.0 * same / cells, cells);
}

}  // namespace

int main(int argc, char** argv) {
    const Options o = parse(argc, argv);
    Options run = o;
    const Corpus corpus = loadCorpus(o.assets, o.holdout ? 0 : -1);
    const Corpus training = o.holdout ? loadCorpus(o.assets, 1) : corpus;
    const wfc::Model model = wfc::learn(training.levels);
    if (o.wfc) run.params.model = &model;
    if (o.wfc && o.algo == "rooms")
        std::printf("WFC: %zu patterns from %zu levels%s\n", model.patterns.size(),
                    training.levels.size(), o.holdout ? " (odd ones; scored against the even ones)" : "");
    if (corpus.levels.empty())
        std::fprintf(stderr, "no original levels under %s: similarity is not measured\n",
                     o.assets.c_str());

    const bool playable = o.params.width == Grid::W && o.params.height == Grid::H;
    Stats generated;
    int verified = 0, won = 0;
    double attempts = 0, totalMs = 0, moves = 0;

    for (int k = 0; k < o.count; ++k) {
        const std::uint32_t seed = o.seed + static_cast<std::uint32_t>(k);
        const auto started = std::chrono::steady_clock::now();

        Canvas canvas;
        std::vector<Dir> solution;
        bool ok = false;
        std::string about;
        if (o.algo == "rooms") {
            const roomgen::Result r = roomgen::generate(run.params, seed);
            canvas = r.canvas;
            solution = r.solution;
            ok = r.verified;
            attempts += r.attempts;
            int path = 0, spikeDoors = 0, merged = 0;
            for (const roomgen::Room& room : r.rooms) {
                if (room.kind != roomgen::RoomKind::Side) ++path;
                if (room.spikeDoor) ++spikeDoors;
                if (room.merged) ++merged;
            }
            char buf[200];
            std::snprintf(buf, sizeof(buf),
                          "%dx%d slots, %zu rooms (%d on the path, %zu side), %s walls, "
                          "%d spike doors, %d merged, attempt %d, rooms by WFC %d/%zu%s",
                          r.slotsX, r.slotsY, r.rooms.size(), path, r.rooms.size() - path,
                          roomgen::wallsName(r.walls), spikeDoors, merged, r.attempts, r.wfcRooms,
                          r.rooms.size(), r.spikesRepaired ? ", spikes repaired" : "");
            about = buf;
        } else {
            const Level level = LevelGenerator::generateRandom(seed);
            canvas = fromLevel(level);
            const hint::Result h = hint::compute(level.grid, level.start, {});
            ok = h.verdict == hint::Verdict::Winnable;
            solution = h.moves;
            attempts += 1;
            about = std::string("random walk, solver: ") + hint::verdictName(h.verdict);
        }
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started).count();
        totalMs += ms;

        Level level;
        const bool asLevel = canvas.toLevel(level, "generated " + std::to_string(seed));
        bool replayed = false;
        if (asLevel && ok) replayed = replay(level, solution);
        verified += ok;
        won += replayed;
        if (ok) moves += static_cast<double>(solution.size());

        const Metrics m = measure(canvas, corpus, false);
        generated.add(m);

        if (!o.out.empty() && asLevel) {
            const std::string path = o.out + "/gen_" + std::to_string(seed) + ".lvl";
            if (!level.saveToFile(path, "generated " + std::to_string(seed) + ", " + about))
                std::fprintf(stderr, "cannot write %s\n", path.c_str());
        }

        if (!o.quiet) {
            const char* verdict = !playable ? "not verified (the engine plays 16x16 only)"
                                  : ok      ? "verified"
                                            : "NOT WINNABLE";
            std::printf("--- seed %u: %s", seed, verdict);
            if (ok) std::printf(", %zu moves, replay %s", solution.size(), replayed ? "won" : "FAILED");
            std::printf(", %.1f ms\n%s\n", ms, about.c_str());
            std::printf("area %.0f  carrots %.0f  spikes %.0f  island %.0fx%.0f  patterns %.0f%%\n",
                        m.area, m.carrots, m.spikes, m.islandW, m.islandH, 100 * m.patterns);
            std::printf("%s\n", canvas.toText().c_str());
        }
    }

    Stats originals;
    for (const Canvas& c : corpus.levels) originals.add(measure(c, corpus, true));

    std::printf("\n%-12s %-16s %-16s %-14s %-16s %-16s %s\n", "", "area", "carrots", "spikes",
                "island w", "island h", "patterns");
    if (!corpus.levels.empty()) originals.print("originals");
    generated.print(o.algo.c_str());
    if (playable)
        std::printf("\nverified %d/%d, replay won %d/%d, %.1f moves, %.2f attempts, %.2f ms per level\n",
                    verified, o.count, won, o.count, verified ? moves / verified : 0.0,
                    o.count ? attempts / o.count : 0.0, o.count ? totalMs / o.count : 0.0);
    else
        std::printf("\n%.2f ms per level, not verified: the engine plays 16x16 only\n",
                    o.count ? totalMs / o.count : 0.0);
    checkAutotile(o.assets);

    return playable && (verified != o.count || won != o.count) ? 1 : 0;
}
