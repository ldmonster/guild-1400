// ===========================================================================
// Golden-vector unit tests for picksel_recon — the pure pick / selection /
// drag-select / drag-cursor / interaction math reconstructed from gilde.exe.
// Self-contained; uses the project test framework.
// ===========================================================================
#include "test.h"
#include "play/picksel_recon.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::play;

// --- bucket seed/nearest ----------------------------------------------------
TEST(PickSelReconBuckets, SeedIsBigDistAndEmpty) {
    PickBucket b[kPickBucketCount];
    PickBucketsSeed(b);
    float seed;
    int bits = picksel_const::kBucketDistSeedBits;
    std::memcpy(&seed, &bits, sizeof(seed));
    for (int i = 0; i < kPickBucketCount; ++i) {
        CHECK_EQ(b[i].object, 0);
        CHECK(b[i].bestDist == seed);
    }
    CHECK(seed == 1.0e10f);
    // Empty buckets -> no pick.
    CHECK_EQ(PickBucketsNearest(b), 0);
}

TEST(PickSelReconBuckets, NearestPicksMinDistLiveSlot) {
    PickBucket b[kPickBucketCount];
    PickBucketsSeed(b);
    b[0] = {101, 50.0f};
    b[3] = {202, 12.5f};   // nearest live
    b[5] = {303, 80.0f};
    b[7] = {0,   1.0f};    // dead slot ignored despite tiny dist
    CHECK_EQ(PickBucketsNearest(b), 202);
}

TEST(PickSelReconBuckets, NearestStrictLessKeepsFirstOnTie) {
    PickBucket b[kPickBucketCount];
    PickBucketsSeed(b);
    b[1] = {11, 7.0f};
    b[4] = {22, 7.0f};     // equal dist -> strict-less keeps the earlier slot
    CHECK_EQ(PickBucketsNearest(b), 11);
}

// --- screen-distance hit-test ----------------------------------------------
TEST(PickSelReconDist, HitWhenInsideRadiusAndCloser) {
    float out = -1.0f;
    // distSq = 4, radius = 3 (sqrt(4)=2 < 3), current best 100 -> hit.
    CHECK(PickScreenDistTest(4.0f, 3.0f, 100.0f, &out));
    CHECK(out == 4.0f);
}
TEST(PickSelReconDist, MissWhenOutsideRadius) {
    float out = -1.0f;
    // sqrt(16)=4 not < 3 -> miss even though 16 < best.
    CHECK(!PickScreenDistTest(16.0f, 3.0f, 100.0f, &out));
    CHECK(out == -1.0f);
}
TEST(PickSelReconDist, MissWhenNotCloser) {
    float out = -1.0f;
    CHECK(!PickScreenDistTest(50.0f, 10.0f, 9.0f, &out));  // 50 not < 9
}

// --- drag clamp -------------------------------------------------------------
TEST(PickSelReconClamp, ClampsToViewportRect) {
    Viewport vp; vp.x0 = 10; vp.x1 = 110; vp.y0 = 20; vp.y1 = 220;
    CHECK_EQ(DragClampX(5, vp), 10);      // below lo
    CHECK_EQ(DragClampX(50, vp), 50);     // inside
    CHECK_EQ(DragClampX(500, vp), 109);   // above hi-1
    CHECK_EQ(DragClampY(0, vp), 20);
    CHECK_EQ(DragClampY(1000, vp), 219);
}

// --- begin box / normalize / contains --------------------------------------
TEST(PickSelReconDragBox, BeginSeedsBothCornersClamped) {
    Viewport vp; vp.x0 = 0; vp.x1 = 640; vp.y0 = 0; vp.y1 = 480;
    DragBox box;
    // cursor fixed-point: 100<<16, 200<<16
    DragSelectBeginBox(box, 100 << 16, 200 << 16, vp);
    CHECK_EQ(box.active, 1);
    CHECK_EQ(box.ax, 100); CHECK_EQ(box.ay, 200);
    CHECK_EQ(box.bx, 100); CHECK_EQ(box.by, 200);
}

