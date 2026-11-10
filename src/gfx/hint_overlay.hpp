// TEMPORARY debug drawing of the hint, until the front end draws it properly.
//
// Draws the suggested path as a yellow line with the next step in orange, or
// tints the board red on a dead end. Everything it needs comes from
// Game::hint() and Game::hintPath(), which stay.
//
// To remove it:
//   1. delete src/gfx/hint_overlay.hpp and src/gfx/hint_overlay.cpp;
//   2. drop src/gfx/hint_overlay.cpp from GAME_SOURCES in CMakeLists.txt;
//   3. in src/gfx/renderer.cpp drop the include and the drawHintOverlay() call,
//      both marked "HINT OVERLAY".
#pragma once

#include <SDL.h>

#include "../core/game.hpp"

namespace bc::gfx {

// camX/camY: top-left of the visible board in pixels, as the renderer uses.
void drawHintOverlay(SDL_Renderer* r, const Game& game, int camX, int camY);

}  // namespace bc::gfx
