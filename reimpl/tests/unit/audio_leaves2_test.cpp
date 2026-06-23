// Unit tests for guild::audio Miles driver-management layer (audio_leaves2.*).
//
// Golden vectors trace the exact integer bookkeeping the original performs over
// the dword_62EA1C/EA5C/EA9C parallel arrays and the dword_62EADC/EAE0 globals.
#include "audio/audio_leaves2.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::audio;

namespace {

// Build an output with `cap` slots, the given Miles handle, and pre-populated
// sample/stream slot contents. Slots beyond the provided lists are 0 (free).
void makeOutput(DriverOutput& o, std::int32_t handle, int cap,
                std::vector<std::int32_t> samples,
                std::vector<std::int32_t> streams,
                int allocated) {
    o.used = true;
    o.handle = handle;
    o.slotCapacity = cap;
    o.allocatedSampleCount = allocated;
    o.sampleSlots.assign(static_cast<std::size_t>(cap), 0);
    o.streamSlots.assign(static_cast<std::size_t>(cap), 0);
    for (std::size_t i = 0; i < samples.size() && i < o.sampleSlots.size(); ++i)
        o.sampleSlots[i] = samples[i];
    for (std::size_t i = 0; i < streams.size() && i < o.streamSlots.size(); ++i)
        o.streamSlots[i] = streams[i];
}

} // namespace

// --- startup / shutdown gate (dword_62EADC) --------------------------------

TEST(AudioLeaves2, StartupArmsDriverFlag) {
    DriverManager m;
    int startups = 0;
    m.hooks().startup = [&] { ++startups; };
    CHECK(!m.driverInstalled());
    CHECK_EQ(m.startupMilesDriver(), 0);  // first call succeeds
    CHECK(m.driverInstalled());
    CHECK_EQ(startups, 1);
    CHECK_EQ(m.startupMilesDriver(), -1); // already up
    CHECK_EQ(startups, 1);                // AIL_startup not called again
}

TEST(AudioLeaves2, ShutdownRequiresInstalledDriver) {
    DriverManager m;
    CHECK_EQ(m.shutdownMilesDriver(), -1); // not up yet
    int shutdowns = 0;
    m.hooks().shutdown = [&] { ++shutdowns; };
    m.startupMilesDriver();
    CHECK_EQ(m.shutdownMilesDriver(), 0);
    CHECK(!m.driverInstalled());
    CHECK_EQ(shutdowns, 1);
}

TEST(AudioLeaves2, TimerHighestDelayRoutesThroughHook) {
    DriverManager m;
    m.hooks().getTimerHighestDelay = [] { return 4242; };
    CHECK_EQ(m.getTimerHighestDelay(), 4242);
}

// --- output close / table walk ---------------------------------------------

TEST(AudioLeaves2, CloseDigitalOutputClearsSlotAndArrays) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(3), 0x100, 4, {11, 0, 0, 0}, {21, 0, 0, 0}, 1);
    std::vector<std::int32_t> closed;
    m.hooks().waveOutClose = [&](std::int32_t h) { closed.push_back(h); };

    CHECK_EQ(m.closeDigitalOutput(0x100), 0);
    CHECK_EQ(static_cast<int>(closed.size()), 1);
    CHECK_EQ(closed[0], 0x100);
    CHECK(!m.output(3).used);               // dword_62EA1C[3] = 0
    CHECK_EQ(static_cast<int>(m.output(3).sampleSlots.size()), 0);
    CHECK_EQ(static_cast<int>(m.output(3).streamSlots.size()), 0);

    CHECK_EQ(m.closeDigitalOutput(0x100), -1); // now unknown
}

TEST(AudioLeaves2, CloseDigitalOutputGatedByDriver) {
    DriverManager m; // driver not installed
    makeOutput(m.output(0), 0x77, 2, {}, {}, 0);
    CHECK_EQ(m.closeDigitalOutput(0x77), -1);
}

TEST(AudioLeaves2, CloseAllDigitalOutputsClosesEveryUsed) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0xA0, 2, {}, {}, 0);
    makeOutput(m.output(5), 0xB0, 2, {}, {}, 0);
    int closes = 0;
    m.hooks().waveOutClose = [&](std::int32_t) { ++closes; };
    CHECK_EQ(m.closeAllDigitalOutputs(), 0);
    CHECK_EQ(closes, 2);
    CHECK(!m.output(0).used);
    CHECK(!m.output(5).used);
}

