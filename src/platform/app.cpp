#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef __EMSCRIPTEN__
    #include <emscripten/val.h>
    #include <emscripten.h>
#endif

namespace bc::platform {
namespace {

constexpr double kStepSeconds = 1.0 / kTicksPerSecond;
constexpr int kClearedFrames = 90;
constexpr float kSwipeThreshold = 0.035f;  // fraction of the window

Dir keyToDir(SDL_Keycode k) {
    switch (k) {
        case SDLK_LEFT:  case SDLK_a: return Dir::Left;
        case SDLK_RIGHT: case SDLK_d: return Dir::Right;
        case SDLK_UP:    case SDLK_w: return Dir::Up;
        case SDLK_DOWN:  case SDLK_s: return Dir::Down;
        default: return Dir::None;
    }
}

bool pointInRect(int x, int y, const SDL_Rect& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// SDL_RenderWindowToLogical only exists from SDL 2.0.18, and the SDL2 port
// bundled with older Emscripten releases predates it. Replicating the
// letterbox maths keeps touch hit-testing working on every toolchain.
void windowToLogical(SDL_Window* win, int logicalW, int logicalH, int winX, int winY,
                     float& outX, float& outY) {
    int ww = 1, wh = 1;
    SDL_GetWindowSize(win, &ww, &wh);
    const float scale = std::min(static_cast<float>(ww) / logicalW,
                                 static_cast<float>(wh) / logicalH);
    const float offX = (ww - logicalW * scale) * 0.5f;
    const float offY = (wh - logicalH * scale) * 0.5f;
    outX = (winX - offX) / scale;
    outY = (winY - offY) / scale;
}

}  // namespace

bool App::init(const std::string& assetRoot, bool headless) {
    assetRoot_ = assetRoot;
    headless_ = headless;
    game_ = Game(assetRoot);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

#ifdef __EMSCRIPTEN__
    windowScale_ = 1;  // the canvas is upscaled by CSS, keeping pixels crisp
#endif

    window_ = SDL_CreateWindow("Bobby Carrot", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               gfx::kFullW * windowScale_, gfx::kFullH * windowScale_,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    Uint32 flags = SDL_RENDERER_ACCELERATED;
    if (headless_) flags = SDL_RENDERER_SOFTWARE;
    renderer_ = SDL_CreateRenderer(window_, -1, flags);
    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    if (!gfx_.init(renderer_, assetRoot_)) {
        std::fprintf(stderr, "failed to load textures from %s/image\n", assetRoot_.c_str());
        return false;
    }
    applyLayout();

    loadProgress(save_);
    if (!headless_) {
        mixer_.init(assetRoot_);
        mixer_.setMuted(save_.muted);
    }

    lastCounter_ = SDL_GetPerformanceCounter();
    return true;
}

void App::shutdown() {
    mixer_.shutdown();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void App::applyLayout() {
    const int w = gfx_.logicalWidth();
    const int h = gfx_.logicalHeight();
    // A big board would make a huge window: past kMaxWindowSide it is scaled
    // down (SDL fits the logical size into it). In the browser the scale is 1
    // and the canvas keeps every pixel; the page's CSS fits it to the screen.
    int ww = w * windowScale_, wh = h * windowScale_;
    const int side = std::max(ww, wh);
    if (side > kMaxWindowSide) {
        ww = ww * kMaxWindowSide / side;
        wh = wh * kMaxWindowSide / side;
    }
    SDL_SetWindowSize(window_, ww, wh);
    SDL_RenderSetLogicalSize(renderer_, w, h);
}

// The board is the level's size while a level is on screen and the classic
// 16x16 on the menus; when that changes, so do the window and the viewport.
void App::syncBoard() {
    const bool level = screen_ == Screen::Play || screen_ == Screen::Cleared;
    const int w = level ? game_.grid().w : Grid::kClassic;
    const int h = level ? game_.grid().h : Grid::kClassic;
    if (w == gfx_.boardW() && h == gfx_.boardH()) return;
    gfx_.setBoard(w, h);
    applyLayout();
}

void App::setViewMode(gfx::ViewMode m) {
    gfx_.setViewMode(m);
    applyLayout();
}

void App::forceLevel(Mode mode, int index) {
    mode_ = mode;
    startLevel(mode, index);
}

void App::startLevel(Mode mode, int index) {
    if (!game_.load(mode, index)) {
        std::fprintf(stderr, "cannot load level %d of %s\n", index + 1,
                     mode == Mode::Normal ? "normal" : "egg");
        return;
    }
    mode_ = mode;
    selected_ = index;
    screen_ = Screen::Play;
    held_ = Dir::None;
    pendingTap_ = Dir::None;
    mixer_.stopMusic();
}

void App::generateLevel(unsigned int seed) {
    if (!game_.loadProcedural(seed, generatedSize_, generatedSize_)) {  // 0 picks a random seed
        std::fprintf(stderr, "cannot generate a level\n");
        return;
    }
    screen_ = Screen::Play;
    held_ = Dir::None;
    pendingTap_ = Dir::None;
    mixer_.stopMusic();
}

void App::askToGenerate() {
#ifdef __EMSCRIPTEN__
    const bool asked = EM_ASM_INT({
        if (!Module.askSeed) return 0;
        Module.askSeed($0);
        return 1;
    }, generatedSize_);
    if (asked) return;
#endif
    generateLevel();
}

bool App::isGeneratedSize(int size) {
    return size == Grid::kClassic || size == Grid::kClassic + 8 || size == Grid::kMaxW;
}

void App::openLevelSelect() {
    screen_ = Screen::Select;
    selected_ = 0;
    held_ = Dir::None;
    pendingTap_ = Dir::None;
}

void App::goToMainMenu() {
    if (screen_ == Screen::Title) return;
    screen_ = Screen::Title;
    held_ = Dir::None;
    pendingTap_ = Dir::None;
    clearedTimer_ = 0;
    mixer_.playMusic("title", 0.5f);
}

// The start page and the footer live in web/shell.html; it shows the menu
// while the game is on the title screen and picks the key hints for the
// screen, so it needs to know the screen and whether the level is generated.
void App::reportScreen() {
    const int current = static_cast<int>(screen_);
    const bool generated = game_.levelIndex() < 0;
    if (current == reportedScreen_ && generated == reportedGenerated_) return;
    reportedScreen_ = current;
    reportedGenerated_ = generated;
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.onScreenChange) Module.onScreenChange($0, !!$1); }, current, generated);
#endif
}

void App::reportLevelUrl() {
    // The seed stays while the generated level is on screen, its "cleared"
    // banner included; the main menu, the level list and the shipped levels
    // have none.
    const bool onLevel = screen_ == Screen::Play || screen_ == Screen::Cleared;
    const bool generated = onLevel && game_.levelIndex() < 0 && game_.seed() != 0;
    const unsigned int seed = generated ? game_.seed() : 0;
    const int size = generated ? game_.generatedWidth() : 0;
    if (seed == reportedSeed_ && size == reportedSize_) return;
    reportedSeed_ = seed;
    reportedSize_ = size;
#ifdef __EMSCRIPTEN__
    // replaceState, not pushState: a new level is no page to go back to
    EM_ASM({
        var u = new URL(window.location.href);
        u.searchParams.delete('level');  // links from before seeds
        if ($0 >>> 0) {
            u.searchParams.set('seed', String($0 >>> 0));
            u.searchParams.set('size', String($1));
        } else {
            u.searchParams.delete('seed');
            u.searchParams.delete('size');
        }
        if (u.href !== window.location.href) window.history.replaceState(null, "", u.href);
    }, seed, size);
#endif
}

void App::rememberProgress() {
    const int slot = (mode_ == Mode::Normal ? 0 : 30) + game_.levelIndex();
    if (slot >= 0 && slot < 50) {
        const int secs = game_.elapsedSeconds();
        if (save_.bestSeconds[slot] == 0 || secs < save_.bestSeconds[slot]) {
            save_.bestSeconds[slot] = secs;
        }
    }
    int& unlocked = (mode_ == Mode::Normal) ? save_.unlockedNormal : save_.unlockedEgg;
    unlocked = std::max(unlocked, std::min(game_.levelIndex() + 2, Level::levelCount(mode_)));
    save_.muted = mixer_.muted();
    saveProgress(save_);
}

void App::advanceLevel() {
    // A generated level is followed by another generated one.
    if (game_.levelIndex() < 0) {
        generateLevel();
        return;
    }
    const int next = game_.levelIndex() + 1;
    if (next < Level::levelCount(mode_)) {
        startLevel(mode_, next);
    } else {
        screen_ = Screen::End;
        mixer_.playSound("end", 0.8f);
    }
}

void App::playEventSounds(unsigned events) {
    if (events & ev::Carrot)       mixer_.playSound("carrot");
    if (events & ev::Egg)          mixer_.playSound("carrot", 0.6f);
    if (events & ev::KeyPickup)    mixer_.playSound("key");
    if (events & ev::LockOpened)   mixer_.playSound("lock");
    if (events & (ev::SwitchRed | ev::SwitchYellow)) mixer_.playSound("switch");
    if (events & ev::Death)        mixer_.playSound("death", 0.8f);
    if (events & ev::Exit)         mixer_.playSound("cleared", 0.8f);
}

void App::onDirection(Dir d, bool pressed) {
    if (screen_ == Screen::Play) {
        if (pressed) {
            held_ = d;
            pendingTap_ = d;
        } else if (held_ == d) {
            held_ = Dir::None;
        }
        return;
    }

    if (!pressed) return;

    if (screen_ == Screen::Select) {
        const int count = Level::levelCount(mode_);
        int next = selected_;
        switch (d) {
            case Dir::Left:  next -= 1; break;
            case Dir::Right: next += 1; break;
            case Dir::Up:    next -= 6; break;
            case Dir::Down:  next += 6; break;
            default: break;
        }
        if (next >= 0 && next < count) selected_ = next;
    } else if (screen_ == Screen::Title) {
        if (d == Dir::Left || d == Dir::Right) {
            mode_ = mode_ == Mode::Normal ? Mode::Egg : Mode::Normal;
        }
    }
}

void App::onConfirm() {
    switch (screen_) {
        case Screen::Title:
            openLevelSelect();
            break;
        case Screen::Select: {
            const int unlocked = mode_ == Mode::Normal ? save_.unlockedNormal : save_.unlockedEgg;
            if (selected_ < unlocked) startLevel(mode_, selected_);
            break;
        }
        case Screen::Cleared:
            clearedTimer_ = 0;
            advanceLevel();
            break;
        case Screen::End:
            screen_ = Screen::Title;
            mixer_.playMusic("title", 0.5f);
            break;
        case Screen::Play:
            break;
    }
}

void App::onBack() {
    switch (screen_) {
        case Screen::Play:
        case Screen::Cleared:
            screen_ = Screen::Select;
            mixer_.playMusic("title", 0.5f);
            break;
        case Screen::Select:
            screen_ = Screen::Title;
            break;
        case Screen::End:
            screen_ = Screen::Title;
            break;
        case Screen::Title:
            break;
    }
}

void App::pressKey(SDL_Keycode key) {
    mixer_.unlock();
    const Dir d = keyToDir(key);
    if (d != Dir::None) {
        // A click is a short tap, like the on-screen arrows.
        onDirection(d, true);
        onDirection(d, false);
        return;
    }
    onKey(key);
}

// Level select: which level number is under a point in logical pixels, or -1.
int App::levelUnderPoint(float lx, float ly) const {
    if (lx < 0.0f || ly < 0.0f) return -1;
    return gfx_.levelAt(mode_, static_cast<int>(lx), static_cast<int>(ly));
}

// Clicking a level number: highlight it and start it unless it is locked.
void App::chooseLevel(int index) {
    selected_ = index;
    const int unlocked = mode_ == Mode::Normal ? save_.unlockedNormal : save_.unlockedEgg;
    if (index < unlocked) startLevel(mode_, index);
}

// Every key that is not a direction. Also reached by clicking the key labels
// under the game on the web page, through pressKey().
void App::onKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: onConfirm(); break;
        case SDLK_ESCAPE: case SDLK_BACKSPACE: onBack(); break;
        case SDLK_r: if (screen_ == Screen::Play) game_.restart(); break;
        case SDLK_h: if (screen_ == Screen::Play) game_.toggleHint(); break;
        case SDLK_f: gfx_.toggleViewMode(); applyLayout(); break;
        case SDLK_m: mixer_.toggleMuted(); save_.muted = mixer_.muted(); saveProgress(save_); break;
        case SDLK_n:
            if (screen_ == Screen::Play &&
                game_.levelIndex() + 1 < Level::levelCount(mode_))
                startLevel(mode_, game_.levelIndex() + 1);
            break;
        case SDLK_p:
            if (screen_ == Screen::Play && game_.levelIndex() > 0)
                startLevel(mode_, game_.levelIndex() - 1);
            break;
        case SDLK_t:
            // A generated level has no carrot/egg counterpart to switch to.
            if ((screen_ == Screen::Play || screen_ == Screen::Cleared) &&
                game_.levelIndex() < 0)
                break;
            mode_ = mode_ == Mode::Normal ? Mode::Egg : Mode::Normal;
            selected_ = 0;
            if (screen_ == Screen::Play) startLevel(mode_, 0);
            break;
        case SDLK_g: askToGenerate(); break;
        case SDLK_b:
            // the size of generated maps: 16, 24, 32 cells a side, then back
            generatedSize_ = generatedSize_ >= Grid::kMaxW ? Grid::kClassic : generatedSize_ + 8;
            std::printf("[gen] map size %dx%d\n", generatedSize_, generatedSize_);
            if (screen_ == Screen::Play && game_.levelIndex() < 0) generateLevel();
            break;
        case SDLK_q: goToMainMenu(); break;
        // export the generated level as a link: its seed and map size
        case SDLK_e: {
            if (screen_ != Screen::Play || game_.levelIndex() >= 0 || game_.seed() == 0) break;
            reportLevelUrl();  // the address already names this level
#ifdef __EMSCRIPTEN__
            EM_ASM({
                var href = window.location.href;
                if (navigator.clipboard && window.isSecureContext) {
                    navigator.clipboard.writeText(href).then(function() {
                        console.log("Copied: " + href);
                    }, function(e) {
                        console.warn("clipboard", e);
                    });
                } else {
                    prompt("Copy link:", href);
                }
            });
#else
            std::printf("share: --seed=%u --size=%d\n", game_.seed(), game_.generatedWidth());
#endif
            break;
        }
        default: break;
    }
}

