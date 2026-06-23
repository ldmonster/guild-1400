#include "tests/framework/test.h"

#include "render/frame.h"

#include <string>
#include <vector>

// =============================================================================
// WAVE-13 W13-FRAME — golden 1:1 pins for the render frame spine
// (src/render/frame.cpp):
//
//   0x5B6074  RenderMainViewFrame   (gate + clear-selection + RenderUniverseFrame)
//   0x5B3DE8  RenderUniverseFrame   (BeginUniverseFrame -> DrawUniverseAndStats)
//   0x5B3900  BeginUniverseFrame    (reentrancy guard, clear, depth-bound reset,
//                                     terrain, light reset, scene walk, particles,
//                                     sky flares, mirrors, sentinel collapse)
//   0x5B3BBC  DrawUniverseAndStats  (reentrancy guard, anim walk, the two
//                                     fps-window computations, frame counter)
//
// Every value asserted here is sourced from the provenance comments in
// render/frame.{h,cpp} (the recovered Hex-Rays pseudocode) — no invented numbers.
// These pin the control-flow edges (the reentrancy guard, the engine/world gate,
// the clear-selection branches), the EXACT subsystem call ORDER, the 1e10 near
// sentinel collapse, and the fps-window integer arithmetic + the `>0x3C`/`>0xA`
// unsigned-compare thresholds.
// =============================================================================
using namespace guild;
using namespace guild::render;

