#pragma once
#include "guild/common/types.h"
#include "camera_recon.h"   // reuse CameraObject / CameraState / CameraInput / CameraHooks

// =============================================================================
// guild::render — VIBE_Camera_* reconstruction cluster, second pass
// (camera_recon2). 1:1 translations of the "big camera movers" of gilde.exe
// whose dependency leaves are now reconstructed:
//
//   0x4b300c  VIBE_Camera_RotateView                  (mouse-drag orbit/rotate)
//   0x4b41a8  VIBE_Camera_UpdateMovement              (per-frame pan/zoom/rotate)
//   0x4b5250  VIBE_Camera_ZoomReset                   (zoom-to-object reset)
//   0x4b5974  VIBE_Camera_ZoomOut                     (zoom out from object)
//   0x4b562c  VIBE_Camera_OrientToTarget              (point camera + 3D listener)
//   0x5e967c  VIBE_Camera_UpdateTrackTargetFromMouse  (track/orbit from mouse)
//
// These reuse the existing camera_recon CameraObject/CameraState/CameraInput
// model (same byte offsets off the original dword_13FCD1C scene node) and the
// already-reconstructed pure math/transform leaves:
//   guild::util::MatrixCopy / Fmod / VectorNormalize / VectorAngleWrapped /
//   AcosGuarded / VectorWithinTolerance  (src/util/math*, src/util/matrix.h,
//   src/util/transform.h::PointThroughBoneChain).
//
// The genuinely-coupled leaves (3D-sound listener, scene-graph clamp box,
// family-record writes, terrain/mesh height sampling, the coordinate
// truncators, anim-free, scene-graph walk, light refresh, transform
// constraints) are NOT in this cluster. They are routed through Camera2Hooks
// with INERT defaults, so the pure control flow / arithmetic / clamps / easing
// are reconstructed faithfully and golden-testable in isolation.
//
// State/input the originals touch that the earlier CameraState/CameraInput did
// not yet model is added in Camera2State / Camera2Input (disjoint globals).
// =============================================================================

namespace guild::render {

// ---------------------------------------------------------------------------
// Camera2State — extra mutable process globals the movers touch (disjoint from
// the fields already in CameraState; addresses noted per field).
// ---------------------------------------------------------------------------
struct Camera2State {
    // --- RotateView (0x4b300c) persistence ---
    i32 rotPrevView = 0;    // dword_631DE4  (SHIWORD(dword_672170))
    i32 rotPrevY    = 0;    // dword_631DE8  (dword_672174>>16)
    i32 rotActive   = 0;    // dword_631DEC

    // --- shared world-translation scratch the originals stage before commit ---
    f32 wt0 = 0.0f, wt1 = 0.0f, wt2 = 0.0f;     // flt_11BC264 / dword_11BC268 / _26C
    // last camera position for the within-tolerance gate (flt_11BC300[3])
    f32 lastPos[3] = {0.0f, 0.0f, 0.0f};         // flt_11BC300

    // pos/world mirror copies (history/save buffers) dword_11BC2D4..2EC
    i32 hist_2D4 = 0, hist_2D8 = 0, hist_2DC = 0;   // pos copy
    i32 hist_2E4 = 0, hist_2E8 = 0, hist_2EC = 0;   // world copy

    // --- UpdateMovement (0x4b41a8) state machine ---
    i32 mvActive   = 0;     // dword_631E20
    i32 mvPanInit  = 0;     // dword_631E08
    i32 mvDone     = 0;     // dword_631E0C
    f32 mvZoomT0   = 0.0f;  // dword_631E10 (float) — zoom-T base at drag start
    f32 mvZoomT0b  = 0.0f;  // dword_631E14 (float)
    f32 mvHeight0  = 0.0f;  // flt_631E1C
    f32 mvWorldY0  = 0.0f;  // flt_631E18
    i32 mvCoordX0  = 0;     // dword_631E00 (unk_67220E>>16 snapshot)
    i32 mvCoordY0  = 0;     // dword_631E04 (dword_672210>>16 snapshot)

