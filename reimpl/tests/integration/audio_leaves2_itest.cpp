// Integration test for the Miles digital-driver MANAGEMENT layer (DriverManager,
// audio_leaves2.cpp).
//
// NO RECONSTRUCTED SIBLING: every leaf this module forwards to is the Miles Sound
// System (mss32.dll: AIL_startup/AIL_shutdown/AIL_waveOutClose/AIL_close_stream/
// AIL_digital_handle_release/reacquire/AIL_active_sample_count) or Win32
// (user32.dll: PostMessageA) boundary — external libraries, not other gilde.exe
// modules we have reconstructed. So per the brief we exercise the module's REAL
// inert default-hook path end to end: the defaults installed by the DriverManager
// ctor (installDefaultHooks() in audio_leaves2.cpp) are the live wiring here, and
// we assert the integer bookkeeping the originals preserve flows correctly through
// a full startup -> populate -> teardown session driven only by those defaults.
#include "test.h"

#include "audio/audio_leaves2.h"

using namespace guild;
using guild::audio::DriverManager;
using guild::audio::DriverOutput;
using guild::audio::kDrv2MaxOutputs;

namespace {

// Configure one used digital output with `cap` stream/sample slots.
DriverOutput* SetupOutput(DriverManager& mgr, int idx, std::int32_t handle,
                          int cap) {
    if (idx < 0 || idx >= kDrv2MaxOutputs)
        return nullptr;
    DriverOutput& o = mgr.output(idx);
    o.used = true;
    o.handle = handle;
    o.slotCapacity = cap;
    o.streamSlots.assign(static_cast<std::size_t>(cap), 0);
    o.sampleSlots.assign(static_cast<std::size_t>(cap), 0);
    return &o;
}

} // namespace

// Startup arms the install flag exactly once; a second startup is rejected (-1)
// just as the original guards on dword_62EADC. Shutdown reverses it.
TEST(AudioLeaves2Itest, StartupIdempotentThroughDefaultHooks) {
    DriverManager mgr;                 // ctor installs the real inert defaults
    CHECK(!mgr.driverInstalled());
    CHECK_EQ(mgr.startupMilesDriver(), 0);
    CHECK(mgr.driverInstalled());
    CHECK_EQ(mgr.startupMilesDriver(), -1);   // already up
    CHECK_EQ(mgr.shutdownMilesDriver(), 0);
    CHECK(!mgr.driverInstalled());
    CHECK_EQ(mgr.shutdownMilesDriver(), -1);  // not up
}

// Every table-walking verb returns the "driver down" -1 before startup, proving
// the dword_62EADC gate is honoured end to end on the default path.
TEST(AudioLeaves2Itest, AllVerbsGatedWhenDriverDown) {
    DriverManager mgr;
    int out = 123;
    CHECK_EQ(mgr.closeDigitalOutput(1), -1);
    CHECK_EQ(mgr.closeAllDigitalOutputs(), -1);
    CHECK_EQ(mgr.releaseDigitalDriver(1), -1);
    CHECK_EQ(mgr.releaseAllDigitalDrivers(), -1);
    CHECK_EQ(mgr.closeStream(1), -1);
    CHECK_EQ(mgr.closeAllStreams(), -1);
    CHECK_EQ(mgr.closeDriverStreams(1), -1);
    CHECK_EQ(mgr.releaseDriverSampleHandles(1), -1);
    CHECK_EQ(mgr.releaseAllSampleHandles(), -1);
    CHECK_EQ(mgr.getActiveSampleCount(1, &out), -1);
    CHECK_EQ(out, 123);                 // untouched on the gated path
}