namespace {

// A recorder that captures the order the frame spine invokes its subsystem hooks.
// The FrameHooks are plain C function pointers, so we route them through a
// process-global recorder pointer (the same trampoline pattern the live drivers
// use). One test at a time -> a single global is fine for the unit test.
struct FrameTrace {
    std::vector<std::string> order;
    char                     terrainA2 = -1;
    char                     sceneA2 = -1;
    char                     particlesA2 = -1;
    char                     mirrorsA2 = -1;
    char                     animA2 = -1;
    i32                      sceneReturn = 0;
    i32                      timeValue = 0;
    // wave-15: the resolved DrawUniverseAndStats a2-block hooks.
    i32                      scrollT = -1;       // ScrollUvCoords(t) argument
    i16                      projectFlags = -1;  // projectWalk flags arg
    i32                      projectT = -1;      // projectWalk t arg
    int                      animNodeCalls = 0;  // # of animPose invocations
    i16                      animPoseFlags = -1; // flags passed to animPose
};

FrameTrace* g_trace = nullptr;

void HClearViewport() { g_trace->order.push_back("clearViewport"); }
void HClearRect() { g_trace->order.push_back("clearRect"); }
void HRenderTerrain(void* /*t*/, char a2) {
    g_trace->order.push_back("terrain");
    g_trace->terrainA2 = a2;
}
void HResetLights() { g_trace->order.push_back("resetLights"); }
i32  HSceneWalk(char a2) {
    g_trace->order.push_back("sceneWalk");
    g_trace->sceneA2 = a2;
    return g_trace->sceneReturn;
}
void HRenderParticles(char a2) {
    g_trace->order.push_back("particles");
    g_trace->particlesA2 = a2;
}
void HUpdateSkyFlares() { g_trace->order.push_back("skyFlares"); }
void HBuildMirrors(char a2) {
    g_trace->order.push_back("mirrors");
    g_trace->mirrorsA2 = a2;
}
i32  HTimeNow() { return g_trace->timeValue; }

// wave-15: the DrawUniverseAndStats a2-block hooks (ScrollUvCoords, the
// unconditional projection walk, and the a3-gated 64-list animation pose walk).
void HScrollUv(i32 t) {
    g_trace->order.push_back("scrollUv");
    g_trace->scrollT = t;
}
void HProjectWalk(i16 flags, i32 t) {
    g_trace->order.push_back("projectWalk");
    g_trace->projectFlags = flags;
    g_trace->projectT = t;
}
// The 64-list anim walk: a single linked-list node per non-null list head, so
// each fired list contributes exactly one animPose call (next == sentinel).
static int   g_sentinel = 0;
static int   g_node = 0;
void* HAnimListHead(int k) {
    // Only list index 0 has a non-empty linked list; all others are empty.
    return (k == 0) ? (void*)&g_node : nullptr;
}
void* HAnimSentinel() { return (void*)&g_sentinel; }
void* HAnimNext(void* /*node*/) { return (void*)&g_sentinel; } // 1-node list
void  HAnimPose(void* /*node*/, i16 flags, i32 /*t*/) {
    g_trace->order.push_back("anim");
    ++g_trace->animNodeCalls;
    g_trace->animPoseFlags = flags;
}

FrameHooks FullHooks() {
    FrameHooks h{};
    h.clearViewport = &HClearViewport;
    h.clearRect = &HClearRect;
    h.renderTerrain = &HRenderTerrain;
    h.resetLights = &HResetLights;
    h.sceneWalk = &HSceneWalk;
    h.renderParticles = &HRenderParticles;
    h.updateSkyFlares = &HUpdateSkyFlares;
    h.buildMirrors = &HBuildMirrors;
    h.scrollUvCoords = &HScrollUv;
    h.projectWalk = &HProjectWalk;
    h.animListHead = &HAnimListHead;
    h.animSentinel = &HAnimSentinel;
    h.animNext = &HAnimNext;
    h.animPose = &HAnimPose;
    h.timeNow = &HTimeNow;
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// BeginUniverseFrame — the EXACT subsystem call order (the recovered sequence:
// clear -> terrain -> resetLights -> sceneWalk -> particles -> skyFlares ->
// mirrors). a2 is threaded into every a2-taking hook.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, BeginUniverseFrameSubsystemOrder) {
    FrameTrace tr;
    g_trace = &tr;
    tr.sceneReturn = 77;

    FrameState fs;
    fs.engineOn = true;
    fs.hasWorld = true;
    fs.hasTerrain = true;     // dword_64A028 != 0 -> terrain runs
    fs.useViewportClear = true;  // -> ClearViewport branch
    fs.reentrancy = 0;

    FrameHooks h = FullHooks();
    BeginUniverseFrame(fs, h, /*a2*/ 1);

    // Recovered order: clear (viewport here), terrain, resetLights, sceneWalk,
    // particles, skyFlares, mirrors. (No anim — that is DrawUniverseAndStats.)
    CHECK_EQ((int)tr.order.size(), 7);
    CHECK(tr.order[0] == "clearViewport");
    CHECK(tr.order[1] == "terrain");
    CHECK(tr.order[2] == "resetLights");
    CHECK(tr.order[3] == "sceneWalk");
    CHECK(tr.order[4] == "particles");
    CHECK(tr.order[5] == "skyFlares");
    CHECK(tr.order[6] == "mirrors");

    // a2 threaded through.
    CHECK_EQ((int)tr.terrainA2, 1);
    CHECK_EQ((int)tr.sceneA2, 1);
    CHECK_EQ((int)tr.particlesA2, 1);
    CHECK_EQ((int)tr.mirrorsA2, 1);

    // sceneWalk return snapshotted into appendedPolys.
    CHECK_EQ(fs.appendedPolys, 77);
    // Reentrancy guard balanced (++ then --).
    CHECK_EQ(fs.reentrancy, 0);
    g_trace = nullptr;
}

// ---------------------------------------------------------------------------
// BeginUniverseFrame — clear-selection: useViewportClear=false picks ClearRect.
// (RenderMainViewFrame gates the rect on clearSuppressed; Begin always re-clears
// via the rect for the non-main-view callers.)
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, BeginUniverseFrameClearRectBranch) {
    FrameTrace tr;
    g_trace = &tr;
    FrameState fs;
    fs.hasTerrain = false;            // no terrain hook fired
    fs.useViewportClear = false;      // -> ClearRect branch
    FrameHooks h = FullHooks();
    BeginUniverseFrame(fs, h, /*a2*/ 0);
    CHECK(tr.order[0] == "clearRect");
    // No terrain (hasTerrain false) -> resetLights is next.
    CHECK(tr.order[1] == "resetLights");
    g_trace = nullptr;
}

// ---------------------------------------------------------------------------
// BeginUniverseFrame — running-depth-bound reset + the 1e10 near sentinel
// collapse. runningNear is reset to 1e10; if no subsystem expanded it, the
// untouched sentinel collapses to 0.0 (the original's flt_62838C sentinel fix).
// runningFar resets to 0.0.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, BeginUniverseFrameDepthSentinelCollapse) {
    FrameTrace tr;
    g_trace = &tr;
    FrameState fs;
    fs.runningNear = 12345.0f;   // garbage prior value
    fs.runningFar = 999.0f;
    FrameHooks h = FullHooks();
    BeginUniverseFrame(fs, h, 1);
    // runningFar reset to 0; runningNear sentinel (1e10, untouched) -> 0.
    CHECK_EQ(fs.runningFar, 0.0f);
    CHECK_EQ(fs.runningNear, 0.0f);
    g_trace = nullptr;
}

