// Unit tests for SdlAudioDevice's PURE mixer core (MixInto / Resample): golden
// PCM vectors for volume, pan, master, clamping, looping, and resampling. No SDL
// audio device is opened — these exercise device-independent math only.
// Compiles in BOTH builds (trivial skip when GUILD_HAVE_SDL2 is undefined).
#include "test.h"

#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl_audio.h"
#include <cstdint>
#include <vector>

using namespace guild::shim;
using MixVoice = SdlAudioDevice::MixVoice;

namespace {
MixVoice makeVoice(const std::int16_t* data, std::size_t frames, int vol, int pan, bool loop) {
    MixVoice v;
    v.data = data;
    v.frames = frames;
    v.pos = 0;
    v.volume = vol;
    v.pan = pan;
    v.loop = loop;
    v.active = true;
    return v;
}
} // namespace

// Full volume, mono, master 127: output equals input exactly.
TEST(SdlAudioMixer, MonoFullVolumePassthrough) {
    std::int16_t src[4] = {100, -200, 300, -400};
    MixVoice v = makeVoice(src, 4, 127, 64, false);
    std::int16_t out[4] = {7, 7, 7, 7};
    SdlAudioDevice::MixInto(&v, 1, 127, out, 4, 1);
    CHECK_EQ(out[0], (std::int16_t)100);
    CHECK_EQ(out[1], (std::int16_t)-200);
    CHECK_EQ(out[2], (std::int16_t)300);
    CHECK_EQ(out[3], (std::int16_t)-400);
    CHECK_EQ(v.pos, (std::size_t)4);
    CHECK(!v.active); // ran out, not looping
}

// Half volume scales by vol/127 (truncating toward zero).
TEST(SdlAudioMixer, VolumeScale) {
    std::int16_t src[2] = {1000, -1000};
    MixVoice v = makeVoice(src, 2, 64, 64, false); // 64/127
    std::int16_t out[2] = {0, 0};
    SdlAudioDevice::MixInto(&v, 1, 127, out, 2, 1);
    CHECK_EQ(out[0], (std::int16_t)(1000 * 64 / 127)); // 503
    CHECK_EQ(out[1], (std::int16_t)(-1000 * 64 / 127)); // -503
}

// Master volume multiplies on top of per-voice volume.
TEST(SdlAudioMixer, MasterScale) {
    std::int16_t src[1] = {1000};
    MixVoice v = makeVoice(src, 1, 127, 64, false);
    std::int16_t out[1] = {0};
    SdlAudioDevice::MixInto(&v, 1, 64, out, 1, 1); // master 64/127
    CHECK_EQ(out[0], (std::int16_t)(1000 * 64 / 127)); // 503
}

// Clamping: two loud voices summing past int16 max clamp to 32767/-32768.
TEST(SdlAudioMixer, ClampOnSum) {
    std::int16_t a[1] = {30000};
    std::int16_t b[1] = {30000};
    std::int16_t c[1] = {-30000};
    std::int16_t d[1] = {-30000};
    MixVoice vs[2] = {makeVoice(a, 1, 127, 64, false), makeVoice(b, 1, 127, 64, false)};
    std::int16_t out[1] = {0};
    SdlAudioDevice::MixInto(vs, 2, 127, out, 1, 1);
    CHECK_EQ(out[0], (std::int16_t)32767); // clamped high

    MixVoice vn[2] = {makeVoice(c, 1, 127, 64, false), makeVoice(d, 1, 127, 64, false)};
    std::int16_t outn[1] = {0};
    SdlAudioDevice::MixInto(vn, 2, 127, outn, 1, 1);
    CHECK_EQ(outn[0], (std::int16_t)-32768); // clamped low
}

// Stereo pan: hard left (pan=0) silences right channel, doubles left.
TEST(SdlAudioMixer, PanHardLeft) {
    std::int16_t src[2] = {5000, 5000}; // one stereo frame {L,R}
    MixVoice v = makeVoice(src, 1, 127, 0, false); // pan 0 = hard left
    std::int16_t out[2] = {0, 0};
    SdlAudioDevice::MixInto(&v, 1, 127, out, 1, 2);
    // panL = (127-0)*2 clamped to 127 -> full; panR = 0*2 = 0 -> silent.
    CHECK_EQ(out[0], (std::int16_t)5000);
    CHECK_EQ(out[1], (std::int16_t)0);
}

// Stereo pan: hard right (pan=127) silences left.
TEST(SdlAudioMixer, PanHardRight) {
    std::int16_t src[2] = {5000, 5000};
    MixVoice v = makeVoice(src, 1, 127, 127, false);
    std::int16_t out[2] = {0, 0};
    SdlAudioDevice::MixInto(&v, 1, 127, out, 1, 2);
    CHECK_EQ(out[0], (std::int16_t)0);
    CHECK_EQ(out[1], (std::int16_t)5000);
}

