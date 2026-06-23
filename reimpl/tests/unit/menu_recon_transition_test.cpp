// =============================================================================
// Golden-vector tests for the menu_recon_transition cluster:
//   VIBE_Hotspot_Register/Remove, VIBE_Fade_UpdateAll/UnregisterAll,
//   VIBE_Transition_FadeOutToBlack/FadeInScene.
// Self-contained: depends only on the new header + the test framework.
// =============================================================================
#include "play/menu_recon_transition.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild;
using namespace guild::play;

// ---------------------------------------------------------------------------
// Hotspot_Register: indices, cap, mirror, field placement.
// ---------------------------------------------------------------------------
TEST(MenuReconHotspot, RegisterAssignsIncrementingIndices) {
    ResetMenuReconTransition();
    // First register -> index 1 (count pre-increments from 0).
    i32 i1 = Hotspot_Register(/*x*/10, /*y*/20, /*cx*/48, /*bx*/49, /*payload*/0x1111);
    CHECK_EQ(i1, 1);
    i32 i2 = Hotspot_Register(100, 200, 7, 8, 0x2222);
    CHECK_EQ(i2, 2);

    HotspotTable& t = Hotspots();
    CHECK_EQ(t.count, 2);
    CHECK_EQ(t.mirror, 2);   // dword_62D31C mirrors the count

    // Field placement (a1->w0, a2->w1, a4(bx)->w2, a3(cx)->w3, a5->payload, active=1).
    CHECK_EQ((int)t.recs[1].w0, 10);
    CHECK_EQ((int)t.recs[1].w1, 20);
    CHECK_EQ((int)t.recs[1].w2, 49);   // bx
    CHECK_EQ((int)t.recs[1].w3, 48);   // cx
    CHECK_EQ(t.recs[1].payload, 0x1111);
    CHECK_EQ(t.recs[1].active, 1);
}

TEST(MenuReconHotspot, RegisterCapAt64ReturnsZeroButStillIncrements) {
    ResetMenuReconTransition();
    HotspotTable& t = Hotspots();
    t.count = 64;  // table already full
    i32 r = Hotspot_Register(1, 2, 3, 4, 5);
    CHECK_EQ(r, 0);          // v6 == 65 > 64 -> return 0
    CHECK_EQ(t.count, 65);   // count still incremented (faithful to original)
    CHECK_EQ(t.mirror, 65);
}

TEST(MenuReconHotspot, RegisterAtIndex64Succeeds) {
    ResetMenuReconTransition();
    HotspotTable& t = Hotspots();
    t.count = 63;
    i32 r = Hotspot_Register(9, 9, 9, 9, 0xABCD);
    CHECK_EQ(r, 64);                 // v6 == 64, not > 64
    CHECK_EQ(t.recs[64].active, 1);
    CHECK_EQ(t.recs[64].payload, 0xABCD);
}

TEST(MenuReconHotspot, RemoveZeroesRecordAndDecrements) {
    ResetMenuReconTransition();
    Hotspot_Register(10, 20, 48, 48, 0x55);
    Hotspot_Register(30, 40, 8, 8, 0x66);
    HotspotTable& t = Hotspots();
    CHECK_EQ(t.count, 2);

    Hotspot_Remove(2);
    CHECK_EQ(t.count, 1);
    CHECK_EQ(t.recs[2].active, 0);   // 16-byte record zeroed
    CHECK_EQ((int)t.recs[2].w0, 0);
    CHECK_EQ(t.recs[2].payload, 0);
}

TEST(MenuReconHotspot, RemoveNoOpWhenEmptyOrNegativeIndex) {
    ResetMenuReconTransition();
    Hotspots().count = 0;
    Hotspot_Remove(1);               // count < 1 -> no-op
    CHECK_EQ(Hotspots().count, 0);

    Hotspots().count = 5;
    Hotspot_Remove(-1);              // index < 0 -> no-op
    CHECK_EQ(Hotspots().count, 5);
}