// ---------------------------------------------------------------------------
// BeginUniverseFrame — gates: engineOff / no-world / nonzero-reentrancy all skip
// the body entirely (no hooks fire). The negative-reentrancy clamp runs first.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, BeginUniverseFrameGates) {
    FrameHooks h = FullHooks();

    // engine off -> no work.
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.engineOn = false;
        BeginUniverseFrame(fs, h, 1);
        CHECK_EQ((int)tr.order.size(), 0);
        g_trace = nullptr;
    }
    // no active world -> no work.
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.hasWorld = false;
        BeginUniverseFrame(fs, h, 1);
        CHECK_EQ((int)tr.order.size(), 0);
        g_trace = nullptr;
    }
    // already re-entered (reentrancy != 0) -> no work (guard).
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.reentrancy = 1;
        BeginUniverseFrame(fs, h, 1);
        CHECK_EQ((int)tr.order.size(), 0);
        CHECK_EQ(fs.reentrancy, 1);   // untouched
        g_trace = nullptr;
    }
    // negative reentrancy clamped to 0 first, then runs normally.
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.reentrancy = -5;
        BeginUniverseFrame(fs, h, 1);
        CHECK(tr.order.size() > 0);    // ran (clamp let it through)
        CHECK_EQ(fs.reentrancy, 0);    // clamped, then ++/-- balanced
        g_trace = nullptr;
    }
}

// ---------------------------------------------------------------------------
// DrawUniverseAndStats — fps WINDOW A fires only when dt > 0x3C (60). The
// computed value is 1000 * frames / (dt * uDelay) with integer division.
//   First call: frames accumulates, no report (dt below threshold from t=0).
// We drive two calls with a controlled clock to land dt past the threshold.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, DrawUniverseStatsFpsWindowA) {
    FrameTrace tr;
    g_trace = &tr;
    FrameState fs;
    fs.uDelay = 1;
    fs.fpsLastTimeA = 0;
    fs.fpsLastTimeB = 0;
    fs.fpsFrameAccA = 0;
    fs.fpsFrameAccB = 0;
    FrameHooks h = FullHooks();

    // Call 1 at t=100: dtA = 100-0 = 100 (>0x3C). frames = accA+1 = 1.
    // valueA = 1000 * 1 / (100 * 1) = 10. Then accA reset, lastTimeA = 100.
    tr.timeValue = 100;
    DrawUniverseAndStats(fs, h, /*a2*/ 1, /*a3*/ 1, /*a4*/ 0);
    CHECK_EQ(fs.fpsValueA, 10);
    CHECK_EQ(fs.fpsFrameAccA, 0);
    CHECK_EQ(fs.fpsLastTimeA, 100);

    // Call 2 at t=120: dtA = 120-100 = 20 (NOT >0x3C) -> no report, accA bumps.
    tr.timeValue = 120;
    DrawUniverseAndStats(fs, h, 1, 1, 0);
    CHECK_EQ(fs.fpsValueA, 10);        // unchanged
    CHECK_EQ(fs.fpsFrameAccA, 1);      // accumulated
    CHECK_EQ(fs.fpsLastTimeA, 100);    // unchanged

    g_trace = nullptr;
}

// ---------------------------------------------------------------------------
// DrawUniverseAndStats — fps WINDOW B fires when dt > 0xA (10). After two frames
// the accumulator (post-increment) is the numerator.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, DrawUniverseStatsFpsWindowB) {
    FrameTrace tr;
    g_trace = &tr;
    FrameState fs;
    fs.uDelay = 1;
    fs.fpsLastTimeA = 0;
    fs.fpsLastTimeB = 0;
    FrameHooks h = FullHooks();

    // Call 1 at t=5: dtB = 5 (NOT >0xA) -> accB bumps to 1, no report.
    tr.timeValue = 5;
    DrawUniverseAndStats(fs, h, 1, 1, 0);
    CHECK_EQ(fs.fpsFrameAccB, 1);
    CHECK_EQ(fs.fpsValueB, 0);

    // Call 2 at t=25: dtB = 25-0 = 25 (>0xA), accB post-increment = 2.
    // valueB = 1000 * 2 / (25 * 1) = 80. accB reset, lastTimeB = 25.
    tr.timeValue = 25;
    DrawUniverseAndStats(fs, h, 1, 1, 0);
    CHECK_EQ(fs.fpsValueB, 80);
    CHECK_EQ(fs.fpsFrameAccB, 0);
    CHECK_EQ(fs.fpsLastTimeB, 25);

    g_trace = nullptr;
}

