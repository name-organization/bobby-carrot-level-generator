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

constexpr const char* kMusic = "title";
constexpr float kMusicVolume = 0.5f;

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

/**
 * @brief Convert window pixels to the letterboxed logical viewport.
 *
 * @note Uses manual scaling for Emscripten SDL ports predating SDL 2.0.18.
 */
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
    }
    applyAudio();

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
    // Preserve window size across views; cap native dimensions while CSS fits browser canvases.
    int ww = gfx_.fullWidth() * windowScale_, wh = gfx_.fullHeight() * windowScale_;
    const int side = std::max(ww, wh);
    if (side > kMaxWindowSide) {
        ww = ww * kMaxWindowSide / side;
        wh = wh * kMaxWindowSide / side;
    }
    SDL_SetWindowSize(window_, ww, wh);
    SDL_RenderSetLogicalSize(renderer_, w, h);
}

/** @brief Match board geometry to the screen; paused dialogs disable close-up view. */
void App::syncBoard() {
    const bool level = screen_ == Screen::Play || screen_ == Screen::Cleared;
    const int w = level ? game_.grid().w : Grid::kClassic;
    const int h = level ? game_.grid().h : Grid::kClassic;
    const bool closeUp = level && !paused_;
    if (w == gfx_.boardW() && h == gfx_.boardH() && closeUp == gfx_.closeUpAllowed()) return;
    gfx_.setBoard(w, h);
    gfx_.setCloseUpAllowed(closeUp);
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
}

void App::generateLevel(unsigned int seed, bool rated) {
    if (!game_.loadProcedural(seed, generatedSize_, generatedSize_, generatedDifficulty_, rated)) {
        std::fprintf(stderr, "cannot generate a level\n");
        return;
    }
    screen_ = Screen::Play;
    held_ = Dir::None;
    pendingTap_ = Dir::None;
}

void App::generateLevel(unsigned int seed, int size, Difficulty difficulty) {
    if (isGeneratedSize(size)) generatedSize_ = size;
    generatedDifficulty_ = difficulty;
    generateLevel(seed);
}

bool App::onGeneratedLevel() const {
    return (screen_ == Screen::Play || screen_ == Screen::Cleared) && game_.levelIndex() < 0;
}

bool App::openNewLevelWindow() {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({
        if (!Module.askSeed) return 0;
        Module.askSeed($0, $1);
        return 1;
    }, generatedSize_, static_cast<int>(generatedDifficulty_)) != 0;
#else
    return false;
#endif
}

void App::askToGenerate() {
    if (!openNewLevelWindow()) generateLevel();
}

bool App::isGeneratedSize(int size) {
    return size == Grid::kClassic || size == Grid::kClassic + 8 || size == Grid::kMaxW;
}

void App::toggleLevelMode() {
    if (screen_ != Screen::Select) return;
    mode_ = mode_ == Mode::Normal ? Mode::Egg : Mode::Normal;
    selected_ = Level::levelCount(mode_);  // the switch stays selected
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
}

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

void App::applyAudio() {
    mixer_.setMuted(save_.muted);
    mixer_.setMusicVolume(save_.musicVolume / 100.0f);
    mixer_.setSoundVolume(save_.soundVolume / 100.0f);
}

void App::setAudio(int musicPercent, int soundPercent, bool muted) {
    mixer_.unlock();  // the page calls this from a click or a key press
    musicPercent = std::clamp(musicPercent, 0, 100);
    soundPercent = std::clamp(soundPercent, 0, 100);
    const bool soundChanged = soundPercent != save_.soundVolume;
    save_.musicVolume = musicPercent;
    save_.soundVolume = soundPercent;
    save_.muted = muted;
    applyAudio();
    saveProgress(save_);
    if (soundChanged) mixer_.playSound("carrot");
}

void App::setPaused(bool paused) {
    paused_ = paused;
    held_ = Dir::None;
    pendingTap_ = Dir::None;
}

void App::reportAudio() {
    const bool muted = mixer_.muted();
    if (save_.musicVolume == reportedMusic_ && save_.soundVolume == reportedSound_ &&
        muted == reportedMuted_)
        return;
    reportedMusic_ = save_.musicVolume;
    reportedSound_ = save_.soundVolume;
    reportedMuted_ = muted;
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.onAudioChange) Module.onAudioChange($0, $1, !!$2); },
           save_.musicVolume, save_.soundVolume, muted);
#endif
}

