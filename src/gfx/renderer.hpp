#pragma once

#include <SDL.h>

#include <algorithm>
#include <string>

#include "../core/game.hpp"
#include "texture.hpp"

namespace bc::gfx {

// The whole island at once. The view is as big as the board, in pixels: a
// 32x32 board is 1024 x 1024 logical pixels, which SDL scales into the window.
constexpr int kFullW     = Grid::kClassic * kTileSize;   // 512, a classic board
constexpr int kFullH     = Grid::kClassic * kTileSize;   // 512

class Renderer {
public:
    bool init(SDL_Renderer* sdl, const std::string& assetRoot);

    // Size of the board in cells: the level's while one is shown, the classic
    // 16x16 on the other screens. The app sets it and then redoes the layout.
    void setBoard(int w, int h) { boardW_ = w; boardH_ = h; }
    int boardW() const { return boardW_; }
    int boardH() const { return boardH_; }

    int logicalWidth() const  { return boardW_ * kTileSize; }
    int logicalHeight() const { return boardH_ * kTileSize; }

    // Where the timer starts, in logical pixels from the left edge: the web
    // page keeps its Back button in the top left corner of the board.
    void setHudLeft(int x) { hudLeft_ = std::max(4, x); }

    void clear();
    void present();

    void drawGame(const Game& game, int frameTick);
    void drawTitle(int frameTick);
    void drawLevelSelect(Mode mode, int selected, int unlocked);
    // Level under a point on the level select screen, or -1 if none.
    int levelAt(Mode mode, int x, int y) const;
    void drawClearedBanner(int frameTick);
    void drawEndScreen();
    void drawTouchArrows(float alpha);

    // Screen-space rectangle of an on-screen arrow, for hit testing taps.
    SDL_Rect arrowRect(Dir d) const;

    const TextureCache& textures() const { return tex_; }

private:
    void drawTiles(const Game& game, int frameTick);
    void drawPlayer(const Game& game);
    void drawHud(const Game& game);
    void drawNumber(int value, int digits, int x, int y);
    void drawGlyph(int glyph, int x, int y);
    void blit(const Texture& t, const SDL_Rect* src, int x, int y);

    SDL_Renderer* r_ = nullptr;
    TextureCache tex_;
    int boardW_ = Grid::kClassic;
    int boardH_ = Grid::kClassic;
    int hudLeft_ = 4;
};

}  // namespace bc::gfx