    i32 mvPrevView = 0;     // dword_11BC334
    i32 mvPrevY    = 0;     // dword_11BC338
    i32 mvPrevView2= 0;     // dword_11BC33C
    i32 mvPrevY2   = 0;     // dword_11BC340
    i32 mvSaveBox0 = 0, mvSaveBox1 = 0, mvSaveBox2 = 0, mvSaveBox3 = 0; // 11BC324..330
    f32 mvTargetX = 0.0f, mvTargetY = 0.0f, mvTargetZ = 0.0f; // dword_11BC344..34C

    // the clamp/AABB globals the pan branch resets (dword_62D0C4..D4)
    i32 box0 = 32000, box1 = 32000, box2 = -32000, box3 = -32000; // 62D0C4..D0
    i32 boxFlag = 0;        // dword_62D0D4

    // --- EdgeScroll (0x4b2c34, camera_edge_scroll.*) edge-snap state ---
    i32 edgeScrollX   = 0;  // dword_6316CC  horizontal edge step, clamped [-1,1]
    i32 edgeScrollY   = 0;  // dword_6316D0  vertical edge step, clamped [-1,1]
    i32 edgeSnapLatch = 0;  // dword_631DE0  snap fired; re-arms when the cursor
                            //               re-enters the inner screen box

    // --- ZoomReset (0x4b5250) / ZoomOut (0x4b5974) ---
    i32 zr_lastReset = 0;   // dword_631740 = a1.meshHandle
    i32 zr_lastA1    = 0;   // dword_11BC278 = a1

    // --- UpdateMovement rotate-branch AABB clamp box (*(off_649D64+44)) ---
    // Modeled as a flat float record: box[0]=*(v18+0), box[2]=*(v18+8),
    // box[4]=*(v18+16), box[6]=*(v18+24); clampBoxCount = *(v18+32) (int).
    // null when in.clampBoxPresent is false.
    const f32* clampBox = nullptr;   // float view of the *(off_649D64+44) record
    i32 clampBoxCount = 0;           // *(box+32) as int
};

// ---------------------------------------------------------------------------
// Camera2Input — extra input snapshot globals (disjoint from CameraInput).
// All packed fixed-point screen deltas are read as (x>>16). Modeled raw as the
// already-shifted integer the original computes.
// ---------------------------------------------------------------------------
struct Camera2Input {
    i32 viewShift = 0;   // SHIWORD(dword_672170)  ((char*)&dword_672170+2)>>16
    i32 d672174   = 0;   // dword_672174 >> 16
    i32 d67220E   = 0;   // (int)unk_67220E >> 16
    i32 d672210   = 0;   // dword_672210 >> 16
    i32 d672220   = 0;   // dword_672220 (rotate/pan-button held)
    i32 d672238   = 0;   // dword_672238 (drag active)
    i32 d672250   = 0;   // dword_672250 (wheel/zoom accumulator low)
    i32 d672254   = 0;   // dword_672254 (wheel/zoom accumulator high)
    i32 d67221C   = 0;   // dword_67221C
    i32 d672230   = 0;   // dword_672230
    i32 d672234   = 0;   // dword_672234
    u8  byte671D6F = 0;  // byte_671D6F (invert axis)

    // gate flags read by RotateView / UpdateMovement
    i32 g_13FCD1C_present = 1; // dword_13FCD1C != 0 (camera object exists)
    i32 g_631610 = 0, g_631618 = 0, g_631744 = 0, g_631748 = 0; // history-save gate
    i32 disableMove = 0;       // dword_62D4E4
    i32 altMoveMode = 0;       // dword_62D4E8