// ---------------------------------------------------------------------------
// Fade_UpdateAll / Fade_UnregisterAll: sweep semantics.
// ---------------------------------------------------------------------------
namespace {
std::vector<i32> g_updated;
std::vector<i32> g_unregistered;
i32              g_lastBack = -1;

void RecUpdate(i32 slot, i32 back) { g_updated.push_back(slot); g_lastBack = back; }
void RecUnreg(i32 slot, i32 /*arg*/) { g_unregistered.push_back(slot); }
} // namespace

TEST(MenuReconFadeAll, UpdateAllVisitsOnlyNonEmptySlots) {
    ResetMenuReconTransition();
    g_updated.clear();
    FadeSlotTable& tab = FadeSlots();
    tab.slots[0]  = 111;
    tab.slots[3]  = 222;   // gap of empties between
    tab.slots[31] = 333;   // last slot

    FadeAllHooks hk;
    hk.fadeUpdate = &RecUpdate;
    hk.fadeUnregister = &RecUnreg;
    hk.backSurface = 7;    // dword_62D210
    const FadeAllHooks* prev = SetFadeAllHooks(&hk);

    Fade_UpdateAll();

    CHECK_EQ((int)g_updated.size(), 3);
    CHECK_EQ(g_updated[0], 111);
    CHECK_EQ(g_updated[1], 222);
    CHECK_EQ(g_updated[2], 333);
    CHECK_EQ(g_lastBack, 7);

    // Slots are NOT cleared by UpdateAll.
    CHECK_EQ(tab.slots[0], 111);
    CHECK_EQ(tab.slots[31], 333);
    SetFadeAllHooks(prev);
}

TEST(MenuReconFadeAll, UpdateAllEmptyTableDoesNothing) {
    ResetMenuReconTransition();
    g_updated.clear();
    FadeAllHooks hk; hk.fadeUpdate = &RecUpdate; hk.fadeUnregister = &RecUnreg;
    const FadeAllHooks* prev = SetFadeAllHooks(&hk);
    Fade_UpdateAll();
    CHECK_EQ((int)g_updated.size(), 0);
    SetFadeAllHooks(prev);
}

TEST(MenuReconFadeAll, UnregisterAllUnregistersAndZeroesSlots) {
    ResetMenuReconTransition();
    g_unregistered.clear();
    FadeSlotTable& tab = FadeSlots();
    tab.slots[1]  = 11;
    tab.slots[2]  = 22;
    tab.slots[30] = 30;

    FadeAllHooks hk;
    hk.fadeUpdate = &RecUpdate;
    hk.fadeUnregister = &RecUnreg;
    const FadeAllHooks* prev = SetFadeAllHooks(&hk);

    Fade_UnregisterAll(/*arg*/0xDEAD);

    CHECK_EQ((int)g_unregistered.size(), 3);
    CHECK_EQ(g_unregistered[0], 11);
    CHECK_EQ(g_unregistered[1], 22);
    CHECK_EQ(g_unregistered[2], 30);
    // All swept slots zeroed.
    CHECK_EQ(tab.slots[1], 0);
    CHECK_EQ(tab.slots[2], 0);
    CHECK_EQ(tab.slots[30], 0);
    SetFadeAllHooks(prev);
}

// ---------------------------------------------------------------------------
// Transitions: frame-state word math + scene-rect writes + call ordering.
// ---------------------------------------------------------------------------
namespace {
struct TransRec {
    std::vector<int> calls;           // sequence tag log
    std::vector<int> fadeDurations;   // each fadeRegister dur
    int frameStateSeen = 0;           // v7 passed to first RunFrameLoop
    int doneCallsBeforeTrue = 0;      // controls fadeDone gating
    int doneCounter = 0;
    int sceneSnapshot = 0;
};
TransRec g_tr;

i32 TRFadeRegister(i32, i32, i32, i32, const char*, i32 dur, i32) {
    g_tr.calls.push_back(1); g_tr.fadeDurations.push_back(dur); return 0x1000 + dur;
}
bool TRFadeDone(i32) {
    // Return false for the first `doneCallsBeforeTrue` queries, then true.
    if (g_tr.doneCounter < g_tr.doneCallsBeforeTrue) { ++g_tr.doneCounter; return false; }
    return true;
}
void TRRunFrame(i32 state, i32, void*) { g_tr.calls.push_back(2); g_tr.frameStateSeen = state; }
void TRUnreg(i32, void*) { g_tr.calls.push_back(3); }
void TRHud(i32) { g_tr.calls.push_back(4); }
void TRScene(i32, i32) { g_tr.calls.push_back(5); }
void TRList(i32) { g_tr.calls.push_back(6); }
void TRGround(i32) { g_tr.calls.push_back(7); }
void TRGroundFade(void*) { g_tr.calls.push_back(8); }
} // namespace

