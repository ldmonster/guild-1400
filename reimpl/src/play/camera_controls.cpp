// Wave 25 PLAY P2 — CITY-VIEW CAMERA CONTROLS implementation. See camera_controls.h.
//
// REUSE of real reconstructions (we call them; we do NOT redefine them):
//   app::ResolveCombatScroll  (app/combat_scroll.h, VIBE_Camera_UpdateCombatScroll
//                              decision core @0x487b2c) — edge -> per-edge scroll code.
//   util::MatrixIdentity      (util/matrix.h) — view-matrix init.
//   util::VectorAngleWrapped  (util/math.h, VIBE_Math_VectorAngleWrapped @0x5ca504) —
//                              the yaw helper the original pan rotation uses; exercised
//                              in the e2e to confirm the rotation matches the engine.
#include "play/camera_controls.h"

#include <cmath>

#include "app/combat_scroll.h"   // ResolveCombatScroll (real edge-scroll decision)
#include "util/matrix.h"         // MatrixIdentity (real)
#include "util/math.h"           // VectorAngleWrapped (real yaw helper)

namespace guild::play {

namespace {

float Clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// One axis of the UpdatePan velocity model. `dir` is the held direction (-1/0/+1).
// On an active axis the velocity is kicked to ±kPanVelKick on the first frame from
// rest, then ramped by ±step and clamped to [kPanVelMin, kPanVelMax]. An idle axis
// decays toward 0 by `step`, snapping to 0 on the zero crossing (the original's
// "subtract step, if it overshot past 0 clamp to 0" decay).
float RampAxis(float vel, int dir, float step) {
    // UpdatePan ramps with NO post-clamp: it only adds the step while strictly
    // inside the bound, so the velocity can overshoot the bound by up to one step
    // (the engine writes `if (vel < 50) vel += step;` — never clamps afterward).
    if (dir > 0) {
        if (vel == 0.0f) vel = kPanVelKick;        // 1092616192 seed
        if (vel < kPanVelMax) vel += step;         // @0x4b3..  no clamp after
        return vel;
    }
    if (dir < 0) {
        if (vel == 0.0f) vel = -kPanVelKick;       // -1054867456 seed
        if (vel > kPanVelMin) vel -= step;         // @0x4b3..  no clamp after
        return vel;
    }
    // Idle: decay toward zero (mirrors the dword_631DF0/F8 decay tails).
    if (vel > 0.0f) {
        vel -= step;
        if (vel < 0.0f) vel = 0.0f;
    } else if (vel < 0.0f) {
        vel += step;
        if (vel > 0.0f) vel = 0.0f;
    }
    return vel;
}

} // namespace

// ---------------------------------------------------------------------------
// Setup / view rebuild.
// ---------------------------------------------------------------------------

void RebuildView(CameraControl& cam) {
    // Top-down city basis: a yaw rotation about world Y seated in the 3x3, the eye in
    // the translation column. Matches the +396 4x4 layout BeginUniverseFrame reads.
    guild::util::MatrixIdentity(cam.view);
    const float c = std::cos(cam.yaw);
    const float s = std::sin(cam.yaw);
    // Rotate the ground (X,Z) plane about Y. Rows [0,_,8] / [2,_,10] carry the basis
    // the engine forward/right vectors are read from (cols 0/1/2 of each 4-float row).
    cam.view[0]  = c;   cam.view[2]  = s;
    cam.view[8]  = -s;  cam.view[10] = c;
    cam.view[12] = cam.eye[0];
    cam.view[13] = cam.eye[1];
    cam.view[14] = cam.eye[2];
}

CameraControl MakeCameraControl(const float eye[3], float yaw, float zoom,
                                int screenW, int screenH) {
    CameraControl cam;
    cam.eye[0] = eye[0];
    cam.eye[1] = eye[1];
    cam.eye[2] = eye[2];
    cam.yaw    = yaw;
    cam.zoom   = Clampf(zoom, 0.0f, 1.0f);
    cam.screenWidth  = screenW;
    cam.screenHeight = screenH;
    RebuildView(cam);
    return cam;
}

// ---------------------------------------------------------------------------
// Pan (VIBE_Camera_UpdatePan core).
// ---------------------------------------------------------------------------

PanStep CameraUpdatePan(CameraControl& cam, PanInput in, float dt) {
    PanStep out;

    // Frame-time factor: ft = clamp(dt * 0.4, max 4.0); default 1.0 with no prior dt
    // (UpdatePan's dword_631DFC == 0 branch). dt is ms since the previous frame.
    float ft = (dt > 0.0f) ? dt * kPanFrameTimeScale : 1.0f;
    if (ft >= kPanFrameTimeCap) ft = kPanFrameTimeCap;

    // Acceleration step applied to the velocity accumulators this frame.
    const float step = ft * kPanAccelStep;

    // Ramp/decay each velocity accumulator.
    cam.velX = RampAxis(cam.velX, in.dirX, step);
    cam.velZ = RampAxis(cam.velZ, in.dirZ, step);
    out.velX = cam.velX;
    out.velZ = cam.velZ;

    // Apply factor: sensitivity, then zoom-scaled (pan speed grows with zoom-in),
    // then the final 1.1 multiplier. This is UpdatePan's
    //   v41 *= flt_6316D4; v41 = (zoom*1.5 + 0.6) * v41; delta = vel * v41 * 1.1
    float applyFt = ft * cam.sensitivity;
    applyFt = (cam.zoom * kPanZoomGain + kPanZoomBias) * applyFt;
    const float localDX = cam.velX * applyFt * kPanApplyMul;
    const float localDZ = cam.velZ * applyFt * kPanApplyMul;

    // Rotate the ground (dx,dz) delta by the camera yaw (the engine rotates the pan
    // vector by VIBE_Math_VectorAngleWrapped(forward, +X) before adding to the eye).
    const float cy = std::cos(cam.yaw);
    const float sy = std::sin(cam.yaw);
    const float worldDX = localDX * cy - localDZ * sy;
    const float worldDZ = localDX * sy + localDZ * cy;

    // Sub-0.1 moves are suppressed (the VectorWithinTolerance(...,0.1) early-out).
    if (std::sqrt(worldDX * worldDX + worldDZ * worldDZ) >= kPanMinMove) {
        cam.eye[0] += worldDX;
        cam.eye[2] += worldDZ;
        out.worldDX = worldDX;
        out.worldDZ = worldDZ;
        out.moved = true;
        RebuildView(cam);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Zoom.
// ---------------------------------------------------------------------------

float CameraZoomBy(CameraControl& cam, float delta) {
    cam.zoom = Clampf(cam.zoom + delta, 0.0f, 1.0f);
    return cam.zoom;
}

void CameraDolly(CameraControl& cam, float amount) {
    // Forward axis == the view 3x3 forward (rows 8/9/10), the basis ZoomIn dollies
    // along. For the top-down yaw basis this is (-sin yaw, 0, cos yaw).
    const float fx = cam.view[8];
    const float fy = cam.view[9];
    const float fz = cam.view[10];
    cam.eye[0] += fx * amount;
    cam.eye[1] += fy * amount;
    cam.eye[2] += fz * amount;
    RebuildView(cam);
}

// ---------------------------------------------------------------------------
// Edge-scroll.
// ---------------------------------------------------------------------------

PanInput ResolveEdgeScroll(int mouseX, int mouseY, int screenW, int screenH,
                           int margin) {
    PanInput out;

    // Raw edge inputs (dword_6316CC horizontal, dword_6316D0 vertical), -1/0/+1, as
    // EdgeScroll derives them from cursor-vs-edge. Left/Top edge = move toward it.
    int scrollX = 0, scrollY = 0;
    if (mouseX <= margin)                 scrollX = 1;   // left edge   -> pan left
    else if (mouseX >= screenW - margin)  scrollX = -1;  // right edge  -> pan right
    if (mouseY <= margin)                 scrollY = -1;  // top edge    -> pan up
    else if (mouseY >= screenH - margin)  scrollY = 1;   // bottom edge -> pan down

    // Headless: no live unit is pushing, so the four edge MOTION vectors are the
    // settled zero vector — ResolveCombatScroll then lights the requested edge.
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    guild::app::CombatScrollDecision dec =
        guild::app::ResolveCombatScroll(scrollX, scrollY, zero, zero, zero, zero);

    const bool left   = dec.left   != guild::app::CombatScrollCode::kNone;
    const bool right  = dec.right  != guild::app::CombatScrollCode::kNone;
    const bool top    = dec.top    != guild::app::CombatScrollCode::kNone;
    const bool bottom = dec.bottom != guild::app::CombatScrollCode::kNone;

    // With scrollX==0 the decision core lights BOTH horizontal edges (settled), so
    // only honour an edge when an actual direction was requested.
    if (scrollX == 1 && left)        out.dirX = -1;
    else if (scrollX == -1 && right) out.dirX = 1;
    if (scrollY == -1 && top)        out.dirZ = -1;
    else if (scrollY == 1 && bottom) out.dirZ = 1;

    return out;
}

PanStep CameraEdgeScrollStep(CameraControl& cam, int mouseX, int mouseY,
                             int margin, float dt) {
    PanInput in = ResolveEdgeScroll(mouseX, mouseY, cam.screenWidth, cam.screenHeight,
                                    margin);
    return CameraUpdatePan(cam, in, dt);
}

} // namespace guild::play
