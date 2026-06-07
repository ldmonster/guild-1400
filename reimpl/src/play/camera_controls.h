#pragma once
// Wave 25 PLAY P2 — CITY-VIEW CAMERA CONTROLS (pan / zoom / edge-scroll).
//
// This is the INTERACTIVE-DRIVE layer for the city camera: it owns a small camera
// state (eye xyz, yaw, zoom) and the per-frame update steps that the original input
// dispatch runs against the active city camera record (dword_13FCD1C) — keyboard
// pan (WASD / arrows), zoom in/out, and mouse edge-scroll. The end product is the
// view transform (eye[3] + view[16]) that VIBE_Render_BeginUniverseFrame @0x5b3900
// reads (eye at +76, the 4x4 at +396) and feeds to the projection helpers. The
// produced (eye, view) pair plugs straight into play::WorldToView / the city render
// camera without re-deriving the projection.
//
// It is a SIBLING of camera_pick.h / scene_pick.h, not a replacement: those own the
// click/pick + the world->view/screen projection (and are NOT edited here). This
// module owns the camera *motion*.
//
// GROUNDING — the pan/zoom math + clamps are reconstructed from the original camera
// update path (decompiled this wave):
//   * VIBE_Camera_UpdatePan        @0x4b365c — the per-frame edge/key pan core.
//       - frame-time factor:  ft = clamp(dt * 0.4 (dbl_61DDB8), max 4.0 (dbl_61DDC0)),
//                             default 1.0 when there is no previous frame.
//       - accel step:         step = ft * 5.0 (dbl_61DDC8).
//       - two velocity accumulators velX (dword_631DF8) / velY (dword_631DF0), each a
//         float, kicked to ±10.0 (the 1092616192 / -1054867456 seeds) when an axis
//         first moves, then ramped by ±step and CLAMPED to [-50, 50] (dbl_61DDD8 /
//         dbl_61DDD0). Idle axes DECAY toward 0 by step (crossing 0 snaps to 0).
//       - apply: ft *= sensitivity (flt_6316D4); ft = (zoom*1.5 (dbl_61DDF0) + 0.6
//         (dbl_61DDF8)) * ft  — i.e. pan speed scales with the zoom fraction; world
//         delta = vel * ft * 1.1 (dbl_61DE00). The (dx,dz) ground delta is rotated by
//         the camera yaw (VIBE_Math_VectorAngleWrapped of the view forward vs the
//         +X reference flt_5CA2B0={1,0,0}) before being added to the eye, then clamped
//         to the world floor bounds (margin 20.0, flt_61DE0C).
//   * VIBE_Camera_EdgeScroll       @0x4b2c34 — resolves which screen edge/corner the
//     cursor sits on into the pan direction inputs (dword_6316CC / dword_6316D0,
//     each -1/0/+1) consumed by the combat-scroll decision core
//     (app::ResolveCombatScroll, combat_scroll.h) — reused verbatim here.
//   * VIBE_Camera_ZoomIn/Out       @0x4b4e24 / 0x4b5974 — dolly the eye toward/along
//     the view forward; the zoom FRACTION the original carries is flt_6316DC in
//     [0,1], which is exactly the term UpdatePan multiplies the pan speed by.
//
// Pure + deterministic (no globals, no time source of its own — `dt` is passed in),
// so the whole camera is drivable headless. Reuses util::VectorAngleWrapped (the
// real yaw helper) and app::ResolveCombatScroll (the real edge-scroll decision).
#include "guild/common/types.h"

namespace guild::play {

// ===========================================================================
// Reconstructed pan/zoom constants (the dbl_/flt_ immediates of UpdatePan).
// Exposed so tests can golden against the exact engine numbers.
// ===========================================================================
constexpr float kPanFrameTimeScale = 0.4f;   // dbl_61DDB8  dt -> frame factor
constexpr float kPanFrameTimeCap   = 4.0f;   // dbl_61DDC0  frame-factor ceiling
constexpr float kPanAccelStep      = 5.0f;   // dbl_61DDC8  accel per frame factor
constexpr float kPanVelMax         = 50.0f;  // dbl_61DDD0  velocity clamp (+)
constexpr float kPanVelMin         = -50.0f; // dbl_61DDD8  velocity clamp (-)
constexpr float kPanVelKick        = 10.0f;  // 1092616192  initial velocity kick
constexpr float kPanZoomGain       = 1.5f;   // dbl_61DDF0  zoom -> speed gain
constexpr float kPanZoomBias       = 0.6f;   // dbl_61DDF8  zoom -> speed bias
constexpr float kPanApplyMul       = 1.1f;   // dbl_61DE00  final world-delta mul
constexpr float kPanMinMove        = 0.1f;   // flt_61DE08  suppress sub-0.1 moves

// ===========================================================================
// Camera control state — eye / yaw / zoom plus the derived view transform the
// renderer consumes. `view` is rebuilt from (yaw, eye) by RebuildView so the
// (eye, view) pair is always the one BeginUniverseFrame would read.
// ===========================================================================
struct CameraControl {
    float eye[3]   = {0.0f, 0.0f, 0.0f};                       // engine +76 world origin
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};     // engine +396 view 4x4