TEST(AudioLeaves2, FindOutputByHandleMissingReturnsMinusOne) {
    DriverManager m;
    makeOutput(m.output(0), 0x1, 1, {}, {}, 0);
    makeOutput(m.output(1), 0x2, 1, {}, {}, 0);
    CHECK_EQ(m.findOutputByHandle(0x1), 0);
    CHECK_EQ(m.findOutputByHandle(0x2), 1);
    CHECK_EQ(m.findOutputByHandle(0x9), -1);
}

// --- driver release / reacquire ---------------------------------------------

TEST(AudioLeaves2, ReleaseDigitalDriverSuccessAndFailure) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(2), 0xC0, 1, {}, {}, 0);
    // default release returns 1 (success) -> 0
    CHECK_EQ(m.releaseDigitalDriver(0xC0), 0);
    // hook returns 0 (failure) -> -1
    m.hooks().digitalHandleRelease = [](std::int32_t) { return 0; };
    CHECK_EQ(m.releaseDigitalDriver(0xC0), -1);
    CHECK_EQ(m.releaseDigitalDriver(0xDEAD), -1); // unknown handle
}

TEST(AudioLeaves2, ReleaseAllDigitalDriversAggregatesFailure) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0x10, 1, {}, {}, 0);
    makeOutput(m.output(1), 0x20, 1, {}, {}, 0);
    CHECK_EQ(m.releaseAllDigitalDrivers(), 0);
    // make 0x20 fail
    m.hooks().digitalHandleRelease = [](std::int32_t h) { return h == 0x10 ? 1 : 0; };
    CHECK_EQ(m.releaseAllDigitalDrivers(), -1);
}

TEST(AudioLeaves2, ReacquireDigitalDriverPostsOnSuccess) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(4), 0x55, 1, {}, {}, 0);
    std::uintptr_t postedW = 0;
    unsigned postedMsg = 0;
    m.hooks().postMessage = [&](void*, unsigned msg, std::uintptr_t w) {
        postedMsg = msg; postedW = w; return 7;
    };
    // reacquire returns 0 (success) -> PostMessageA fires, returns 7
    CHECK_EQ(m.reacquireDigitalDriver(0x55, nullptr, 0x400), 7);
    CHECK_EQ(postedMsg, 0x400u);
    CHECK_EQ(static_cast<int>(postedW), 0x55);
}

TEST(AudioLeaves2, ReacquireDigitalDriverMsgGate) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0x55, 1, {}, {}, 0);
    // msg < 0x400 -> guarded out; returns the entry handle value unchanged.
    CHECK_EQ(m.reacquireDigitalDriver(0x55, nullptr, 0x100), 0x55);
}

TEST(AudioLeaves2, ReacquireDigitalDriverReacquireFailDoesNotPost) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0x55, 1, {}, {}, 0);
    m.hooks().digitalHandleReacquire = [](std::int32_t) { return 9; };
    int posts = 0;
    m.hooks().postMessage = [&](void*, unsigned, std::uintptr_t) { ++posts; return 1; };
    // reacquire returns 9 (!=0) -> no post, returns 9
    CHECK_EQ(m.reacquireDigitalDriver(0x55, nullptr, 0x400), 9);
    CHECK_EQ(posts, 0);
}

TEST(AudioLeaves2, ReacquireAllDigitalDriversPostsPerSuccess) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0x1, 1, {}, {}, 0);
    makeOutput(m.output(2), 0x2, 1, {}, {}, 0);
    int posts = 0;
    m.hooks().postMessage = [&](void*, unsigned, std::uintptr_t) { ++posts; return 3; };
    CHECK_EQ(m.reacquireAllDigitalDrivers(nullptr, 0x400), 3);
    CHECK_EQ(posts, 2);
    // msg gate: nothing posted. The binary leaves eax == dword_62EADC (-1) after a
    // used slot fails the msg check (disasm 449d66 mov eax,dword_62EADC; 449d79
    // jb loc_449D4D). dword_62EADC is -1 once the driver is up, so the result is
    // -1, not 0. (Golden corrected to match the binary.)
    posts = 0;
    CHECK_EQ(m.reacquireAllDigitalDrivers(nullptr, 0x10), -1);
    CHECK_EQ(posts, 0);
}

// --- stream close bookkeeping (+0x14 decrement, slot clear) -----------------

TEST(AudioLeaves2, CloseStreamDecrementsCountAndClearsSlot) {
    DriverManager m;
    m.startupMilesDriver();
    // slot capacity 4; stream handle 0x300 lives in slot 1; allocated count 2.
    makeOutput(m.output(1), 0xE0, 4, {}, {0, 0x300, 0, 0}, 2);
    std::vector<std::int32_t> closed;
    m.hooks().closeStream = [&](std::int32_t s) { closed.push_back(s); };

    CHECK_EQ(m.closeStream(0x300), 0);
    CHECK_EQ(static_cast<int>(closed.size()), 1);
    CHECK_EQ(closed[0], 0x300);
    CHECK_EQ(m.output(1).allocatedSampleCount, 1);     // 2 -> 1
    CHECK_EQ(m.output(1).streamSlots[1], 0);           // slot cleared
    CHECK_EQ(m.closeStream(0x300), -1);                // gone now
}