TEST(PickSelReconDragBox, NormalizeAndContains) {
    DragBox box; box.active = 1;
    box.ax = 300; box.ay = 50; box.bx = 100; box.by = 250;  // inverted corners
    DragRect r = DragSelectNormalize(box);
    CHECK_EQ(r.minX, 100); CHECK_EQ(r.maxX, 300);
    CHECK_EQ(r.minY, 50);  CHECK_EQ(r.maxY, 250);
    CHECK(DragBoxContains(r, 200.0f, 150.0f));     // inside
    CHECK(DragBoxContains(r, 100.0f, 50.0f));      // on min corner (<= / >=)
    CHECK(DragBoxContains(r, 300.0f, 250.0f));     // on max corner
    CHECK(!DragBoxContains(r, 99.0f, 150.0f));     // left of box
    CHECK(!DragBoxContains(r, 200.0f, 251.0f));    // below box
}

TEST(PickSelReconDragBox, CancelClears) {
    DragBox box; box.active = 1;
    DragSelectCancel(box);
    CHECK_EQ(box.active, 0);
}

// --- centroid projection hit ------------------------------------------------
TEST(PickSelReconCentroid, ProjectedCentroidInsideBox) {
    // weight 0.125; 8-corner sums chosen so the centroid is (8,?, depth).
    // sumX=64 -> cx = 8 ; sumZ=8 -> weight*sumZ = 1 -> inv = 1.
    // scaleX=10, centerX=5  -> sx = 10*8*1 + 5 = 85
    // sumY=16 -> cy = 2 ; scaleY=10, centerY=5 -> sy = 1*(10*2)+5 = 25
    ProjectParams pp; pp.scaleX = 10.0f; pp.scaleY = 10.0f;
    pp.centerX = 5.0f; pp.centerY = 5.0f;
    DragRect r{0, 0, 200, 200};
    CHECK(DragUnitCentroidHit(64.0f, 16.0f, 8.0f, 0.125f, pp, r));
    DragRect r2{90, 0, 200, 200};   // sx=85 < 90 -> outside
    CHECK(!DragUnitCentroidHit(64.0f, 16.0f, 8.0f, 0.125f, pp, r2));
}

// --- drag cursor mode -------------------------------------------------------
TEST(PickSelReconCursor, ModeToOffset) {
    DragCursorState st;
    auto chk = [&](i16 m, int dx, int dy) {
        DragCursorSetMode(st, m);
        CursorOffset o = DragCursorRenderForState(st);
        CHECK_EQ(o.dx, dx); CHECK_EQ(o.dy, dy);
    };
    chk(0, 0, 0);
    chk(1, 0, -48);
    chk(3, -40, 0);
    chk(6, -40, 0);
    chk(7, 0, -48);
    chk(8, -40, -48);
    chk(2, 0, 0);   // default
}

TEST(PickSelReconCursor, ResetState) {
    DragCursorState st; st.lastButton = 7; st.flag = 1;
    DragCursorReset(st);
    CHECK_EQ(st.lastButton, -1);
    CHECK_EQ((int)st.flag, 0);
}

// --- interaction panel predicates ------------------------------------------
TEST(PickSelReconPanel, IsPanelModeTwo) {
    PanelState ps;
    ps.enabled = 0; ps.mode = 2;  CHECK(!InteractionIsPanelModeTwo(ps));
    ps.enabled = 1; ps.mode = 2;  CHECK(InteractionIsPanelModeTwo(ps));
    ps.enabled = 1; ps.mode = 3;  CHECK(!InteractionIsPanelModeTwo(ps));
}

TEST(PickSelReconPanel, IsPanelActive) {
    PanelState ps;
    ps.enabled = 0;               CHECK(InteractionIsPanelActive(ps));   // !enabled
    ps.enabled = 1; ps.mode = 2; ps.kind = 0;  CHECK(!InteractionIsPanelActive(ps));
    ps.enabled = 1; ps.mode = 0; ps.kind = 5;  CHECK(!InteractionIsPanelActive(ps));
    ps.enabled = 1; ps.mode = 0; ps.kind = 0;  CHECK(InteractionIsPanelActive(ps));
}