void App::reportLevelUrl() {
    const bool onLevel = screen_ == Screen::Play || screen_ == Screen::Cleared;
    const bool generated = onLevel && game_.levelIndex() < 0 && game_.seed() != 0;
    const unsigned int seed = generated ? game_.seed() : 0;
    const int size = generated ? game_.generatedWidth() : 0;
    // Preserve unrated generation when sharing legacy links.
    const int difficulty =
        generated && game_.generatedRated() ? static_cast<int>(game_.generatedDifficulty()) : -1;
    if (seed == reportedSeed_ && size == reportedSize_ && difficulty == reportedDifficulty_) return;
    reportedSeed_ = seed;
    reportedSize_ = size;
    reportedDifficulty_ = difficulty;
#ifdef __EMSCRIPTEN__
    // Update the shared seed without adding a browser-history entry per level.
    EM_ASM({
        var u = new URL(window.location.href);
        u.searchParams.delete('level');  // links from before seeds
        if ($0 >>> 0) {
            u.searchParams.set('seed', String($0 >>> 0));
            u.searchParams.set('size', String($1));
            if ($2 >= 0) u.searchParams.set('difficulty', ['easy', 'medium', 'hard'][$2]);
            else u.searchParams.delete('difficulty');
        } else {
            u.searchParams.delete('seed');
            u.searchParams.delete('size');
            u.searchParams.delete('difficulty');
        }
        if (u.href !== window.location.href) window.history.replaceState(null, "", u.href);
    }, seed, size, difficulty);
#endif
}

void App::reportLevelList() {
    const int mode = screen_ == Screen::Select ? (mode_ == Mode::Normal ? 1 : 2) : 0;
    const bool switchSelected = mode != 0 && selected_ >= Level::levelCount(mode_);
    const bool backSelected = (mode != 0 && selected_ < 0) ||
                              (screen_ == Screen::End && endBackSelected_);
    if (mode == reportedListMode_ && switchSelected == reportedSwitchSelected_ &&
        backSelected == reportedBackSelected_)
        return;
    reportedListMode_ = mode;
    reportedSwitchSelected_ = switchSelected;
    reportedBackSelected_ = backSelected;
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.onLevelList) Module.onLevelList($0, !!$1, !!$2); },
           mode, switchSelected, backSelected);
#endif
}

void App::reportLevelInfo() {
    const bool onLevel = screen_ == Screen::Play || screen_ == Screen::Cleared;
    int kind = 0, number = 0, size = 0, difficulty = 0;
    unsigned int seed = 0;
    if (onLevel && game_.levelIndex() >= 0) {
        kind = game_.mode() == Mode::Normal ? 1 : 2;
        number = game_.levelIndex() + 1;
    } else if (onLevel && game_.seed() != 0) {
        kind = 3;
        size = game_.generatedWidth();
        difficulty = static_cast<int>(game_.generatedDifficulty());
        seed = game_.seed();
    }

    const int viewPx = gfx_.logicalWidth();
    if (kind == reportedKind_ && number == reportedNumber_ && seed == reportedInfoSeed_ &&
        size == reportedInfoSize_ && difficulty == reportedInfoDifficulty_ &&
        viewPx == reportedViewPx_)
        return;
    reportedViewPx_ = viewPx;
    reportedKind_ = kind;
    reportedNumber_ = number;
    reportedInfoSeed_ = seed;
    reportedInfoSize_ = size;
    reportedInfoDifficulty_ = difficulty;
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.onLevelInfo) Module.onLevelInfo($0, $1, $2, $3, $4 >>> 0, $5); },
           kind, number, size, difficulty, seed, viewPx);
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
    if (game_.levelIndex() < 0) {
        generateLevel();
        return;
    }
    const int next = game_.levelIndex() + 1;
    if (next < Level::levelCount(mode_)) {
        startLevel(mode_, next);
    } else {
        screen_ = Screen::End;
        endBackSelected_ = false;
        mixer_.stopMusic();
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
        // Selection sentinels: -1 is Back, count is the carrot/egg switch.
        const int count = Level::levelCount(mode_);
        if (selected_ >= count) {
            if (d == Dir::Up) selected_ = count - 1;
            return;
        }
        if (selected_ < 0) {
            if (d == Dir::Down) selected_ = 0;
            return;
        }
        int next = selected_;
        switch (d) {
            case Dir::Left:  next -= 1; break;
            case Dir::Right: next += 1; break;
            case Dir::Up:    next = next - 6 >= 0 ? next - 6 : -1; break;
            case Dir::Down:  next = next + 6 < count ? next + 6 : count; break;
            default: break;
        }
        if (next >= -1 && next <= count && !(next < 0 && d != Dir::Up)) selected_ = next;
    } else if (screen_ == Screen::End) {
        if (d == Dir::Up) endBackSelected_ = true;
        if (d == Dir::Down) endBackSelected_ = false;
    } else if (screen_ == Screen::Title) {
        if (d == Dir::Left || d == Dir::Right) {
            mode_ = mode_ == Mode::Normal ? Mode::Egg : Mode::Normal;
        }
    }
}

void App::tapDirection(Dir d) {
    onDirection(d, true);
    onDirection(d, false);
}

bool App::tapArrowAt(float logicalX, float logicalY) {
    for (Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
        if (pointInRect(static_cast<int>(logicalX), static_cast<int>(logicalY),
                        gfx_.arrowRect(d))) {
            tapDirection(d);
            return true;
        }
    }
    return false;
}

