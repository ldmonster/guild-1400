// Golden tests for guild::drm disc dispatch / spin-wait / CRC accumulator.
// Suite prefix: DiscDispatch. No main() (shared test_main.cpp provides it).
//
// Verifies, against the gilde.exe reconstruction:
//   * the CRC-16 accumulator polynomial (VIBE_Disc_AccumulateCrc @0x1412330,
//     reflected 0x8005 / ARC, init 0) on known vectors,
//   * the spin-wait busy-loop bound (VIBE_Disc_SpinWaitDelay @0x1412390:
//     unsigned (cur-start) >= a1), and
//   * that each dispatcher routes to the correct backend hook per the mode flags.

#include "drm/disc_dispatch.h"
#include "test.h"

using namespace guild;
using guild::drm::DiscState;
using guild::drm::DiscDispatchHooks;
using guild::drm::SpinWaitHooks;
using Backend = guild::drm::DiscDispatchHooks::Backend;

// =============================================================================
// CRC-16 accumulator golden vectors (reflected 0x8005 / ARC, init 0).
//   CRC16("123456789") == 0xBB3D is the canonical ARC check value.
// =============================================================================
TEST(DiscDispatch, Crc16KnownVectors) {
    const guild::u8 check[9] = {'1','2','3','4','5','6','7','8','9'};
    CHECK_EQ(drm::DiscAccumulateCrcBuffer(check, 9), guild::u16(0xBB3D));

    // Empty buffer -> init value unchanged.
    CHECK_EQ(drm::DiscAccumulateCrcBuffer(nullptr, 0), guild::u16(0x0000));

    const guild::u8 a[1] = {'A'};
    CHECK_EQ(drm::DiscAccumulateCrcBuffer(a, 1), guild::u16(0x30C0));

    const guild::u8 zeros[3] = {0, 0, 0};
    CHECK_EQ(drm::DiscAccumulateCrcBuffer(zeros, 3), guild::u16(0x0000));

    const guild::u8 g[9] = {'T','h','e',' ','G','u','i','l','d'};
    CHECK_EQ(drm::DiscAccumulateCrcBuffer(g, 9), guild::u16(0xDFA9));
}

// =============================================================================
// Spin-wait bound. With a monotonic +1-per-call tick source: start=ticks() (0),
// cur=ticks() (1); loop while (unsigned)(cur-start) < a1. cur takes values
// 2,3,... from the body. Exit when cur-start >= a1, i.e. cur == a1, so the body
// runs for cur in {2..a1} => (a1 - 1) iterations.
// =============================================================================
namespace {
int g_tick = 0;
int MonotonicTick() { return g_tick++; }
}  // namespace

TEST(DiscDispatch, SpinWaitBound) {
    DiscState st;
    st.modeFlag = 1;  // SPTI busy-poll branch

    SpinWaitHooks h;
    h.ticks = &MonotonicTick;

    g_tick = 0;
    CHECK_EQ(drm::DiscSpinWaitDelay(st, h, 5u), 4);   // a1-1
    g_tick = 0;
    CHECK_EQ(drm::DiscSpinWaitDelay(st, h, 1u), 0);   // cur=1 already >= 1
    g_tick = 0;
    CHECK_EQ(drm::DiscSpinWaitDelay(st, h, 10u), 9);  // a1-1

    // a1 == 0 returns immediately (no tick calls).
    g_tick = 0;
    CHECK_EQ(drm::DiscSpinWaitDelay(st, h, 0u), 0);
    CHECK_EQ(g_tick, 0);

    // SPTI branch with no tick hook is inert.
    SpinWaitHooks noh;
    CHECK_EQ(drm::DiscSpinWaitDelay(st, noh, 5u), 0);
}

TEST(DiscDispatch, SpinWaitAspiBranch) {
    DiscState st;
    st.modeFlag = 0;  // ASPI timer branch

    static int g_setupCalls = 0;
    static unsigned g_setupA1 = 0;
    static int g_sleepMs = -1;

    SpinWaitHooks h;
    h.timerHandle = 0x1234;
    h.longSleep = 0;
    h.timerSetup = [](unsigned a1, int, int, int, int) -> int {
        g_setupCalls++; g_setupA1 = a1; return 1;
    };
    h.sleepMs = [](int, int ms) { g_sleepMs = ms; };

    g_setupCalls = 0; g_setupA1 = 0; g_sleepMs = -1;
    CHECK_EQ(drm::DiscSpinWaitDelay(st, h, 42u), 0);
    CHECK_EQ(g_setupCalls, 1);
    CHECK_EQ(g_setupA1, 42u);
    CHECK_EQ(g_sleepMs, 10);   // longSleep == 0 -> 10 ms

    // longSleep != 0 -> 50 ms
    h.longSleep = 1;
    g_sleepMs = -1;
    CHECK_EQ(drm::DiscSpinWaitDelay(st, h, 42u), 0);
    CHECK_EQ(g_sleepMs, 50);

    // timerSetup returning 0 -> no sleep.
    h.timerSetup = [](unsigned, int, int, int, int) -> int { return 0; };
    g_sleepMs = -1;
    CHECK_EQ(drm::DiscSpinWaitDelay(st, h, 42u), 0);
    CHECK_EQ(g_sleepMs, -1);
}

