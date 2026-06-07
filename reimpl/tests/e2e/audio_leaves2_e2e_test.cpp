// e2e flow for the Miles driver-management layer (audio_leaves2.*): bring the
// driver up, populate outputs with sample + stream handles, then exercise the
// full teardown path (CloseAllStreams -> ReleaseAllSampleHandles ->
// CloseAllDigitalOutputs -> AIL_shutdown) the way VIBE_Audio_Shutdown_49878
// drives it, observing every Miles call through installed hooks.
#include "audio/audio_leaves2.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {
void seed(DriverOutput& o, std::int32_t handle, int cap,
          std::vector<std::int32_t> samples, std::vector<std::int32_t> streams) {
    o.used = true;
    o.handle = handle;
    o.slotCapacity = cap;
    o.sampleSlots.assign(static_cast<std::size_t>(cap), 0);
    o.streamSlots.assign(static_cast<std::size_t>(cap), 0);
    int alloc = 0;
    for (std::size_t i = 0; i < samples.size() && i < o.sampleSlots.size(); ++i) {
        o.sampleSlots[i] = samples[i];
        if (samples[i]) ++alloc;
    }
    for (std::size_t i = 0; i < streams.size() && i < o.streamSlots.size(); ++i) {
        o.streamSlots[i] = streams[i];
        if (streams[i]) ++alloc;
    }
    o.allocatedSampleCount = alloc;
}
} // namespace

TEST(AudioLeaves2E2E, FullDriverLifecycleTeardown) {
    DriverManager m;

    // Track every Miles boundary call.
    int startups = 0, shutdowns = 0;
    std::vector<std::int32_t> streamCloses, waveCloses;
    m.hooks().startup       = [&] { ++startups; };
    m.hooks().shutdown      = [&] { ++shutdowns; };
    m.hooks().closeStream   = [&](std::int32_t s) { streamCloses.push_back(s); };
    m.hooks().waveOutClose  = [&](std::int32_t h) { waveCloses.push_back(h); };

    // 1. Bring the driver up.
    CHECK_EQ(m.startupMilesDriver(), 0);
    CHECK(m.driverInstalled());
    CHECK_EQ(startups, 1);

    // 2. Two outputs: handles 0x100 / 0x200, each with 2 sample + 2 stream slots.
    seed(m.output(0), 0x100, 4, {0xA1, 0, 0xA2, 0}, {0xB1, 0, 0xB2, 0});
    seed(m.output(2), 0x200, 4, {0xC1, 0, 0, 0},    {0xD1, 0, 0, 0});
    // output 0 allocated = 2 samples + 2 streams = 4; output 2 = 1 + 1 = 2.
    CHECK_EQ(m.output(0).allocatedSampleCount, 4);
    CHECK_EQ(m.output(2).allocatedSampleCount, 2);

    // 3. Query active samples (routed through hook).
    m.hooks().activeSampleCount = [](std::int32_t h) { return h == 0x100 ? 3 : 1; };
    int active = -1;
    CHECK_EQ(m.getActiveSampleCount(0x100, &active), 0);
    CHECK_EQ(active, 3);

    // 4. Full teardown via the Shutdown_49878 path.
    CHECK_EQ(m.shutdownMilesDriver(), 0);
    CHECK(!m.driverInstalled());

    // Streams closed: 0xB1,0xB2 (out0) + 0xD1 (out2) = 3.
    CHECK_EQ(static_cast<int>(streamCloses.size()), 3);
    // Outputs closed: 0x100 and 0x200.
    CHECK_EQ(static_cast<int>(waveCloses.size()), 2);
    CHECK_EQ(waveCloses[0], 0x100);
    CHECK_EQ(waveCloses[1], 0x200);
    CHECK_EQ(shutdowns, 1);

    // Both output slots are now empty (descriptors freed / table cleared).
    CHECK(!m.output(0).used);
    CHECK(!m.output(2).used);

    // 5. After shutdown, every gated op refuses.
    int dummy = 0;
    CHECK_EQ(m.shutdownMilesDriver(), -1);
    CHECK_EQ(m.closeAllStreams(), -1);
    CHECK_EQ(m.releaseAllSampleHandles(), -1);
    CHECK_EQ(m.getActiveSampleCount(0x100, &dummy), -1);
}

TEST(AudioLeaves2E2E, ReacquireRoundTripAfterFocusLoss) {
    // Simulate the Windows focus-loss reacquire path: drivers were released,
    // then reacquired and a window message posted per success.
    DriverManager m;
    m.startupMilesDriver();
    seed(m.output(0), 0x100, 1, {}, {});
    seed(m.output(1), 0x200, 1, {}, {});

    int releases = 0, reacquires = 0, posts = 0;
    m.hooks().digitalHandleRelease   = [&](std::int32_t) { ++releases; return 1; };
    m.hooks().digitalHandleReacquire = [&](std::int32_t) { ++reacquires; return 0; };
    m.hooks().postMessage = [&](void*, unsigned, std::uintptr_t) { ++posts; return 1; };

    CHECK_EQ(m.releaseAllDigitalDrivers(), 0);
    CHECK_EQ(releases, 2);

    CHECK_EQ(m.reacquireAllDigitalDrivers(reinterpret_cast<void*>(0x1234), 0x401), 1);
    CHECK_EQ(reacquires, 2);
    CHECK_EQ(posts, 2);
}
