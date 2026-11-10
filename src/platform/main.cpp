#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/console.h>
#include <emscripten/val.h> 
#else
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#endif

namespace {

bc::platform::App g_app;

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

int main(int argc, char** argv) {
    std::string assetRoot = "assets/v1";
    bool handheld = false;
    std::string startLevelB64;

    #ifdef __EMSCRIPTEN__
        {
            emscripten::val module = emscripten::val::global("Module");
            emscripten::val level  = module["__levelB64"];
            if (!level.isUndefined() && !level.isNull()) {
                startLevelB64 = level.as<std::string>();
            }
            emscripten_console_log(("startLevelB64 len=" + std::to_string(startLevelB64.size())).c_str());
        }
    #endif

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--handheld") == 0) handheld = true;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            assetRoot = argv[++i];
        }
        else if (std::strncmp(argv[i], "--level=", 8) == 0) {
            startLevelB64 = argv[i] + 8;
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

    #ifdef __EMSCRIPTEN__
        emscripten_console_log("bobik");
        emscripten_console_log(startLevelB64.c_str());
        emscripten_console_log("sysel");
    #endif

    // If shell.html forwarded ?level=<base64url> jump into that level
    if (!startLevelB64.empty()) {
        g_app.startFromEncoded(startLevelB64);
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