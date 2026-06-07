// End-to-end flow for the audio leaves: open a digital output, allocate sample
// + stream handles, drive the ambient-voice list and a volume-settings update,
// then tear everything down — all over a mock IAudioDevice.
#include "audio/audio_leaves.h"
#include "shim/IAudioDevice.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

// A recording mock device: tracks live voices, master volume, and stops.
class RecDevice : public shim::IAudioDevice {
public:
    bool init(int, int, int) override { return true; }
    void shutdown() override {}
    shim::VoiceHandle allocVoice() override { live_.push_back(next_); return next_++; }
    void freeVoice(shim::VoiceHandle h) override {
        for (auto& v : live_) if (v == h) { v = -1; ++freed_; }
    }
    void playSample(shim::VoiceHandle, const void*, std::size_t, int, int) override {}
    void stop(shim::VoiceHandle) override { ++stops_; }
    void setVolume(shim::VoiceHandle, int) override {}
    void setPan(shim::VoiceHandle, int) override {}
    void setMasterVolume(int v) override { master_ = v; }

    std::vector<shim::VoiceHandle> live_;
    shim::VoiceHandle next_ = 1;
    int master_ = -1;
    int freed_ = 0;
    int stops_ = 0;
};

} // namespace

TEST(AudioLeavesE2E, FullPlayQueueMixFlow) {
    RecDevice dev;
    DigitalAudio d(&dev);
    d.setDriverInstalled(true);

    // 1. Open a digital output and confirm the global preference round-trips.
    int out = d.openDigitalOutput(2, 44100, 16, 8);
    CHECK(out >= 0);
    int pref = 0;
    CHECK_EQ(SetGlobalPreference(d, &pref, 8), 0);
    CHECK_EQ(pref, 8);
    int gotPref = -1;
    CHECK_EQ(GetGlobalPreference(d, pref, &gotPref), 0);
    CHECK_EQ(gotPref, 8);

    // 2. Allocate a few sample handles and open a stream; verify the counters
    //    and the index lookups stay consistent.
    shim::VoiceHandle s0 = d.allocateSampleHandle(out);
    shim::VoiceHandle s1 = d.allocateSampleHandle(out);
    shim::VoiceHandle st = d.openStream(out);
    CHECK(s0 >= 0 && s1 >= 0 && st >= 0);
    CHECK_EQ(CountAllocatedVoices(d), 3); // 2 samples + 1 stream

    CHECK_EQ(LookupSampleDriverIndex(d, s1), out);
    CHECK_EQ(LookupStreamDriverIndex(d, st), out);
    CHECK_EQ(LookupStreamHandleIndex(d, st), 0); // first stream slot
    CHECK_EQ(FindFreeStreamSlot(d, out), 1);     // next free stream slot

    // 3. Push a volume-settings update; the derived master volume should hit the
    //    device through ApplyVolumeSettings + ClampDigitalMasterVolume.
    VolumeSettingsIn vin{120, 80, 100, 60, 40, 1.0f, 0.5f};
    VolumeSettingsOut vout = ApplyVolumeSettings(vin);
    CHECK_EQ(vout.masterVolume, 120); // 120 * 1.0
    int clamped = ClampDigitalMasterVolume(vout.masterVolume);
    CHECK_EQ(clamped, 120);
    dev.setMasterVolume(clamped);
    CHECK_EQ(dev.master_, 120);

    // 4. Drive the ambient-voice list: append handles, then stop them all,
    //    routing the stops through the device.
    AmbientVoiceList amb{};
    AppendAmbientVoice(amb, s0);
    AppendAmbientVoice(amb, s1);
    CHECK_EQ(amb.count, 2);
    int stopped = StopAmbientVoices(amb, [&](shim::VoiceHandle h) { dev.stop(h); });
    CHECK_EQ(stopped, 2);
    CHECK_EQ(dev.stops_, 2);
    CHECK_EQ(amb.count, 0);

    // 5. Release a sample handle and confirm the allocated count drops and the
    //    device freed it.
    CHECK_EQ(d.releaseSampleHandle(s0), 0);
    CHECK_EQ(CountAllocatedVoices(d), 2);
    CHECK(dev.freed_ >= 1);
    CHECK_EQ(LookupSampleDriverIndex(d, s0), -1); // no longer present
}