void App::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT:
            running_ = false;
            return;

        case SDL_KEYDOWN: {
            mixer_.unlock();
            if (e.key.repeat) {
                // Holding a key is handled through held_, not through repeats.
                return;
            }
            const Dir d = keyToDir(e.key.keysym.sym);
            if (d != Dir::None) {
                onDirection(d, true);
                return;
            }
            onKey(e.key.keysym.sym);
            return;
        }

        case SDL_KEYUP: {
            const Dir d = keyToDir(e.key.keysym.sym);
            if (d != Dir::None) onDirection(d, false);
            return;
        }

        case SDL_FINGERDOWN: {
            mixer_.unlock();
            touchSeen_ = true;
            swiping_ = true;
            swipeStartX_ = e.tfinger.x;
            swipeStartY_ = e.tfinger.y;

            int wx = 0, wy = 0;
            SDL_GetWindowSize(window_, &wx, &wy);
            float lx = 0.0f, ly = 0.0f;
            windowToLogical(window_, gfx_.logicalWidth(), gfx_.logicalHeight(),
                            static_cast<int>(e.tfinger.x * wx),
                            static_cast<int>(e.tfinger.y * wy), lx, ly);

            if (screen_ == Screen::Play) {
                for (Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
                    if (pointInRect(static_cast<int>(lx), static_cast<int>(ly),
                                    gfx_.arrowRect(d))) {
                        onDirection(d, true);
                        onDirection(d, false);
                        swiping_ = false;
                        return;
                    }
                }
            }
            return;
        }

        case SDL_FINGERUP: {
            if (!swiping_) return;
            swiping_ = false;
            const float dx = e.tfinger.x - swipeStartX_;
            const float dy = e.tfinger.y - swipeStartY_;

            if (std::fabs(dx) < kSwipeThreshold && std::fabs(dy) < kSwipeThreshold) {
                if (screen_ == Screen::Select) {
                    int wx = 0, wy = 0;
                    SDL_GetWindowSize(window_, &wx, &wy);
                    float lx = 0.0f, ly = 0.0f;
                    windowToLogical(window_, gfx_.logicalWidth(), gfx_.logicalHeight(),
                                    static_cast<int>(e.tfinger.x * wx),
                                    static_cast<int>(e.tfinger.y * wy), lx, ly);
                    const int level = levelUnderPoint(lx, ly);
                    if (level >= 0) {
                        chooseLevel(level);
                        return;
                    }
                }
                if (screen_ != Screen::Play) onConfirm();
                return;
            }
            const Dir d = std::fabs(dx) > std::fabs(dy)
                              ? (dx > 0 ? Dir::Right : Dir::Left)
                              : (dy > 0 ? Dir::Down : Dir::Up);
            onDirection(d, true);
            onDirection(d, false);
            return;
        }

        case SDL_MOUSEBUTTONDOWN: {
            mixer_.unlock();
            // With a logical size set, SDL already reports mouse positions in
            // logical pixels, so they need no conversion.
            const float lx = static_cast<float>(e.button.x);
            const float ly = static_cast<float>(e.button.y);
            if (screen_ == Screen::Play) {
                for (Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
                    if (pointInRect(static_cast<int>(lx), static_cast<int>(ly),
                                    gfx_.arrowRect(d))) {
                        onDirection(d, true);
                        onDirection(d, false);
                        return;
                    }
                }
            } else if (screen_ == Screen::Select) {
                // Only a click on a level number does anything here.
                const int level = levelUnderPoint(lx, ly);
                if (level >= 0) chooseLevel(level);
            } else {
                onConfirm();
            }
            return;
        }

        case SDL_MOUSEMOTION: {
            // Hovering a level number highlights it, as the arrow keys do.
            if (screen_ == Screen::Select && e.motion.which != SDL_TOUCH_MOUSEID) {
                const int level = levelUnderPoint(static_cast<float>(e.motion.x),
                                                  static_cast<float>(e.motion.y));
                if (level >= 0) selected_ = level;
            }
            return;
        }

        default:
            return;
    }
}