TEST(MenuReconTransition, FadeOutToBlackFrameStateMathAndRectAndOrder) {
    ResetMenuReconTransition();
    g_tr = TransRec{};
    g_tr.doneCallsBeforeTrue = 0;     // fade already done -> the do/while still runs once

    // dword_11BC2D0 starts with bit 0x2000 set + some other bits; verify the
    // v7 = (x | 0x100000) with byte1 &= 0xDF transform.
    FrameStateWord() = 0x00002500;    // byte1 = 0x25, has 0x20 set

    TransitionHooks hk;
    hk.fadeRegister = &TRFadeRegister;
    hk.fadeDone = &TRFadeDone;
    hk.runFrameLoop = &TRRunFrame;
    hk.fadeUnregister = &TRUnreg;
    hk.hudToggleHighlight = &TRHud;
    hk.renderEntityScene = &TRScene;
    hk.renderEntityList = &TRList;
    hk.groundplanSetVisible = &TRGround;
    hk.groundplanFadeIn = &TRGroundFade;
    hk.scrW = 640; hk.scrH = 480;
    const TransitionHooks* prev = SetTransitionHooks(&hk);

    i32 ret = Transition_FadeOutToBlack(nullptr);

    // v7 math (verified in the separate pump test): 0x2500 | 0x100000 = 0x102500;
    // byte1 (0x25) & 0xDF = 0x05 -> 0x100500. With doneCallsBeforeTrue==0 the
    // `if ((*h & 4)==0)` guard is false, so the do/while frame loop never runs here.

    // Scene rect: x80=scrH, x88=0, x84=0, x8c=scrW.
    SceneRect& r = SceneRectGlobals();
    CHECK_EQ(r.x80, 480);
    CHECK_EQ(r.x88, 0);
    CHECK_EQ(r.x84, 0);
    CHECK_EQ(r.x8c, 640);

    // Frame state word set to 147591 afterward.
    CHECK_EQ(FrameStateWord(), 147591);

    // Two fade registers: durations 30 then 50; return is the trailing handle.
    CHECK_EQ((int)g_tr.fadeDurations.size(), 2);
    CHECK_EQ(g_tr.fadeDurations[0], 30);
    CHECK_EQ(g_tr.fadeDurations[1], 50);
    CHECK_EQ(ret, 0x1000 + 50);

    // Call order after the (skipped) loop: register(1), hud(4), scene(5), list(6),
    // unreg(3), ground(7), register(1).
    std::vector<int> expect = {1, 4, 5, 6, 3, 7, 1};
    CHECK_EQ((int)g_tr.calls.size(), (int)expect.size());
    for (size_t i = 0; i < expect.size() && i < g_tr.calls.size(); ++i)
        CHECK_EQ(g_tr.calls[i], expect[i]);
    SetTransitionHooks(prev);
}

TEST(MenuReconTransition, FadeOutToBlackPumpsFrameLoopUntilDone) {
    ResetMenuReconTransition();
    g_tr = TransRec{};
    g_tr.doneCallsBeforeTrue = 3;     // fadeDone false 3x: guard(1)+2 loop iters... see below
    FrameStateWord() = 0x00002500;

    TransitionHooks hk;
    hk.fadeRegister = &TRFadeRegister;
    hk.fadeDone = &TRFadeDone;
    hk.runFrameLoop = &TRRunFrame;
    hk.fadeUnregister = &TRUnreg;
    hk.hudToggleHighlight = &TRHud;
    hk.renderEntityScene = &TRScene;
    hk.renderEntityList = &TRList;
    hk.groundplanSetVisible = &TRGround;
    hk.groundplanFadeIn = &TRGroundFade;
    hk.scrW = 800; hk.scrH = 600;
    const TransitionHooks* prev = SetTransitionHooks(&hk);

    Transition_FadeOutToBlack(nullptr);

    // The frame loop must have run at least once and seen v7 = 0x100500.
    int frames = 0;
    for (int c : g_tr.calls) if (c == 2) ++frames;
    CHECK(frames >= 1);
    CHECK_EQ(g_tr.frameStateSeen, 0x100500);
    SetTransitionHooks(prev);
}

