#pragma once

#include <string>
#include <utility>
#include <vector>

#include "difficulty.hpp"
#include "hint.hpp"
#include "level.hpp"
#include "player.hpp"
#include "rules.hpp"
#include "wfc.hpp"

namespace bc {

/** @brief Level simulation, progress and hints, independent of SDL and the platform. */
class Game {
public:
    explicit Game(std::string assetRoot = "assets/v1") : root_(std::move(assetRoot)) {}

    bool load(Mode mode, int index);
    /**
     * @brief Generate and load a room-based level.
     *
     * @param seed Reproducible seed; zero chooses a random nonzero seed.
     * @param width Requested map width, up to Grid::kMaxW.
     * @param height Requested map height, up to Grid::kMaxH.
     * @param difficulty Requested difficulty band.
     * @param rated False selects the first Medium candidate for legacy seed links.
     * @return True after loading, including the random-walk fallback.
     */
    bool loadProcedural(unsigned int seed = 0, int width = Grid::kClassic,
                        int height = Grid::kClassic, Difficulty difficulty = Difficulty::Medium,
                        bool rated = true);
    /**
     * @brief Load a generated or imported level.
     *
     * @param level Level with metadata already computed by rescan().
     * @param reference Optional winning moves from the start.
     * @return True after loading.
     */
    bool loadLevel(const Level& level, const std::vector<Dir>& reference = {});
    void restart();

    /** @brief Set input for the next tick; a tap remains queued until consumed. */
    void setHeld(Dir d) { held_ = d; }
    void queueTap(Dir d) { tap_ = d; }

    void update();

    const Grid& grid() const        { return grid_; }
    const Player& player() const    { return player_; }
    const Level& level() const      { return level_; }
    Mode mode() const               { return mode_; }
    int levelIndex() const          { return index_; }
    /**
     * @brief Generated seed and requested dimensions; seed zero identifies shipped/imported levels.
     *
     * @note A generator fallback can produce different actual dimensions.
     */
    unsigned int seed() const       { return seed_; }
    int generatedWidth() const      { return genWidth_; }
    int generatedHeight() const     { return genHeight_; }
    /** @brief Requested difficulty, selection mode and measured rating. */
    Difficulty generatedDifficulty() const { return genDifficulty_; }
    bool generatedRated() const     { return genRated_; }
    const difficulty::Rating& generatedRating() const { return genRating_; }
    const Progress& progress() const{ return progress_; }

    /** @brief Check whether every collectible required to open the exit is taken. */
    bool allCollected() const;
    int remaining() const;
    bool usesCarrots() const        { return level_.carrotTotal > 0; }

    bool levelComplete() const      { return player_.fadeOutFinished(); }
    bool playerDead() const         { return player_.deathFinished(); }

    int elapsedTicks() const        { return elapsedTicks_; }
    int elapsedSeconds() const      { return elapsedTicks_ / kTicksPerSecond; }
    int moveCount() const           { return moveCount_; }

    /** @brief Events from the last simulation tick, as ev:: flags. */
    unsigned lastEvents() const     { return lastEvents_; }

    /** @brief Remember winning moves from the start, including across restarts. */
    void setReference(const std::vector<Dir>& moves);

    /** @brief Request a hint at the next stationary tick. */
    void requestHint() { hintRequested_ = true; }

    /** @brief Toggle a shown or pending hint while retaining the reference route. */
    void toggleHint();
    void hideHint();

    /**
     * @brief Return the visible hint, or nullptr when hidden.
     *
     * @details Moves start at the current cell. Following the route shortens it;
     * deviating first tries to rejoin the reference, then uses a bounded SAT search.
     * Taking carrots off-route rebuilds the route after their block is finished
     * or left. A timed-out reroute hides the hint until the next request.
     */
    const hint::Result* hint() const { return hintShown_ ? &hint_ : nullptr; }
    /** @brief Cells ahead on the visible hint route, next step first. */
    const std::vector<Coord>& hintPath() const { return hintPath_; }

private:
    static constexpr long long kRerouteConflicts = 50000;

    void rememberSeed(unsigned int seed, int width, int height,
                      Difficulty difficulty = Difficulty::Medium, bool rated = true);
    void answerHint(bool rejoinOnly, bool tookCarrot = false, int block = -1);
    hint::Result routeAroundCarrots(Coord pos, int block) const;
    void findCarrotBlocks();
    bool blockDone(int block) const;
    void followHint();
    /**
     * @brief Lazily learn WFC patterns from original levels.
     *
     * @return Model, or nullptr when no samples are available.
     */
    const wfc::Model* wfcModel();

    std::string root_;
    wfc::Model wfcModel_;
    bool wfcLearned_ = false;
    Level level_;
    Grid grid_;
    Player player_;
    Progress progress_;
    Mode mode_ = Mode::Normal;
    int index_ = 0;
    unsigned int seed_ = 0;
    int genWidth_ = 0;
    int genHeight_ = 0;
    Difficulty genDifficulty_ = Difficulty::Medium;
    bool genRated_ = true;
    difficulty::Rating genRating_;
    int elapsedTicks_ = 0;
    int moveCount_ = 0;
    unsigned lastEvents_ = ev::None;
    Dir held_ = Dir::None;
    Dir tap_  = Dir::None;
    bool wasMoving_ = false;

    std::vector<Coord> levelReference_;   ///< from the level start, may be empty
    std::vector<Coord> reference_;        ///< what the next hint builds on
    hint::Result hint_;
    std::vector<Coord> hintPath_;
    Coord hintCell_{};                    ///< where Bobby was when hintPath_ was valid
    bool hintShown_ = false;
    std::vector<int> carrotBlock_;        ///< cell -> carrot block, -1 if none
    int pendingBlock_ = -1;               ///< block whose carrots wait for a re-route
    bool hintRequested_ = false;
};

}  // namespace bc