void App::stepLogic() {
    ++frameTick_;
    touchFade_ = touchSeen_ ? std::min(1.0f, touchFade_ + 0.05f)
                            : std::max(0.0f, touchFade_ - 0.02f);

    switch (screen_) {
        case Screen::Play: {
            game_.setHeld(held_);
            if (pendingTap_ != Dir::None) {
                game_.queueTap(pendingTap_);
                pendingTap_ = Dir::None;
            }
            game_.update();
            playEventSounds(game_.lastEvents());

            if (game_.playerDead()) {
                game_.restart();
            } else if (game_.levelComplete()) {
                rememberProgress();
                screen_ = Screen::Cleared;
                clearedTimer_ = 0;
            }
            break;
        }

        case Screen::Cleared:
            if (++clearedTimer_ >= kClearedFrames) advanceLevel();
            break;

        case Screen::Title:
            if (frameTick_ % 600 == 1 && mixer_.unlocked()) mixer_.playMusic("title", 0.5f);
            break;

        default:
            break;
    }
}

void App::render() {
    syncBoard();
    gfx_.clear();

    switch (screen_) {
        case Screen::Title:
            gfx_.drawTitle(frameTick_);
            break;
        case Screen::Select: {
            const int unlocked = mode_ == Mode::Normal ? save_.unlockedNormal : save_.unlockedEgg;
            gfx_.drawLevelSelect(mode_, selected_, unlocked);
            break;
        }
        case Screen::Play:
            gfx_.drawGame(game_, frameTick_);
            gfx_.drawTouchArrows(touchFade_);
            break;
        case Screen::Cleared:
            gfx_.drawGame(game_, frameTick_);
            gfx_.drawClearedBanner(frameTick_);
            break;
        case Screen::End:
            gfx_.drawEndScreen();
            break;
    }

    gfx_.present();
}

bool App::startFromSeed(unsigned int seed, int size) {
    if (seed == 0 || !isGeneratedSize(size)) {
        std::fprintf(stderr, "[bobby] bad level link: seed %u, size %d\n", seed, size);
        return false;
    }
    generatedSize_ = size;
    generateLevel(seed);
    return screen_ == Screen::Play;
}

void App::frame() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handleEvent(e);

    const Uint64 now = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    double elapsed = static_cast<double>(now - lastCounter_) / freq;
    lastCounter_ = now;

    // A tab that was in the background can hand us a huge delta; clamp it so
    // the simulation never fast-forwards through a level.
    elapsed = std::min(elapsed, 0.25);
    accumulator_ += elapsed;

    int guard = 0;
    while (accumulator_ >= kStepSeconds && guard++ < 8) {
        stepLogic();
        accumulator_ -= kStepSeconds;
    }

    render();
    reportScreen();
    reportLevelUrl();
}

}  // namespace bc::platform