// Full session: bring the driver up, populate stream + sample slots on a real
// output, then close streams and release sample handles through the default
// hooks. The +0x14 allocated-sample bookkeeping must drain exactly to zero.
TEST(AudioLeaves2Itest, StreamAndSampleTeardownBookkeeping) {
    DriverManager mgr;
    CHECK_EQ(mgr.startupMilesDriver(), 0);

    DriverOutput* o = SetupOutput(mgr, 0, /*handle=*/0x500, /*cap=*/3);
    CHECK(o != nullptr);
    if (o) {
        o->streamSlots[0] = 11;
        o->streamSlots[1] = 22;
        o->sampleSlots[0] = 71;
        o->sampleSlots[2] = 73;
        // allocatedSampleCount tracks live streams+samples the originals decrement.
        o->allocatedSampleCount = 4;   // 2 streams + 2 samples
    }

    // Close one stream by handle: clears its slot, decrements +0x14.
    CHECK_EQ(mgr.closeStream(11), 0);
    if (o) {
        CHECK_EQ(o->streamSlots[0], 0);
        CHECK_EQ(o->allocatedSampleCount, 3);
    }
    // Unknown stream handle -> -1, no change.
    CHECK_EQ(mgr.closeStream(999), -1);

    // Close all remaining streams across the table.
    CHECK_EQ(mgr.closeAllStreams(), 0);
    if (o) {
        CHECK_EQ(o->streamSlots[1], 0);
        CHECK_EQ(o->allocatedSampleCount, 2);  // both streams gone, samples remain
    }

    // Release this driver's sample handles (range [0, capacity)).
    CHECK_EQ(mgr.releaseDriverSampleHandles(0x500), 0);
    if (o) {
        CHECK_EQ(o->sampleSlots[0], 0);
        CHECK_EQ(o->sampleSlots[2], 0);
        CHECK_EQ(o->allocatedSampleCount, 0);  // fully drained
    }

    CHECK_EQ(mgr.shutdownMilesDriver(), 0);
}

// Find-by-handle + reacquire/post path. The default digitalHandleReacquire
// returns 0 (success) and the default postMessage returns 1 (posted), so a
// reacquire with a valid msg (>= 0x400) on a known output reports the posted 1.
TEST(AudioLeaves2Itest, ReacquireUsesDefaultPostMessage) {
    DriverManager mgr;
    CHECK_EQ(mgr.startupMilesDriver(), 0);
    DriverOutput* o = SetupOutput(mgr, 2, /*handle=*/0x900, /*cap=*/1);
    CHECK(o != nullptr);

    CHECK_EQ(mgr.findOutputByHandle(0x900), 2);
    CHECK_EQ(mgr.findOutputByHandle(0xDEAD), -1);

    // msg below 0x400 short-circuits: returns the entry handle value unchanged.
    CHECK_EQ(mgr.reacquireDigitalDriver(0x900, nullptr, 0x10), 0x900);
    // valid msg + known output: default reacquire==0 -> default postMessage==1.
    CHECK_EQ(mgr.reacquireDigitalDriver(0x900, nullptr, 0x400), 1);
    // valid msg + unknown output: nothing posted. The binary searches with eax as
    // a byte offset (0,4,...) into dword_62EA1C and, on the not-found break, eax
    // has reached 0x40 (64); that value is returned (disasm @0x449cf8 jge ->
    // loc_449D06 retn). Golden corrected from 0 to 64 to match the binary.
    CHECK_EQ(mgr.reacquireDigitalDriver(0xDEAD, nullptr, 0x400), 64);

    // ReleaseDigitalDriver: default digitalHandleRelease returns 1 (success) -> 0.
    CHECK_EQ(mgr.releaseDigitalDriver(0x900), 0);
    CHECK_EQ(mgr.releaseDigitalDriver(0xDEAD), -1);

    // GetActiveSampleCount writes the default activeSampleCount() == 0.
    int cnt = -7;
    CHECK_EQ(mgr.getActiveSampleCount(0x900, &cnt), 0);
    CHECK_EQ(cnt, 0);

    // Closing the output frees its slot arrays and clears the table entry.
    CHECK_EQ(mgr.closeDigitalOutput(0x900), 0);
    CHECK(!mgr.output(2).used);
    CHECK_EQ(mgr.findOutputByHandle(0x900), -1);

    CHECK_EQ(mgr.shutdownMilesDriver(), 0);
}
