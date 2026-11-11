// Drives the real Game class through the hint API, without graphics:
// for every base-cell original level it asks for a hint at the start, follows
// it move by move to the end and expects the level to be won. It also walks
// level 3 into a known dead end and expects the hint to say so.
//
//   hint_game [ASSET_ROOT]

#include <cstdio>
#include <string>
#include <vector>

#include "../src/core/game.hpp"

using namespace bc;

namespace {

// Runs ticks until Bobby stands still again (or the level ends). Standing
// still is !isMoving() in Idle or Walk: Bobby stays in Walk for seconds after
// he stops, and a hint must not wait for that.
void settle(Game& g) {
    for (int i = 0; i < 400; ++i) {
        g.update();
        if (g.levelComplete() || g.playerDead()) return;
        const auto st = g.player().state();
        if ((st == Player::State::Idle || st == Player::State::Walk) && !g.player().isMoving())
            return;
    }
}

void move(Game& g, Dir d) {
    g.queueTap(d);
    g.update();
    settle(g);
}

int failures = 0;
int slowAnswers = 0;

// A hint asked while Bobby stands still is answered on the very next tick.
const hint::Result* askHint(Game& g) {
    g.requestHint();
    g.update();
    if (!g.hint()) {
        ++slowAnswers;
        settle(g);
        g.update();
    }
    return g.hint();
}

void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : "assets/v1";
    Game g(root);

    for (int index = 0; index < 7; ++index) {
        const std::string name = "normal" + std::to_string(index + 1);
        if (!g.load(Mode::Normal, index)) {
            check(false, name + ": cannot load");
            continue;
        }
        settle(g);
        const hint::Result* h = askHint(g);
        if (!h || h->verdict != hint::Verdict::Winnable) {
            check(false, name + ": no winning hint at the start");
            continue;
        }
        const std::size_t moves = h->moves.size();
        const std::vector<Dir> plan = h->moves;
        bool followed = true;
        for (std::size_t i = 0; i < plan.size() && !g.levelComplete(); ++i) {
            move(g, plan[i]);
            // the shown path shortens as Bobby walks along it
            if (!g.levelComplete() && (!g.hint() || g.hintPath().size() != plan.size() - i - 1 ||
                                       g.hint()->moves.size() != g.hintPath().size()))
                followed = false;
        }
        for (int i = 0; i < 200 && !g.levelComplete(); ++i) g.update();
        check(g.levelComplete() && followed,
              name + ": follow the hint (" + std::to_string(moves) + " moves) to the exit");

        // after a restart the first hint comes from the remembered reference
        g.restart();
        settle(g);
        h = askHint(g);
        check(h && h->stage == hint::Stage::Reference,
              name + ": hint after restart uses the reference");

        // step off the path at every point of it: the hint must re-route on
        // its own (no new request) -- back to the route when it survives, by
        // the solver when it does not -- or say the level is lost
        int rerouted = 0, hidden = 0, broken = 0, lost = 0;
        for (std::size_t k = 0; k < plan.size(); ++k) {
            g.restart();
            settle(g);
            askHint(g);
            for (std::size_t i = 0; i < k; ++i) move(g, plan[i]);
            // off the path the hint shows now, which after a restart may differ
            // from `plan` (the reference stage can find a shorter way back)
            const Coord before = g.player().cell();
            if (!g.hint() || g.hintPath().empty()) continue;
            const Coord next = g.hintPath().front();
            bool moved = false;
            for (Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
                if (before.x + dirDX(d) == next.x && before.y + dirDY(d) == next.y) continue;
                move(g, d);
                if (!(g.player().cell() == before)) { moved = true; break; }
            }
            if (!moved || g.levelComplete() || g.playerDead()) continue;
            const hint::Result* shown = g.hint();
            if (!shown) { ++hidden; continue; }
            if (shown->verdict == hint::Verdict::DeadEnd) { ++lost; continue; }
            if ((shown->stage != hint::Stage::Reference && shown->stage != hint::Stage::Solver) ||
                shown->verdict != hint::Verdict::Winnable) {
                ++broken;
                std::printf("  step %zu: unexpected %s/%s\n", k, hint::stageName(shown->stage),
                            hint::verdictName(shown->verdict));
                continue;
            }
            const std::vector<Dir> back = shown->moves;
            for (Dir d : back) move(g, d);
            for (int i = 0; i < 200 && !g.levelComplete(); ++i) g.update();
            if (g.levelComplete()) {
                ++rerouted;
            } else {
                ++broken;
                std::printf("  step %zu: following %zu re-routed moves did not win (dead=%d, "
                            "left=%d, at %d,%d)\n", k, back.size(), g.playerDead(),
                            g.remaining(), g.player().cell().x, g.player().cell().y);
            }
        }
        check(broken == 0, name + ": off-path steps re-routed " + std::to_string(rerouted) +
                               ", lost " + std::to_string(lost) + ", hidden " +
                               std::to_string(hidden) + ", wrong " + std::to_string(broken));

        // stepping off the path onto a spike used to hide the hint whenever
        // the remembered route needed that spike later; now the hint stays,
        // with a new route or with the dead end
        int spikeSteps = 0, spikeHidden = 0, spikeWrong = 0;
        for (std::size_t k = 0; k < plan.size(); ++k) {
            for (Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
                g.restart();
                settle(g);
                askHint(g);
                for (std::size_t i = 0; i < k; ++i) move(g, plan[i]);
                if (g.levelComplete() || g.playerDead() || !g.hint() || g.hintPath().empty())
                    continue;
                const Coord at = g.player().cell();
                const Coord to{at.x + dirDX(d), at.y + dirDY(d)};
                if (to == g.hintPath().front() || !Grid::inBounds(to.x, to.y) ||
                    g.grid().at(to) != T_CRACKED)
                    continue;
                move(g, d);
                if (!(g.player().cell() == to)) continue;
                ++spikeSteps;
                const hint::Result* shown = g.hint();
                if (!shown) { ++spikeHidden; continue; }
                if (shown->verdict == hint::Verdict::DeadEnd) {
                    // must really be lost: the full solver agrees
                    if (hint::compute(g.grid(), to, {}).verdict != hint::Verdict::DeadEnd)
                        ++spikeWrong;
                    continue;
                }
                const std::vector<Dir> rest = shown->moves;
                for (Dir r : rest) move(g, r);
                for (int i = 0; i < 200 && !g.levelComplete(); ++i) g.update();
                if (!g.levelComplete()) ++spikeWrong;
            }
        }
        check(spikeHidden == 0 && spikeWrong == 0,
              name + ": " + std::to_string(spikeSteps) + " steps onto a spike off the path keep " +
                  "the hint (hidden " + std::to_string(spikeHidden) + ", wrong " +
                  std::to_string(spikeWrong) + ")");
    }