    // UpdateMovement extra inputs
    i32 d1233564 = 0;          // dword_1233564 (rotate-speed scalar)
    i32 clampBoxPresent = 0;   // *(off_649D64 + 44) (AABB clamp record) != 0
};

// ---------------------------------------------------------------------------
// Camera2Hooks — genuinely-coupled cross-module leaves, INERT by default.
// The pure math/transform leaves are NOT hooks here; they call the
// reconstructed guild::util functions directly (see .cpp).
// ---------------------------------------------------------------------------
struct Camera2Hooks {
    // 0x427468 VIBE_Terrain_AverageAreaHeight(x, z) -> height. Inert: 0.
    f32 (*terrainHeight)(i32 a1, i32 a2) = nullptr;
    // 0x42698c VIBE_Mesh_ComputeHeightRange(mesh, outMin[?], outMax) — fills the
    // two scratch triples. Inert: zero them.
    void (*meshHeightRange)(i32 mesh, f32* outA, f32* outB) = nullptr;
    // 0x5cec00 VIBE_Anim_FreeObjAnimData(obj, a3, a4) — inert: no-op.
    void (*animFree)(CameraObject* obj, i32 a3, i32 a4) = nullptr;
    // 0x4262a0 VIBE_Sound3d_SetListenerFromVectors(obj, posVec, dist, angVec, kind)
    //   -> int. Inert: return 0.
    i32 (*sound3dSetListener)(CameraObject* obj, const f32* posVec, i32 dist,
                              const f32* angVec, i32 kind) = nullptr;
    // 0x58c408 VIBE_Person_GetFamilyRecord(rowPtr) -> record* (or null).
    //   Inert: nullptr (skips the family-record writes). When non-null the
    //   movers write the camera pos/world into it; we model the record as a
    //   flat float/dword array via this pointer.
    void* (*personGetFamilyRecord)() = nullptr;
    // 0x5ac738 VIBE_SceneGraph_WalkAndInvoke(root, node, cb, mode, a5) -> int.
    //   Inert: return 0 (=> light-refresh-all branch).
    i32 (*sceneGraphWalk)(CameraObject* node, i32 mode) = nullptr;
    // 0x5c8538 VIBE_Light_RequestObjectCache(node) — inert: no-op.
    void (*lightRequestCache)(CameraObject* node) = nullptr;
    // 0x5c886c VIBE_Light_RefreshAllObjects(flag) — inert: no-op.
    void (*lightRefreshAll)(u32 flag) = nullptr;
    // 0x5e89b4 VIBE_Object_ApplyTransformConstraints(obj, dpos, basis, dworld,
    //   outPos, outWorld) -> u8 (bit0=pos changed, bit1=world changed).
    //   Inert: copy obj->{pos,world}+deltas into outPos/outWorld, return 3.
    u8 (*applyConstraints)(CameraObject* obj, const f32* dpos, const f32* basis,
                           const f32* dworld, f32* outPos, f32* outWorld) = nullptr;
    // 0x5e872c VIBE_Scene_HandleDebugKeyToggle(arg) -> u8. Inert: 0.
    u8 (*sceneDebugToggle)(i32 arg) = nullptr;
    // pos/world object setters (reused from CameraHooks semantics, but kept here
    // so this cluster is independently wireable). Inert: write the CameraObject.
    void (*setPosition)(CameraObject* obj, const f32* v) = nullptr;
    void (*setWorldTranslation)(CameraObject* obj, const f32* v) = nullptr;
};

Camera2Hooks Camera2_DefaultHooks();

// ---------------------------------------------------------------------------
// 0x4b300c — VIBE_Camera_RotateView. Returns 0/1 (1 == camera moved).
// ---------------------------------------------------------------------------
i32 Camera_RotateView(CameraObject& obj, Camera2State& st, Camera2Input& in,
                      const Camera2Hooks& h);

// ---------------------------------------------------------------------------
// 0x4b41a8 — VIBE_Camera_UpdateMovement. Returns the original's
// dword_672254 + dword_631E0C + dword_631E08 - dword_672250 (or 0 on early-out).
// CameraState supplies the flight/height params (baseHeight/baseAngle/...).
// `anchorHooks` (optional) is forwarded to the wheel branch's
// VIBE_Camera_AnchorToTerrain call @0x4b46b1 so a bound terrainHeight reaches
// the anchor exactly as in the original; nullptr keeps the inert defaults.
// ---------------------------------------------------------------------------
i32 Camera_UpdateMovement(CameraObject& obj, CameraState& cs, Camera2State& st,
                          Camera2Input& in, const Camera2Hooks& h,
                          const CameraHooks* anchorHooks = nullptr);

// ---------------------------------------------------------------------------
// 0x4b5250 — VIBE_Camera_ZoomReset. a1 = building/mesh record (a1+97 byte =
// mesh handle). Returns the mesh handle (*(a1+97)). a3/a2 forwarded to animFree.
// ---------------------------------------------------------------------------
i32 Camera_ZoomReset(CameraObject& obj, CameraState& cs, Camera2State& st,
                     const Camera2Hooks& h, i32 a1MeshHandle, i32 a2, i32 a3);

// ---------------------------------------------------------------------------
// 0x4b5974 — VIBE_Camera_ZoomOut. a1 = mesh handle. Returns the 3D-sound
// listener result. a2/a3/a4 forwarded.
// ---------------------------------------------------------------------------
i32 Camera_ZoomOut(CameraObject& obj, CameraState& cs, Camera2State& st,
                   const Camera2Hooks& h, i32 a1MeshHandle, i32 a2, i32 a3, i32 a4);

// ---------------------------------------------------------------------------
// 0x4b562c — VIBE_Camera_OrientToTarget. a1 = frame/bone record (float*),
// also the listener object. Returns the 3D-sound listener result.
// ---------------------------------------------------------------------------
i32 Camera_OrientToTarget(CameraObject& obj, CameraState& cs, Camera2State& st,
                          const Camera2Hooks& h, f32* a1Frame);

// ---------------------------------------------------------------------------
// 0x5e967c — VIBE_Camera_UpdateTrackTargetFromMouse. Returns bool (something
// moved). Drives the active track object (dword_13FD45C[0]) by mouse delta.
// ---------------------------------------------------------------------------
struct TrackTargetCtx {
    // dword_13FD45C[0] — active track object (the thing being moved). If null,
    // the function returns 0 immediately.
    CameraObject* active = nullptr;
    // dword_13FD458 — basis selector (1/2/3/default) -> picks one of the four
    // named camera frames below as the transform basis (v2 = float*).
    i32 basisSel = 0;
    // dword_649EF0/4/8/C — the four well-known camera/track frames. The default
    // (649EFC) gets special yaw handling.
    f32* frame649EF0 = nullptr;
    f32* frame649EF4 = nullptr;
    f32* frame649EF8 = nullptr;
    f32* frame649EFC = nullptr;
    // *(active + 533) — the "trackable" flag byte.
    u8 activeTrackable = 1;
    // screen size: dword_7626E0 (width), cy@0x7626dc (height).
    i32 screenW = 1;
    i32 screenH = 1;
    // input edge-tracking persistence (dword_64A7AC/B0/B4/B8).
    i32 prevX = 0;          // dword_64A7AC
    i32 prevY = 0;          // dword_64A7B0
    i32 latchPan = 0;       // dword_64A7B4
    i32 latchTilt = 0;      // dword_64A7B8
    // button edges: byte_64A7A0 (X moved), byte_64A7A1 (Y moved).
    u8 btnX = 0;
    u8 btnY = 0;
    // mode bytes: byte_671D8A, byte_671D7D (axis-lock), byte_64A024 (alt routing).
    u8 lock671D8A = 0;
    u8 lock671D7D = 0;
    u8 alt64A024 = 0;
};
bool Camera_UpdateTrackTargetFromMouse(Camera2Input& in, const Camera2Hooks& h,
                                       TrackTargetCtx& ctx);

} // namespace guild::render
