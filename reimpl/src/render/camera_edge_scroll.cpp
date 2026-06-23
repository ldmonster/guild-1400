// camera_edge_scroll.cpp — 1:1 translation of VIBE_Camera_EdgeScroll @0x4b2c34.
// See camera_edge_scroll.h for the behavior overview, callers and the
// register-dataflow notes; control flow below follows the disasm label for
// label (addresses in comments).
#include "camera_edge_scroll.h"

#include "util/math.h"   // VectorWithinTolerance @0x5caa4c (reused, not redefined)

#include <cstring>

namespace guild::render {

namespace {

// dword_4AD1C4 — 16 zero bytes embedded ahead of the function in .text
// (get_bytes verified: 00 x16). The "unset edge view" reference vector v8.
f32 kZeroVec_4AD1C4[4] = {0.0f, 0.0f, 0.0f, 0.0f};

// 0x4b2e4c / 0x4b2ef3 / 0x4b2f54 / 0x4b2fb5: push 3E4CCCCDh = 0.2f.
constexpr f32 kEdgeViewTol = 0.2f;

// VIBE_Math_VectorWithinTolerance(a@eax, b@edx, tol) — util signature takes
// mutable pointers; the callee only reads. Wrap with local copies.
bool within_tol(const f32* a, f32 tol) {
    f32 ac[3] = {a[0], a[1], a[2]};
    return guild::util::VectorWithinTolerance(ac, kZeroVec_4AD1C4, tol);
}

// loc_4B2D44 (LABEL_18) — the commit tail shared by every snap.
i32 commit_view(CameraObject& obj, CameraState& cs, Camera2State& st,
                Camera2Input& in, const Camera2Hooks& h,
                const f32 pos[3], const f32 rot[3]) {
    // 0x4b2d44..0x4b2d7d: history mirrors dword_11BC2D4..DC / dword_11BC2E4..EC.
    std::memcpy(&st.hist_2D4, &pos[0], sizeof(i32));
    std::memcpy(&st.hist_2D8, &pos[1], sizeof(i32));
    std::memcpy(&st.hist_2DC, &pos[2], sizeof(i32));
    std::memcpy(&st.hist_2E4, &rot[0], sizeof(i32));
    std::memcpy(&st.hist_2E8, &rot[1], sizeof(i32));
    std::memcpy(&st.hist_2EC, &rot[2], sizeof(i32));

    // 0x4b2d89: VIBE_Anim_FreeObjAnimData(node@eax, edi, esi) — edi/esi are
    // leftover registers; the callee only frees the node's +464 anim block.
    h.animFree(&obj, 0, 0);

    // 0x4b2d95/0x4b2d9b: dword_62D4E4 = 0; dword_62D4E8 = 0 (ecx zeroed at
    // 0x4b2d87 and PRESERVED by the anim-free callee). THE RE-ATTACH: this
    // ends the UpdateMovement drag-release freeze. The two globals are modeled
    // in both Camera2Input and CameraState — clear both mirrors.
    in.disableMove = 0;
    in.altMoveMode = 0;
    cs.disableMove = 0;
    cs.altMoveMode = 0;

    // 0x4b2dac: VIBE_Sound3d_SetListenerFromVectors(node, &pos, dword_6316C8,
    //                                               &rot, 8).
    const i32 result = h.sound3dSetListener(&obj, pos, cs.minZoomDist, rot, 8);

    // 0x4b2db1: dword_631DE0 = 1 — latch until the cursor re-enters the box.
    st.edgeSnapLatch = 1;
    return result;        // 0x4b2dc0
}

// loc_4B2CFE / loc_4B2EA1 — the (edgeScrollX, edgeScrollY) -> view dispatch
// shared by the vertical path and the horizontal cc==0 fall-through. Entered
// with edgeScrollX known-zero OR edgeScrollY known-zero (the stepper zeroed
// the other axis).
i32 dispatch_from_center_check(CameraObject& obj, CameraState& cs,
                               Camera2State& st, Camera2Input& in,
                               const Camera2Hooks& h) {
    if (st.edgeScrollY == 0 && st.edgeScrollX == 0) {
        // 0x4b2d0b: CENTER (cc==0, d0==0) — commit the node's CURRENT world
        // pose: +92/+96/+100 and +144/+148/+152. NO tolerance gate.
        const f32 pos[3] = {obj.wposX, obj.wposY, obj.wposZ};
        const f32 rot[3] = {obj.wrotX, obj.wrotY, obj.wrotZ};
        return commit_view(obj, cs, st, in, h, pos, rot);
    }

    // loc_4B2EA1: the off-center states.
    if (st.edgeScrollX == 1 && st.edgeScrollY == 0) {
        // 0x4b2f48: RIGHT view @ node+252 (0xFC).
        if (within_tol(obj.edgeRight, kEdgeViewTol))
            return 1;                                  // 0x4b2f60
        return commit_view(obj, cs, st, in, h, &obj.edgeRight[0], &obj.edgeRight[3]);
    }
    if (st.edgeScrollX == 0 && st.edgeScrollY == -1) {
        // 0x4b2fa9: TOP view @ node+180 (0xB4).
        if (within_tol(obj.edgeTop, kEdgeViewTol))
            return 1;                                  // 0x4b2fc1
        return commit_view(obj, cs, st, in, h, &obj.edgeTop[0], &obj.edgeTop[3]);
    }
    if (st.edgeScrollX == 0 && st.edgeScrollY == 1) {
        // 0x4b2ee7: BOTTOM view @ node+204 (0xCC).
        if (within_tol(obj.edgeBottom, kEdgeViewTol))
            return 1;                                  // 0x4b2eff
        return commit_view(obj, cs, st, in, h, &obj.edgeBottom[0], &obj.edgeBottom[3]);
    }

    // 0x4b2ed4/0x4b2ee1 -> loc_4B2D44 with the UNINITIALIZED stack staging.
    // UNREACHABLE under the machine's invariants: each stepper clamps its axis
    // to [-1,1] and zeroes the other axis before dispatch, so one of the four
    // cases above always matches. Reproduced as a zero-view commit (the
    // closest deterministic reading of the original's garbage-stack commit).
    const f32 z[3] = {0.0f, 0.0f, 0.0f};
    return commit_view(obj, cs, st, in, h, z, z);
}

} // namespace

// gilde.exe 0x4b2c34 — VIBE_Camera_EdgeScroll
i32 Camera_EdgeScroll(CameraObject& obj, CameraState& cs, Camera2State& st,
                      Camera2Input& in, const Camera2Hooks& h) {
    // 0x4b2c44..0x4b2c68: the inner box = the screen-edge box inset by 1px.
    const i32 left   = st.box2 + 1;   // dword_62D0CC + 1
    const i32 top    = st.box3 + 1;   // dword_62D0D0 + 1
    const i32 bottom = st.box1 - 1;   // dword_62D0C8 - 1
    const i32 right  = st.box0 - 1;   // dword_62D0C4 - 1

    // 0x4b2c69: latched — a snap fired; wait for the cursor to re-enter.
    if (st.edgeSnapLatch) {           // dword_631DE0
        const i32 mx = in.d67220E;    // (int)unk_67220E >> 16
        if (mx >= left && mx <= right) {
            const i32 my = in.d672210;             // dword_672210 >> 16
            if (my >= top && my <= bottom)
                st.edgeSnapLatch = 0;              // 0x4b2c9b
        }
        return bottom;                // eax leftover at loc_4B2C8F
    }

    const i32 my = in.d672210;
    if (my > top && my < bottom) {
        // 0x4b2cbc: strictly inside vertically — test the horizontal edges.
        const i32 mx = in.d67220E;
        if (mx < left) {
            // LEFT edge: step dword_6316CC down, clamped at -1.
            if (st.edgeScrollX <= -1)              // 0x4b2cd5
                return mx;
            --st.edgeScrollX;                      // 0x4b2cd7
        } else {
            if (mx <= right)                       // 0x4b2dc3: inside the box
                return mx;
            // RIGHT edge: step dword_6316CC up, clamped at +1.
            if (st.edgeScrollX >= 1)               // 0x4b2dd2
                return mx;
            ++st.edgeScrollX;                      // 0x4b2ddb
        }
        st.edgeScrollY = 0;                        // 0x4b2ce7: dword_6316D0 = 0
        if (st.edgeScrollX == -1) {
            // 0x4b2e40: LEFT view @ node+228 (0xE4).
            if (within_tol(obj.edgeLeft, kEdgeViewTol))
                return 1;                          // 0x4b2e58
            return commit_view(obj, cs, st, in, h, &obj.edgeLeft[0], &obj.edgeLeft[3]);
        }
        // cc == 0 falls through to the center check (loc_4B2CFE with d0 just
        // zeroed); cc == +1 dispatches the off-center states (loc_4B2EA1).
        return dispatch_from_center_check(obj, cs, st, in, h);
    }

    // loc_4B2DE6: on/over a vertical edge band.
    if (my < top) {
        // TOP edge: step dword_6316D0 down, clamped at -1.
        if (st.edgeScrollY <= -1)                  // 0x4b2dfc
            return bottom;
        --st.edgeScrollY;                          // 0x4b2e05
    } else if (my > bottom) {
        // BOTTOM edge (beyond the last row): step dword_6316D0 up, clamp +1.
        if (st.edgeScrollY >= 1)                   // 0x4b2e24
            return bottom;
        ++st.edgeScrollY;                          // 0x4b2e2d
    }
    // my == top or my == bottom exactly: boundary row, NO step (loc_4B2E0A
    // joins here directly) — with d0 still 0 this is the CENTER re-attach row.
    st.edgeScrollX = 0;                            // 0x4b2e0c: dword_6316CC = 0
    return dispatch_from_center_check(obj, cs, st, in, h);   // loc_4B2CFE
}

} // namespace guild::render