// Stereo pan: center (pan=64) passes both channels near unity.
TEST(SdlAudioMixer, PanCenter) {
    std::int16_t src[2] = {4000, 4000};
    MixVoice v = makeVoice(src, 1, 127, 64, false);
    std::int16_t out[2] = {0, 0};
    SdlAudioDevice::MixInto(&v, 1, 127, out, 1, 2);
    // panL = (127-64)*2 = 126; panR = 64*2 = 128 clamped to 127.
    CHECK_EQ(out[0], (std::int16_t)(4000 * 126 / 127)); // 3968
    CHECK_EQ(out[1], (std::int16_t)4000);               // pan clamped to 127
}

// Looping: a 2-frame voice fills a 5-frame buffer by repeating, stays active.
TEST(SdlAudioMixer, LoopRepeats) {
    std::int16_t src[2] = {10, 20};
    MixVoice v = makeVoice(src, 2, 127, 64, true);
    std::int16_t out[5] = {0, 0, 0, 0, 0};
    SdlAudioDevice::MixInto(&v, 1, 127, out, 5, 1);
    CHECK_EQ(out[0], (std::int16_t)10);
    CHECK_EQ(out[1], (std::int16_t)20);
    CHECK_EQ(out[2], (std::int16_t)10);
    CHECK_EQ(out[3], (std::int16_t)20);
    CHECK_EQ(out[4], (std::int16_t)10);
    CHECK(v.active); // still looping
}

// Non-looping voice that runs out mid-buffer leaves the tail silent.
TEST(SdlAudioMixer, OnceStopsMidBuffer) {
    std::int16_t src[2] = {77, 88};
    MixVoice v = makeVoice(src, 2, 127, 64, false);
    std::int16_t out[4] = {1, 1, 1, 1};
    SdlAudioDevice::MixInto(&v, 1, 127, out, 4, 1);
    CHECK_EQ(out[0], (std::int16_t)77);
    CHECK_EQ(out[1], (std::int16_t)88);
    CHECK_EQ(out[2], (std::int16_t)0); // past end -> silence
    CHECK_EQ(out[3], (std::int16_t)0);
    CHECK(!v.active);
}

// Inactive voices contribute nothing.
TEST(SdlAudioMixer, InactiveSilent) {
    std::int16_t src[2] = {1000, 2000};
    MixVoice v = makeVoice(src, 2, 127, 64, false);
    v.active = false;
    std::int16_t out[2] = {0, 0};
    SdlAudioDevice::MixInto(&v, 1, 127, out, 2, 1);
    CHECK_EQ(out[0], (std::int16_t)0);
    CHECK_EQ(out[1], (std::int16_t)0);
}

// Resample identity: equal rates returns the input verbatim.
TEST(SdlAudioResample, IdentityWhenRatesEqual) {
    std::int16_t in[4] = {1, 2, 3, 4};
    auto out = SdlAudioDevice::Resample(in, 4, 1, 44100, 44100);
    CHECK_EQ(out.size(), (std::size_t)4);
    CHECK_EQ(out[0], (std::int16_t)1);
    CHECK_EQ(out[3], (std::int16_t)4);
}

// Upsample 2x doubles the frame count; endpoints preserved, midpoints interpolate.
TEST(SdlAudioResample, Upsample2x) {
    std::int16_t in[2] = {0, 1000}; // mono, 2 frames at 22050
    auto out = SdlAudioDevice::Resample(in, 2, 1, 22050, 44100);
    CHECK_EQ(out.size(), (std::size_t)4); // round(2*44100/22050)=4
    CHECK_EQ(out[0], (std::int16_t)0);    // exact sample 0
    // step = 22050/44100 = 0.5; out[1] at srcPos 0.5 -> 0 + (1000-0)*0.5 = 500
    CHECK_EQ(out[1], (std::int16_t)500);
    CHECK_EQ(out[2], (std::int16_t)1000); // exact sample 1
}

// Downsample halves the frame count.
TEST(SdlAudioResample, DownsampleHalf) {
    std::int16_t in[4] = {0, 100, 200, 300};
    auto out = SdlAudioDevice::Resample(in, 4, 1, 44100, 22050);
    CHECK_EQ(out.size(), (std::size_t)2); // round(4*22050/44100)=2
    CHECK_EQ(out[0], (std::int16_t)0);
    CHECK_EQ(out[1], (std::int16_t)200); // srcPos 2.0
}

#else
TEST(SdlAudioMixer, SkippedNoBackend) { CHECK(true); }
#endif