// ---------------------------------------------------------------------------
// DrawUniverseAndStats — uDelay divides the denominator (1000*frames/(dt*uDelay)).
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, DrawUniverseStatsUDelayDivisor) {
    FrameTrace tr;
    g_trace = &tr;
    FrameState fs;
    fs.uDelay = 4;                 // denominator scaled by 4
    fs.fpsLastTimeA = 0;
    fs.fpsLastTimeB = 0;
    FrameHooks h = FullHooks();
    // t=100: dtA=100>0x3C. valueA = 1000*1/(100*4) = 1000/400 = 2 (int div).
    tr.timeValue = 100;
    DrawUniverseAndStats(fs, h, 1, 1, 0);
    CHECK_EQ(fs.fpsValueA, 2);
    g_trace = nullptr;
}

// ---------------------------------------------------------------------------
// DrawUniverseAndStats — a3 gates the anim pose walk; a4 bumps the frame counter;
// a2=0 suppresses the whole fps/anim block.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, DrawUniverseStatsAnimAndFrameCounterGates) {
    FrameHooks h = FullHooks();

    // a3=0 -> scrollUv + projectWalk fire (a2 block) but NO anim walk.
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.fpsLastTimeA = 0; fs.fpsLastTimeB = 0;
        tr.timeValue = 100;
        DrawUniverseAndStats(fs, h, /*a2*/1, /*a3*/0, /*a4*/0);
        CHECK_EQ((int)tr.order.size(), 2);   // scrollUv, projectWalk only
        CHECK(tr.order[0] == "scrollUv");
        CHECK(tr.order[1] == "projectWalk");
        CHECK_EQ(tr.scrollT, 100);
        CHECK_EQ(tr.animNodeCalls, 0);
        g_trace = nullptr;
    }
    // a3=1 -> the 64-list anim walk fires (one non-empty list => one pose call).
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.fpsLastTimeA = 0; fs.fpsLastTimeB = 0;
        tr.timeValue = 100;
        DrawUniverseAndStats(fs, h, 1, 1, 0);
        CHECK_EQ((int)tr.order.size(), 3);   // scrollUv, projectWalk, anim
        CHECK(tr.order[0] == "scrollUv");
        CHECK(tr.order[1] == "projectWalk");
        CHECK(tr.order[2] == "anim");
        CHECK_EQ(tr.animNodeCalls, 1);
        // walk flags = (appendedPolys|0x181); fps appendedPolys default 0 => 0x181.
        CHECK_EQ((int)tr.projectFlags, 0x181);
        CHECK_EQ((int)tr.animPoseFlags, 0x181);
        g_trace = nullptr;
    }
    // a3=1 but animSkipIndex==0 -> the only non-empty list (index 0) is skipped.
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.fpsLastTimeA = 0; fs.fpsLastTimeB = 0;
        fs.animSkipIndex = 0;
        tr.timeValue = 100;
        DrawUniverseAndStats(fs, h, 1, 1, 0);
        CHECK_EQ(tr.animNodeCalls, 0);       // list 0 skipped, rest empty
        g_trace = nullptr;
    }
    // a4=1 -> frame counter bumps.
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs;
        DrawUniverseAndStats(fs, h, /*a2*/0, /*a3*/0, /*a4*/1);
        CHECK_EQ(fs.frameCounter, 1);
        g_trace = nullptr;
    }
    // a2=0 -> no anim/fps block at all (no hooks), but a4 still bumps.
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs;
        DrawUniverseAndStats(fs, h, /*a2*/0, /*a3*/1, /*a4*/1);
        CHECK_EQ((int)tr.order.size(), 0);
        CHECK_EQ(fs.frameCounter, 1);
        g_trace = nullptr;
    }
}

