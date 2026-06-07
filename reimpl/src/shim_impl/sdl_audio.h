#pragma once
// OPTIONAL real audio backend for IAudioDevice using SDL2's audio device API.
//
// This entire file is a no-op unless GUILD_HAVE_SDL2 is defined at compile time.
// The portable default (NullAudioDevice) produces no sound and needs no deps; the
// SdlAudioDevice here opens a real SDL2 output device (SDL_OpenAudioDevice) and
// mixes a table of active voices into the device callback on SDL's audio thread.
//
// ---------------------------------------------------------------------------
// How to enable / build (mirrors sdl2_backend)
// ---------------------------------------------------------------------------
//   g++ -std=c++17 -DGUILD_HAVE_SDL2 $(pkg-config --cflags sdl2)
//       src/shim_impl/sdl_audio.cpp ... $(pkg-config --libs sdl2)
//   Without -DGUILD_HAVE_SDL2 the .cpp compiles to nothing and no SDL symbols
//   are referenced, so the default build links cleanly with zero deps.
//
// ---------------------------------------------------------------------------
// Assumed sample format
// ---------------------------------------------------------------------------
// playSample() PCM is interpreted as interleaved SIGNED 16-BIT (AUDIO_S16SYS).
// Channel count is taken from the device's `channels` (1 = mono, 2 = stereo);
// the bytes/sampleRate args describe the supplied buffer. If the sample's rate
// differs from the device rate, it is linearly resampled to the device rate at
// playSample() time (nearest two-tap interpolation). The output device is always
// opened as AUDIO_S16SYS at the requested rate/channels (no SDL format/rate
// conversion is requested, so the callback sees exactly that format).
//
// The mixer core (MixInto) is a pure function of (voices, master, frames) -> PCM
// and is exposed for unit tests so golden PCM vectors can be asserted with no
// device present.
// ---------------------------------------------------------------------------
#ifdef GUILD_HAVE_SDL2

#include "shim/IAudioDevice.h"

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace guild::shim {

class SdlAudioDevice : public IAudioDevice {
public:
    SdlAudioDevice() = default;
    ~SdlAudioDevice() override { shutdown(); }

    // IAudioDevice
    bool init(int voices, int channels, int sampleRate) override;
    void shutdown() override;
    VoiceHandle allocVoice() override;
    void freeVoice(VoiceHandle) override;
    void playSample(VoiceHandle, const void* pcm, std::size_t bytes,
                    int sampleRate, int loops) override;
    void stop(VoiceHandle) override;
    void setVolume(VoiceHandle, int vol) override;   // 0..127
    void setPan(VoiceHandle, int pan) override;       // 0..127, 64 = center
    void setMasterVolume(int vol) override;

    // ---- pure, device-independent mixer core (unit-testable) ----
    // A single voice as seen by the mixer. `data` is interleaved S16 at the
    // device rate/channels; `pos`/`loop` describe playback state.
    struct MixVoice {
        const std::int16_t* data = nullptr; // device-rate, device-channel PCM
        std::size_t frames = 0;             // number of frames in `data`
        std::size_t pos = 0;                // current frame cursor
        int volume = 127;                   // 0..127
        int pan = 64;                       // 0..127, 64 = center (stereo only)
        bool loop = false;                  // repeat when reaching the end
        bool active = false;
    };

    // Mix `frames` frames of `channels`-channel audio from `voices` into `out`
    // (interleaved S16, length frames*channels), applying per-voice volume+pan
    // and `master` (0..127), clamping to int16. Advances each voice's `pos` and
    // deactivates non-looping voices that run out. Pure: no SDL, no locks.
    static void MixInto(MixVoice* voices, int voiceCount, int master,
                        std::int16_t* out, std::size_t frames, int channels);

    // Resample interleaved S16 `in` (inFrames at inRate) to `outRate`, keeping
    // `channels` interleaving. Returns the resampled buffer (outRate-frames).
    static std::vector<std::int16_t> Resample(const std::int16_t* in,
                                              std::size_t inFrames, int channels,
                                              int inRate, int outRate);

    // Test/inspection hooks (not part of the interface).
    int deviceRate() const { return rate_; }
    int deviceChannels() const { return channels_; }
    int masterVolume() const { return master_; }
    bool opened() const { return dev_ != 0; }
    int activeVoices();

    // Deterministic pull-mix for tests/headless: lock, run MixInto for `frames`,
    // unlock. Returns interleaved S16 (frames*channels). No SDL callback needed.
    std::vector<std::int16_t> pullMix(std::size_t frames);

private:
    struct Voice {
        std::vector<std::int16_t> pcm; // device-rate/channel interleaved S16
        std::size_t pos = 0;
        int volume = 127;
        int pan = 64;
        bool loop = false;
        bool active = false;
        bool allocated = false;
    };

    void lock();
    void unlock();
    // Build a MixVoice snapshot array from voices_ (caller holds the lock).
    int snapshot(std::vector<MixVoice>& out);
    // Write back updated cursors/active flags after MixInto (holds the lock).
    void commit(const std::vector<MixVoice>& snap);

    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);

    std::vector<Voice> voices_;
    SDL_AudioDeviceID dev_ = 0;
    int rate_ = 0;
    int channels_ = 0;
    int master_ = 127;
    bool inited_ = false;
};

} // namespace guild::shim

#endif // GUILD_HAVE_SDL2
