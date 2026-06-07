// Integration tests for SdlAudioDevice wired through the guild::audio facade
// (DigitalAudio -> shim::IAudioDevice) AND end-to-end through the device itself
// (allocVoice -> playSample -> pullMix -> stop). Headless via SDL_AUDIODRIVER=dummy.
// Compiles in BOTH builds (trivial skip when GUILD_HAVE_SDL2 is undefined).
#include "test.h"

#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl_audio.h"
#include "audio/digital_output.h"

#include <SDL.h>
#include <cstdint>
#include <vector>

using namespace guild;

namespace {
// Build a constant-amplitude mono S16 buffer of `frames` frames.
std::vector<std::int16_t> tone(std::size_t frames, std::int16_t amp) {
    return std::vector<std::int16_t>(frames, amp);
}
} // namespace

// The guild::audio facade opens a digital output that routes init/allocVoice to
// the SDL device, then plays a sample and pull-mixes it.
TEST(SdlAudioIntegration, FacadeOpensAndAllocates) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    shim::SdlAudioDevice dev;
    audio::DigitalAudio digi(&dev);
    digi.setDriverInstalled(true);

    // openDigitalOutput(channels=1, rate=22050, bits=16) -> device.init(...).
    int out = digi.openDigitalOutput(/*channels*/ 1, /*rate*/ 22050, /*bits*/ 16,
                                     /*maxHandles*/ 8);
    CHECK(out >= 0);
    CHECK(dev.opened());
    CHECK_EQ(dev.deviceChannels(), 1);

    // Allocate two sample handles through the facade bookkeeping.
    shim::VoiceHandle a = digi.allocateSampleHandle(out);
    shim::VoiceHandle b = digi.allocateSampleHandle(out);
    CHECK(a >= 0);
    CHECK(b >= 0);
    CHECK(a != b);

    audio::DigitalOutput* o = digi.outputAt(out);
    CHECK(o != nullptr);
    if (o) {
        CHECK_EQ(o->channels, (guild::u16)1);
        CHECK_EQ(o->sampleRate, (guild::u32)22050);
        CHECK_EQ(o->allocatedSampleCount, (guild::u32)2);
    }

    // Play a tone on the first handle and confirm the device mixes it.
    auto pcm = tone(64, 4000);
    dev.setVolume(a, 127);
    dev.playSample(a, pcm.data(), pcm.size() * sizeof(std::int16_t), 22050, /*loops*/ 0);
    auto mixed = dev.pullMix(32);
    bool nonzero = false;
    for (auto s : mixed) if (s != 0) { nonzero = true; break; }
    CHECK(nonzero);

    // Release through the facade, then shutdown.
    CHECK_EQ(digi.releaseSampleHandle(a), 0);
    dev.shutdown();
    CHECK(!dev.opened());
}

// End-to-end through the raw device: alloc -> play (with resample) -> mix -> stop.
TEST(SdlAudioIntegration, DeviceAllocPlayMixStop) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    shim::SdlAudioDevice dev;
    CHECK(dev.init(/*voices*/ 4, /*channels*/ 2, /*rate*/ 44100));
    if (!dev.opened()) { CHECK(false); return; }

    shim::VoiceHandle v = dev.allocVoice();
    CHECK(v >= 0);

    // Supply a stereo tone at HALF the device rate -> exercises resample path.
    std::vector<std::int16_t> pcm(2 * 50, 6000); // 50 stereo frames @ 22050
    dev.setVolume(v, 127);
    dev.setPan(v, 64);
    dev.playSample(v, pcm.data(), pcm.size() * sizeof(std::int16_t), 22050, 0);
    CHECK_EQ(dev.activeVoices(), 1);

    auto mixed = dev.pullMix(16); // 16 stereo frames
    CHECK_EQ(mixed.size(), (std::size_t)(16 * 2));
    bool nonzero = false;
    for (auto s : mixed) if (s != 0) { nonzero = true; break; }
    CHECK(nonzero);

    dev.stop(v);
    CHECK_EQ(dev.activeVoices(), 0);
    auto silent = dev.pullMix(8);
    bool allzero = true;
    for (auto s : silent) if (s != 0) { allzero = false; break; }
    CHECK(allzero);

    dev.freeVoice(v);
    dev.shutdown();
}

// Looping voice keeps producing audio across many pull-mix calls.
TEST(SdlAudioIntegration, LoopingVoiceSustains) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    shim::SdlAudioDevice dev;
    CHECK(dev.init(2, 1, 44100));
    if (!dev.opened()) { CHECK(false); return; }
    shim::VoiceHandle v = dev.allocVoice();
    CHECK(v >= 0);
    auto pcm = tone(8, 3000);
    dev.playSample(v, pcm.data(), pcm.size() * sizeof(std::int16_t), 44100, /*loops*/ -1);
    for (int i = 0; i < 5; ++i) {
        auto m = dev.pullMix(64); // far longer than the 8-frame sample
        bool nonzero = false;
        for (auto s : m) if (s != 0) { nonzero = true; break; }
        CHECK(nonzero); // still looping
    }
    CHECK_EQ(dev.activeVoices(), 1);
    dev.shutdown();
}

#else
TEST(SdlAudioIntegration, SkippedNoBackend) { CHECK(true); }
#endif