// =============================================================================
// Dispatch selection — verify each op routes to the correct backend per flags.
// =============================================================================

// Sentinel returns so we can confirm WHICH hook ran (not just the tag).
namespace {
int RetSpti(int, int) { return 0x5071; }
int RetSpti1(int) { return 0x5071; }
int RetSpti4(int, int, int, int) { return 0x5071; }
int RetSpti2(int, int) { return 0x5071; }
int RetAspi(int, int) { return 0xA591; }
int RetAspi1(int) { return 0xA591; }
int RetAspi6(int, int, int, int, int, int) { return 0xA591; }
int RetAspi4(int, int, int, int) { return 0xA591; }
int RetAspiToc(int, int, int, void*, int*, int*, int) { return 0xA591; }
int RetFbRead(int, int, int, int, int) { return 0xFB12; }
int RetFbToc(int, int*, int*, int) { return 0xFB12; }
int RetFbSub(int, int, int) { return 0xFB12; }
}  // namespace

TEST(DiscDispatch, SetSpeedSelectsByModeFlag) {
    DiscDispatchHooks h;
    h.sptiSetSpeed = &RetSpti;
    h.aspiSetSpeed = &RetAspi;

    DiscState spti; spti.modeFlag = 1;
    CHECK_EQ(drm::DiscSetSpeed(spti, h, 1, 2), 0x5071);
    CHECK(h.lastBackend == Backend::kSpti);

    DiscState aspi; aspi.modeFlag = 0;
    CHECK_EQ(drm::DiscSetSpeed(aspi, h, 1, 2), 0xA591);
    CHECK(h.lastBackend == Backend::kAspi);
}

TEST(DiscDispatch, SetReadSpeedSelectsByModeFlag) {
    DiscDispatchHooks h;
    h.sptiSetReadSpeed = &RetSpti1;
    h.aspiSetReadSpeed = &RetAspi1;

    DiscState spti; spti.modeFlag = 7;
    CHECK_EQ(drm::DiscSetReadSpeed(spti, h, 4), 0x5071);
    CHECK(h.lastBackend == Backend::kSpti);

    DiscState aspi;  // modeFlag 0
    CHECK_EQ(drm::DiscSetReadSpeed(aspi, h, 4), 0xA591);
    CHECK(h.lastBackend == Backend::kAspi);
}

TEST(DiscDispatch, ReadRawSectorTwoWay) {
    DiscDispatchHooks h;
    h.sptiReadRawSector = &RetSpti4;
    h.aspiReadRawSector = &RetAspi6;

    DiscState spti; spti.modeFlag = 1;
    CHECK_EQ(drm::DiscReadRawSector(spti, h, 1, 2, 3), 0x5071);
    CHECK(h.lastBackend == Backend::kSpti);

    DiscState aspi;  // modeFlag 0 -> ASPI unconditionally (no fallback for raw)
    CHECK_EQ(drm::DiscReadRawSector(aspi, h, 1, 2, 3), 0xA591);
    CHECK(h.lastBackend == Backend::kAspi);
}

TEST(DiscDispatch, ReadSectorThreeWay) {
    DiscDispatchHooks h;
    h.sptiReadSector = &RetSpti4;
    h.aspiReadSector = &RetAspi6;
    h.fbReadSector = &RetFbRead;

    DiscState spti; spti.modeFlag = 1;
    CHECK_EQ(drm::DiscReadSector(spti, h, 1, 2, 3), 0x5071);
    CHECK(h.lastBackend == Backend::kSpti);

    DiscState aspi; aspi.modeFlag = 0; aspi.aspiTarget = 1;
    CHECK_EQ(drm::DiscReadSector(aspi, h, 1, 2, 3), 0xA591);
    CHECK(h.lastBackend == Backend::kAspi);

    DiscState fb;  // modeFlag 0, aspiTarget 0 -> fallback
    CHECK_EQ(drm::DiscReadSector(fb, h, 1, 2, 3), 0xFB12);
    CHECK(h.lastBackend == Backend::kFallback);
}

TEST(DiscDispatch, ReadSectorFallbackSwapsArgs) {
    // Verify the fallback path passes (fbDev, a2, a1, 0, sptiDev) — a1/a2 swapped.
    static int cap[5];
    DiscDispatchHooks h;
    h.fbReadSector = [](int dev, int lba, int n, int flag, int a5) -> int {
        cap[0] = dev; cap[1] = lba; cap[2] = n; cap[3] = flag; cap[4] = a5;
        return 1;
    };
    DiscState fb; fb.modeFlag = 0; fb.aspiTarget = 0;
    fb.fbDev = 11; fb.sptiDev = 22;
    drm::DiscReadSector(fb, h, /*a1=*/100, /*a2=*/200, /*a3=*/300);
    CHECK_EQ(cap[0], 11);    // fbDev
    CHECK_EQ(cap[1], 200);   // a2 (swapped into lba)
    CHECK_EQ(cap[2], 100);   // a1 (swapped into n)
    CHECK_EQ(cap[3], 0);     // literal 0
    CHECK_EQ(cap[4], 22);    // sptiDev
}

