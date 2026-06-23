// camera_recon.cpp — 1:1 translations of the VIBE_Camera_* cluster of gilde.exe.
// See camera_recon.h for the cluster overview and the hooks/state model.
#include "camera_recon.h"

#include <cmath>
#include <cstring>

namespace guild::render {

// ===========================================================================
// Recovered constants (exact bytes via get_bytes; see header for addresses).
// ===========================================================================
// height/flight params modeled in CameraState (flt_6316B4/B8/BC/C0, dword_6316C8)

// ClampToTerrainHeight (0x4b2a0c):
static const double kClamp_dbl_61DD70 = 5.0;          // deadzone (|dy| <= -> no move)
static const float  kClamp_flt_61DD78 = 0.06666667014360428f; // dy * this -> step
static const double kClamp_dbl_61DD80 = -10.0;        // lower step bound
static const float  kClamp_flt_61DD88 = 10.0f;        // upper step bound
static const double kClamp_dbl_61DD90 = -0.005;       // worldX ease down
static const double kClamp_dbl_61DD98 = 0.005;        // worldX ease up
// the saturated step magic ints: -1054867456 -> -10.0f, 1092616192 -> 10.0f
static const float  kClamp_neg10f = -10.0f;           // bits 0xC1200000
static const float  kClamp_pos10f = 10.0f;            // bits 0x41200000

// ComputeZoomScale (0x4c20ec):
static const float kZoom_flt_61E578 = 500.0f;
static const float kZoom_flt_61E57C = 1600.0f;
static const float kZoom_flt_61E580 = 16000.0f;
static const float kZoom_const_16000 = 16000.0f;
static const float kZoom_const_1600  = 1600.0f;

// ===========================================================================
// Coordinate truncation toward zero (the original VIBE_Coord_ConvertX, 0x5c6b08,
// is an x87 round-toward-zero chop of a double on the FPU stack — the value the
// code stores via (int)v9). We translate that as a C truncation, which is the
// identical behavior for the in-range magnitudes the camera produces.
// ===========================================================================
static inline i32 trunc_toward_zero(double x) { return (i32)x; }

// ===========================================================================
// Default inert hooks.
// ===========================================================================
static f32 inert_terrainHeight(i32, i32, const f32*, void*) { return 0.0f; }
static void inert_setPosition(CameraObject* obj, const f32* v) {
    obj->posX = v[0]; obj->posY = v[1]; obj->posZ = v[2];
}
static void inert_setWorldTranslation(CameraObject* obj, const f32* v) {
    obj->worldX = v[0]; obj->worldY = v[1]; obj->worldZ = v[2];
}
static f32 inert_fmod(f32 x, f32 m) { return std::fmod(x, m); }
static void inert_matrixCopy(const f32* src, f32* dst) {
    std::memcpy(dst, src, 16 * sizeof(f32));
}
static bool inert_withinTolerance(const f32* a, const f32* b, f32 tol) {
    // VIBE_Math_VectorWithinTolerance(a,b,tol): true iff each |a-b| <= tol.
    for (int i = 0; i < 3; ++i)
        if (std::fabs(a[i] - b[i]) > tol) return false;
    return true;
}

CameraHooks Camera_DefaultHooks() {
    CameraHooks h;
    h.terrainHeight       = &inert_terrainHeight;
    h.setPosition         = &inert_setPosition;
    h.setWorldTranslation = &inert_setWorldTranslation;
    h.fmodHook            = &inert_fmod;
    h.matrixCopy          = &inert_matrixCopy;
    h.withinTolerance     = &inert_withinTolerance;
    return h;
}

// ===========================================================================
// 0x43f528 — VIBE_Camera_CmdCameraFlight
// 0x43f5dc — VIBE_Camera_CmdCameraFlightTimed
//
// Two near-identical script-command shims. The original:
//   if ( dword_62E8CC && *(slot+44) == THIS_CMD ) {
//       if ( dword_62D4E4 || dword_62D4E8 ) *(...+2528) = THIS_CMD;
//       return 0;
//   } else {
//       if ( *(...+2564) == 1 ) *(...+2528) = THIS_CMD;
//       <flight: DrawTextLabels3D(a1[0]/17, 6, a2[0])>      (Timed uses /14)
//       <flight cmd reports error on failure>               (Timed: no report)
//       return 1;
//   }
// We model the dispatch-slot self-check, the move-flag and script-flag gates,
// and capture the observable side effects in CmdCameraFlightCtx. divisor=17/14.
// ===========================================================================
static i32 cmd_camera_flight_common(i32 a1_val, CmdCameraFlightCtx& ctx,
                                     i32 divisor, bool reportOnFail) {
    if (ctx.slotPresent && ctx.slotIsSelf) {
        if (ctx.movingFlags) ctx.wroteSlot = true;     // *(...+2528) = cmd
        return 0;
    } else {
        if (ctx.scriptFlag1) ctx.wroteSlot = true;      // *(...+2528) = cmd
        ctx.drawArg = a1_val / divisor;                 // a1[0] / 17 (or /14)
        ctx.drawCalled = true;
        // DrawTextLabels3D returns nonzero on success; flight cmd reports on 0.
        if (reportOnFail && !ctx.drawOk)
            ctx.reportedError = true;
        return 1;
    }
}

i32 Camera_CmdCameraFlight(i32 a1_val, CmdCameraFlightCtx& ctx) {
    return cmd_camera_flight_common(a1_val, ctx, 17, /*reportOnFail=*/true);
}

i32 Camera_CmdCameraFlightTimed(i32 a1_val, CmdCameraFlightCtx& ctx) {
    return cmd_camera_flight_common(a1_val, ctx, 14, /*reportOnFail=*/false);
}

// ===========================================================================
// 0x4b2900 — VIBE_Camera_AnchorToTerrain(a1=x, a2=z, a3=zoomT bits)
//
//   if ( !dword_649D60 ) {
//       flt_6316DC  = (float)a3;            // zoomT = (float)a3
//       dword_6316E0 = a3;                  // zoomTBits = a3 (raw)
//       v3 = VIBE_Terrain_AverageAreaHeight(a1, a2);
//       v4 = obj+76; v5 = obj+80; v6 = obj+84;
//       v5 = v3 + flt_6316B4 + (flt_6316BC - flt_6316B4) * flt_6316DC;
//       VIBE_Object_SetPosition(obj, &v4);
//       w0 = obj+132; w1 = obj+136; w2 = obj+140;
//       w0 = flt_6316B8 + (flt_6316C0 - flt_6316B8) * *(float*)&dword_6316E0;
//       VIBE_Object_SetWorldTranslation(obj, w);
//       hist_2E4 = obj+132; hist_2E8 = obj+136; hist_2EC = obj+140;
//       hist_2D4 = obj+76;  hist_2D8 = obj+80;  hist_2DC = obj+84;
//   }
//
// Note flt_6316DC is the int a3 reinterpreted: LODWORD(flt_6316DC)=a3 sets the
// FLOAT's BITS to a3, then it is used as a float. dword_6316E0=a3 (the integer)
// but read back via *(float*)&dword_6316E0. So both are the SAME float bits.
// ===========================================================================
void Camera_AnchorToTerrain(CameraObject& obj, CameraState& st, const CameraHooks& h,
                            i32 a1, i32 a2, i32 a3) {
    if (st.globalDisabled)
        return;

    // LODWORD(flt_6316DC) = a3  -> the float's raw bits become a3.
    f32 zoomT;
    std::memcpy(&zoomT, &a3, sizeof(f32));
    st.zoomT = zoomT;
    st.zoomTBits = a3;                       // dword_6316E0 = a3
    f32 zoomTAsFloat;
    std::memcpy(&zoomTAsFloat, &st.zoomTBits, sizeof(f32)); // *(float*)&dword_6316E0

    // 0x4b292a: VIBE_Terrain_AverageAreaHeight(a1@ecx, a2@edx, node+76@eax).
    // The terrain sample point is the camera node's position triple.
    const f32 wq[3] = {obj.posX, obj.posY, obj.posZ};
    const f32 v3 = h.terrainHeight(a1, a2, wq, h.terrainUser);

    // position triple (read +76/+80/+84, replace Y)
    f32 v[3];
    v[0] = obj.posX;
    v[1] = v3 + st.baseHeight + (st.spanHeight - st.baseHeight) * st.zoomT;
    v[2] = obj.posZ;
    h.setPosition(&obj, v);                  // writes +76/+80/+84

    // world-translation triple (read +132/+136/+140, replace X)
    f32 w[3];
    w[0] = st.baseAngle + (st.spanAngle - st.baseAngle) * zoomTAsFloat;
    w[1] = obj.worldY;
    w[2] = obj.worldZ;
    h.setWorldTranslation(&obj, w);          // writes +132/+136/+140

    // mirror copies (the original copies the object's NOW-updated fields)
    std::memcpy(&st.hist_2E4, &obj.worldX, sizeof(i32));
    std::memcpy(&st.hist_2E8, &obj.worldY, sizeof(i32));
    std::memcpy(&st.hist_2EC, &obj.worldZ, sizeof(i32));
    std::memcpy(&st.hist_2D4, &obj.posX, sizeof(i32));
    std::memcpy(&st.hist_2D8, &obj.posY, sizeof(i32));
    std::memcpy(&st.hist_2DC, &obj.posZ, sizeof(i32));
}

// ===========================================================================
// 0x4b2a0c — VIBE_Camera_ClampToTerrainHeight(this=x sample base)
//
//   if ( !obj ) return;
//   v11 = VIBE_Terrain_AverageAreaHeight(this, obj);
//   v12 = baseH + (spanH-baseH)*zoomT + v11 - (obj+80 + obj+124);   // dy
//   if ( |v12| <= 5.0 ) { clampResultBits = 0; goto LABEL_12; }
//   step = v12 * 0.06666667;
//   if ( step <= 0 ) {
//       if ( step >= 0 ) goto LABEL_9;          // step == 0
//       if ( step <= -10.0 ) step = -10.0f;     // saturate low
//       // else keep step
//   } else {
//       if ( step >= 10.0f ) step = 10.0f;      // saturate high  (uses flt_61DD88)
//       // else keep
//   }
// LABEL_9:
//   v5 = obj+76; v6 = obj+80; v7 = obj+84; v6 += step;
//   if ( dword_62D4E8 ) {                        // alt mode: read +120/+124/+128
//       v5 = obj+120; v6 = obj+124; v7 = obj+128; v6 += step;
//       // NOTE: does NOT call SetPosition in alt mode
//   } else {
//       VIBE_Object_SetPosition(obj, &v5);       // commit (+76/+80/+84 + step)
//   }
//   clampResultBits = step bits;                 // dword_631DDC = v17
// LABEL_12:
//   v8 = obj+132; v9 = obj+136; v10 = obj+140;
//   v15 = spanH - baseH;
//   if ( (v15 bits & 0x7FFFFFFF) != 0 ) {        // v15 != 0 (incl -0)
//       v16 = (obj+80 - v11)/v15 * (spanA-baseA) + baseA;
//       if ( obj+132 > v16 ) { v8 = obj+132 - 0.005; if (v8 < v16) v8 = v16; }
//       if ( v8 < v16 )       v8 = v8 + 0.005;
//       // (v8 is local only; not written back to the object)
//   }
//
// The trailing v8/v16 block computes a target world-X and eases v8 toward it,
// but the result lives only in the local (no setter call), matching the
// decompile exactly. clampResultBits records the applied step (0 in deadzone).
// ===========================================================================
void Camera_ClampToTerrainHeight(CameraObject& obj, CameraState& st, const CameraInput& in,
                                 const CameraHooks& h, i32 thisX) {
    (void)in;  // the decompile's alt-mode gate is dword_62D4E8 (st.altMoveMode),
               // not an input field; CameraInput is kept for API symmetry.
    if (!obj.present)
        return;

    // 0x4b2a2b: VIBE_Terrain_AverageAreaHeight(this@ecx, node@edx, node+76@eax)
    // — ecx/edx leftovers; the sample point is the camera node position.
    const f32 wq[3] = {obj.posX, obj.posY, obj.posZ};
    const f32 v11 = h.terrainHeight(thisX, /*node ptr leftover*/ 0, wq, h.terrainUser);

    const double v12 = (double)st.baseHeight
                     + (double)(st.spanHeight - st.baseHeight) * (double)st.zoomT
                     + (double)v11
                     - ((double)obj.posY + (double)obj.pos2Y);

    if (std::fabs(v12) <= kClamp_dbl_61DD70) {
        st.clampResultBits = 0;          // dword_631DDC = 0
        // fallthrough to the LABEL_12 world-X ease (no object write there)
    } else {
        f32 step = (f32)(v12 * kClamp_flt_61DD78);
        if (step <= 0.0f) {
            if (step >= 0.0f) {
                // step == 0: LABEL_9 (apply with step==0)
            } else if (step <= (f32)kClamp_dbl_61DD80) {
                step = kClamp_neg10f;    // -10.0f
            } // else keep step
        } else {
            if (step >= (double)kClamp_flt_61DD88)
                step = kClamp_pos10f;    // +10.0f
            // else keep
        }

        // LABEL_9: build position triple from primary (+76/+80/+84), Y += step.
        f32 v[3];
        v[0] = obj.posX; v[1] = obj.posY + step; v[2] = obj.posZ;
        if (st.altMoveMode) {             // if ( dword_62D4E8 )
            // alt mode: re-read secondary triple (+120/+124/+128), Y += step,
            // and DO NOT call SetPosition (matches decompile's else-less path).
            v[0] = obj.pos2X; v[1] = obj.pos2Y + step; v[2] = obj.pos2Z;
        } else {
            h.setPosition(&obj, v);       // commit primary
        }
        std::memcpy(&st.clampResultBits, &step, sizeof(i32)); // dword_631DDC = step
    }

    // LABEL_12 — world-X ease toward terrain-derived target (local only).
    f32 v8 = obj.worldX;          // +132
    (void)obj.worldY;             // +136 read into v9 (unused for write)
    (void)obj.worldZ;             // +140 read into v10
    const f32 v15 = st.spanHeight - st.baseHeight;
    u32 v15bits;
    std::memcpy(&v15bits, &v15, sizeof(u32));
    if ((v15bits & 0x7FFFFFFFu) != 0u) {
        const f32 v16 = (obj.posY - v11) / v15 * (st.spanAngle - st.baseAngle) + st.baseAngle;
        if ((double)v8 > (double)v16) {
            const double v4 = (double)v8 + kClamp_dbl_61DD90;  // -0.005
            v8 = (f32)v4;
            if ((double)v8 < (double)v16) v8 = v16;
        }
        if ((double)v8 < (double)v16) {
            v8 = (f32)((double)v8 + kClamp_dbl_61DD98);        // +0.005
        }
    }
    (void)v8;  // result not written back (matches decompile)
}

// ===========================================================================
// 0x4c20ec — VIBE_Camera_ComputeZoomScale
//
//   VIBE_Amt_FindNextActiveBuilding(&v20, &v14);   // v14 = area divisor
//   VIBE_Person_SumCurrencyHeld(...);
//   w = VIBE_Person_ComputeTotalWealth(...);        // recomputed each test
//   //  ratio = w * 500.0 / area
//   if ( ratio < 1600.0 || (ratio2 = ..., ratio2 <= 16000.0) ) {
//       w = ComputeTotalWealth(...);
//       if ( ratio3 >= 1600.0 ) {
//           w = ComputeTotalWealth(...);
//           result = w * 500.0 / area;              // mid band
//       } else {
//           result = 1600.0;                        // low band
//       }
//   } else {
//       result = 16000.0;                           // high band
//   }
//   v9 = result; truncate to int (VIBE_Coord_ConvertX); v18 = (int)v9;
//   if ( v10 <= (int)v9 ) return 0; else return (int)v9;
//
// All ComputeTotalWealth calls in the body return the same wealth here (same
// args, no state change), so ctx.wealth models one value. flt_61E578=500,
// flt_61E57C=1600, flt_61E580=16000.
// ===========================================================================
i32 Camera_ComputeZoomScale(const ComputeZoomScaleCtx& ctx) {
    const double area = (double)ctx.area;
    const double w    = (double)ctx.wealth;
    const double ratio = w * (double)kZoom_flt_61E578 / area;

    double result;
    if (ratio < (double)kZoom_flt_61E57C
        || (w * (double)kZoom_flt_61E578 / area) <= (double)kZoom_flt_61E580) {
        const double ratio3 = w * (double)kZoom_flt_61E578 / area;
        if (ratio3 >= (double)kZoom_flt_61E57C) {
            result = w * (double)kZoom_flt_61E578 / area;   // mid band
        } else {
            result = (double)kZoom_const_1600;              // low band
        }
    } else {
        result = (double)kZoom_const_16000;                 // high band
    }

    const i32 truncated = trunc_toward_zero(result);
    if (ctx.truncMax <= truncated)
        return 0;
    return truncated;
}

} // namespace guild::render
