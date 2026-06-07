// End-to-end test for SdlAudioDevice: open a REAL SDL audio device (dummy driver,
// headless), play several samples on several voices, let the real audio callback
// run on SDL's audio thread, then tear everything down cleanly.
// Compiles in BOTH builds (trivial skip when GUILD_HAVE_SDL2 is undefined).
#include "test.h"

#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl_audio.h"

#include <SDL.h>
#include <cstdint>
#include <vector>

using namespace guild::shim;

TEST(SdlAudioE2E, OpenPlayPumpTeardown) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    SdlAudioDevice dev;
    CHECK(dev.init(/*voices*/ 8, /*channels*/ 2, /*rate*/ 44100));
    if (!dev.opened()) {
        // No audio at all in this environment: report and stop gracefully.
        CHECK(false);
        return;
    }
    CHECK_EQ(dev.deviceChannels(), 2);
    CHECK(dev.deviceRate() > 0);

    // Allocate and play several voices with different volume / pan / loop.
    std::vector<std::int16_t> a(2 * 200, 5000);  // loud stereo tone
    std::vector<std::int16_t> b(2 * 120, -3000); // quieter, panned right
    std::vector<std::int16_t> c(2 * 80, 2000);   // looping bed

    VoiceHandle v0 = dev.allocVoice();
    VoiceHandle v1 = dev.allocVoice();
    VoiceHandle v2 = dev.allocVoice();
    CHECK(v0 >= 0);
    CHECK(v1 >= 0);
    CHECK(v2 >= 0);

    dev.setMasterVolume(100);
    dev.setVolume(v0, 127);
    dev.setPan(v0, 64);
    dev.setVolume(v1, 80);
    dev.setPan(v1, 110);
    dev.setVolume(v2, 60);
    dev.setPan(v2, 30);

    dev.playSample(v0, a.data(), a.size() * sizeof(std::int16_t), 22050, 0);  // resampled
    dev.playSample(v1, b.data(), b.size() * sizeof(std::int16_t), 44100, 0);
    dev.playSample(v2, c.data(), c.size() * sizeof(std::int16_t), 44100, -1); // looping

    CHECK(dev.activeVoices() >= 1);

    // Let SDL's audio thread call the callback several times. On the dummy driver
    // the callback fires on a timer; give it real wall-clock to advance.
    for (int i = 0; i < 20; ++i)
        SDL_Delay(5);

    // The looping voice (v2) must still be active after the others may have ended.
    CHECK(dev.activeVoices() >= 1);

    // Stop everything and confirm the device is quiescent.
    dev.stop(v0);
    dev.stop(v1);
    dev.stop(v2);
    CHECK_EQ(dev.activeVoices(), 0);

    dev.freeVoice(v0);
    dev.freeVoice(v1);
    dev.freeVoice(v2);

    // Clean teardown: shutdown must close the device and quit the subsystem.
    dev.shutdown();
    CHECK(!dev.opened());

    // A second init/shutdown cycle must also succeed (no leaked state).
    CHECK(dev.init(2, 1, 22050));
    CHECK(dev.opened());
    dev.shutdown();
    CHECK(!dev.opened());
}

#else
TEST(SdlAudioE2E, SkippedNoBackend) { CHECK(true); }
#endif