TEST(AudioLeaves2, CloseStreamGatedByDriver) {
    DriverManager m;
    makeOutput(m.output(0), 0xE0, 2, {}, {0x300, 0}, 1);
    CHECK_EQ(m.closeStream(0x300), -1); // driver down
}

TEST(AudioLeaves2, CloseAllStreamsClosesEveryNonZeroSlot) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0x10, 3, {}, {0x1, 0, 0x2}, 2);
    makeOutput(m.output(1), 0x20, 2, {}, {0x3, 0}, 1);
    int closes = 0;
    m.hooks().closeStream = [&](std::int32_t) { ++closes; };
    CHECK_EQ(m.closeAllStreams(), 0);
    CHECK_EQ(closes, 3);                       // 0x1, 0x2, 0x3
    CHECK_EQ(m.output(0).allocatedSampleCount, 0);
    CHECK_EQ(m.output(1).allocatedSampleCount, 0);
}

TEST(AudioLeaves2, CloseDriverStreamsOnlyThatOutput) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0x10, 2, {}, {0x1, 0x2}, 2);
    makeOutput(m.output(1), 0x20, 2, {}, {0x3, 0x4}, 2);
    int closes = 0;
    m.hooks().closeStream = [&](std::int32_t) { ++closes; };
    CHECK_EQ(m.closeDriverStreams(0x20), 0);
    CHECK_EQ(closes, 2);                        // only output 1's two streams
    CHECK_EQ(m.output(1).allocatedSampleCount, 0);
    CHECK_EQ(m.output(0).allocatedSampleCount, 2); // untouched
    CHECK_EQ(m.closeDriverStreams(0xBAD), -1);
}

// --- sample-handle release --------------------------------------------------

TEST(AudioLeaves2, ReleaseDriverSampleHandlesClearsSlots) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0x10, 3, {0x5, 0, 0x6}, {}, 2);
    CHECK_EQ(m.releaseDriverSampleHandles(0x10), 0);
    CHECK_EQ(m.output(0).sampleSlots[0], 0);
    CHECK_EQ(m.output(0).sampleSlots[2], 0);
    CHECK_EQ(m.output(0).allocatedSampleCount, 0); // decremented twice
    CHECK_EQ(m.releaseDriverSampleHandles(0xBAD), -1);
}

TEST(AudioLeaves2, ReleaseAllSampleHandlesAcrossOutputs) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(0), 0x10, 2, {0x5, 0}, {}, 1);
    makeOutput(m.output(3), 0x40, 2, {0x6, 0x7}, {}, 2);
    CHECK_EQ(m.releaseAllSampleHandles(), 0);
    CHECK_EQ(m.output(0).allocatedSampleCount, 0);
    CHECK_EQ(m.output(3).allocatedSampleCount, 0);
    CHECK_EQ(m.output(3).sampleSlots[0], 0);
    CHECK_EQ(m.output(3).sampleSlots[1], 0);
}

// --- active sample count query ----------------------------------------------

TEST(AudioLeaves2, GetActiveSampleCountRoutesThroughHook) {
    DriverManager m;
    m.startupMilesDriver();
    makeOutput(m.output(2), 0x90, 4, {}, {}, 0);
    m.hooks().activeSampleCount = [](std::int32_t h) { return h == 0x90 ? 5 : -1; };
    int out = -999;
    CHECK_EQ(m.getActiveSampleCount(0x90, &out), 0);
    CHECK_EQ(out, 5);
    CHECK_EQ(m.getActiveSampleCount(0xBAD, &out), -1);
}

TEST(AudioLeaves2, GetActiveSampleCountGatedByDriver) {
    DriverManager m;
    makeOutput(m.output(0), 0x90, 1, {}, {}, 0);
    int out = 0;
    CHECK_EQ(m.getActiveSampleCount(0x90, &out), -1);
}

// --- global preference (carried alongside; mirrors EAE0 default) ------------

TEST(AudioLeaves2, GlobalPreferenceRoundTrips) {
    DriverManager m;
    CHECK_EQ(m.globalPreference(), 0);
    m.setGlobalPreference(48);
    CHECK_EQ(m.globalPreference(), 48);
}