// ---------------------------------------------------------------------------
// DrawUniverseAndStats — the gate returns the appendedPolys snapshot unchanged
// when the engine/world gate blocks, and the reentrancy guard stays balanced.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, DrawUniverseStatsGateReturnsSnapshot) {
    FrameHooks h = FullHooks();
    FrameTrace tr; g_trace = &tr;
    FrameState fs;
    fs.appendedPolys = 42;
    fs.engineOn = false;            // gate blocks
    i32 r = DrawUniverseAndStats(fs, h, 1, 1, 1);
    CHECK_EQ(r, 42);
    CHECK_EQ(fs.frameCounter, 0);   // a4 bump did NOT run (gated out)
    g_trace = nullptr;
}

// ---------------------------------------------------------------------------
// RenderUniverseFrame — composition: Begin then DrawUniverseAndStats, returning
// the appended-poly snapshot. The Begin walk runs the full subsystem order, then
// Draw runs the fps/anim block; the return is the sceneWalk count.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, RenderUniverseFrameComposesBeginThenDraw) {
    FrameTrace tr; g_trace = &tr;
    tr.sceneReturn = 5;
    tr.timeValue = 0;
    FrameState fs;
    fs.hasTerrain = true;
    fs.useViewportClear = true;
    fs.fpsLastTimeA = 0; fs.fpsLastTimeB = 0;
    FrameHooks h = FullHooks();

    i32 r = RenderUniverseFrame(fs, h, /*a2*/1, /*a3*/1);

    // Begin's 7 subsystem calls, then Draw's a2 block: scrollUv, projectWalk,
    // anim (one non-empty list) = 10 total.
    CHECK_EQ((int)tr.order.size(), 10);
    CHECK(tr.order[0] == "clearViewport");
    CHECK(tr.order[7] == "scrollUv");
    CHECK(tr.order[8] == "projectWalk");
    CHECK(tr.order[9] == "anim");
    CHECK_EQ(r, 5);                 // appendedPolys snapshot
    CHECK_EQ(fs.appendedPolys, 5);
    // projectWalk flags = (appendedPolys|0x181); appendedPolys==5 => 5|0x181=0x185.
    CHECK_EQ((int)tr.projectFlags, 0x185);
    g_trace = nullptr;
}

// ---------------------------------------------------------------------------
// RenderMainViewFrame — gate + clear-selection. engineOn && reentrancy<=0:
//   useViewportClear -> ClearViewport ; else ClearRect unless clearSuppressed.
// Then RenderUniverseFrame (Begin+Draw) runs over the rest of the spine.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, RenderMainViewFrameClearSelection) {
    FrameHooks h = FullHooks();

    // useViewportClear -> ClearViewport is the FIRST clear, then Begin re-clears
    // (RenderMainViewFrame clears, then RenderUniverseFrame->Begin clears again).
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.useViewportClear = true; fs.hasTerrain = false;
        fs.fpsLastTimeA = 0; fs.fpsLastTimeB = 0;
        RenderMainViewFrame(fs, h);
        CHECK(tr.order[0] == "clearViewport");   // main-view clear
        g_trace = nullptr;
    }
    // !useViewportClear && !clearSuppressed -> ClearRect.
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.useViewportClear = false; fs.clearSuppressed = false;
        fs.hasTerrain = false; fs.fpsLastTimeA = 0; fs.fpsLastTimeB = 0;
        RenderMainViewFrame(fs, h);
        CHECK(tr.order[0] == "clearRect");
        g_trace = nullptr;
    }
    // clearSuppressed -> the main-view ClearRect is skipped; the first recorded
    // clear is Begin's re-clear (ClearRect, since !useViewportClear).
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.useViewportClear = false; fs.clearSuppressed = true;
        fs.hasTerrain = false; fs.fpsLastTimeA = 0; fs.fpsLastTimeB = 0;
        RenderMainViewFrame(fs, h);
        // Exactly ONE clear in the trace (Begin's), not two — main-view skipped.
        int clears = 0;
        for (auto& s : tr.order) if (s == "clearRect" || s == "clearViewport") ++clears;
        CHECK_EQ(clears, 1);
        g_trace = nullptr;
    }
}

