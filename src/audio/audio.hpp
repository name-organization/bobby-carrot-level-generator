#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bc::audio {

/**
 * @brief SDL audio mixer for Ogg music and synthesized effects.
 *
 * @details Carrot and lock effects replace the original SMAF clips, which
 * are not decoded at runtime.
 */
class Mixer {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels = 2;
    static constexpr int kVoices = 8;

    bool init(const std::string& assetRoot);
    void shutdown();

    /** @brief Start playback after the first user gesture, as required by browsers. */
    void unlock();
    bool unlocked() const { return unlocked_; }

    void playSound(const std::string& name, float volume = 1.0f);
    /** @brief Loop a music track; requesting the current track preserves playback position. */
    void playMusic(const std::string& name, float volume = 0.6f);
    void stopMusic();

    void setMuted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }
    void toggleMuted() { muted_ = !muted_; }

    /** @brief Set music and effect slider levels in [0, 1], independently of mute. */
    void setMusicVolume(float v);
    void setSoundVolume(float v);

private:
    struct Clip {
        std::vector<std::int16_t> pcm;  ///< interleaved stereo
    };
    struct Voice {
        const Clip* clip = nullptr;
        std::size_t pos = 0;
        float volume = 1.0f;
        bool loop = false;
        bool active = false;
    };

    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);
    void mix(std::int16_t* out, int frames);

    bool loadOgg(const std::string& name, const std::string& path);
    void synthesise(const std::string& name, float freqStart, float freqEnd,
                    float seconds, float gain);

    std::unordered_map<std::string, Clip> clips_;
    Voice voices_[kVoices];
    Voice music_;
    SDL_AudioDeviceID device_ = 0;
    bool unlocked_ = false;
    bool muted_ = false;
    float musicGain_ = 1.0f;
    float soundGain_ = 1.0f;
};

}  // namespace bc::audio
