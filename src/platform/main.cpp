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

// Whole decimal numbers only: "12abc", "-1" or a seed above 2^32 - 1 is no
// level link, rather than some other level than the one shared.
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

void frameCallback(void* arg) {
    auto* app = static_cast<bc::platform::App*>(arg);
    app->frame();
}

#ifndef __EMSCRIPTEN__

// Offscreen capture used to eyeball the renderer without a display, and handy
// for anyone wanting a quick look at a level: --shot normal 1 out.png
int screenshot(const std::string& assetRoot, bc::Mode mode, int level, int warmupTicks,
               const char* outPath, bool handheld) {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    bc::platform::App app;
    if (!app.init(assetRoot, /*headless=*/true)) return 1;

    if (handheld) app.setViewMode(bc::gfx::ViewMode::Handheld);
    app.forceLevel(mode, level);
    for (int i = 0; i < warmupTicks; ++i) app.tick();
    app.draw();

    // Read back the renderer's real output, not the logical size: the window
    // is created at an integer multiple of it, and reading into a buffer sized
    // for the logical resolution walks off the end.
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
// Called from web/shell.html as Module._bc_generate_level(seed) etc.
extern "C" {
// seed 0 picks a random one
EMSCRIPTEN_KEEPALIVE void bc_generate_level(unsigned int seed) { g_app.generateLevel(seed); }
EMSCRIPTEN_KEEPALIVE void bc_select_level()   { g_app.openLevelSelect(); }
// key is an SDL keycode: the lower-case letter's code, or 27 for Escape.
EMSCRIPTEN_KEEPALIVE void bc_press_key(int key) { g_app.pressKey(key); }
// The Settings page: volumes in percent, 0..100; muted 0 or 1.
EMSCRIPTEN_KEEPALIVE void bc_set_audio(int music, int sound, int muted) {
    g_app.setAudio(music, sound, muted != 0);
}
}
#endif

int main(int argc, char** argv) {
    std::string assetRoot = "assets/v1";
    bool handheld = false;
    // A shared generated level: its seed and map side, as text.
    std::string startSeed;
    std::string startSize;

#ifdef __EMSCRIPTEN__
    {
        // shell.html copies ?seed= and ?size= here
        emscripten::val module = emscripten::val::global("Module");
        emscripten::val seed = module["__seed"];
        emscripten::val size = module["__size"];
        if (seed.isString()) startSeed = seed.as<std::string>();
        if (size.isString()) startSize = size.as<std::string>();
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

    // A link to a generated level starts in it. Without a size it is the
    // classic 16 x 16.
    if (!startSeed.empty()) {
        unsigned int seed = 0;
        int size = 0;
        if (parseSeed(startSeed, seed) && parseSize(startSize, size)) {
            g_app.startFromSeed(seed, size);
        } else {
            std::fprintf(stderr, "[bobby] bad level link: seed '%s', size '%s'\n",
                         startSeed.c_str(), startSize.c_str());
        }
    }

#ifdef __EMSCRIPTEN__
    // Zero means "drive from requestAnimationFrame", which keeps the canvas in
    // step with the compositor and pauses in background tabs.
    emscripten_set_main_loop_arg(frameCallback, &g_app, 0, 1);
#else
    while (g_app.running()) frameCallback(&g_app);
    g_app.shutdown();
#endif
    return 0;
}