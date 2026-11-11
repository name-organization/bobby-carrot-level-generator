#include "storage.hpp"

#include <cstdio>
#include <sstream>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace bc::platform {
namespace {

constexpr const char* kFileName = "bobby_save.txt";

int clampPercent(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

// v2 is v1 with the music and sound volumes after the best times. A v1 save
// (from before the volume settings) still loads, with full volume.
std::string serialise(const SaveData& d) {
    std::ostringstream os;
    os << "v2 " << d.unlockedNormal << ' ' << d.unlockedEgg << ' ' << (d.muted ? 1 : 0);
    for (int s : d.bestSeconds) os << ' ' << s;
    os << ' ' << d.musicVolume << ' ' << d.soundVolume;
    return os.str();
}

bool deserialise(const std::string& text, SaveData& d) {
    std::istringstream is(text);
    std::string tag;
    if (!(is >> tag) || (tag != "v1" && tag != "v2")) return false;
    int muted = 0;
    if (!(is >> d.unlockedNormal >> d.unlockedEgg >> muted)) return false;
    d.muted = muted != 0;
    for (int& s : d.bestSeconds) {
        if (!(is >> s)) return true;
    }
    int music = 0, sound = 0;
    if (tag == "v2" && is >> music >> sound) {
        d.musicVolume = clampPercent(music);
        d.soundVolume = clampPercent(sound);
    }
    return true;
}

}  // namespace

void saveProgress(const SaveData& data) {
    const std::string text = serialise(data);
#ifdef __EMSCRIPTEN__
    EM_ASM(
        {
            try {
                localStorage.setItem('bobbycarrot.save', UTF8ToString($0));
            } catch (e) {
                // Private mode or blocked storage: progress just will not stick.
            }
        },
        text.c_str());
#else
    if (std::FILE* f = std::fopen(kFileName, "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
#endif
}

bool loadProgress(SaveData& out) {
#ifdef __EMSCRIPTEN__
    // emscripten_run_script_string has been stable across every Emscripten
    // release, unlike the EM_ASM pointer-returning macros.
    const char* raw = emscripten_run_script_string(
        "(function(){ try { return localStorage.getItem('bobbycarrot.save') || ''; }"
        "catch (e) { return ''; } })()");
    if (!raw || !*raw) return false;
    return deserialise(std::string(raw), out);
#else
    std::FILE* f = std::fopen(kFileName, "rb");
    if (!f) return false;
    std::string text;
    char buf[512];
    while (std::size_t n = std::fread(buf, 1, sizeof(buf), f)) text.append(buf, n);
    std::fclose(f);
    return deserialise(text, out);
#endif
}

}  // namespace bc::platform
