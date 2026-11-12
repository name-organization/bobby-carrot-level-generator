#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/val.h>
#else
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#endif

namespace {

bc::platform::App g_app;

/** @brief Parse a nonzero unsigned 32-bit seed without accepting signs or suffixes. */
bool parseSeed(const std::string& text, unsigned int& out) {
    if (text.empty() || text.size() > 10) return false;
    unsigned long long value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > 0xFFFFFFFFull) return false;
    out = static_cast<unsigned int>(value);
    return true;
}

bool parseSize(const std::string& text, int& out) {
    if (text.empty()) {
        out = bc::Grid::kClassic;
        return true;
    }
    unsigned int value = 0;
    if (!parseSeed(text, value) || value > 1000) return false;
    out = static_cast<int>(value);
    return true;
}

/** @brief Parse difficulty; an empty value preserves legacy unrated generation. */
bool parseDifficulty(const std::string& text, bc::Difficulty& out, bool& rated) {
    rated = !text.empty();
    out = bc::Difficulty::Medium;
    return text.empty() || bc::difficulty::parse(text, out);
}

void frameCallback(void* arg) {
    auto* app = static_cast<bc::platform::App*>(arg);
    app->frame();
}

#ifndef __EMSCRIPTEN__

/** @brief Capture a native headless frame via --shot MODE LEVEL TICKS OUTPUT. */
int screenshot(const std::string& assetRoot, bc::Mode mode, int level, int warmupTicks,
               const char* outPath, bool handheld) {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    bc::platform::App app;
    if (!app.init(assetRoot, /*headless=*/true)) return 1;

    if (handheld) app.setViewMode(bc::gfx::ViewMode::Handheld);
    app.forceLevel(mode, level);
    for (int i = 0; i < warmupTicks; ++i) app.tick();
    app.draw();

    // The output size can exceed the logical viewport; size readback storage for actual pixels.
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(app.sdlRenderer(), &w, &h) != 0 || w <= 0 || h <= 0) {
        w = app.gfx().logicalWidth();
        h = app.gfx().logicalHeight();
    }
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 4);
    if (SDL_RenderReadPixels(app.sdlRenderer(), nullptr, SDL_PIXELFORMAT_ABGR8888,
                             pixels.data(), w * 4) != 0) {
        std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        app.shutdown();
        return 1;
    }
    stbi_write_png(outPath, w, h, 4, pixels.data(), w * 4);
    std::printf("wrote %s (%dx%d)\n", outPath, w, h);
    app.shutdown();
    return 0;
}

#endif

}  // namespace

#ifdef __EMSCRIPTEN__
/** @brief Browser exports consumed by web/shell.html through Module._bc_*. */
extern "C" {
/** @brief Generate a square map; seed zero is random, difficulty is 0/1/2. */
EMSCRIPTEN_KEEPALIVE void bc_generate_level(unsigned int seed, int size, int difficulty) {
    if (difficulty < 0 || difficulty > 2) difficulty = 1;
    g_app.generateLevel(seed, size, static_cast<bc::Difficulty>(difficulty));
}
/** @brief Set the timer position in logical board pixels. */
EMSCRIPTEN_KEEPALIVE void bc_set_timer_at(int x, int y) { g_app.setTimerAt(x, y); }

EMSCRIPTEN_KEEPALIVE void bc_ask_to_generate() { g_app.askToGenerate(); }
EMSCRIPTEN_KEEPALIVE void bc_select_level()   { g_app.openLevelSelect(); }

EMSCRIPTEN_KEEPALIVE void bc_toggle_level_mode() { g_app.toggleLevelMode(); }
/** @brief Dispatch an SDL keycode from the browser controls. */
EMSCRIPTEN_KEEPALIVE void bc_press_key(int key) { g_app.pressKey(key); }
/** @brief Apply music and sound percentages in [0, 100] and a boolean mute flag. */
EMSCRIPTEN_KEEPALIVE void bc_set_audio(int music, int sound, int muted) {
    g_app.setAudio(music, sound, muted != 0);
}
/** @brief Pause the simulation while a browser dialog is open. */
EMSCRIPTEN_KEEPALIVE void bc_set_paused(int paused) { g_app.setPaused(paused != 0); }
}
#endif

int main(int argc, char** argv) {
    std::string assetRoot = "assets/v1";
    bool handheld = false;

    std::string startSeed;
    std::string startSize;
    std::string startDifficulty;

#ifdef __EMSCRIPTEN__
    {
        emscripten::val module = emscripten::val::global("Module");
        emscripten::val seed = module["__seed"];
        emscripten::val size = module["__size"];
        emscripten::val difficulty = module["__difficulty"];
        if (seed.isString()) startSeed = seed.as<std::string>();
        if (size.isString()) startSize = size.as<std::string>();
        if (difficulty.isString()) startDifficulty = difficulty.as<std::string>();
    }
#endif

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--handheld") == 0) handheld = true;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            assetRoot = argv[++i];
        }
        else if (std::strncmp(argv[i], "--seed=", 7) == 0) {
            startSeed = argv[i] + 7;
        }
        else if (std::strncmp(argv[i], "--size=", 7) == 0) {
            startSize = argv[i] + 7;
        }
        else if (std::strncmp(argv[i], "--difficulty=", 13) == 0) {
            startDifficulty = argv[i] + 13;
        }
#ifndef __EMSCRIPTEN__
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 4 < argc) {
            const bc::Mode mode =
                std::strcmp(argv[i + 1], "egg") == 0 ? bc::Mode::Egg : bc::Mode::Normal;
            const int level = std::atoi(argv[i + 2]) - 1;
            const int ticks = std::atoi(argv[i + 3]);
            return screenshot(assetRoot, mode, level, ticks, argv[i + 4], handheld);
        }
#endif
    }

    if (!g_app.init(assetRoot, /*headless=*/false)) return 1;
    if (handheld) g_app.setViewMode(bc::gfx::ViewMode::Handheld);

    // Omitted size defaults to classic; omitted difficulty preserves legacy generation.
    if (!startSeed.empty()) {
        unsigned int seed = 0;
        int size = 0;
        bc::Difficulty difficulty = bc::Difficulty::Medium;
        bool rated = true;
        if (parseSeed(startSeed, seed) && parseSize(startSize, size) &&
            parseDifficulty(startDifficulty, difficulty, rated)) {
            g_app.startFromSeed(seed, size, difficulty, rated);
        } else {
            std::fprintf(stderr, "[bobby] bad level link: seed '%s', size '%s', difficulty '%s'\n",
                         startSeed.c_str(), startSize.c_str(), startDifficulty.c_str());
        }
    }

#ifdef __EMSCRIPTEN__
    // A zero frame rate follows requestAnimationFrame and pauses in background tabs.
    emscripten_set_main_loop_arg(frameCallback, &g_app, 0, 1);
#else
    while (g_app.running()) frameCallback(&g_app);
    g_app.shutdown();
#endif
    return 0;
}