void App::onConfirm() {
    switch (screen_) {
        case Screen::Title:
            openLevelSelect();
            break;
        case Screen::Select: {
            if (selected_ < 0) {  // the Back button
                onBack();
                break;
            }
            if (selected_ >= Level::levelCount(mode_)) {
                toggleLevelMode();
                break;
            }
            const int unlocked = mode_ == Mode::Normal ? save_.unlockedNormal : save_.unlockedEgg;
            if (selected_ < unlocked) startLevel(mode_, selected_);
            break;
        }
        case Screen::Cleared:
            clearedTimer_ = 0;
            advanceLevel();
            break;
        case Screen::End:

            if (endBackSelected_) onBack();
            else screen_ = Screen::Title;
            break;
        case Screen::Play:
            break;
    }
}

void App::onBack() {
    switch (screen_) {
        case Screen::Play:
        case Screen::Cleared:

            if (game_.levelIndex() >= 0) {
                screen_ = Screen::Select;
            } else if (!openNewLevelWindow()) {
                goToMainMenu();
            }
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
        tapDirection(d);
        return;
    }
    onKey(key);
}

int App::levelUnderPoint(float lx, float ly) const {
    if (lx < 0.0f || ly < 0.0f) return -1;
    return gfx_.levelAt(mode_, static_cast<int>(lx), static_cast<int>(ly));
}

void App::chooseLevel(int index) {
    selected_ = index;
    const int unlocked = mode_ == Mode::Normal ? save_.unlockedNormal : save_.unlockedEgg;
    if (index < unlocked) startLevel(mode_, index);
}

void App::onKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: onConfirm(); break;
        case SDLK_ESCAPE: case SDLK_BACKSPACE: onBack(); break;
        case SDLK_r: if (screen_ == Screen::Play) game_.restart(); break;
        case SDLK_h: if (screen_ == Screen::Play) game_.toggleHint(); break;

        case SDLK_f:
            if (screen_ == Screen::Play || screen_ == Screen::Cleared) {
                gfx_.toggleViewMode();
                applyLayout();
            }
            break;
        case SDLK_m: mixer_.toggleMuted(); save_.muted = mixer_.muted(); saveProgress(save_); break;

        case SDLK_v:
#ifdef __EMSCRIPTEN__
            EM_ASM({ if (Module.toggleSettings) Module.toggleSettings(); });
#endif
            break;
        case SDLK_n:
            if (screen_ == Screen::Play && game_.levelIndex() >= 0 &&
                game_.levelIndex() + 1 < Level::levelCount(mode_))
                startLevel(mode_, game_.levelIndex() + 1);
            break;
        case SDLK_p:
            if (screen_ == Screen::Play && game_.levelIndex() > 0)
                startLevel(mode_, game_.levelIndex() - 1);
            break;
        case SDLK_g:

            if (onGeneratedLevel()) generateLevel();
            break;
        case SDLK_q: goToMainMenu(); break;

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
            if (game_.generatedRated())
                std::printf("share: --seed=%u --size=%d --difficulty=%s\n", game_.seed(),
                            game_.generatedWidth(), difficulty::name(game_.generatedDifficulty()));
            else
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
                // Held input is tracked separately; SDL repeats must not enqueue taps.
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

            if (screen_ == Screen::Play && tapArrowAt(lx, ly)) swiping_ = false;
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
            tapDirection(d);
            return;
        }

        case SDL_MOUSEBUTTONDOWN: {
            mixer_.unlock();
            // SDL mouse coordinates already use logical pixels; touch coordinates need conversion.
            const float lx = static_cast<float>(e.button.x);
            const float ly = static_cast<float>(e.button.y);
            if (screen_ == Screen::Play) {
                tapArrowAt(lx, ly);
            } else if (screen_ == Screen::Select) {
                const int level = levelUnderPoint(lx, ly);
                if (level >= 0) chooseLevel(level);
            } else {
                onConfirm();
            }
            return;
        }

        case SDL_MOUSEMOTION: {
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

    if (mixer_.unlocked() && screen_ != Screen::End) mixer_.playMusic(kMusic, kMusicVolume);
    if (paused_) return;
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

bool App::startFromSeed(unsigned int seed, int size, Difficulty difficulty, bool rated) {
    if (seed == 0 || !isGeneratedSize(size)) {
        std::fprintf(stderr, "[bobby] bad level link: seed %u, size %d\n", seed, size);
        return false;
    }
    generatedSize_ = size;
    generatedDifficulty_ = rated ? difficulty : Difficulty::Medium;
    generateLevel(seed, rated);
    return screen_ == Screen::Play;
}

void App::frame() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handleEvent(e);

    const Uint64 now = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    double elapsed = static_cast<double>(now - lastCounter_) / freq;
    lastCounter_ = now;

    // Clamp resumed-tab deltas to avoid fast-forwarding the simulation.
    elapsed = std::min(elapsed, 0.25);
    accumulator_ += elapsed;

    int guard = 0;
    while (accumulator_ >= kStepSeconds && guard++ < 8) {
        stepLogic();
        accumulator_ -= kStepSeconds;
    }

    render();
    reportScreen();
    reportAudio();
    reportLevelUrl();
    reportLevelInfo();
    reportLevelList();
}

}  // namespace bc::platform
