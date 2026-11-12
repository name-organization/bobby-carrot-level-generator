#pragma once

#include <SDL.h>

#include <algorithm>
#include <string>

#include "../core/game.hpp"
#include "texture.hpp"

namespace bc::gfx {

// Two presentations of the same board: the whole island at once, and a
// close-up that scrolls behind Bobby. The full view is as big as the board,
// in pixels: a 32x32 board is 1024 x 1024 logical pixels, which SDL scales
// into the window. The close-up is kViewCells cells across (and as many rows
// as keep the board's proportions) in a window of the same size, so it shows
// fewer cells, each of them bigger.
enum class ViewMode { FullMap, Handheld };

constexpr int kFullW     = Grid::kClassic * kTileSize;   // 512, a classic board
constexpr int kFullH     = Grid::kClassic * kTileSize;   // 512
constexpr int kViewCells = 10;

class Renderer {
public:
    bool init(SDL_Renderer* sdl, const std::string& assetRoot);

    void setViewMode(ViewMode m) { view_ = m; }
    ViewMode viewMode() const { return view_; }
    void toggleViewMode() { view_ = view_ == ViewMode::FullMap ? ViewMode::Handheld : ViewMode::FullMap; }

    // Size of the board in cells: the level's while one is shown, the classic
    // 16x16 on the other screens. The app sets it and then redoes the layout.
    void setBoard(int w, int h) { boardW_ = w; boardH_ = h; }
    int boardW() const { return boardW_; }
    int boardH() const { return boardH_; }

    // The close-up is for playing only: the app allows it while a level is
    // being played, and the menus, the level list and the web page's windows
    // over a paused level (Settings, New level) keep the whole view, whatever
    // the view mode is.
    void setCloseUpAllowed(bool on) { closeUpAllowed_ = on; }
    bool closeUpAllowed() const { return closeUpAllowed_; }
    bool closeUp() const { return view_ == ViewMode::Handheld && closeUpAllowed_; }

    // The whole board, in logical pixels: the size of the window in either view.
    int fullWidth() const  { return boardW_ * kTileSize; }
    int fullHeight() const { return boardH_ * kTileSize; }
    // What is drawn, in logical pixels: the whole board, or the close-up.
    int logicalWidth() const  { return closeUp() ? closeUpCols() * kTileSize : fullWidth(); }
    int logicalHeight() const { return closeUp() ? closeUpRows() * kTileSize : fullHeight(); }

    // Where the timer is, in logical pixels from the top left corner: the web
    // page keeps its Back button there and the timer right of it, level with it.
    void setTimerAt(int x, int y) {
        timerX_ = std::max(4, x);
        timerY_ = std::max(4, y);
    }

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
    int closeUpCols() const { return std::min(kViewCells, boardW_); }
    int closeUpRows() const {
        return std::clamp((kViewCells * boardH_ + boardW_ / 2) / boardW_, 1, boardH_);
    }
    void updateCamera(const Game& game);
    void drawTiles(const Game& game, int frameTick);
    void drawPlayer(const Game& game);
    void drawHud(const Game& game);
    void drawNumber(int value, int digits, int x, int y);
    void drawGlyph(int glyph, int x, int y);
    void blit(const Texture& t, const SDL_Rect* src, int x, int y);

    SDL_Renderer* r_ = nullptr;
    TextureCache tex_;
    ViewMode view_ = ViewMode::FullMap;
    bool closeUpAllowed_ = false;
    int boardW_ = Grid::kClassic;
    int boardH_ = Grid::kClassic;
    int camX_ = 0;
    int camY_ = 0;
    int timerX_ = 4;
    int timerY_ = 4;
};

}  // namespace bc::gfx
