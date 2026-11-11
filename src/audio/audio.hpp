#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bc::audio {

// A tiny mixer on top of raw SDL audio.
//
// The original soundtrack ships as Standard MIDI plus Yamaha SMAF (.mmf),
// neither of which a browser can play. The MIDI tracks were rendered to Ogg
// Vorbis ahead of time; the two effects that only ever existed as SMAF
// (carrot pickup and locked door) are synthesised here instead, which keeps
// the asset pipeline free of a SMAF decoder nobody maintains.
class Mixer {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels = 2;
    static constexpr int kVoices = 8;

    bool init(const std::string& assetRoot);
    void shutdown();

    // Browsers refuse to start audio before a user gesture, so playback is
    // armed on the first key press or tap.
    void unlock();
    bool unlocked() const { return unlocked_; }

    void playSound(const std::string& name, float volume = 1.0f);
    // Loops the track. Asking for the one already playing changes only its
    // volume and keeps its place, so it can be asked for every frame.
    void playMusic(const std::string& name, float volume = 0.6f);
    void stopMusic();

    void setMuted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }
    void toggleMuted() { muted_ = !muted_; }

    // Volume of the music and of the sound effects, 0 (silent) to 1 (full).
    // Applied on top of the volume each clip is played with, and independent
    // of mute: unmuting brings back the volume set here.
    void setMusicVolume(float v);
    void setSoundVolume(float v);

private:
    struct Clip {
        std::vector<std::int16_t> pcm;  // interleaved stereo
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