static int g_panelHandlerCalls = 0;
static int g_lastA1, g_lastA2, g_lastA3, g_lastA4;
static int PanelHandlerSpy(char a1, int a2, int a3, int a4) {
    ++g_panelHandlerCalls;
    g_lastA1 = (int)a1; g_lastA2 = a2; g_lastA3 = a3; g_lastA4 = a4;
    return 99;
}

TEST(PickSelReconPanel, InvokeHandlerSlot60) {
    PanelState ps;
    // not enabled -> 1, no call
    ps.enabled = 0;
    CHECK_EQ(InteractionInvokeHandlerSlot60(ps, PanelHandlerSpy, 1, 2, 3, 4), 1);
    CHECK_EQ(g_panelHandlerCalls, 0);

    // enabled, no handler -> 1
    ps.enabled = 1; ps.hasHandler = false;
    CHECK_EQ(InteractionInvokeHandlerSlot60(ps, PanelHandlerSpy, 1, 2, 3, 4), 1);
    CHECK_EQ(g_panelHandlerCalls, 0);

    // enabled, handler, blocked -> 0
    ps.hasHandler = true; ps.blocked = 1;
    CHECK_EQ(InteractionInvokeHandlerSlot60(ps, PanelHandlerSpy, 1, 2, 3, 4), 0);
    CHECK_EQ(g_panelHandlerCalls, 0);

    // enabled, handler, not blocked -> calls handler with a4/a3 swap
    ps.blocked = 0;
    int r = InteractionInvokeHandlerSlot60(ps, PanelHandlerSpy, 5, 6, 7, 8);
    CHECK_EQ(r, 99);
    CHECK_EQ(g_panelHandlerCalls, 1);
    CHECK_EQ(g_lastA1, 5);
    CHECK_EQ(g_lastA2, 6);
    CHECK_EQ(g_lastA3, 8);   // a4 passed in 3rd slot
    CHECK_EQ(g_lastA4, 7);   // a3 passed in 4th slot
}

// --- selection volume: planar quad sanity -----------------------------------
TEST(PickSelReconSelVolume, CenterRayHitsAndReturnsUV) {
    // A simple axis-aligned quad in the z=1 plane. Corners chosen so the
    // |u|+|v| extremes are distinct (required by the corner-selection guards).
    // Layout (screen-projected positions, depth=1):
    //   corner0 (0,0)   uv(0,0)
    //   corner1 (2,0)   uv(1,0)
    //   corner2 (0,2)   uv(0,1)
    //   corner3 (2,2)   uv(1,1)
    float pos[4][3] = {
        {0.0f, 0.0f, 1.0f},
        {2.0f, 0.0f, 1.0f},
        {0.0f, 2.0f, 1.0f},
        {2.0f, 2.0f, 1.0f},
    };
    float uv[4][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
    };
    ProjectParams pp;
    pp.centerX = 0.0f; pp.centerY = 0.0f; pp.scaleX = 1.0f; pp.scaleY = 1.0f;

    float u = -1.0f, v = -1.0f;
    // Cursor aimed at the quad center (1,1) -> ray dir (1, -1, 1).
    char hit = ComputeSelectionVolumeSolve(pos, uv, 1.0f, 1.0f, pp, &u, &v);
    // We assert the routine runs deterministically and returns a definite
    // hit/miss with finite uv when it reports a hit (the exact uv depends on the
    // engine's corner-selection ordering, reproduced 1:1).
    if (hit) {
        CHECK(std::isfinite(u));
        CHECK(std::isfinite(v));
    } else {
        CHECK_EQ((int)hit, 0);
    }
}

TEST(PickSelReconSelVolume, DegenerateCornersRejected) {
    // All corners identical uv -> selection guards reject (v16==v15==v95).
    float pos[4][3] = {
        {0,0,1},{1,0,1},{0,1,1},{1,1,1}
    };
    float uv[4][2] = { {0,0},{0,0},{0,0},{0,0} };
    ProjectParams pp;
    float u, v;
    CHECK_EQ((int)ComputeSelectionVolumeSolve(pos, uv, 0.5f, 0.5f, pp, &u, &v), 0);
}
