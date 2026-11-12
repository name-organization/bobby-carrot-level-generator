#include "renderer.hpp"

#include "hint_overlay.hpp"

#include <algorithm>
#include <cmath>

namespace bc::gfx {
namespace {

constexpr int kWalkW = 36, kWalkH = 50, kWalkFrames = 8;
constexpr int kIdleFrames = 3;
constexpr int kFadeFrames = 9;
constexpr int kDeathW = 44, kDeathH = 54, kDeathFrames = 8;
constexpr int kGlyphW = 12, kGlyphH = 18;
constexpr int kGlyphColon = 10;

const char* walkTexture(Dir d) {
    switch (d) {
        case Dir::Left:  return "bobby_left";
        case Dir::Right: return "bobby_right";
        case Dir::Up:    return "bobby_up";
        default:         return "bobby_down";
    }
}

const char* conveyorTexture(std::uint8_t tile) {
    switch (tile) {
        case T_CONVEYOR_L: return "tile_conveyor_left";
        case T_CONVEYOR_R: return "tile_conveyor_right";
        case T_CONVEYOR_U: return "tile_conveyor_up";
        default:           return "tile_conveyor_down";
    }
}

}  // namespace

bool Renderer::init(SDL_Renderer* sdl, const std::string& assetRoot) {
    r_ = sdl;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest, keep it crisp

    return tex_.loadDirectory(r_, assetRoot + "/image",
                              {"tileset", "hud", "numbers", "bobby_idle", "bobby_left",
                               "bobby_right", "bobby_up", "bobby_down", "bobby_fade",
                               "bobby_death", "tile_finish", "tile_conveyor_left",
                               "tile_conveyor_right", "tile_conveyor_up",
                               "tile_conveyor_down", "cleared", "title", "logo", "end",
                               "arrow_l", "arrow_r", "arrow_u", "arrow_d", "BobbyCarrot"});
}

void Renderer::clear() {
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
}

void Renderer::present() { SDL_RenderPresent(r_); }

void Renderer::blit(const Texture& t, const SDL_Rect* src, int x, int y) {
    if (!t) return;
    SDL_Rect dst{x, y, src ? src->w : t.w, src ? src->h : t.h};
    SDL_RenderCopy(r_, t.handle, src, &dst);
}

void Renderer::updateCamera(const Game& game) {
    if (!closeUp()) {
        camX_ = camY_ = 0;
        return;
    }
    const int px = static_cast<int>(game.player().fx() * kTileSize);
    const int py = static_cast<int>(game.player().fy() * kTileSize);
    const Grid& g = game.grid();
    const int viewW = logicalWidth();
    const int viewH = logicalHeight();
    camX_ = std::clamp(px - viewW / 2 + kTileSize / 2, 0, std::max(0, g.w * kTileSize - viewW));
    camY_ = std::clamp(py - viewH / 2 + kTileSize / 2, 0, std::max(0, g.h * kTileSize - viewH));
}

void Renderer::drawTiles(const Game& game, int frameTick) {
    const Texture& tileset = tex_.get("tileset");
    const int animFrame = (frameTick * 16 / kTicksPerSecond) % 4;  // 4 frames at 16 fps
    const bool open = game.allCollected();

    for (int y = 0; y < game.grid().h; ++y) {
        for (int x = 0; x < game.grid().w; ++x) {
            const std::uint8_t tile = game.grid().at(x, y);
            const int dx = x * kTileSize - camX_;
            const int dy = y * kTileSize - camY_;
            if (dx <= -kTileSize || dy <= -kTileSize) continue;
            if (dx >= logicalWidth() || dy >= logicalHeight()) continue;

            if (isConveyor(tile)) {
                SDL_Rect src{animFrame * kTileSize, 0, kTileSize, kTileSize};
                blit(tex_.get(conveyorTexture(tile)), &src, dx, dy);
            } else if (tile == T_EXIT && open) {
                SDL_Rect src{animFrame * kTileSize, 0, kTileSize, kTileSize};
                blit(tex_.get("tile_finish"), &src, dx, dy);
            } else {
                SDL_Rect src{(tile % 8) * kTileSize, (tile / 8) * kTileSize, kTileSize,
                             kTileSize};
                blit(tileset, &src, dx, dy);
            }
        }
    }
}

void Renderer::drawPlayer(const Game& game) {
    const Player& p = game.player();
    const float fx = p.fx();
    const float fy = p.fy();

    if (p.state() == Player::State::Death) {
        const int frame = std::min(kDeathFrames - 1, p.stateTick() * kDeathFrames / kFadeTicks);
        SDL_Rect src{frame * kDeathW, 0, kDeathW, kDeathH};
        const int x = static_cast<int>(kTileSize * (fx + 0.5f)) - kDeathW / 2 - camX_;
        const int y = static_cast<int>(kTileSize * (fy + 0.5f)) - (kDeathH - kTileSize / 2) - camY_;
        blit(tex_.get("bobby_death"), &src, x, y);
        return;
    }

    const int x = static_cast<int>(kTileSize * (fx + 0.5f)) - kWalkW / 2 - camX_;
    const int y = static_cast<int>(kTileSize * (fy + 0.5f)) - (kWalkH - kTileSize / 2) - camY_;

    const char* texName = walkTexture(p.facing());
    int frame = 0;

    switch (p.state()) {
        case Player::State::FadeIn:
            texName = "bobby_fade";
            frame = kFadeFrames - 1 -
                    std::min(kFadeFrames - 1, p.stateTick() * kFadeFrames / kFadeTicks);
            break;
        case Player::State::FadeOut:
            texName = "bobby_fade";
            frame = std::min(kFadeFrames - 1, p.stateTick() * kFadeFrames / kFadeTicks);
            break;
        case Player::State::Idle:
            texName = "bobby_idle";
            frame = (p.animTick() / 4) % kIdleFrames;  // 15 fps
            break;
        case Player::State::Walk:
            // The original holds the final walk frame when facing an obstacle.
            frame = p.isMoving() ? (p.animTick() / 2) % kWalkFrames : kWalkFrames - 1;
            break;
        default:
            break;
    }

    SDL_Rect src{frame * kWalkW, 0, kWalkW, kWalkH};
    blit(tex_.get(texName), &src, x, y);
}

void Renderer::drawGlyph(int glyph, int x, int y) {
    SDL_Rect src{glyph * kGlyphW, 0, kGlyphW, kGlyphH};
    blit(tex_.get("numbers"), &src, x, y);
}

void Renderer::drawNumber(int value, int digits, int x, int y) {
    value = std::clamp(value, 0, 99);
    for (int i = digits - 1; i >= 0; --i) {
        drawGlyph(value % 10, x + i * kGlyphW, y);
        value /= 10;
    }
}

void Renderer::drawHud(const Game& game) {
    const Texture& hud = tex_.get("hud");
    const int viewW = logicalWidth();

    const bool carrots = game.usesCarrots();
    const int iconW = carrots ? 46 : 34;
    SDL_Rect iconSrc{carrots ? 0 : 46, 0, iconW, 44};
    const int iconX = viewW - iconW - 4;
    blit(hud, &iconSrc, iconX, 4);
    drawNumber(game.remaining(), 2, iconX - 27, 18);

    int slot = 0;
    const int counts[3] = {game.progress().inv.gray, game.progress().inv.yellow,
                           game.progress().inv.red};
    for (int i = 0; i < 3; ++i) {
        if (counts[i] <= 0) continue;
        SDL_Rect src{122 + i * 22, 0, 22, 44};
        blit(hud, &src, viewW - 22 - 4 - slot * 22, 50);
        ++slot;
    }

    const int secs = game.elapsedSeconds();
    int m = std::min(99, secs / 60);
    int s = secs / 60 > 99 ? 99 : secs % 60;
    const int glyphs[5] = {m / 10, m % 10, kGlyphColon, s / 10, s % 10};
    for (int i = 0; i < 5; ++i) drawGlyph(glyphs[i], timerX_ + i * kGlyphW, timerY_);
}

void Renderer::drawGame(const Game& game, int frameTick) {
    updateCamera(game);
    drawTiles(game, frameTick);
    drawHintOverlay(r_, game, camX_, camY_);
    drawPlayer(game);
    drawHud(game);
}

void Renderer::drawTitle(int frameTick) {
    const Texture& title = tex_.get("title");
    const int cx = (logicalWidth() - title.w) / 2;
    blit(title, nullptr, cx, logicalHeight() / 2 - title.h - 8);

    const Texture& logo = tex_.get("logo");
    blit(logo, nullptr, (logicalWidth() - logo.w) / 2, logicalHeight() / 2 + 16);

    // The original assets have no font; reuse arrow art for the blinking prompt.
    if ((frameTick / 30) % 2 == 0) {
        const Texture& a = tex_.get("arrow_d");
        blit(a, nullptr, (logicalWidth() - a.w) / 2, logicalHeight() / 2 + 72);
    }
}

// Share level-list geometry between drawing and hit testing.
namespace {
constexpr int kSelectCols = 6;
constexpr int kSelectCellW = 44, kSelectCellH = 40;
constexpr int kSelectOriginY = 90;
}  // namespace

int Renderer::levelAt(Mode mode, int x, int y) const {
    const int originX = (logicalWidth() - kSelectCols * kSelectCellW) / 2;
    // Center hit boxes on the two-digit labels and tile them without gaps.
    const int left = originX + kGlyphW - kSelectCellW / 2;
    const int top = kSelectOriginY + kGlyphH / 2 - kSelectCellH / 2;
    if (x < left || y < top) return -1;
    const int col = (x - left) / kSelectCellW;
    const int row = (y - top) / kSelectCellH;
    if (col >= kSelectCols) return -1;
    const int index = row * kSelectCols + col;
    return index < Level::levelCount(mode) ? index : -1;
}

void Renderer::drawLevelSelect(Mode mode, int selected, int unlocked) {
    const int count = Level::levelCount(mode);
    const int cols = kSelectCols;
    const int cellW = kSelectCellW, cellH = kSelectCellH;
    const int gridW = cols * cellW;
    const int originX = (logicalWidth() - gridW) / 2;
    const int originY = kSelectOriginY;

    const Texture& hud = tex_.get("hud");
    SDL_Rect iconSrc{mode == Mode::Normal ? 0 : 46, 0, mode == Mode::Normal ? 46 : 34, 44};
    blit(hud, &iconSrc, (logicalWidth() - iconSrc.w) / 2, 20);

    const Texture& numbers = tex_.get("numbers");
    for (int i = 0; i < count; ++i) {
        const int x = originX + (i % cols) * cellW;
        const int y = originY + (i / cols) * cellH;
        const bool locked = i >= unlocked;

        if (i == selected) {
            SDL_SetRenderDrawColor(r_, 255, 236, 120, 190);
            SDL_Rect box{x - 3, y - 4, kGlyphW * 2 + 6, kGlyphH + 8};
            SDL_RenderFillRect(r_, &box);
        }
        SDL_SetTextureAlphaMod(numbers.handle, locked ? 70 : 255);
        drawNumber(i + 1, 2, x, y);
        SDL_SetTextureAlphaMod(numbers.handle, 255);
    }
}

void Renderer::drawClearedBanner(int frameTick) {
    const Texture& t = tex_.get("cleared");
    const int bob = static_cast<int>(3.0 * std::sin(frameTick * 0.12));
    blit(t, nullptr, (logicalWidth() - t.w) / 2, logicalHeight() / 3 + bob);
}

void Renderer::drawEndScreen() {
    const Texture& t = tex_.get("end");
    blit(t, nullptr, (logicalWidth() - t.w) / 2, (logicalHeight() - t.h) / 2);
}

SDL_Rect Renderer::arrowRect(Dir d) const {
    const int pad = 14;
    const int size = 44;
    const int cx = logicalWidth() - pad - size * 2;      // cluster origin x
    const int cy = logicalHeight() - pad - size * 3;     // cluster origin y
    switch (d) {
        case Dir::Up:    return SDL_Rect{cx + size, cy, size, size};
        case Dir::Left:  return SDL_Rect{cx, cy + size, size, size};
        case Dir::Right: return SDL_Rect{cx + size * 2, cy + size, size, size};
        case Dir::Down:  return SDL_Rect{cx + size, cy + size * 2, size, size};
        default:         return SDL_Rect{0, 0, 0, 0};
    }
}

void Renderer::drawTouchArrows(float alpha) {
    if (alpha <= 0.01f) return;
    const Uint8 a = static_cast<Uint8>(std::clamp(alpha, 0.0f, 1.0f) * 255);

    const struct { Dir d; const char* tex; } arrows[4] = {
        {Dir::Up, "arrow_u"}, {Dir::Down, "arrow_d"},
        {Dir::Left, "arrow_l"}, {Dir::Right, "arrow_r"},
    };

    for (const auto& entry : arrows) {
        const SDL_Rect box = arrowRect(entry.d);
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r_, 0, 0, 0, static_cast<Uint8>(a * 0.35f));
        SDL_RenderFillRect(r_, &box);

        const Texture& t = tex_.get(entry.tex);
        if (!t) continue;
        SDL_SetTextureAlphaMod(t.handle, a);
        blit(t, nullptr, box.x + (box.w - t.w) / 2, box.y + (box.h - t.h) / 2);
        SDL_SetTextureAlphaMod(t.handle, 255);
    }
}

}  // namespace bc::gfx
