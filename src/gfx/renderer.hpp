#pragma once

#include <SDL.h>

#include <string>

#include "../core/game.hpp"
#include "texture.hpp"

namespace bc::gfx {

// Two presentations of the same board: the whole 16x16 island at once, which
// suits a desktop window, and the original handset viewport that scrolls
// behind Bobby.
enum class ViewMode { FullMap, Handheld };

constexpr int kFullW     = Grid::W * kTileSize;   // 512
constexpr int kFullH     = Grid::H * kTileSize;   // 512
constexpr int kViewCells = 10;
constexpr int kViewRows  = 12;
constexpr int kHandheldW = kViewCells * kTileSize;  // 320
constexpr int kHandheldH = kViewRows * kTileSize;   // 384

class Renderer {
public:
    bool init(SDL_Renderer* sdl, const std::string& assetRoot);

    void setViewMode(ViewMode m) { view_ = m; }
    ViewMode viewMode() const { return view_; }
    void toggleViewMode() { view_ = view_ == ViewMode::FullMap ? ViewMode::Handheld : ViewMode::FullMap; }

    int logicalWidth() const  { return view_ == ViewMode::FullMap ? kFullW : kHandheldW; }
    int logicalHeight() const { return view_ == ViewMode::FullMap ? kFullH : kHandheldH; }

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
    int camX_ = 0;
    int camY_ = 0;
};

}  // namespace bc::gfx