// ---------------------------------------------------------------------------
// RenderMainViewFrame — the entry gate: engine off OR reentrancy>0 -> no work.
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, RenderMainViewFrameGate) {
    FrameHooks h = FullHooks();
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.engineOn = false;
        RenderMainViewFrame(fs, h);
        CHECK_EQ((int)tr.order.size(), 0);
        g_trace = nullptr;
    }
    {
        FrameTrace tr; g_trace = &tr;
        FrameState fs; fs.reentrancy = 1;   // > 0 -> gated
        RenderMainViewFrame(fs, h);
        CHECK_EQ((int)tr.order.size(), 0);
        g_trace = nullptr;
    }
}

// ---------------------------------------------------------------------------
// WAVE-15 — the resolved per-frame poly-counter resets (the wave-13
// NEEDS-LIVE-MCP queue). BeginUniverseFrame zeroes dword_64A060 / dword_64A058
// and the obj+492 +252/+256 counters (0x5b3982..0x5b39b8); DrawUniverseAndStats
// zeroes byte_64A068, obj+492 +252/+256, and dword_1408A64/68 (0x5b3c19..5b3c50).
// ---------------------------------------------------------------------------
TEST(RenderFrameSpine, BeginUniverseFramePolyCounterResets) {
    FrameTrace tr; g_trace = &tr;
    FrameHooks h = FullHooks();
    FrameState fs;
    // Dirty every counter; the frame must zero them.
    fs.polyCounterA = 7;     // dword_64A060
    fs.polyCounterB = 9;     // dword_64A058
    fs.shadowPolyCount = 11; // *(*(obj+492)+256)
    fs.framePolyCount = 13;  // *(*(obj+492)+252)
    fs.runningFar = 5.0f;    // flt_13FCF3C
    fs.runningNear = 3.0f;   // flt_13FD168[0]

    BeginUniverseFrame(fs, h, /*a2*/1);

    CHECK_EQ(fs.polyCounterA, 0);
    CHECK_EQ(fs.polyCounterB, 0);
    CHECK_EQ(fs.shadowPolyCount, 0);
    CHECK_EQ(fs.framePolyCount, 0);
    CHECK_EQ(fs.runningFar, 0.0f);     // reset to 0.0 (flt_13FCF3C)
    // runningNear is set to 1e10 then sentinel-collapsed to 0 (nothing expanded).
    CHECK_EQ(fs.runningNear, 0.0f);
    g_trace = nullptr;
}

TEST(RenderFrameSpine, DrawUniverseStatsTrailingResets) {
    FrameTrace tr; g_trace = &tr;
    FrameHooks h = FullHooks();
    FrameState fs;
    fs.fpsLastTimeA = 0; fs.fpsLastTimeB = 0;
    tr.timeValue = 5;
    fs.drawFrameFlag = 1;    // byte_64A068
    fs.shadowPolyCount = 4;  // *(*(obj+492)+256)
    fs.framePolyCount = 6;   // *(*(obj+492)+252)
    fs.mirrorPolyA = 8;      // dword_1408A64
    fs.mirrorPolyB = 10;     // dword_1408A68

    DrawUniverseAndStats(fs, h, /*a2*/1, /*a3*/0, /*a4*/0);

    CHECK_EQ((int)fs.drawFrameFlag, 0);
    CHECK_EQ(fs.shadowPolyCount, 0);
    CHECK_EQ(fs.framePolyCount, 0);   // = shadowPolyCount (0)
    CHECK_EQ(fs.mirrorPolyA, 0);
    CHECK_EQ(fs.mirrorPolyB, 0);
    g_trace = nullptr;
}

// The trailing resets DO run even when a2=0 (they are outside the a2 block, only
// gated by the engine/world reentrancy gate).
TEST(RenderFrameSpine, DrawUniverseStatsResetsRunWithoutA2) {
    FrameTrace tr; g_trace = &tr;
    FrameHooks h = FullHooks();
    FrameState fs;
    fs.mirrorPolyA = 3;
    fs.drawFrameFlag = 1;

    DrawUniverseAndStats(fs, h, /*a2*/0, /*a3*/0, /*a4*/0);

    CHECK_EQ(fs.mirrorPolyA, 0);
    CHECK_EQ((int)fs.drawFrameFlag, 0);
    CHECK_EQ((int)tr.order.size(), 0); // a2=0 => no scrollUv/projectWalk/anim
    g_trace = nullptr;
}
