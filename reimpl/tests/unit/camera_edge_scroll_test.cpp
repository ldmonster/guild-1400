// camera_edge_scroll_test.cpp — golden-vector unit tests for
// render::Camera_EdgeScroll, the 1:1 translation of VIBE_Camera_EdgeScroll
// @0x4b2c34 (the screen-edge view-snap / camera re-attach machine).
// Vectors derived from the Hex-Rays decompile + disasm (addresses in the
// implementation comments). Box layout matches the dispatcher's early-path
// init for a 640x480 screen: dword_62D0C4=632 (right), dword_62D0C8=480
// (bottom), dword_62D0CC=0 (left), dword_62D0D0=0 (top) -> inner box
// x in [1..631], y in [1..479].
#include "tests/framework/test.h"

#include "render/camera_edge_scroll.h"

#include <cstring>

using namespace guild::render;
using guild::i32;

namespace {

// captured hook traffic
int   g_animFreeCalls = 0;
int   g_listenerCalls = 0;
float g_listenerPos[3];
float g_listenerRot[3];
i32   g_listenerDist = 0;
i32   g_listenerKind = 0;
i32   g_listenerRet  = 0;

void cap_animFree(CameraObject*, i32, i32) { ++g_animFreeCalls; }
i32 cap_listener(CameraObject*, const f32* pos, i32 dist, const f32* rot, i32 kind) {
    ++g_listenerCalls;
    std::memcpy(g_listenerPos, pos, sizeof(g_listenerPos));
    std::memcpy(g_listenerRot, rot, sizeof(g_listenerRot));
    g_listenerDist = dist;
    g_listenerKind = kind;
    return g_listenerRet;
}

struct Rig {
    CameraObject obj;
    CameraState  cs;
    Camera2State st;
    Camera2Input in;
    Camera2Hooks h;

    Rig() {
        g_animFreeCalls = g_listenerCalls = 0;
        g_listenerDist = g_listenerKind = 0;
        g_listenerRet = 0;
        obj.present = true;
        in.g_13FCD1C_present = 1;
        // the dispatcher-initialized screen edge box (640x480).
        st.box0 = 632;   // dword_62D0C4 (right outer)
        st.box1 = 480;   // dword_62D0C8 (bottom outer)
        st.box2 = 0;     // dword_62D0CC (left outer)
        st.box3 = 0;     // dword_62D0D0 (top outer)
        st.boxFlag = 1;
        h = Camera2_DefaultHooks();
        h.animFree = &cap_animFree;
        h.sound3dSetListener = &cap_listener;
    }

    i32 run(int mx, int my) {
        in.d67220E = mx;
        in.d672210 = my;
        return Camera_EdgeScroll(obj, cs, st, in, h);
    }
};

f32 bitsAsFloat(i32 bits) { f32 f; std::memcpy(&f, &bits, sizeof(f)); return f; }

} // namespace

// ---------------------------------------------------------------------------
// Cursor inside the inner box: nothing happens; returns the cursor x (the
// 0x4b2dc3 early-out path leaves eax = unk_67220E >> 16).
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, InsideBoxIsInert) {
    Rig r;
    r.in.disableMove = 1;                    // detached camera stays detached
    CHECK_EQ(r.run(320, 240), 320);
    CHECK_EQ(r.st.edgeScrollX, 0);
    CHECK_EQ(r.st.edgeScrollY, 0);
    CHECK_EQ(r.st.edgeSnapLatch, 0);
    CHECK_EQ(r.in.disableMove, 1);           // no re-attach
    CHECK_EQ(g_animFreeCalls, 0);
    CHECK_EQ(g_listenerCalls, 0);
}

// ---------------------------------------------------------------------------
// LEFT edge with an UNSET left view (node+228 within 0.2 of the 0x4AD1C4 zero
// vector): the state steps to -1 but the snap bails returning the tolerance
// result (1) — no commit, no latch, no re-attach.
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, LeftEdgeUnsetViewStepsAndBails) {
    Rig r;
    r.in.disableMove = 1;
    CHECK_EQ(r.run(0, 240), 1);              // VectorWithinTolerance result
    CHECK_EQ(r.st.edgeScrollX, -1);          // dword_6316CC stepped
    CHECK_EQ(r.st.edgeScrollY, 0);           // dword_6316D0 zeroed
    CHECK_EQ(r.st.edgeSnapLatch, 0);
    CHECK_EQ(r.in.disableMove, 1);
    CHECK_EQ(g_listenerCalls, 0);
}

// at -1 another left-edge frame early-outs (0x4b2cd5: cc <= -1) returning mx.
TEST(CameraEdgeScroll, LeftEdgeClampsAtMinusOne) {
    Rig r;
    r.run(0, 240);                           // cc -> -1
    CHECK_EQ(r.run(0, 240), 0);              // returns mx, no further step
    CHECK_EQ(r.st.edgeScrollX, -1);
}

