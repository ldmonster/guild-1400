// ===========================================================================
// Golden-vector tests for gui/edgescroll — map drag-scroll state machine
// (0x543994) + HUD edge-scroll re-attach finisher (0x4bc07c), wave-20.
// ===========================================================================
#include "test.h"
#include "gui/edgescroll.h"

using namespace guild;
using namespace guild::gui;

// --- 0x543994 MapView_UpdateScrollState ------------------------------------
static MapScrollState MakeState() {
    MapScrollState s;
    s.offX = 100; s.offY = 50;
    s.worldW = 1000; s.worldH = 800;
    s.bandHiX = 0x316; s.bandHiY = 0x257; s.bandLoX = 0; s.bandLoY = 0;
    s.bandArmed = 1; s.latched = 0;
    return s;
}

TEST(EdgeScrollW20_MapScroll, PressLatchesAndSnapshots) {
    MapScrollState s = MakeState();
    int cx = 300 << 16, cy = 150 << 16;
    int r = MapView_UpdateScrollState(s, /*press*/1, cx, cy, cx, cy);
    // Press frame: block 1 latches, then block 3 runs (pressFlag still set). With
    // anchor==dragOrigin the delta is 0 and offsets are unchanged -> changed == 0.
    CHECK_EQ(r, 0);
    CHECK_EQ(s.latched, 1);
    CHECK_EQ(s.anchorX, 300);
    CHECK_EQ(s.anchorY, 150);
    CHECK_EQ(s.snapOffX, 100);
    CHECK_EQ(s.snapOffY, 50);
    // snapshot of the band before reprogramming
    CHECK_EQ(s.snapBandHiX, 0x316);
    CHECK_EQ(s.snapBandHiY, 0x257);
    // reprogrammed band: loX = anchorX - snapOffX = 300-100 = 200
    CHECK_EQ(s.bandLoX, 200);
    // hiX = worldW - 512 + anchorX - snapOffX = 1000-512+300-100 = 688
    CHECK_EQ(s.bandHiX, 688);
    // loY = anchorY - snapOffY = 150-50 = 100
    CHECK_EQ(s.bandLoY, 100);
    // hiY = worldH - 360 + anchorY - snapOffY = 800-360+150-50 = 540
    CHECK_EQ(s.bandHiY, 540);
    CHECK_EQ(s.bandArmed, 0);
}

TEST(EdgeScrollW20_MapScroll, PressFrameDeltaZeroNoChange) {
    MapScrollState s = MakeState();
    int cx = 300 << 16, cy = 150 << 16;
    // dragOrigin == cursor at press -> delta 0
    int r = MapView_UpdateScrollState(s, 1, cx, cy, cx, cy);
    CHECK_EQ(r, 0);             // offsets unchanged
    CHECK_EQ(s.offX, 100);
    CHECK_EQ(s.offY, 50);
}

TEST(EdgeScrollW20_MapScroll, HeldDragSlidesOffsetByDelta) {
    MapScrollState s = MakeState();
    // Frame 1: press at (300,150)
    MapView_UpdateScrollState(s, 1, 300 << 16, 150 << 16, 300 << 16, 150 << 16);
    // Frame 2: still held; drag origin moved to (320,170) -> delta (+20,+20)
    int r = MapView_UpdateScrollState(s, 1, 320 << 16, 170 << 16, 320 << 16, 170 << 16);
    CHECK_EQ(r, 1);
    CHECK_EQ(s.offX, 120);   // snapOffX 100 + 20
    CHECK_EQ(s.offY, 70);    // snapOffY 50  + 20
    CHECK_EQ(s.latched, 1);  // still latched
}

TEST(EdgeScrollW20_MapScroll, HeldDragTruncatesTowardZero) {
    // ConvertX truncates toward zero. The deltas here come from (>>16) ints so they
    // are integer-valued; verify a negative drag truncates as integer subtraction
    // (no rounding artifacts) and slides the offset by exactly that.
    MapScrollState s = MakeState();
    MapView_UpdateScrollState(s, 1, 300 << 16, 150 << 16, 300 << 16, 150 << 16);
    int r = MapView_UpdateScrollState(s, 1, 290 << 16, 140 << 16, 290 << 16, 140 << 16);
    CHECK_EQ(r, 1);
    CHECK_EQ(s.offX, 90);    // 100 + (290-300) = 90
    CHECK_EQ(s.offY, 40);    // 50  + (140-150) = 40
}