    // level 3: up the spike column, left, up, right twice -- carrots get
    // stranded behind fired spikes
    g.load(Mode::Normal, 2);
    settle(g);
    const hint::Result* h = askHint(g);
    check(h && h->verdict == hint::Verdict::Winnable, "normal3: winnable at the start");
    for (Dir d : {Dir::Up, Dir::Up, Dir::Up, Dir::Up, Dir::Left, Dir::Up, Dir::Right, Dir::Right})
        move(g, d);
    check(g.hint() == nullptr, "normal3: walking off the path clears the hint");
    h = askHint(g);
    check(h && h->verdict == hint::Verdict::DeadEnd, "normal3: dead end detected");

    // H toggles: second press hides, moves keep it hidden, third press shows
    // it again from the remembered route
    g.load(Mode::Normal, 4);
    settle(g);
    g.toggleHint();
    g.update();
    check(g.hint() != nullptr, "toggle: first H shows the hint");
    g.toggleHint();
    check(g.hint() == nullptr && g.hintPath().empty(), "toggle: second H hides it");
    g.update();
    check(g.hint() == nullptr, "toggle: hidden hint is not recomputed");
    const Dir first = g.player().cell().y > 0 ? Dir::Up : Dir::Down;
    move(g, first);
    move(g, first == Dir::Up ? Dir::Down : Dir::Up);
    check(g.hint() == nullptr, "toggle: moving does not bring it back");
    g.toggleHint();
    g.update();
    h = g.hint();
    check(h && h->stage == hint::Stage::Reference && h->verdict == hint::Verdict::Winnable,
          "toggle: third H shows it again via the reference");
    g.toggleHint();
    g.toggleHint();
    g.update();
    check(g.hint() != nullptr, "toggle: hide and show within one tick still answers");

    // levels with conveyors and other extras are not covered
    g.load(Mode::Normal, 7);
    settle(g);
    h = askHint(g);
    check(h && h->stage == hint::Stage::Unsupported, "normal8: reported as unsupported");

    check(slowAnswers == 0, "every hint answered on the next tick (late: " +
                                std::to_string(slowAnswers) + ")");

    std::printf(failures ? "FAILED\n" : "all ok\n");
    return failures ? 1 : 0;
}
