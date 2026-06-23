// Unit tests for guild::audio self-contained leaves (src/audio/audio_leaves.*).
#include "audio/audio_leaves.h"
#include "shim/IAudioDevice.h"
#include "test.h"

#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

// Minimal mock device: hands out incrementing voice handles, records frees.
class MockDevice : public shim::IAudioDevice {
public:
    bool init(int, int, int) override { return ok_; }
    void shutdown() override {}
    shim::VoiceHandle allocVoice() override { return next_++; }
    void freeVoice(shim::VoiceHandle) override {}
    void playSample(shim::VoiceHandle, const void*, std::size_t, int, int) override {}
    void stop(shim::VoiceHandle) override {}
    void setVolume(shim::VoiceHandle, int) override {}
    void setPan(shim::VoiceHandle, int) override {}
    void setMasterVolume(int) override {}
    bool ok_ = true;
    shim::VoiceHandle next_ = 1;
};

// Build a DigitalAudio with one open output of `cap` handles, and allocate
// `nAlloc` sample handles + `nStream` stream handles into it.
DigitalAudio makeDriver(MockDevice& dev, int cap, int nAlloc, int nStream,
                        int* outIdx) {
    DigitalAudio d(&dev);
    d.setDriverInstalled(true);
    int idx = d.openDigitalOutput(2, 44100, 16, cap);
    *outIdx = idx;
    for (int i = 0; i < nAlloc; ++i)
        d.allocateSampleHandle(idx);
    for (int i = 0; i < nStream; ++i)
        d.openStream(idx);
    return d;
}

} // namespace

TEST(AudioLeaves, ClampDigitalMasterVolume) {
    CHECK_EQ(ClampDigitalMasterVolume(0), 0);
    CHECK_EQ(ClampDigitalMasterVolume(127), 127);
    CHECK_EQ(ClampDigitalMasterVolume(64), 64);
    CHECK_EQ(ClampDigitalMasterVolume(128), -1);   // a2 >= 0x80
    CHECK_EQ(ClampDigitalMasterVolume(200), -1);
    CHECK_EQ(ClampDigitalMasterVolume(-1), -1);    // unsigned wrap -> >= 0x80
}

TEST(AudioLeaves, ApplyVolumeSettingsGolden) {
    // Golden corrected to the binary (disasm @0x56c148, reference of record):
    //   v6     = (float)(soundByte * scale0)          // fstp var_C (32-bit float)
    //   master = trunc(musicByte * v6)                // fild byte_1233551, fmul var_C
    //   sfx    = trunc(sfxByte   * v6)                // fild byte_1233552, fmul var_C
    //   music  = trunc(musicByte * v6)                // fild byte_1233551, fmul var_C
    //   amb    = ambByte  * scale0 ; amb2 = amb2Byte * scale1
    // (Hex-Rays mislabelled the operands; master & music both use musicByte, and
    //  soundByte only feeds v6 — the prior goldens encoded the wrong master.)
    struct C { u8 s, m, sf, a, a2; float sc0, sc1; int em, esfx, emus; float eamb, eamb2; };
    const C cases[] = {
        {200,128,255,100,50,0.5f,0.8f, 12800,25500,12800, 50.0f,40.0f},
        {255,255,255,255,255,1.0f,1.0f, 65025,65025,65025, 255.0f,255.0f},
        {0,0,0,0,0,1.0f,1.0f, 0,0,0, 0.0f,0.0f},
        {180,90,200,30,77,0.25f,0.5f, 4050,9000,4050, 7.5f,38.5f},
        {127,64,127,200,10,1.0f,0.333f, 8128,16129,8128, 200.0f,3.33f},
    };
    for (const auto& c : cases) {
        VolumeSettingsIn in{c.s, c.m, c.sf, c.a, c.a2, c.sc0, c.sc1};
        VolumeSettingsOut o = ApplyVolumeSettings(in);
        CHECK_EQ(o.masterVolume, c.em);
        CHECK_EQ(o.sfxVolume, c.esfx);
        CHECK_EQ(o.musicVolume, c.emus);
        CHECK(std::fabs(o.ambientScale - c.eamb) < 1e-3f);
        CHECK(std::fabs(o.ambient2Scale - c.eamb2) < 1e-3f);
    }
}