TEST(EdgeScrollW20_MapScroll, ReleaseRestoresBandAndUnlatches) {
    MapScrollState s = MakeState();
    MapView_UpdateScrollState(s, 1, 300 << 16, 150 << 16, 300 << 16, 150 << 16);
    // release: pressFlag 0, latched 1 -> block 2
    int r = MapView_UpdateScrollState(s, 0, 0, 0, 0, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(s.bandHiX, 0x316);  // restored
    CHECK_EQ(s.bandHiY, 0x257);
    CHECK_EQ(s.bandLoX, 0);
    CHECK_EQ(s.bandLoY, 0);
    CHECK_EQ(s.bandArmed, 1);
    CHECK_EQ(s.latched, 0);
    CHECK_EQ(s.anchorX, 0);      // dword_63D4F4 = dword_672238 (0)
    CHECK_EQ(s.anchorY, 0);
}

TEST(EdgeScrollW20_MapScroll, IdleNotPressedNotLatchedNoop) {
    MapScrollState s = MakeState();
    int r = MapView_UpdateScrollState(s, 0, 0, 0, 0, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(s.offX, 100);
    CHECK_EQ(s.latched, 0);
}

// --- 0x4bc07c Hud_UpdateEdgeScroll re-attach -------------------------------
static int g_freeAnim, g_repose, g_listener, g_selReset;
static void Spy_FreeAnim() { ++g_freeAnim; }
static void Spy_Repose() { ++g_repose; }
static void Spy_Listener() { ++g_listener; }
static void Spy_SelReset() { ++g_selReset; }
static EdgeScrollHooks MakeEsHooks() {
    EdgeScrollHooks h;
    h.freeObjAnim = &Spy_FreeAnim; h.setNodePose = &Spy_Repose;
    h.updateListener = &Spy_Listener; h.selectionReset = &Spy_SelReset;
    return h;
}
static void ResetEs() { g_freeAnim = g_repose = g_listener = g_selReset = 0; }

TEST(EdgeScrollW20_Reattach, DetachedAndSettledReattaches) {
    ResetEs();
    EdgeScrollState s; s.detached = 1; s.detach2 = 1; s.settled = true;
    s.selectionLive = 1;
    int d = Hud_UpdateEdgeScroll(s, MakeEsHooks());
    CHECK_EQ(d, 0);                 // re-attached (detached cleared)
    CHECK_EQ(s.detached, 0);
    CHECK_EQ(s.detach2, 0);
    CHECK_EQ(g_freeAnim, 1);
    CHECK_EQ(g_repose, 1);          // settled -> repose
    CHECK_EQ(g_listener, 1);
    CHECK_EQ(g_selReset, 1);        // selection live -> reset
}

TEST(EdgeScrollW20_Reattach, DetachedNotSettledStillClearsNoRepose) {
    ResetEs();
    EdgeScrollState s; s.detached = 1; s.detach2 = 1; s.settled = false;
    s.selectionLive = 0;
    int d = Hud_UpdateEdgeScroll(s, MakeEsHooks());
    CHECK_EQ(d, 0);
    CHECK_EQ(s.detached, 0);
    CHECK_EQ(g_freeAnim, 1);
    CHECK_EQ(g_repose, 0);          // not settled -> no repose
    CHECK_EQ(g_listener, 1);
    CHECK_EQ(g_selReset, 0);        // selection not live
}

TEST(EdgeScrollW20_Reattach, NotDetachedSettledOnlySelReset) {
    ResetEs();
    EdgeScrollState s; s.detached = 0; s.settled = true; s.selectionLive = 1;
    int d = Hud_UpdateEdgeScroll(s, MakeEsHooks());
    CHECK_EQ(d, 0);
    CHECK_EQ(g_freeAnim, 0);
    CHECK_EQ(g_listener, 0);        // settled non-detached path: no listener update
    CHECK_EQ(g_selReset, 1);
}

TEST(EdgeScrollW20_Reattach, NotDetachedNotSettledUpdatesListener) {
    ResetEs();
    EdgeScrollState s; s.detached = 0; s.settled = false; s.selectionLive = 0;
    Hud_UpdateEdgeScroll(s, MakeEsHooks());
    CHECK_EQ(g_listener, 1);
    CHECK_EQ(g_selReset, 0);
}