// ---------------------------------------------------------------------------
// LEFT edge with a CONFIGURED left view: full commit — history mirrors get the
// view, anim data freed, dword_62D4E4/E8 cleared (the 0x4b2d95 re-attach),
// listener updated with (pos, dword_6316C8, rot, kind 8), latch set, listener
// result returned.
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, LeftEdgeConfiguredViewCommits) {
    Rig r;
    const f32 view[6] = {10.0f, 20.0f, 30.0f, 0.1f, 0.2f, 0.3f};
    std::memcpy(r.obj.edgeLeft, view, sizeof(view));
    r.in.disableMove = 1;  r.cs.disableMove = 1;   // detached (both mirrors)
    r.in.altMoveMode = 1;  r.cs.altMoveMode = 1;
    g_listenerRet = 77;
    CHECK_EQ(r.run(0, 240), 77);             // the listener result
    // history mirrors dword_11BC2D4..DC / 11BC2E4..EC
    CHECK_EQ(bitsAsFloat(r.st.hist_2D4), 10.0f);
    CHECK_EQ(bitsAsFloat(r.st.hist_2D8), 20.0f);
    CHECK_EQ(bitsAsFloat(r.st.hist_2DC), 30.0f);
    CHECK_EQ(bitsAsFloat(r.st.hist_2E4), 0.1f);
    CHECK_EQ(bitsAsFloat(r.st.hist_2E8), 0.2f);
    CHECK_EQ(bitsAsFloat(r.st.hist_2EC), 0.3f);
    // the 0x4b2d95/0x4b2d9b re-attach (both global mirrors)
    CHECK_EQ(r.in.disableMove, 0);
    CHECK_EQ(r.in.altMoveMode, 0);
    CHECK_EQ(r.cs.disableMove, 0);
    CHECK_EQ(r.cs.altMoveMode, 0);
    CHECK_EQ(g_animFreeCalls, 1);            // VIBE_Anim_FreeObjAnimData
    CHECK_EQ(g_listenerCalls, 1);
    CHECK_EQ(g_listenerPos[0], 10.0f);
    CHECK_EQ(g_listenerRot[0], 0.1f);
    CHECK_EQ(g_listenerDist, 50);            // dword_6316C8 default
    CHECK_EQ(g_listenerKind, 8);             // the pushed 8
    CHECK_EQ(r.st.edgeSnapLatch, 1);         // dword_631DE0 = 1
}

// ---------------------------------------------------------------------------
// Latch behavior (0x4b2c69 path): while latched the machine only waits; the
// cursor re-entering the inner box clears the latch (0x4b2c9b). Returns
// bottom - 1 on every latched call.
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, LatchBlocksUntilCursorReenters) {
    Rig r;
    r.st.edgeSnapLatch = 1;
    CHECK_EQ(r.run(0, 240), 479);            // outside (x < left): stays latched
    CHECK_EQ(r.st.edgeSnapLatch, 1);
    CHECK_EQ(r.run(320, 0), 479);            // outside (y < top): stays latched
    CHECK_EQ(r.st.edgeSnapLatch, 1);
    CHECK_EQ(r.run(320, 240), 479);          // inside: latch cleared
    CHECK_EQ(r.st.edgeSnapLatch, 0);
}

// ---------------------------------------------------------------------------
// RIGHT edge from the left state steps back to CENTER, which commits the
// node's CURRENT world pose (+92/+144) with NO tolerance gate — the re-attach
// commit even when the world pose is zero.
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, RightEdgeFromLeftStateRecentersAndReattaches) {
    Rig r;
    r.st.edgeScrollX = -1;                   // in the left view state
    r.obj.wposX = 5.0f; r.obj.wposY = 6.0f; r.obj.wposZ = 7.0f;
    r.obj.wrotX = 0.5f; r.obj.wrotY = 0.6f; r.obj.wrotZ = 0.7f;
    r.in.disableMove = 1;
    r.run(640, 240);                         // x > right(631): step -1 -> 0
    CHECK_EQ(r.st.edgeScrollX, 0);
    CHECK_EQ(bitsAsFloat(r.st.hist_2D4), 5.0f);   // CURRENT world pos
    CHECK_EQ(bitsAsFloat(r.st.hist_2E4), 0.5f);   // CURRENT world rot
    CHECK_EQ(r.in.disableMove, 0);           // re-attached
    CHECK_EQ(r.st.edgeSnapLatch, 1);
}

TEST(CameraEdgeScroll, CenterCommitHasNoToleranceGate) {
    Rig r;
    r.st.edgeScrollX = -1;
    r.in.disableMove = 1;
    // world pose all zeros — the edge views would bail, the center MUST commit.
    CHECK_EQ(r.run(640, 240), 0);            // inert listener returns 0
    CHECK_EQ(g_listenerCalls, 1);
    CHECK_EQ(r.in.disableMove, 0);
    CHECK_EQ(r.st.edgeSnapLatch, 1);
}