TEST(DiscDispatch, CheckMediaPresentThreeWay) {
    DiscDispatchHooks h;
    h.sptiTestUnitReady = &RetSpti2;
    h.aspiTestUnitReady = &RetAspi4;

    DiscState spti; spti.modeFlag = 1;
    CHECK_EQ(drm::DiscCheckMediaPresent(spti, h, 9), 0x5071);
    CHECK(h.lastBackend == Backend::kSpti);

    DiscState aspi; aspi.modeFlag = 0; aspi.aspiTarget = 1;
    CHECK_EQ(drm::DiscCheckMediaPresent(aspi, h, 9), 0xA591);
    CHECK(h.lastBackend == Backend::kAspi);
}

TEST(DiscDispatch, CheckMediaPresentFallbackStatusBit) {
    // Fallback: zero status, query, return ((status & 0x800) == 0).
    DiscDispatchHooks h;
    // status without bit 0x800 -> media present (returns 1).
    h.fbQueryStatus = [](int, int* out, int) { *out = 0x123; };
    DiscState fb; fb.modeFlag = 0; fb.aspiTarget = 0; fb.statusWord = 0xDEAD;
    CHECK_EQ(drm::DiscCheckMediaPresent(fb, h, 0), 1);
    CHECK(h.lastBackend == Backend::kFallback);
    CHECK_EQ(fb.statusWord, 0x123);  // confirms it was zeroed then filled

    // status with bit 0x800 set -> not present (returns 0).
    h.fbQueryStatus = [](int, int* out, int) { *out = 0x800; };
    DiscState fb2; fb2.modeFlag = 0; fb2.aspiTarget = 0;
    CHECK_EQ(drm::DiscCheckMediaPresent(fb2, h, 0), 0);

    // No hook -> status stays 0 -> (0 & 0x800)==0 -> present (1).
    DiscDispatchHooks empty;
    DiscState fb3; fb3.modeFlag = 0; fb3.aspiTarget = 0;
    CHECK_EQ(drm::DiscCheckMediaPresent(fb3, empty, 0), 1);
    CHECK_EQ(fb3.statusWord, 0);
}

TEST(DiscDispatch, ReadTocEntrySelection) {
    DiscDispatchHooks h;
    h.aspiReadTocAndDecode = &RetAspiToc;
    h.fbReadTocEntry = &RetFbToc;

    // tocReader && aspiTarget(low byte) && modeFlag==0 -> ASPI.
    DiscState aspi; aspi.tocReader = 1; aspi.aspiTarget = 1; aspi.modeFlag = 0;
    CHECK_EQ(drm::DiscReadTocEntry(aspi, h), 0xA591);
    CHECK(h.lastBackend == Backend::kAspi);

    // modeFlag != 0 -> condition false -> fallback.
    DiscState spti; spti.tocReader = 1; spti.aspiTarget = 1; spti.modeFlag = 1;
    CHECK_EQ(drm::DiscReadTocEntry(spti, h), 0xFB12);
    CHECK(h.lastBackend == Backend::kFallback);

    // tocReader == 0 -> fallback regardless.
    DiscState noReader; noReader.tocReader = 0; noReader.aspiTarget = 1;
    CHECK_EQ(drm::DiscReadTocEntry(noReader, h), 0xFB12);
    CHECK(h.lastBackend == Backend::kFallback);

    // aspiTarget low byte zero (0x100) -> (u8)aspiTarget == 0 -> fallback.
    DiscState lowZero; lowZero.tocReader = 1; lowZero.aspiTarget = 0x100;
    lowZero.modeFlag = 0;
    CHECK_EQ(drm::DiscReadTocEntry(lowZero, h), 0xFB12);
    CHECK(h.lastBackend == Backend::kFallback);
}

TEST(DiscDispatch, ReadSubchannelSelection) {
    DiscDispatchHooks h;
    h.aspiReadSector = &RetAspi6;
    h.fbReadSubchannel = &RetFbSub;

    // tocReader && !modeFlag && aspiTarget -> ASPI.
    DiscState aspi; aspi.tocReader = 1; aspi.modeFlag = 0; aspi.aspiTarget = 1;
    CHECK_EQ(drm::DiscReadSubchannelEntry(aspi, h), 0xA591);
    CHECK(h.lastBackend == Backend::kAspi);

    // modeFlag set -> fallback.
    DiscState spti; spti.tocReader = 1; spti.modeFlag = 1; spti.aspiTarget = 1;
    CHECK_EQ(drm::DiscReadSubchannelEntry(spti, h), 0xFB12);
    CHECK(h.lastBackend == Backend::kFallback);

    // aspiTarget 0 -> fallback.
    DiscState noTgt; noTgt.tocReader = 1; noTgt.modeFlag = 0; noTgt.aspiTarget = 0;
    CHECK_EQ(drm::DiscReadSubchannelEntry(noTgt, h), 0xFB12);
    CHECK(h.lastBackend == Backend::kFallback);
}