    float yaw    = 0.0f;   // ground-plane rotation (radians) about the world Y axis
    float zoom   = 0.0f;   // zoom FRACTION in [0,1] (flt_6316DC) — 0 = out, 1 = in
    float sensitivity = 1.0f; // flt_6316D4 pan sensitivity multiplier

    // The persistent pan-velocity accumulators (dword_631DF8 / dword_631DF0). They
    // carry between frames so a held key ramps up and a released key decays.
    float velX = 0.0f;     // dword_631DF8
    float velZ = 0.0f;     // dword_631DF0

    int screenWidth  = 0;
    int screenHeight = 0;
};

// Build a city camera centred on `eye` looking top-down, with the given yaw/zoom and
// screen size. Rebuilds `view` from (yaw, eye). Deterministic; no globals touched.
CameraControl MakeCameraControl(const float eye[3], float yaw, float zoom,
                                int screenW, int screenH);

// Rebuild `cam.view` (the +396 4x4) from `cam.yaw` and `cam.eye`. The 3x3 is a yaw
// rotation about world Y (top-down city basis); the translation column (12..14) holds
// the eye. After this the (eye, view) pair is exactly what BeginUniverseFrame reads
// and what WorldToView/the city render projection consume.
void RebuildView(CameraControl& cam);

// ---------------------------------------------------------------------------
// Per-frame pan (the VIBE_Camera_UpdatePan core).
// ---------------------------------------------------------------------------

// Held-key / edge direction for one frame: each axis is -1 / 0 / +1.
//   dirX: -1 = pan left (A / Left),  +1 = pan right (D / Right)
//   dirZ: -1 = pan up/north (W / Up), +1 = pan down/south (S / Down)
struct PanInput {
    int dirX = 0;
    int dirZ = 0;
};

// Result of one pan step: the velocity after this frame and the world delta applied.
struct PanStep {
    float velX = 0.0f;     // post-frame X velocity accumulator
    float velZ = 0.0f;     // post-frame Z velocity accumulator
    float worldDX = 0.0f;  // world-space eye delta applied this frame (X)
    float worldDZ = 0.0f;  // world-space eye delta applied this frame (Z)
    bool  moved = false;   // an above-threshold move was applied
};

// Advance the camera one frame given the held direction and the frame time `dt`
// (milliseconds since the previous frame; 0 => the original's "no previous frame"
// default factor of 1.0). Ramps/decays the velocity accumulators exactly as
// UpdatePan, scales by the zoom fraction, rotates the (dx,dz) delta by the camera
// yaw, and adds it to the eye (rebuilding `view`). Returns the step detail.
PanStep CameraUpdatePan(CameraControl& cam, PanInput in, float dt);

// ---------------------------------------------------------------------------
// Zoom (VIBE_Camera_ZoomIn/Out — the zoom FRACTION + forward dolly).
// ---------------------------------------------------------------------------

// Nudge the zoom fraction by `delta` (positive = zoom in), CLAMPED to [0,1]. Returns
// the new zoom fraction. Updating the fraction changes how fast pan moves (UpdatePan
// scales by zoom*1.5+0.6) — the city view's "tighter zoom pans slower" feel.
float CameraZoomBy(CameraControl& cam, float delta);

// Dolly the eye along the camera forward axis by `amount` world units (the engine's
// ZoomIn/Out eye move). +amount moves toward the look point. Rebuilds `view`.
void CameraDolly(CameraControl& cam, float amount);

// ---------------------------------------------------------------------------
// Edge-scroll (VIBE_Camera_EdgeScroll -> ResolveCombatScroll -> pan).
// ---------------------------------------------------------------------------

// Resolve the cursor position to a pan direction. -1/0/+1 per axis: left/right edge
// on X, top/bottom edge on Z. `margin` is the edge band in pixels. Uses the REAL
// app::ResolveCombatScroll decision core to turn the raw edge inputs into per-edge
// scroll codes, then maps the active edge back to a PanInput direction.
PanInput ResolveEdgeScroll(int mouseX, int mouseY, int screenW, int screenH,
                           int margin);

// Convenience: resolve the cursor edge AND run a pan step in one call. Equivalent to
// CameraUpdatePan(cam, ResolveEdgeScroll(...), dt). Returns the pan step.
PanStep CameraEdgeScrollStep(CameraControl& cam, int mouseX, int mouseY,
                             int margin, float dt);

} // namespace guild::play
