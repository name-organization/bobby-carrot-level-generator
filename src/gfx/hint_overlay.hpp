/**
 * @file
 * @brief Render hint paths and dead-end tint from the Game hint API. */
#pragma once

#include <SDL.h>

#include "../core/game.hpp"

namespace bc::gfx {

/**
 * @brief Draw the hint over the visible board.
 *
 * @param r Target SDL renderer.
 * @param game Current simulation and hint state.
 * @param camX Left edge of the camera in board pixels.
 * @param camY Top edge of the camera in board pixels.
 */
void drawHintOverlay(SDL_Renderer* r, const Game& game, int camX, int camY);

}  // namespace bc::gfx
