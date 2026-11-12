#pragma once

#include <SDL.h>

#include <algorithm>
#include <string>

#include "../core/game.hpp"
#include "texture.hpp"

namespace bc::gfx {

/**
 * @brief Full-board view or a scrolling close-up within the same window.
 *
 * @details FullMap uses one logical pixel per art pixel; Handheld shows fewer
 * cells at a larger scale while preserving the board aspect ratio.
 */
enum class ViewMode { FullMap, Handheld };

constexpr int kFullW     = Grid::kClassic * kTileSize;   ///< 512, a classic board
constexpr int kFullH     = Grid::kClassic * kTileSize;   ///< 512
constexpr int kViewCells = 10;

class Renderer {
public:
    bool init(SDL_Renderer* sdl, const std::string& assetRoot);

    void setViewMode(ViewMode m) { view_ = m; }
    ViewMode viewMode() const { return view_; }
    void toggleViewMode() { view_ = view_ == ViewMode::FullMap ? ViewMode::Handheld : ViewMode::FullMap; }

    /** @brief Set board dimensions in cells before updating the application layout. */
    void setBoard(int w, int h) { boardW_ = w; boardH_ = h; }
    int boardW() const { return boardW_; }
    int boardH() const { return boardH_; }

    /** @brief Allow close-up rendering during play; menus and paused dialogs use FullMap. */
    void setCloseUpAllowed(bool on) { closeUpAllowed_ = on; }
    bool closeUpAllowed() const { return closeUpAllowed_; }
    bool closeUp() const { return view_ == ViewMode::Handheld && closeUpAllowed_; }

    /** @brief Full-board dimensions in logical pixels, independent of the current view. */
    int fullWidth() const  { return boardW_ * kTileSize; }
    int fullHeight() const { return boardH_ * kTileSize; }
    /** @brief Visible viewport dimensions in logical pixels. */
    int logicalWidth() const  { return closeUp() ? closeUpCols() * kTileSize : fullWidth(); }
    int logicalHeight() const { return closeUp() ? closeUpRows() * kTileSize : fullHeight(); }

    /** @brief Position the timer beside the page Back button, in logical pixels. */
    void setTimerAt(int x, int y) {
        timerX_ = std::max(4, x);
        timerY_ = std::max(4, y);
    }

    void clear();
    void present();

    void drawGame(const Game& game, int frameTick);
    void drawTitle(int frameTick);
    void drawLevelSelect(Mode mode, int selected, int unlocked);
    /**
     * @brief Hit-test the level list in logical pixels.
     *
     * @return Zero-based level index, or -1 outside the grid.
     */
    int levelAt(Mode mode, int x, int y) const;
    void drawClearedBanner(int frameTick);
    void drawEndScreen();
    void drawTouchArrows(float alpha);

    /** @brief Return a touch arrow rectangle in logical viewport pixels. */
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
