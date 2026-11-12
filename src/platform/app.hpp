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

    // Plays the generated level a shared link names:
    // ?seed=<seed>&size=<side>&difficulty=<easy|medium|hard>. A link without
    // a difficulty is one from before difficulties: `rated` false, and it
    // gives the level it always gave. Returns false, leaving the screen as it
    // is, if the seed or the size is not one a generated level can have.
    bool startFromSeed(unsigned int seed, int size, Difficulty difficulty = Difficulty::Medium,
                       bool rated = true);

    // One display frame: drains events, runs as many fixed logic ticks as the
    // elapsed wall time calls for, then draws once.
    void frame();

    bool running() const { return running_; }
    void quit() { running_ = false; }

    SDL_Renderer* sdlRenderer() { return renderer_; }
    gfx::Renderer& gfx() { return gfx_; }

    // Changing the view also resizes the window and the logical viewport, so
    // it has to go through the app rather than straight to the renderer.
    void setViewMode(gfx::ViewMode m);

    // The web page's Back button covers the top left corner of the board:
    // the timer goes right of it, level with it (logical pixels).
    void setTimerAt(int x, int y) { gfx_.setTimerAt(x, y); }

    // Actions behind the web page's buttons and the G key. A generated level
    // is generatedSize_ cells a side and of generatedDifficulty_: those of the
    // last generated level, or what the New level window last generated
    // with. Seed 0 picks a random one. `rated` false: see startFromSeed().
    void generateLevel(unsigned int seed = 0, bool rated = true);
    // The New level window's Generate: a level of this size and difficulty,
    // which the next ones (G, a finished level) keep. A size that is not one
    // of isGeneratedSize() keeps the current one.
    void generateLevel(unsigned int seed, int size, Difficulty difficulty);
    // The "Generate level" button of the start page: the web page opens the
    // New level window (map size, difficulty, a random seed or a given one),
    // which then calls generateLevel(). Without the page (native builds) it
    // generates a random level at once.
    void askToGenerate();
    // Sides a generated map can have: 16, 24 or 32 cells.
    static bool isGeneratedSize(int size);
    void openLevelSelect();
    // The level list's switch between the carrot and the egg levels (the page's
    // button under the list, or Enter on it).
    void toggleLevelMode();
    void goToMainMenu();
    // Same as pressing a non-direction key, for the clickable key labels.
    void pressKey(SDL_Keycode key);
    // The Settings page: music and sound volume in percent (0..100) and mute.
    // Saved at once. A new sound volume plays a sample at it.
    void setAudio(int musicPercent, int soundPercent, bool muted);
    // While the Settings page is open over a level, the level stands still:
    // no moves, no clock, no "level cleared" countdown.
    void setPaused(bool paused);

    // Test hooks used by the screenshot tool.
    void forceLevel(Mode mode, int index);
    void tick() { stepLogic(); }
    void draw() { render(); }

private:
    void handleEvent(const SDL_Event& e);
    void onDirection(Dir d, bool pressed);
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
    // A generated level is on screen (playing it or its "cleared" banner):
    // where G makes another one and Esc opens the New level window.
    bool onGeneratedLevel() const;
    // Opens the web page's New level window; false without the page.
    bool openNewLevelWindow();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    gfx::Renderer gfx_;
    audio::Mixer mixer_;
    Game game_;
    SaveData save_;

    std::string assetRoot_ = "assets/v1";
    Screen screen_ = Screen::Title;
    int reportedScreen_ = -1;  // last screen told to the web page
    bool reportedGenerated_ = false;
    // Last volumes and mute told to the web page; -1: not told yet.
    int reportedMusic_ = -1;
    int reportedSound_ = -1;
    bool reportedMuted_ = false;
    unsigned int reportedSeed_ = 0;  // seed last put into the page address
    int reportedSize_ = 0;
    int reportedDifficulty_ = -1;  // -1: none in the address
    // What the page shows above the board: kind (0 none, 1 shipped carrot
    // level, 2 shipped egg level, 3 generated), level number, size,
    // difficulty and seed; -1 kind: not told yet.
    int reportedListMode_ = -1;  // 0 no list, 1 carrot levels, 2 egg levels
    bool reportedSwitchSelected_ = false;
    bool reportedBackSelected_ = false;
    // The ending: Up selected the page's Back button.
    bool endBackSelected_ = false;
    int reportedKind_ = -1;
    int reportedNumber_ = 0;
    unsigned int reportedInfoSeed_ = 0;
    int reportedInfoSize_ = 0;
    int reportedInfoDifficulty_ = 0;
    int reportedViewPx_ = 0;  // logical width of the view, for the Back button
    Mode mode_ = Mode::Normal;
    int selected_ = 0;
    int frameTick_ = 0;
    int clearedTimer_ = 0;
    int windowScale_ = 2;
    // Largest window side, in window pixels: the classic board at scale 2.
    static constexpr int kMaxWindowSide = 1024;
    // Side (16, 24, 32 cells) and difficulty of generated maps, chosen in the
    // New level window or by a level link.
    int generatedSize_ = Grid::kClassic;
    Difficulty generatedDifficulty_ = Difficulty::Medium;
    bool running_ = true;
    bool paused_ = false;
    bool headless_ = false;
    bool touchSeen_ = false;
    float touchFade_ = 0.0f;

    Dir held_ = Dir::None;
    Dir pendingTap_ = Dir::None;

    // Swipe tracking.
    bool swiping_ = false;
    float swipeStartX_ = 0.0f;
    float swipeStartY_ = 0.0f;

    double accumulator_ = 0.0;
    Uint64 lastCounter_ = 0;
};

}  // namespace bc::platform
