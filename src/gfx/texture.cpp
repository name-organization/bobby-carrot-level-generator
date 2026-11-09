#include "texture.hpp"

#include <cstdio>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"

namespace bc::gfx {

const Texture TextureCache::kMissing{};

TextureCache::~TextureCache() {
    for (auto& [name, tex] : textures_) {
        if (tex.handle) SDL_DestroyTexture(tex.handle);
    }
}

bool TextureCache::load(SDL_Renderer* r, const std::string& name, const std::string& path) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        std::fprintf(stderr, "[gfx] cannot load %s: %s\n", path.c_str(), stbi_failure_reason());
        return false;
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, w * 4,
                                                           SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        stbi_image_free(pixels);
        return false;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    stbi_image_free(pixels);
    if (!tex) return false;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    textures_[name] = Texture{tex, w, h};
    return true;
}

bool TextureCache::loadDirectory(SDL_Renderer* r, const std::string& dir,
                                 const std::vector<std::string>& names) {
    bool ok = true;
    for (const auto& n : names) {
        if (!load(r, n, dir + "/" + n + ".png")) ok = false;
    }
    return ok;
}

const Texture& TextureCache::get(const std::string& name) const {
    auto it = textures_.find(name);
    return it == textures_.end() ? kMissing : it->second;
}

}  // namespace bc::gfx