TEST(MenuReconTransition, FadeInSceneSnapshotPathShortCircuits) {
    ResetMenuReconTransition();
    g_tr = TransRec{};
    g_tr.doneCallsBeforeTrue = 0;

    TransitionHooks hk;
    hk.fadeRegister = &TRFadeRegister;
    hk.fadeDone = &TRFadeDone;
    hk.runFrameLoop = &TRRunFrame;
    hk.fadeUnregister = &TRUnreg;
    hk.hudToggleHighlight = &TRHud;
    hk.renderEntityScene = &TRScene;
    hk.renderEntityList = &TRList;
    hk.groundplanSetVisible = &TRGround;
    hk.groundplanFadeIn = &TRGroundFade;
    hk.scrW = 640; hk.scrH = 480;
    hk.sceneSnapshot = 0x999;    // dword_63CC30 != 0 -> short path
    const TransitionHooks* prev = SetTransitionHooks(&hk);

    Transition_FadeInScene(nullptr);

    // Snapshot path: register(1), scene(5), unreg(3). No groundplan / second register.
    std::vector<int> expect = {1, 5, 3};
    CHECK_EQ((int)g_tr.calls.size(), (int)expect.size());
    for (size_t i = 0; i < expect.size() && i < g_tr.calls.size(); ++i)
        CHECK_EQ(g_tr.calls[i], expect[i]);
    CHECK_EQ((int)g_tr.fadeDurations.size(), 1);   // only the 30-dur fade
    SetTransitionHooks(prev);
}

TEST(MenuReconTransition, FadeInSceneNoSnapshotRestoresRectAndFullSequence) {
    ResetMenuReconTransition();
    g_tr = TransRec{};
    g_tr.doneCallsBeforeTrue = 0;

    TransitionHooks hk;
    hk.fadeRegister = &TRFadeRegister;
    hk.fadeDone = &TRFadeDone;
    hk.runFrameLoop = &TRRunFrame;
    hk.fadeUnregister = &TRUnreg;
    hk.hudToggleHighlight = &TRHud;
    hk.renderEntityScene = &TRScene;
    hk.renderEntityList = &TRList;
    hk.groundplanSetVisible = &TRGround;
    hk.groundplanFadeIn = &TRGroundFade;
    hk.scrW = 640; hk.scrH = 480;
    hk.sceneSnapshot = 0;        // no snapshot -> full restore path
    hk.srcLeft = 11; hk.srcTop = 22; hk.srcRight = 33; hk.srcBottom = 44;
    const TransitionHooks* prev = SetTransitionHooks(&hk);

    i32 ret = Transition_FadeInScene(nullptr);

    // Rect restore: x88=srcBottom(44), x80=srcLeft(11), x84=srcTop(22), x8c=srcRight(33).
    SceneRect& r = SceneRectGlobals();
    CHECK_EQ(r.x88, 44);
    CHECK_EQ(r.x80, 11);
    CHECK_EQ(r.x84, 22);
    CHECK_EQ(r.x8c, 33);

    // Sequence: register(1), scene(5), ground-visible(7), ground-fade(8), hud(4),
    // unreg(3), register(1).
    std::vector<int> expect = {1, 5, 7, 8, 4, 3, 1};
    CHECK_EQ((int)g_tr.calls.size(), (int)expect.size());
    for (size_t i = 0; i < expect.size() && i < g_tr.calls.size(); ++i)
        CHECK_EQ(g_tr.calls[i], expect[i]);
    CHECK_EQ(ret, 0x1000 + 50);     // trailing 50-dur fade handle
    SetTransitionHooks(prev);
}
