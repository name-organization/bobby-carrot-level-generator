#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../third_party/stb_vorbis_wrapper.h"

namespace bc::audio {

bool Mixer::loadOgg(const std::string& name, const std::string& path) {
    int channels = 0, rate = 0;
    short* data = nullptr;
    const int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &rate, &data);
    if (frames <= 0 || !data) {
        std::fprintf(stderr, "[audio] cannot decode %s\n", path.c_str());
        return false;
    }

    // Nearest-neighbour resample to the device rate. The source files are
    // already 44.1 kHz, so in practice this is a straight copy; the branch
    // exists so a differently encoded asset cannot play back at the wrong
    // pitch.
    const double ratio = static_cast<double>(rate) / kSampleRate;
    const std::size_t outFrames =
        ratio == 1.0 ? static_cast<std::size_t>(frames)
                     : static_cast<std::size_t>(frames / ratio);

    Clip clip;
    clip.pcm.resize(outFrames * kChannels);

    for (std::size_t i = 0; i < outFrames; ++i) {
        const std::size_t src = ratio == 1.0 ? i : static_cast<std::size_t>(i * ratio);
        const std::size_t s = std::min(src, static_cast<std::size_t>(frames - 1));
        if (channels == 1) {
            clip.pcm[i * 2] = clip.pcm[i * 2 + 1] = data[s];
        } else {
            clip.pcm[i * 2]     = data[s * channels];
            clip.pcm[i * 2 + 1] = data[s * channels + 1];
        }
    }
    std::free(data);

    clips_[name] = std::move(clip);
    return true;
}

// Short swept sine with an exponential decay -- enough to stand in for the
// handset blips the SMAF files held.
void Mixer::synthesise(const std::string& name, float freqStart, float freqEnd,
                       float seconds, float gain) {
    const std::size_t frames = static_cast<std::size_t>(kSampleRate * seconds);
    Clip clip;
    clip.pcm.resize(frames * kChannels);

    double phase = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / frames;
        const double freq = freqStart + (freqEnd - freqStart) * t;
        phase += 2.0 * M_PI * freq / kSampleRate;
        const double env = std::exp(-4.0 * t);
        const double s = std::sin(phase) * env * gain;
        const auto v = static_cast<std::int16_t>(std::clamp(s, -1.0, 1.0) * 26000);
        clip.pcm[i * 2] = clip.pcm[i * 2 + 1] = v;
    }
    clips_[name] = std::move(clip);
}

bool Mixer::init(const std::string& assetRoot) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "[audio] no audio subsystem: %s\n", SDL_GetError());
        return false;
    }

    const std::string dir = assetRoot + "/audio/";
    for (const char* name : {"title", "cleared", "death", "end"}) {
        loadOgg(name, dir + name + ".ogg");
    }
    synthesise("carrot", 900.0f, 1500.0f, 0.10f, 0.55f);
    synthesise("lock",   320.0f, 180.0f,  0.14f, 0.50f);
    synthesise("key",    700.0f, 1200.0f, 0.12f, 0.45f);
    synthesise("switch", 420.0f, 640.0f,  0.09f, 0.45f);

    SDL_AudioSpec want{};
    want.freq = kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = kChannels;
    want.samples = 1024;
    want.callback = &Mixer::audioCallback;
    want.userdata = this;

    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        std::fprintf(stderr, "[audio] cannot open device: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

void Mixer::shutdown() {
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

void Mixer::unlock() {
    if (unlocked_ || device_ == 0) return;
    SDL_PauseAudioDevice(device_, 0);
    unlocked_ = true;
}

void Mixer::playSound(const std::string& name, float volume) {
    auto it = clips_.find(name);
    if (it == clips_.end() || device_ == 0) return;

    SDL_LockAudioDevice(device_);
    Voice* slot = nullptr;
    for (Voice& v : voices_) {
        if (!v.active) { slot = &v; break; }
    }
    if (!slot) slot = &voices_[0];  // steal the oldest-ish voice
    *slot = Voice{&it->second, 0, volume, false, true};
    SDL_UnlockAudioDevice(device_);
}

void Mixer::playMusic(const std::string& name, float volume) {
    auto it = clips_.find(name);
    if (device_ == 0) return;

    SDL_LockAudioDevice(device_);
    if (it == clips_.end()) {
        music_ = Voice{};
    } else if (music_.active && music_.clip == &it->second) {
        music_.volume = volume;
    } else {
        music_ = Voice{&it->second, 0, volume, true, true};
    }
    SDL_UnlockAudioDevice(device_);
}

void Mixer::stopMusic() {
    if (device_ == 0) return;
    SDL_LockAudioDevice(device_);
    music_ = Voice{};
    SDL_UnlockAudioDevice(device_);
}

namespace {
// Ears hear loudness roughly logarithmically: squaring the slider value makes
// its steps sound about even (50% is about -12 dB) instead of crowding all the
// change into the bottom of the range.
float sliderToGain(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v * v;
}
}  // namespace

void Mixer::setMusicVolume(float v) {
    if (device_) SDL_LockAudioDevice(device_);
    musicGain_ = sliderToGain(v);
    if (device_) SDL_UnlockAudioDevice(device_);
}

void Mixer::setSoundVolume(float v) {
    if (device_) SDL_LockAudioDevice(device_);
    soundGain_ = sliderToGain(v);
    if (device_) SDL_UnlockAudioDevice(device_);
}

void Mixer::mix(std::int16_t* out, int frames) {
    std::fill(out, out + static_cast<std::size_t>(frames) * kChannels, 0);
    if (muted_) return;

    auto render = [&](Voice& v, float gain) {
        if (!v.active || !v.clip) return;
        const float volume = v.volume * gain;
        const auto& pcm = v.clip->pcm;
        for (int i = 0; i < frames; ++i) {
            if (v.pos >= pcm.size()) {
                if (!v.loop) { v.active = false; return; }
                v.pos = 0;
            }
            for (int c = 0; c < kChannels; ++c) {
                const int idx = i * kChannels + c;
                const int mixed = out[idx] + static_cast<int>(pcm[v.pos + c] * volume);
                out[idx] = static_cast<std::int16_t>(std::clamp(mixed, -32768, 32767));
            }
            v.pos += kChannels;
        }
    };

    render(music_, musicGain_);
    for (Voice& v : voices_) render(v, soundGain_);
}

void SDLCALL Mixer::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<Mixer*>(userdata);
    self->mix(reinterpret_cast<std::int16_t*>(stream),
              len / static_cast<int>(sizeof(std::int16_t) * kChannels));
}

}  // namespace bc::audio
