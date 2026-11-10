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

std::string toBase64Url(std::string s) {
    std::replace(s.begin(), s.end(), '+', '-');
    std::replace(s.begin(), s.end(), '/', '_');
    while (!s.empty() && s.back() == '=') {
        s.pop_back();
    }
    return s;
}

std::string fromBase64Url(std::string s) {
    std::replace(s.begin(), s.end(), '-', '+');
    std::replace(s.begin(), s.end(), '_', '/');
    while (s.size() % 4 != 0) s.push_back('=');
    return s;
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
    SDL_SetWindowSize(window_, w * windowScale_, h * windowScale_);
    SDL_RenderSetLogicalSize(renderer_, w, h);
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
            screen_ = Screen::Select;
            selected_ = 0;
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
            switch (e.key.keysym.sym) {
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
                    mode_ = mode_ == Mode::Normal ? Mode::Egg : Mode::Normal;
                    selected_ = 0;
                    if (screen_ == Screen::Play) startLevel(mode_, 0);
                    break;
                case SDLK_g:
                    if (screen_ == Screen::Play || screen_ == Screen::Select) {
                        game_.loadProcedural(0); // 0 — случайный сид
                        screen_ = Screen::Play;
                        held_ = Dir::None;
                        pendingTap_ = Dir::None;
                        mixer_.stopMusic();
                    }
                    break;

                // export level in a base64 encoded way into URL
                case SDLK_e: {
                    std::string encoded = toBase64Url(game_.level().encode());
                    #ifdef __EMSCRIPTEN__
                        EM_ASM({
                            var u = new URL(window.location.href);
                            u.searchParams.set('level', UTF8ToString($0));
                            var href = u.toString();

                            window.history.replaceState(null, "", href);

                            if (navigator.clipboard && window.isSecureContext) {
                                navigator.clipboard.writeText(href).then(function() {
                                    console.log("Copied: " + href);
                                }, function(e) {
                                    console.warn("clipboard", e);
                                });
                            } else {
                                prompt("Copy link:", href);
                            }
                        }, encoded.c_str());
                    #else
                        std::fprintf(stderr, "share: %s\n", encoded.c_str());
                    #endif
                        break;
                    }
                default: break;
            }
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
            float lx = 0.0f, ly = 0.0f;
            windowToLogical(window_, gfx_.logicalWidth(), gfx_.logicalHeight(), e.button.x,
                            e.button.y, lx, ly);
            if (screen_ == Screen::Play) {
                for (Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
                    if (pointInRect(static_cast<int>(lx), static_cast<int>(ly),
                                    gfx_.arrowRect(d))) {
                        onDirection(d, true);
                        onDirection(d, false);
                        return;
                    }
                }
            } else {
                onConfirm();
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

void App::startFromEncoded(const std::string& base64url) {
    Level level;
    std::string err;
    if (!level.decode(fromBase64Url(base64url), &err)) {
        std::fprintf(stderr, "[bobby] bad ?level: %s\n", err.c_str());
        return;
    }
    if (!game_.loadLevel(level)) {
        std::fprintf(stderr, "[bobby] loadDecoded refused level\n");
        return;
    }
    screen_ = Screen::Play;
    held_ = Dir::None;
    pendingTap_ = Dir::None;
    mixer_.stopMusic();
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
}

}  // namespace bc::platform