// ---------------------------------------------------------------------------
// TOP edge (y < top): steps dword_6316D0 to -1, zeroes dword_6316CC, uses the
// node+180 (0xB4) top view.
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, TopEdgeUsesTopView) {
    Rig r;
    const f32 view[6] = {1.0f, 2.0f, 3.0f, -0.1f, -0.2f, -0.3f};
    std::memcpy(r.obj.edgeTop, view, sizeof(view));
    r.st.edgeScrollX = -1;                   // proves the cross-axis zeroing
    r.run(320, 0);
    CHECK_EQ(r.st.edgeScrollY, -1);
    CHECK_EQ(r.st.edgeScrollX, 0);           // 0x4b2e0c
    CHECK_EQ(bitsAsFloat(r.st.hist_2D4), 1.0f);
    CHECK_EQ(bitsAsFloat(r.st.hist_2E4), -0.1f);
    CHECK_EQ(r.st.edgeSnapLatch, 1);
}

// at -1 another top-edge frame early-outs (0x4b2dfc) returning bottom-1.
TEST(CameraEdgeScroll, TopEdgeClampsAtMinusOne) {
    Rig r;
    r.st.edgeScrollY = -1;
    CHECK_EQ(r.run(320, 0), 479);
    CHECK_EQ(r.st.edgeScrollY, -1);
    CHECK_EQ(g_listenerCalls, 0);
}

// ---------------------------------------------------------------------------
// BOTTOM edge: only y > bottom-1 (beyond the last inner row) steps the state;
// it selects the node+204 (0xCC) bottom view.
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, BottomEdgeBeyondUsesBottomView) {
    Rig r;
    const f32 view[6] = {4.0f, 5.0f, 6.0f, 0.4f, 0.5f, 0.6f};
    std::memcpy(r.obj.edgeBottom, view, sizeof(view));
    r.run(320, 480);                         // y(480) > bottom(479)
    CHECK_EQ(r.st.edgeScrollY, 1);
    CHECK_EQ(bitsAsFloat(r.st.hist_2D4), 4.0f);
    CHECK_EQ(bitsAsFloat(r.st.hist_2E4), 0.4f);
    CHECK_EQ(r.st.edgeSnapLatch, 1);
}

// ---------------------------------------------------------------------------
// The BOUNDARY ROW (y == top or y == bottom): no step (loc_4B2E0A joins with
// neither ++ nor --), dword_6316CC zeroed — with the vertical state at 0 this
// IS the center re-attach row the session adapter relies on.
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, BoundaryRowCommitsCenter) {
    Rig r;
    r.obj.wposX = 9.0f;
    r.in.disableMove = 1;
    r.run(320, 479);                         // y == bottom (boundary row)
    CHECK_EQ(r.st.edgeScrollX, 0);
    CHECK_EQ(r.st.edgeScrollY, 0);
    CHECK_EQ(bitsAsFloat(r.st.hist_2D4), 9.0f);
    CHECK_EQ(r.in.disableMove, 0);           // re-attached
    CHECK_EQ(r.st.edgeSnapLatch, 1);
}

// y == top with a NONZERO vertical state dispatches that state's view instead
// (cc zeroed, d0 kept — the loc_4B2EA1 (0,-1) branch).
TEST(CameraEdgeScroll, BoundaryRowKeepsVerticalState) {
    Rig r;
    const f32 view[6] = {7.0f, 8.0f, 9.0f, 0.7f, 0.8f, 0.9f};
    std::memcpy(r.obj.edgeTop, view, sizeof(view));
    r.st.edgeScrollY = -1;                   // already in the top view state
    r.run(320, 1);                           // y == top: no step
    CHECK_EQ(r.st.edgeScrollY, -1);
    CHECK_EQ(bitsAsFloat(r.st.hist_2D4), 7.0f);  // top view re-committed
    CHECK_EQ(r.st.edgeSnapLatch, 1);
}

// ---------------------------------------------------------------------------
// RIGHT edge from rest uses the node+252 (0xFC) right view.
// ---------------------------------------------------------------------------
TEST(CameraEdgeScroll, RightEdgeUsesRightView) {
    Rig r;
    const f32 view[6] = {11.0f, 12.0f, 13.0f, 1.1f, 1.2f, 1.3f};
    std::memcpy(r.obj.edgeRight, view, sizeof(view));
    r.run(632, 240);                         // x(632) > right(631)
    CHECK_EQ(r.st.edgeScrollX, 1);
    CHECK_EQ(r.st.edgeScrollY, 0);
    CHECK_EQ(bitsAsFloat(r.st.hist_2D4), 11.0f);
    CHECK_EQ(bitsAsFloat(r.st.hist_2E4), 1.1f);
    CHECK_EQ(r.st.edgeSnapLatch, 1);
    // and clamps at +1 on the next right-edge frame (0x4b2dd2) — after the
    // latch re-arms.
    r.st.edgeSnapLatch = 0;
    CHECK_EQ(r.run(632, 240), 632);
    CHECK_EQ(r.st.edgeScrollX, 1);
}
