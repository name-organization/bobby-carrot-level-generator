
#include "hint_overlay.hpp"

#include <algorithm>
#include <cstdlib>

namespace bc::gfx {
namespace {

/** @brief Draw an axis-aligned segment between cell centers in camera coordinates. */
void segment(SDL_Renderer* r, Coord a, Coord b, int half, int camX, int camY) {
    const int ax = a.x * kTileSize + kTileSize / 2 - camX;
    const int ay = a.y * kTileSize + kTileSize / 2 - camY;
    const int bx = b.x * kTileSize + kTileSize / 2 - camX;
    const int by = b.y * kTileSize + kTileSize / 2 - camY;
    const SDL_Rect rect{std::min(ax, bx) - half, std::min(ay, by) - half,
                        std::abs(ax - bx) + 2 * half, std::abs(ay - by) + 2 * half};
    SDL_RenderFillRect(r, &rect);
}

void dot(SDL_Renderer* r, Coord c, int half, int camX, int camY) {
    segment(r, c, c, half, camX, camY);
}

}  // namespace

void drawHintOverlay(SDL_Renderer* r, const Game& game, int camX, int camY) {
    const hint::Result* h = game.hint();
    if (!h) return;

    if (h->verdict == hint::Verdict::DeadEnd) {
        SDL_SetRenderDrawColor(r, 170, 20, 20, 110);
        const SDL_Rect all{-camX, -camY, game.grid().w * kTileSize, game.grid().h * kTileSize};
        SDL_RenderFillRect(r, &all);
        return;
    }
    const std::vector<Coord>& path = game.hintPath();
    if (h->verdict != hint::Verdict::Winnable || path.empty()) return;

    const Coord from = game.player().cell();

    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0) SDL_SetRenderDrawColor(r, 20, 20, 20, 190);
        else           SDL_SetRenderDrawColor(r, 255, 216, 77, 255);
        const int half = pass == 0 ? 4 : 2;
        Coord prev = from;
        for (Coord c : path) {
            segment(r, prev, c, half, camX, camY);
            prev = c;
        }
        dot(r, path.back(), half + 3, camX, camY);
    }

    // the very next step stands out
    SDL_SetRenderDrawColor(r, 20, 20, 20, 210);
    segment(r, from, path.front(), 5, camX, camY);
    dot(r, path.front(), 7, camX, camY);
    SDL_SetRenderDrawColor(r, 255, 90, 31, 255);
    segment(r, from, path.front(), 3, camX, camY);
    dot(r, path.front(), 5, camX, camY);
}

}  // namespace bc::gfx