TEST(AudioLeaves, FindFreeStreamSlot) {
    MockDevice dev;
    int idx = -1;
    DigitalAudio d = makeDriver(dev, 8, 0, 3, &idx);
    // 3 stream slots used -> first free slot index is 3.
    CHECK_EQ(FindFreeStreamSlot(d, idx), 3);
    // Invalid output index -> -1.
    CHECK_EQ(FindFreeStreamSlot(d, 99), -1);

    // Full output: no free stream slot.
    int idx2 = -1;
    MockDevice dev2;
    DigitalAudio full = makeDriver(dev2, 4, 0, 4, &idx2);
    CHECK_EQ(FindFreeStreamSlot(full, idx2), -1);
}

TEST(AudioLeaves, StreamHandleAndDriverLookup) {
    MockDevice dev;
    int idx = -1;
    DigitalAudio d = makeDriver(dev, 8, 0, 0, &idx);
    // Open three streams; capture their handles in slot order.
    shim::VoiceHandle s0 = d.openStream(idx);
    shim::VoiceHandle s1 = d.openStream(idx);
    shim::VoiceHandle s2 = d.openStream(idx);

    CHECK_EQ(LookupStreamHandleIndex(d, s0), 0);
    CHECK_EQ(LookupStreamHandleIndex(d, s1), 1);
    CHECK_EQ(LookupStreamHandleIndex(d, s2), 2);
    CHECK_EQ(LookupStreamHandleIndex(d, 99999), -1);

    CHECK_EQ(LookupStreamDriverIndex(d, s1), idx);
    CHECK_EQ(LookupStreamDriverIndex(d, 99999), -1);
}

TEST(AudioLeaves, SampleDriverAndFindDriverIndex) {
    MockDevice dev;
    int idx = -1;
    DigitalAudio d = makeDriver(dev, 8, 0, 0, &idx);
    shim::VoiceHandle h = d.allocateSampleHandle(idx);
    CHECK_EQ(LookupSampleDriverIndex(d, h), idx);
    CHECK_EQ(LookupSampleDriverIndex(d, 42424), -1);

    CHECK_EQ(FindDriverIndex(d, idx), idx);
    CHECK_EQ(FindDriverIndex(d, 99), -1);
}

TEST(AudioLeaves, CountAllocatedVoices) {
    MockDevice dev;
    int idx = -1;
    DigitalAudio d = makeDriver(dev, 8, 3, 2, &idx);
    // 3 samples + 2 streams both bump allocatedSampleCount.
    CHECK_EQ(CountAllocatedVoices(d), 5);

    MockDevice dev2;
    DigitalAudio empty(&dev2);
    empty.setDriverInstalled(true);
    CHECK_EQ(CountAllocatedVoices(empty), 0);
}

TEST(AudioLeaves, GlobalPreferenceGetSet) {
    MockDevice dev;
    int idx = -1;
    DigitalAudio d = makeDriver(dev, 8, 0, 0, &idx); // one open output

    int pref = 48;
    int got = -999;
    CHECK_EQ(GetGlobalPreference(d, pref, &got), 0);
    CHECK_EQ(got, 48);

    // Set is allowed with <=1 open output.
    int stored = 0;
    CHECK_EQ(SetGlobalPreference(d, &stored, 32), 0);
    CHECK_EQ(stored, 32);

    // No driver -> -1.
    MockDevice dev2;
    DigitalAudio nodrv(&dev2); // driverInstalled defaults false
    int o2 = 0;
    CHECK_EQ(GetGlobalPreference(nodrv, 0, &o2), -1);
    CHECK_EQ(SetGlobalPreference(nodrv, &o2, 16), -1);

    // No outputs and pref==0 -> get returns -1.
    MockDevice dev3;
    DigitalAudio drvNoOut(&dev3);
    drvNoOut.setDriverInstalled(true);
    int o3 = 0;
    CHECK_EQ(GetGlobalPreference(drvNoOut, 0, &o3), -1);
}

TEST(AudioLeaves, SetGlobalPreferenceRefusedMultiOutput) {
    MockDevice dev;
    DigitalAudio d(&dev);
    d.setDriverInstalled(true);
    d.openDigitalOutput(2, 44100, 16, 8);
    d.openDigitalOutput(2, 44100, 16, 8); // second open output
    int stored = 7;
    CHECK_EQ(SetGlobalPreference(d, &stored, 32), -1); // count > 1
    CHECK_EQ(stored, 7); // unchanged
}

