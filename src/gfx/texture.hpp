#pragma once

#include <SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace bc::gfx {

struct Texture {
    SDL_Texture* handle = nullptr;
    int w = 0;
    int h = 0;
    explicit operator bool() const { return handle != nullptr; }
};

// Loads PNGs with stb_image so the build needs no SDL_image port: one less
// moving part in both the native and the Emscripten toolchain.
class TextureCache {
public:
    ~TextureCache();

    bool loadDirectory(SDL_Renderer* r, const std::string& dir,
                       const std::vector<std::string>& names);
    const Texture& get(const std::string& name) const;

private:
    bool load(SDL_Renderer* r, const std::string& name, const std::string& path);

    std::unordered_map<std::string, Texture> textures_;
    static const Texture kMissing;
};

}  // namespace bc::gfx
