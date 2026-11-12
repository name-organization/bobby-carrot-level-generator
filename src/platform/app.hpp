#pragma once

#include <SDL.h>

#include <string>

#include "../audio/audio.hpp"
#include "../core/game.hpp"
#include "../gfx/renderer.hpp"
#include "storage.hpp"

namespace bc::platform {

class App {
public:
    enum class Screen { Title, Select, Play, Cleared, End };

    bool init(const std::string& assetRoot, bool headless);
    void shutdown();

    /**
     * @brief Start the generated level identified by a shared link.
     *
     * @param seed Nonzero seed.
     * @param size Supported square side: 16, 24 or 32.
     * @param difficulty Requested difficulty band.
     * @param rated False preserves generation for links predating difficulties.
     * @return False for invalid seed or size, leaving the screen unchanged.
     */
    bool startFromSeed(unsigned int seed, int size, Difficulty difficulty = Difficulty::Medium,
                       bool rated = true);

    /** @brief Poll events, run fixed simulation ticks and render one display frame. */
    void frame();

    bool running() const { return running_; }
    void quit() { running_ = false; }

    SDL_Renderer* sdlRenderer() { return renderer_; }
    gfx::Renderer& gfx() { return gfx_; }

    /** @brief Change the view and update window and logical viewport dimensions. */
    void setViewMode(gfx::ViewMode m);

    /** @brief Position the timer beside the page Back button, in logical pixels. */
    void setTimerAt(int x, int y) { gfx_.setTimerAt(x, y); }

    /**
     * @brief Generate using the last selected size and difficulty.
     *
     * @param seed Zero selects a random seed.
     * @param rated False preserves legacy Medium generation.
     */
    void generateLevel(unsigned int seed = 0, bool rated = true);
    /**
     * @brief Generate with new settings retained for subsequent generated levels.
     *
     * @param seed Zero selects a random seed.
     * @param size Supported square side; an unsupported value keeps the current size.
     * @param difficulty Requested band, retained for subsequent levels.
     */
    void generateLevel(unsigned int seed, int size, Difficulty difficulty);
    /** @brief Open the browser generation dialog, or generate immediately in native builds. */
    void askToGenerate();

    static bool isGeneratedSize(int size);
    void openLevelSelect();
    /** @brief Switch the level list between carrot and egg levels. */
    void toggleLevelMode();
    void goToMainMenu();
    /** @brief Dispatch a key press from a clickable browser control. */
    void pressKey(SDL_Keycode key);
    /**
     * @brief Apply and save music/sound volumes and mute.
     *
     * @param musicPercent Music slider value, clamped to 0..100.
     * @param soundPercent Sound slider value, clamped to 0..100; a change plays a sample.
     * @param muted Whether audio is muted.
     */
    void setAudio(int musicPercent, int soundPercent, bool muted);
    /** @brief Pause movement, timer and completion countdown under a browser dialog. */
    void setPaused(bool paused);

    /** @brief Load a shipped level for native screenshot capture. */
    void forceLevel(Mode mode, int index);
    void tick() { stepLogic(); }
    void draw() { render(); }

private:
    void handleEvent(const SDL_Event& e);
    void onDirection(Dir d, bool pressed);
    void tapDirection(Dir d);
    bool tapArrowAt(float logicalX, float logicalY);
    void onConfirm();
    void onBack();
    void onKey(SDL_Keycode key);
    int levelUnderPoint(float logicalX, float logicalY) const;
    void chooseLevel(int index);
    void stepLogic();
    void render();

    void startLevel(Mode mode, int index);
    void advanceLevel();
    void applyLayout();
    void syncBoard();
    void playEventSounds(unsigned events);
    void rememberProgress();
    void reportScreen();
    void applyAudio();
    void reportAudio();
    void reportLevelUrl();
    void reportLevelInfo();
    void reportLevelList();
    /** @brief Check whether a generated level or its completion banner is shown. */
    bool onGeneratedLevel() const;
    /** @brief Open the browser generation dialog; return false in native builds. */
    bool openNewLevelWindow();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    gfx::Renderer gfx_;
    audio::Mixer mixer_;
    Game game_;
    SaveData save_;

    std::string assetRoot_ = "assets/v1";
    Screen screen_ = Screen::Title;
    int reportedScreen_ = -1;  ///< last screen told to the web page
    bool reportedGenerated_ = false;

    int reportedMusic_ = -1;
    int reportedSound_ = -1;
    bool reportedMuted_ = false;
    unsigned int reportedSeed_ = 0;  ///< seed last put into the page address
    int reportedSize_ = 0;
    int reportedDifficulty_ = -1;  ///< -1: none in the address

    int reportedListMode_ = -1;  ///< 0 no list, 1 carrot levels, 2 egg levels
    bool reportedSwitchSelected_ = false;
    bool reportedBackSelected_ = false;

    bool endBackSelected_ = false;
    int reportedKind_ = -1;
    int reportedNumber_ = 0;
    unsigned int reportedInfoSeed_ = 0;
    int reportedInfoSize_ = 0;
    int reportedInfoDifficulty_ = 0;
    int reportedViewPx_ = 0;  ///< logical width of the view, for the Back button
    Mode mode_ = Mode::Normal;
    int selected_ = 0;
    int frameTick_ = 0;
    int clearedTimer_ = 0;
    int windowScale_ = 2;

    static constexpr int kMaxWindowSide = 1024;

    int generatedSize_ = Grid::kClassic;
    Difficulty generatedDifficulty_ = Difficulty::Medium;
    bool running_ = true;
    bool paused_ = false;
    bool headless_ = false;
    bool touchSeen_ = false;
    float touchFade_ = 0.0f;

    Dir held_ = Dir::None;
    Dir pendingTap_ = Dir::None;

    bool swiping_ = false;
    float swipeStartX_ = 0.0f;
    float swipeStartY_ = 0.0f;

    double accumulator_ = 0.0;
    Uint64 lastCounter_ = 0;
};

}  // namespace bc::platform