TEST(AudioLeaves, FadeOutTrackArm) {
    TrackFadeState t{};
    // Inactive track: no-op.
    FadeOutTrack(&t, 2, 5000);
    CHECK_EQ((int)t.fadeMode, 0);

    t.streamActive = true;
    t.curVolume = 90;
    FadeOutTrack(&t, 2, 5000);
    CHECK_EQ((int)t.fadeMode, 2);
    CHECK_EQ(t.msPosition, 5000);
    CHECK_EQ(t.fadeStartMs, 5000);
    CHECK_EQ(t.fadeBaseVol, 90);

    // mode 0 -> no change to an already-active track.
    TrackFadeState t2{};
    t2.streamActive = true;
    FadeOutTrack(&t2, 0, 1234);
    CHECK_EQ((int)t2.fadeMode, 0);
    CHECK_EQ(t2.fadeStartMs, 0);

    // Null track is safe.
    FadeOutTrack(nullptr, 1, 100);
}

TEST(AudioLeaves, SetTrackNamePrefix) {
    std::string dst = "garbage";
    char r = SetTrackNamePrefix(dst, "MSX_");
    CHECK_EQ(r, (char)0);
    CHECK(dst == "MSX_");

    SetTrackNamePrefix(dst, "");
    CHECK(dst.empty());

    SetTrackNamePrefix(dst, nullptr);
    CHECK(dst.empty());
}

// --- WAVE-11 hardening: out-of-bounds / degenerate-input edge tests ----------

// A DigitalOutput whose maxSampleHandles is larger than its actual slot vectors
// (an inconsistent/malformed record) must NOT drive the Lookup scans off the end
// of the std::vector. The faithful guard clamps the scan to the real slot count
// (the original's array always had exactly maxSampleHandles entries).
TEST(AudioLeaves, LookupClampsToActualSlotSize) {
    MockDevice dev;
    int idx = -1;
    DigitalAudio d = makeDriver(dev, /*cap=*/4, /*alloc=*/2, /*stream=*/2, &idx);
    CHECK_EQ(idx, 0);
    DigitalOutput* o = d.outputAt(idx);
    CHECK(o != nullptr);
    // Corrupt the capacity field to be far beyond the real vector sizes (4).
    o->maxSampleHandles = 4096;
    // These scans must stay in-bounds (ASAN would flag a heap read past the end).
    CHECK_EQ(LookupStreamHandleIndex(d, 99999), -1);
    CHECK_EQ(LookupStreamDriverIndex(d, 99999), -1);
    CHECK_EQ(LookupSampleDriverIndex(d, 99999), -1);
}

TEST(AudioLeaves, AmbientVoiceListAppendAndStop) {
    AmbientVoiceList list{};
    CHECK_EQ(AppendAmbientVoice(list, 10), 1);
    CHECK_EQ(AppendAmbientVoice(list, 20), 2);
    CHECK_EQ(AppendAmbientVoice(list, 30), 3);
    CHECK_EQ(list.count, 3);
    CHECK_EQ(list.handles[0], 10);
    CHECK_EQ(list.handles[2], 30);

    // Fill to capacity then overflow: count stays at 16. When full the binary
    // returns `result` (the StartVoiceSample handle == the passed-in handle), not
    // the count (decompile @0x505ba8: `return result;`). Golden = the handle.
    AmbientVoiceList big{};
    for (int i = 0; i < kMaxAmbientVoices; ++i)
        CHECK_EQ(AppendAmbientVoice(big, 100 + i), i + 1);
    CHECK_EQ(AppendAmbientVoice(big, 999), 999); // full -> returns the handle
    CHECK_EQ(big.count, kMaxAmbientVoices);

    // Stop clears and resets.
    std::vector<shim::VoiceHandle> stopped;
    int n = StopAmbientVoices(list, [&](shim::VoiceHandle h) { stopped.push_back(h); });
    CHECK_EQ(n, 3);
    CHECK_EQ((int)stopped.size(), 3);
    CHECK_EQ(stopped[0], 10);
    CHECK_EQ(list.count, 0);
    CHECK_EQ(list.handles[0], 0);
}
