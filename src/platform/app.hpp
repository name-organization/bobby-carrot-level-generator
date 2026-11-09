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

    // Test hooks used by the screenshot tool.
    void forceLevel(Mode mode, int index);
    void tick() { stepLogic(); }
    void draw() { render(); }

private:
    void handleEvent(const SDL_Event& e);
    void onDirection(Dir d, bool pressed);
    void onConfirm();
    void onBack();
    void stepLogic();
    void render();

    void startLevel(Mode mode, int index);
    void advanceLevel();
    void applyLayout();
    void playEventSounds(unsigned events);
    void rememberProgress();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    gfx::Renderer gfx_;
    audio::Mixer mixer_;
    Game game_;
    SaveData save_;

    std::string assetRoot_ = "assets/v1";
    Screen screen_ = Screen::Title;
    Mode mode_ = Mode::Normal;
    int selected_ = 0;
    int frameTick_ = 0;
    int clearedTimer_ = 0;
    int windowScale_ = 2;
    bool running_ = true;
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
