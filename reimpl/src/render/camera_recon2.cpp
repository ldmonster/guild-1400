// camera_recon2.cpp — 1:1 translations of the VIBE_Camera_* "big movers" of
// gilde.exe (RotateView / UpdateMovement / ZoomReset / ZoomOut / OrientToTarget
// / UpdateTrackTargetFromMouse). See camera_recon2.h for the model + hooks.
//
// Provenance: every block is annotated with the original address. The pure
// math/transform leaves are the already-reconstructed guild::util functions;
// the genuinely-coupled leaves (3D sound, scene-graph, family record, terrain,
// mesh-height, anim-free, light refresh, constraints) are routed through
// Camera2Hooks with inert defaults.
#include "camera_recon2.h"

#include "util/math.h"        // VectorNormalize, VectorWithinTolerance, VectorAngleWrapped, AcosGuarded
#include "util/matrix.h"      // MatrixCopy
#include "util/float_math.h"  // (Fmod via guild::util — see note below)
#include "util/transform.h"   // PointThroughBoneChain

#include <cmath>
#include <cstring>

namespace guild::render {

// ===========================================================================
// Recovered constants (exact bytes via get_bytes; addresses noted).
// ===========================================================================
// RotateView (0x4b300c):
static const float  kRot_flt_61DDA0 = 0.0024999999441206455f; // mouse->yaw scale
static const double kRot_dbl_61DDA8 = 6.28318530718;          // 2*pi (fmod wrap)
static const double kRot_dbl_61DDB0 = 2.5;                    // pitch scale (inverted axis)
// the dword_4AD1C4 right/up/fwd basis the rotate path multiplies by:
//   v26/v27 (dword_4AD1C4[4],[5]) = 0.0, 0.0
//   v28 (*float dword_4AD1DC=0x4ad1dc) = -1.0  ; v29=+0 v30(0x4ad1e4)=0
//   v31 (*float dword_4AD1E8) = +1.0 ; v32(0x4ad1ec)=0
//   v33 (dword_4AD1F0[0]) = +1.0 ; v16/17/18 = +1,0,0 ; v19(dword_4AD200)=+1
static const float  kRot_v26 = 0.0f;   // 0x4ad1d4
static const float  kRot_v27 = 0.0f;   // 0x4ad1d8
static const float  kRot_v28 = -1.0f;  // 0x4ad1dc
static const float  kRot_v30 = 0.0f;   // 0x4ad1e4
static const float  kRot_v31 = 1.0f;   // 0x4ad1e8
static const float  kRot_v32 = 0.0f;   // 0x4ad1ec
static const float  kRot_v16 = 1.0f;   // 0x4ad1f4 (dword_4AD1F0[1])
static const float  kRot_v17 = 0.0f;   // 0x4ad1f8 (dword_4AD1F0[2])
static const float  kRot_v18 = 0.0f;   // 0x4ad1fc (dword_4AD1F0[3])

// UpdateMovement (0x4b41a8):
static const float  kMv_flt_61DE10 = 666.6666870117188f;
static const double kMv_dbl_61DE18 = 0.004;
static const float  kMv_flt_61DE20 = 5.0f;
static const float  kMv_flt_61DE24 = 0.001500000013038516f;
static const float  kMv_flt_61DE28 = 0.0035000001080334187f;
static const float  kMv_flt_61DE2C = 0.10000000149011612f;
static const double kMv_dbl_61DE30 = 6.0;
static const double kMv_dbl_61DE38 = 0.010101010101010102;
static const float  kMv_flt_61DE40 = -1.0f;
static const float  kMv_flt_61DE44 = 20.0f;

// ZoomReset (0x4b5250):
static const float  kZR_flt_61DE5C = -600.0f;
static const float  kZR_flt_61DE60 = 0.5f;
static const double kZR_dbl_61DE68 = 1.8;
static const float  kZR_flt_61DE70 = 0.0005000000237487257f;

// OrientToTarget (0x4b562c):
static const float  kOT_flt_61DE74 = 600.0f;
static const float  kOT_flt_61DE78 = 900.0f;
static const double kOT_dbl_61DE80 = 0.0;
static const float  kOT_flt_61DE88 = -1.875f;

// ZoomOut (0x4b5974):
static const float  kZO_flt_61DE8C = -600.0f;
static const float  kZO_flt_61DE90 = 0.5f;
static const double kZO_dbl_61DE98 = 1.8;
static const float  kZO_flt_61DEA0 = 0.0005000000237487257f;

// Forward axis (flt_5CA2B0/B4/B8) = (0,0,1).
static const float  kAxis0 = 0.0f, kAxis1 = 0.0f, kAxis2 = 1.0f;

// UpdateTrackTargetFromMouse (0x5e967c):
static const float  kTT_flt_62BF2C = 3.1415927410125732f; // pi
static const float  kTT_flt_62BF30 = 30.0f;
static const float  kTT_flt_62BF34 = 3.0f;
static const float  kTT_flt_62BF38 = 256.0f;
static const float  kTT_flt_62BF3C = -256.0f;

// minZoomDist (dword_6316C8) lives in CameraState.minZoomDist (=50).

// ===========================================================================
// Coordinate truncation toward zero — the original VIBE_Coord_ConvertX/Y
// (0x5c6b08 / 0x40da48) are x87 round-toward-zero chops of an FPU value. We
// chop to int the same way (the magnitudes the camera produces are in range).
// ===========================================================================
static inline i32 trunc_toward_zero(double x) { return (i32)x; }

// ===========================================================================
// Default inert hooks.
// ===========================================================================
static f32 inert_terrainHeight(i32, i32) { return 0.0f; }
static void inert_meshHeightRange(i32, f32* outA, f32* outB) {
    outA[0] = outA[1] = outA[2] = 0.0f;
    outB[0] = outB[1] = outB[2] = 0.0f;
}
static void inert_animFree(CameraObject*, i32, i32) {}
static i32 inert_sound3d(CameraObject*, const f32*, i32, const f32*, i32) { return 0; }
static void* inert_familyRecord() { return nullptr; }
static i32 inert_sceneGraphWalk(CameraObject*, i32) { return 0; }
static void inert_lightRequestCache(CameraObject*) {}
static void inert_lightRefreshAll(u32) {}
static u8 inert_applyConstraints(CameraObject* obj, const f32* dpos, const f32* /*basis*/,
                                 const f32* dworld, f32* outPos, f32* outWorld) {
    // Inert default: pos/world += deltas; report both changed (bit0|bit1).
    outPos[0]   = obj->posX  + dpos[0];
    outPos[1]   = obj->posY  + dpos[1];
    outPos[2]   = obj->posZ  + dpos[2];
    outWorld[0] = obj->worldX + dworld[0];
    outWorld[1] = obj->worldY + dworld[1];
    outWorld[2] = obj->worldZ + dworld[2];
    return 3;
}
static u8 inert_sceneDebugToggle(i32) { return 0; }
static void inert_setPosition(CameraObject* obj, const f32* v) {
    obj->posX = v[0]; obj->posY = v[1]; obj->posZ = v[2];
}
static void inert_setWorldTranslation(CameraObject* obj, const f32* v) {
    obj->worldX = v[0]; obj->worldY = v[1]; obj->worldZ = v[2];
}

Camera2Hooks Camera2_DefaultHooks() {
    Camera2Hooks h;
    h.terrainHeight         = &inert_terrainHeight;
    h.meshHeightRange       = &inert_meshHeightRange;
    h.animFree              = &inert_animFree;
    h.sound3dSetListener    = &inert_sound3d;
    h.personGetFamilyRecord = &inert_familyRecord;
    h.sceneGraphWalk        = &inert_sceneGraphWalk;
    h.lightRequestCache     = &inert_lightRequestCache;
    h.lightRefreshAll       = &inert_lightRefreshAll;
    h.applyConstraints      = &inert_applyConstraints;
    h.sceneDebugToggle      = &inert_sceneDebugToggle;
    h.setPosition           = &inert_setPosition;
    h.setWorldTranslation   = &inert_setWorldTranslation;
    return h;
}

// Helper: write the camera pos/world into a family record (float/dword indices
// 32..39) exactly as the originals do. `rec` is the GetFamilyRecord pointer.
//   *((float*)rec + 32) = zoomTBits-as-float ; +33..35 = pos ; +37..39 = world.
static void family_write_full(void* rec, const CameraObject& obj, i32 zoomTBits) {
    if (!rec) return;
    f32*  fr = reinterpret_cast<f32*>(rec);
    i32*  dr = reinterpret_cast<i32*>(rec);
    std::memcpy(&fr[32], &zoomTBits, sizeof(f32)); // *((float*)rec+32) = *(float*)&dword_6316E0
    std::memcpy(&dr[33], &obj.posX,  sizeof(i32));
    std::memcpy(&dr[34], &obj.posY,  sizeof(i32));
    std::memcpy(&dr[35], &obj.posZ,  sizeof(i32));
    std::memcpy(&dr[37], &obj.worldX, sizeof(i32));
    std::memcpy(&dr[38], &obj.worldY, sizeof(i32));
    std::memcpy(&dr[39], &obj.worldZ, sizeof(i32));
}
// pos-only variant used by UpdateMovement LABEL_45 (writes +33..35 from pos,
// +37..39 from world; no +32).
static void family_write_posworld(void* rec, const CameraObject& obj) {
    if (!rec) return;
    i32* dr = reinterpret_cast<i32*>(rec);
    std::memcpy(&dr[33], &obj.posX,  sizeof(i32));
    std::memcpy(&dr[34], &obj.posY,  sizeof(i32));
    std::memcpy(&dr[35], &obj.posZ,  sizeof(i32));
    std::memcpy(&dr[37], &obj.worldX, sizeof(i32));
    std::memcpy(&dr[38], &obj.worldY, sizeof(i32));
    std::memcpy(&dr[39], &obj.worldZ, sizeof(i32));
}

// Build a frame-entity buffer (>=512 bytes) from the camera object so the
// reconstructed PointThroughBoneChain can run on it. The original passes the
// whole scene node dword_13FCD1C; CameraObject only models the touched fields,
// so we materialize a frame whose:
//   frame[30..32] (bytes 120/124/128) = obj.pos2* (the chain's base translation)
//   parent link (byte 504) = null  (the camera node's common no-parent case)
// PointThroughBoneChain then returns point + frame[30..32], matching the
// original for a parent-less camera node. (When the real node has a parent the
// caller must supply a full frame; OrientToTarget already takes a1 as one.)
struct FrameBuf { f32 data[160]; };  // 160 floats = 640 bytes > byte-504 link
static void build_frame_from_object(const CameraObject& obj, FrameBuf& fb) {
    std::memset(fb.data, 0, sizeof(fb.data));
    fb.data[30] = obj.pos2X;   // byte 120
    fb.data[31] = obj.pos2Y;   // byte 124
    fb.data[32] = obj.pos2Z;   // byte 128
    // parent link at byte 504 (float 126) left null -> chain loop terminates.
}

// The history-save gate shared by RotateView/UpdateMovement:
//   dword_631610 == dword_631618 && (!dword_631744 || dword_631748)
static inline bool history_save_gate(const Camera2Input& in) {
    return in.g_631610 == in.g_631618 && (!in.g_631744 || in.g_631748);
}

// ===========================================================================
// 0x4b300c — VIBE_Camera_RotateView
// Mouse-drag orbit: when drag active and the rotate button (672220) is held it
// applies a pure world-translation yaw/pitch (the flt_11BC264 triple). When the
// drag is held but the button is NOT down it orbits the camera POSITION about
// the target (the v34 vector branch), with axis-invert via byte_671D6F.
// ===========================================================================
i32 Camera_RotateView(CameraObject& obj, Camera2State& st, Camera2Input& in,
                      const Camera2Hooks& h) {
    // Snapshot the dword_4AD1C4 basis the original copies into locals (constants).
    const f32 v26 = kRot_v26, v27 = kRot_v27, v28 = kRot_v28;
    const f32 v30 = kRot_v30, v31 = kRot_v31, v32 = kRot_v32;
    const f32 v16 = kRot_v16, v17 = kRot_v17, v18 = kRot_v18; // dword_4AD1F0[1..3]
    // v33 (dword_4AD1F0[0]) and v19 (dword_4AD200) are loaded but never used.

    i32 result = 0;

    // 0x4b3055: if ( dword_672238 && !dword_631DEC ) latch the drag start.
    if (in.d672238 && !st.rotActive) {
        st.rotPrevView = in.viewShift;     // dword_631DE4 = SHIWORD(dword_672170)
        st.rotActive   = 1;                // dword_631DEC = 1
        st.rotPrevY    = in.d672174;       // dword_631DE8 = dword_672174>>16
    }

    // 0x4b30a2: button-held world-translation yaw branch.
    if (in.d672220 && in.g_13FCD1C_present && in.d672238) {
        if (in.byte671D6F) {
            const i32 v36 = in.d672174 - st.rotPrevY;
            st.wt0 = (f32)((double)v36 * kRot_flt_61DDA0 + (double)obj.worldX);
            std::memcpy(&st.wt1, &obj.worldY, sizeof(f32));
            std::memcpy(&st.wt2, &obj.worldZ, sizeof(f32));
            st.wt0 = (f32)guild::util::Fmod((double)st.wt0, kRot_dbl_61DDA8);
        } else {
            const f32 v4 = obj.worldX;
            const i32 v36 = in.viewShift - st.rotPrevView;
            st.wt0 = v4;
            st.wt1 = (f32)((double)obj.worldY - (double)v36 * kRot_flt_61DDA0);
            std::memcpy(&st.wt2, &obj.worldZ, sizeof(f32));
            st.wt1 = (f32)guild::util::Fmod((double)st.wt1, kRot_dbl_61DDA8);
        }
        f32 wt[3] = {st.wt0, st.wt1, st.wt2};
        h.setWorldTranslation(&obj, wt);   // VIBE_Object_SetWorldTranslation

        if (history_save_gate(in)) {
            std::memcpy(&st.hist_2E4, &obj.worldX, sizeof(i32));
            std::memcpy(&st.hist_2E8, &obj.worldY, sizeof(i32));
            std::memcpy(&st.hist_2EC, &obj.worldZ, sizeof(i32));
            std::memcpy(&st.hist_2D4, &obj.posX,  sizeof(i32));
            std::memcpy(&st.hist_2D8, &obj.posY,  sizeof(i32));
            std::memcpy(&st.hist_2DC, &obj.posZ,  sizeof(i32));
        }
        st.rotPrevView = in.viewShift;
        st.rotPrevY    = in.d672174;
        result = 1;
    }

    // 0x4b31b3: drag released -> clear active and bail.
    if (!in.d672238 && st.rotActive) {
        st.rotActive = in.d672238;         // = 0
        return 0;
    }

    // 0x4b31dc: orbit the camera POSITION about the target (button NOT held).
    if (in.g_13FCD1C_present && in.d672238 && !in.d672220) {
        f32 m[16];
        guild::util::MatrixCopy(obj.matrix, m); // VIBE_Math_MatrixCopy(obj+396, &v7)

        i32 v36 = in.viewShift - st.rotPrevView;
        double v2 = (double)v36;
        f32 v20 = (f32)(v16 * v2);
        f32 v21 = (f32)(v17 * v2);
        f32 v22 = (f32)(v2 * v18);
        f32 v34[3];
        v34[0] = v20 * m[0] + v21 * m[3] + v22 * m[6];
        v34[1] = v20 * m[1] + v21 * m[4] + v22 * m[7];
        v34[2] = v20 * m[2] + v21 * m[5] + v22 * m[8];

        if (in.byte671D6F) {
            v36 = st.rotPrevY - in.d672174;
            double v3 = (double)v36 * kRot_dbl_61DDB0;
            f32 v23 = (f32)(v30 * v3);
            f32 v24 = (f32)(v31 * v3);
            f32 v25 = (f32)(v3 * v32);
            v34[0] = v34[0] + v23;
            v34[1] = v34[1] + v24;
            v34[2] = v34[2] + v25;
            v34[0] = v34[0] + obj.posX;
            v34[1] = v34[1] + obj.posY;
            v34[2] = v34[2] + obj.posZ;
        } else {
            v36 = in.d672174 - st.rotPrevY;
            double v5 = (double)v36 * kRot_dbl_61DDB0;
            v20 = (f32)(v26 * v5);
            v21 = (f32)(v27 * v5);
            v22 = (f32)(v5 * v28);
            f32 v23 = v20 * m[0] + v21 * m[3] + v22 * m[6];
            f32 v24 = v20 * m[1] + v21 * m[4] + v22 * m[7];
            f32 v25 = v20 * m[2] + v21 * m[5] + v22 * m[8];
            f32 v35 = (f32)std::sqrt((double)v23 * v23 + (double)v24 * v24 + (double)v25 * v25);
            v34[0] = v34[0] + v23;
            v34[2] = v34[2] + v25;
            // v6 = v35 / sqrt(v34[0]^2 + 0 + v34[2]^2)
            double denom = std::sqrt((double)v34[0] * v34[0] + 0.0 * 0.0 + (double)v34[2] * v34[2]);
            double v6 = (double)v35 / denom;
            v34[0] = (f32)((double)v34[0] * v6);
            v34[1] = (f32)(0.0 * v6);
            v34[2] = (f32)(v6 * (double)v34[2]);
            v34[0] = v34[0] + obj.posX;
            v34[1] = v34[1] + obj.posY;
            v34[2] = v34[2] + obj.posZ;
            v34[1] = obj.posY;  // final overwrite: Y locked to current posY
        }

        if (!guild::util::VectorWithinTolerance(st.lastPos, v34, 0.1f)) {
            h.setPosition(&obj, v34);      // VIBE_Object_SetPosition
            if (history_save_gate(in)) {
                std::memcpy(&st.hist_2E4, &obj.worldX, sizeof(i32));
                std::memcpy(&st.hist_2E8, &obj.worldY, sizeof(i32));
                std::memcpy(&st.hist_2EC, &obj.worldZ, sizeof(i32));
                std::memcpy(&st.hist_2D4, &obj.posX,  sizeof(i32));
                std::memcpy(&st.hist_2D8, &obj.posY,  sizeof(i32));
                std::memcpy(&st.hist_2DC, &obj.posZ,  sizeof(i32));
            }
        }
        std::memcpy(st.lastPos, v34, sizeof(st.lastPos));
        st.rotPrevView = in.viewShift;
        st.rotPrevY    = in.d672174;
        return 1;
    }
    return result;
}

// ===========================================================================
// 0x4b41a8 — VIBE_Camera_UpdateMovement
// Per-frame state machine: latches drag start, runs the pan branch (button +
// drag), the wheel-zoom branch (672254 != 672250), and the rotate branch.
// ===========================================================================
i32 Camera_UpdateMovement(CameraObject& obj, CameraState& cs, Camera2State& st,
                          Camera2Input& in, const Camera2Hooks& h,
                          const CameraHooks* anchorHooks) {
    if (in.disableMove)                    // dword_62D4E4
        return 0;

    // 0x4b41d6: latch drag start.
    if (in.d672238 && !st.mvActive) {
        in.disableMove = in.disableMove;   // dword_62D0D4 = dword_62D4E4 (==0 here)
        st.boxFlag = in.disableMove;
        st.mvActive = 1;                   // dword_631E20 = 1
        st.mvCoordY0 = in.d672210;         // dword_631E04
        st.mvPrevY   = in.d672210;         // dword_11BC338
        st.mvCoordX0 = in.d67220E;         // dword_631E00
        st.mvPrevView= in.d67220E;         // dword_11BC334
        st.mvSaveBox0 = st.box0; st.box0 = 32000;   // 11BC324 = 62D0C4; 62D0C4 = 32000
        st.mvSaveBox1 = st.box1; st.box1 = 32000;
        st.mvSaveBox2 = st.box2; st.box2 = -32000;
        st.mvSaveBox3 = st.box3; st.box3 = -32000;
    }

    // 0x4b427f: pan-init (button held, drag active, not yet initialized).
    if (in.d672238 && in.d672220 && !st.mvPanInit) {
        st.mvZoomT0 = cs.zoomT;            // dword_631E10 = LODWORD(flt_6316DC)
        // flt_631E1C = obj+80 - ((flt_6316BC-flt_6316B4)*flt_6316DC + flt_6316B8)
        st.mvHeight0 = obj.posY - ((cs.spanHeight - cs.baseHeight) * cs.zoomT + cs.baseAngle);
        std::memcpy(&st.mvZoomT0b, &cs.zoomTBits, sizeof(f32)); // dword_631E14 = dword_6316E0
        f32 zoomBitsF; std::memcpy(&zoomBitsF, &cs.zoomTBits, sizeof(f32)); // v0 = *(float*)&dword_6316E0
        const double v1 = (double)kMv_flt_61DE10;
        const double v2 = (1.0 - (double)zoomBitsF) * v1;
        // VIBE_Coord_ConvertX() truncates v2 (the FPU top) -> v3.
        const i32 v3 = trunc_toward_zero(v2);
        st.mvPrevView2 = v3;               // dword_11BC33C = v3
        st.mvPanInit = 1;                  // dword_631E08 = 1
        st.mvPrevY2 = in.d672174;          // dword_11BC340 = dword_672174>>16
        st.box1 = in.d672174 + (i32)v2;    // 62D0C8 = (dword_672174>>16) + (int)v2
        // second ConvertX: v59 = (int)(v1 * -v0). v0 = *(float*)&dword_6316E0.
        const i32 v59 = trunc_toward_zero(v1 * -(double)zoomBitsF);
        st.mvWorldY0 = obj.worldY;         // flt_631E18 = *(v4+136) where v4 alias obj
        st.box3 = trunc_toward_zero(0.0) + v59; // 62D0D0 = v5 + v59 (v5 from ConvertX top)
    }

    // 0x4b4355: drag/button released -> finalize pan-init zoom-T.
    if ((!in.d672220 || !in.d672238) && st.mvPanInit) {
        const i32 v57 = in.d672174 - st.mvPrevY2;
        st.mvPanInit = 0;                  // dword_631E08 = 0
        st.box0 = 32000;
        st.mvPrevY = in.d672174;           // dword_11BC338
        st.box1 = 32000;
        st.box2 = -32000;
        st.box3 = -32000;
        st.mvPrevView = in.viewShift;      // dword_11BC334 = SHIWORD(672170)
        st.mvZoomT0 = (f32)((double)v57 * kMv_dbl_61DE18 + (double)st.mvZoomT0);
    }

    if (in.d672238 || !st.mvActive) {
        if (in.g_13FCD1C_present) {
            // 0x4b43f1: pan branch (drag + button + pan-init).
            if (in.d672238 && in.d672220 && st.mvPanInit) {
                i32 v57 = in.d672254 - in.d672250;
                double v7 = ((double)v57 * kMv_flt_61DE20
                             + (double)in.viewShift - (double)st.mvPrevY2) * kMv_flt_61DE24;
                f32 v55 = (f32)((double)st.mvZoomT0 + v7);
                cs.zoomT = v55;            // LODWORD(flt_6316DC) = v55
                // build position triple: pos with Y replaced.
                f32 vpos[3];
                vpos[0] = obj.posX; vpos[2] = obj.posZ;
                vpos[1] = (f32)((double)(cs.spanHeight - cs.baseHeight) * (double)v55 + (double)st.mvHeight0);
                v55 = (f32)(v7 + (double)st.mvZoomT0b);
                std::memcpy(&cs.zoomTBits, &v55, sizeof(i32)); // dword_6316E0 = v55
                // build world triple.
                f32 vwld[3];
                vwld[0] = (f32)((double)cs.baseAngle + (double)(cs.spanAngle - cs.baseAngle) * (double)v55);
                vwld[1] = obj.worldY; vwld[2] = obj.worldZ;
                v57 = in.viewShift - st.mvPrevView2;
                vwld[1] = (f32)((double)st.mvWorldY0 - (double)v57 * kMv_flt_61DE28);

                f32 curPos[3]   = {obj.posX, obj.posY, obj.posZ};
                f32 curWorld[3] = {obj.worldX, obj.worldY, obj.worldZ};
                if (!guild::util::VectorWithinTolerance(curPos, vpos, 0.1f)
                    || !guild::util::VectorWithinTolerance(curWorld, vwld, 0.001f)) {
                    h.setPosition(&obj, vpos);
                    h.setWorldTranslation(&obj, vwld);
                    void* fr = h.personGetFamilyRecord();
                    family_write_full(fr, obj, cs.zoomTBits);
                }
            }
        }

        // 0x4b45f? : wheel-zoom branch (no pan-init, wheel delta nonzero).
        if (in.g_13FCD1C_present && !st.mvPanInit && in.d672254 != in.d672250) {
            cs.zoomT = (f32)((double)(in.d672254 - in.d672250) * kMv_flt_61DE2C + (double)cs.zoomT);
            double v52 = (double)cs.zoomT;
            i32 v50, v51;
            if (cs.zoomT <= 1.0f) {
                std::memcpy(&v50, reinterpret_cast<char*>(&v52) + 0, sizeof(i32));
                std::memcpy(&v51, reinterpret_cast<char*>(&v52) + 4, sizeof(i32));
            } else {
                v51 = 1072693248;          // high dword of 1.0 double
                v50 = st.mvPanInit;        // == 0
            }
            double paired;
            { i32 tmp[2] = {v50, v51}; std::memcpy(&paired, tmp, sizeof(double)); }
            double v49 = paired >= 0.0 ? paired : 0.0;
            i32 v30bits; { f32 fz = (f32)v49; std::memcpy(&v30bits, &fz, sizeof(i32)); }
            // 0x4b46b1: VIBE_Camera_AnchorToTerrain(a1@eax, a2@edx=0, a3=v49 pushed).
            // a1@eax is an indeterminate leftover register at the call site (the
            // decompiler's "v6"); a2 is 0; a3 is the clamped zoom-T float bits.
            // With inert terrainHeight, a1/a2 do not affect the output. We reuse
            // the earlier-pass reconstruction (camera_recon.h) for the anchor;
            // `anchorHooks` (when supplied by the dispatcher) carries the bound
            // terrainHeight so the wheel zoom follows the real terrain.
            Camera_AnchorToTerrain(obj, cs,
                                   anchorHooks ? *anchorHooks : Camera_DefaultHooks(),
                                   /*a1*/0, /*a2*/0, v30bits);
            void* fr = h.personGetFamilyRecord();
            family_write_full(fr, obj, cs.zoomTBits);
        }

        // 0x4b474e: bail unless rotate branch applies (drag + !button).
        if (!in.g_13FCD1C_present || !in.d672238 || in.d672220)
            return in.d672254 + st.mvDone + st.mvPanInit - in.d672250;

        // 0x4b475c: rotate branch.
        f32 m[16];
        guild::util::MatrixCopy(obj.matrix, m);
        i32 v57 = in.viewShift - st.mvPrevView;
        double v16d = (double)in.d1233564 * kMv_dbl_61DE30 * kMv_dbl_61DE38;
        double v17d = (double)v57 * v16d;
        v57 = in.d672174 - st.mvPrevY;
        f32 v65 = (f32)v17d;
        f32 v66 = (f32)(v16d * ((double)v57 * kMv_flt_61DE40));

        f32 v37 = kAxis0 * m[0] + kAxis1 * m[3] + kAxis2 * m[6];
        f32 v39 = kAxis0 * m[1] + kAxis1 * m[4] + kAxis2 * m[7];
        f32 v38 = 0.0f;
        f32 v43, v44, v45;
        if (std::sqrt((double)v37 * v37 + 0.0 * 0.0 + (double)v39 * v39) >= (double)kMv_flt_61DE2C) {
            f32 axis[3] = {kAxis0, kAxis1, kAxis2};
            f32 dir[3]  = {v37, 0.0f, v39};   // v37 written; v38 set above
            double v27 = -guild::util::VectorAngleWrapped(axis, dir);
            double s = std::sin(v27);
            double c = std::cos(v27);
            v37 = (f32)((double)v66 * s + (double)v65 * c);
            v39 = (f32)(s * -(double)v65 + (double)v66 * c);
            v38 = 0.0f;
        } else {
            v37 = (f32)((double)v65 * m[0] + (double)v66 * m[3] + 0.0 * m[6]);
            v39 = (f32)((double)v65 * m[1] + (double)v66 * m[4] + 0.0 * m[7]);
            v38 = 0.0f;
        }
        v43 = v37 + obj.posX;
        v44 = v38 + obj.posY;
        v45 = v39 + obj.posZ;

        f32 target[3] = {v43, v44, v45};
        if (guild::util::VectorWithinTolerance(&st.mvTargetX, target, 0.1f)) {
            // LABEL_47
            std::memcpy(&st.mvTargetX, &v43, sizeof(i32));
            std::memcpy(&st.mvTargetY, &v44, sizeof(i32));
            std::memcpy(&st.mvTargetZ, &v45, sizeof(i32));
            st.mvPrevView = in.viewShift;
            st.mvPrevY    = in.d672174;
            st.mvDone     = 1;
            return in.d672254 + st.mvDone + st.mvPanInit - in.d672250;
        }

        // 0x4b490e: optional AABB clamp (off_649D64+44).
        if (in.clampBoxPresent && st.clampBox) {
            const f32* box = st.clampBox;  // box[0]=minX base, box[2]=stepX? ... (see notes)
            // v62 = box[4]*flt_61DE44 + box[0] ; v18=box record base.
            f32 v62 = box[4] * kMv_flt_61DE44 + box[0];
            if (v43 >= (double)v62) {
                i32 cnt = st.clampBoxCount - 20;   // *(v18+32) - 20
                f32 v64 = (f32)((double)cnt * box[4] + box[0]);
                if (!(v43 <= (double)v64)) v43 = v64;
            } else {
                v43 = v62;
            }
            // LABEL_40: Z clamp.
            double v20d = (double)box[6] * kMv_flt_61DE44 + (double)box[2]; // box[6]=*(v18+24), box[2]=*(v18+8)
            i32 cnt = st.clampBoxCount - 20;
            f32 v60 = (f32)v20d;
            f32 v61 = (f32)((double)cnt * box[6] + (double)box[2]);
            f32 v21f = (v45 <= (double)v61) ? v61 : v45;
            v45 = v21f;
            f32 v22f = (v21f >= (double)v60) ? v60 : v21f;
            v45 = v22f;
            v43 = v43; // (already set)
        }
        // LABEL_45: commit.
        f32 commit[3] = {v43, v44, v45};
        h.setPosition(&obj, commit);
        void* fr = h.personGetFamilyRecord();
        family_write_posworld(fr, obj);
        // LABEL_47
        std::memcpy(&st.mvTargetX, &v43, sizeof(i32));
        std::memcpy(&st.mvTargetY, &v44, sizeof(i32));
        std::memcpy(&st.mvTargetZ, &v45, sizeof(i32));
        st.mvPrevView = in.viewShift;
        st.mvPrevY    = in.d672174;
        st.mvDone     = 1;
        return in.d672254 + st.mvDone + st.mvPanInit - in.d672250;
    }

    // 0x4b4ae3: drag fully released with mvActive -> restore box + clear.
    st.box0 = st.mvSaveBox0;
    st.box1 = st.mvSaveBox1;
    st.box2 = st.mvSaveBox2;
    st.mvActive = in.d672238;              // dword_631E20 = dword_672238 (==0)
    st.box3 = st.mvSaveBox3;
    st.boxFlag = 1;                        // dword_62D0D4 = 1
    in.disableMove = 1;                    // dword_62D4E4 = 1
    // VIBE_Coord_ConvertY(dword_631E00, dword_631E04) -> v26 (truncated). The
    // original stores that into 631E00/04/08; with our snapshot it is the same
    // pair fed back. Model as the truncated identity of the X snapshot.
    const i32 v26 = trunc_toward_zero((double)st.mvCoordX0);
    st.mvCoordX0 = v26;
    st.mvCoordY0 = v26;
    st.mvPanInit = v26;
    return 0;
}

// ===========================================================================
// 0x4b5250 — VIBE_Camera_ZoomReset
// Snaps the camera to a fixed offset above/behind an object, eased toward the
// terrain, then commits and updates the family record. Returns the mesh handle.
// ===========================================================================
i32 Camera_ZoomReset(CameraObject& obj, CameraState& cs, Camera2State& st,
                     const Camera2Hooks& h, i32 a1MeshHandle, i32 a2, i32 a3) {
    (void)a2;
    f32 lo[3], hi[3];
    h.meshHeightRange(a1MeshHandle, lo, hi);     // v28(lo)/v29(hi) BYREF

    f32 m[16];
    guild::util::MatrixCopy(obj.matrix, m);

    f32 v19 = kAxis0 * kZR_flt_61DE5C;
    f32 v20 = kAxis1 * kZR_flt_61DE5C;
    f32 v21 = kZR_flt_61DE5C * kAxis2;
    f32 v22 = v19 * m[0] + v20 * m[4] + v21 * m[8];
    f32 v23 = v19 * m[1] + v20 * m[5] + v21 * m[9];
    f32 v24 = v19 * m[2] + v20 * m[6] + v21 * m[10];

    // PointThroughBoneChain(obj, obj+19floats, &v16). frame=obj node, point=obj+76.
    f32 v16[3];
    {
        FrameBuf fb; build_frame_from_object(obj, fb);
        f32 point[3] = {obj.posX, obj.posY, obj.posZ}; // obj+19 floats (bytes 76/80/84)
        guild::util::PointThroughBoneChain(fb.data, point, v16);
    }

    f32 v13 = v16[0] + v22;
    f32 v14 = v16[1] + v23;
    f32 v15 = v16[2] + v24;
    // v14 = terrainAvg + baseH + (spanH-baseH)*zoomT
    v14 = h.terrainHeight(0, 0) + cs.baseHeight + (cs.spanHeight - cs.baseHeight) * cs.zoomT;

    v19 = v13 - v16[0];
    v20 = v14 - v16[1];
    v21 = v15 - v16[2];

    h.meshHeightRange(0, lo, hi);                 // recompute with v7 (handle)
    v20 = (hi[0] - lo[0]) * kZR_flt_61DE60 + v20;
    f32 v31 = v23 / v20;
    v13 = (f32)((double)v13 - ((double)v22 * v31 - (double)v22 * kZR_dbl_61DE68));
    v15 = (f32)((double)v15 - ((double)v24 * v31 - (double)v24 * kZR_dbl_61DE68));

    i32 v25, v26, v27;
    std::memcpy(&v25, &obj.worldX, sizeof(i32));
    std::memcpy(&v26, &obj.worldY, sizeof(i32));
    std::memcpy(&v27, &obj.worldZ, sizeof(i32));

    h.animFree(&obj, a3, a1MeshHandle);           // VIBE_Anim_FreeObjAnimData
    cs.altMoveMode = 0;                           // dword_62D4E8 = 0
    cs.disableMove = 0;                           // dword_62D4E4 = 0

    v14 = h.terrainHeight(0, 0) + cs.baseHeight + (cs.spanHeight - cs.baseHeight) * cs.zoomT;
    v19 = v13 - obj.posX;
    v20 = v14 - obj.posY;
    v21 = v15 - obj.posZ;
    f32 v30 = (f32)cs.minZoomDist;                // (float)dword_6316C8
    f32 v31b = (f32)(std::sqrt((double)v19 * v19 + (double)v20 * v20 + (double)v21 * v21)
                     * (double)v30 * (double)kZR_flt_61DE70);
    f32 v9 = (v30 > (double)v31b) ? v30 : v31b;
    hi[2] = v9;                                   // v29[2] = v9

    // history mirrors + commit.
    std::memcpy(&st.hist_2E4, &v25, sizeof(i32));
    std::memcpy(&st.hist_2E8, &v26, sizeof(i32));
    std::memcpy(&st.hist_2EC, &v27, sizeof(i32));
    std::memcpy(&st.hist_2D4, &v13, sizeof(i32));
    std::memcpy(&st.hist_2D8, &v14, sizeof(i32));
    std::memcpy(&st.hist_2DC, &v15, sizeof(i32));

    f32 newPos[3]   = {v13, v14, v15};
    h.setPosition(&obj, newPos);
    f32 newWorld[3];
    std::memcpy(&newWorld[0], &v25, sizeof(f32));
    std::memcpy(&newWorld[1], &v26, sizeof(f32));
    std::memcpy(&newWorld[2], &v27, sizeof(f32));
    h.setWorldTranslation(&obj, newWorld);

    void* fr = h.personGetFamilyRecord();
    if (fr) {
        f32* frf = reinterpret_cast<f32*>(fr);
        i32* frd = reinterpret_cast<i32*>(fr);
        frf[33] = v13; frf[34] = v14; frf[35] = v15;
        frd[37] = v25; frd[38] = v26; frd[39] = v27;
    }

    st.zr_lastA1   = a1MeshHandle;                // dword_11BC278 = a1
    st.zr_lastReset = a1MeshHandle;               // dword_631740 = *(a1+97) (mesh handle)
    return a1MeshHandle;                          // result = *(a1+97)
}

// ===========================================================================
// 0x4b5974 — VIBE_Camera_ZoomOut
// Like ZoomReset but ends by setting the 3D-sound listener (kind 104) instead
// of committing a position. Returns the listener result.
// ===========================================================================
i32 Camera_ZoomOut(CameraObject& obj, CameraState& cs, Camera2State& st,
                   const Camera2Hooks& h, i32 a1MeshHandle, i32 a2, i32 a3, i32 a4) {
    (void)a2; (void)st;
    f32 lo[3], hi[3];
    h.meshHeightRange(a1MeshHandle, lo, hi);      // v33(lo)/v32(hi)

    f32 m[16];
    guild::util::MatrixCopy(obj.matrix, m);

    f32 v28 = kAxis0 * kZO_flt_61DE8C;
    f32 v29 = kAxis1 * kZO_flt_61DE8C;
    f32 v30 = kZO_flt_61DE8C * kAxis2;
    f32 v22 = v28 * m[0] + v29 * m[4] + v30 * m[8];
    f32 v23 = v28 * m[1] + v29 * m[5] + v30 * m[9];
    f32 v24 = v28 * m[2] + v29 * m[6] + v30 * m[10];

    f32 v25[3];
    {
        FrameBuf fb; build_frame_from_object(obj, fb);
        f32 point[3] = {obj.posX, obj.posY, obj.posZ};
        guild::util::PointThroughBoneChain(fb.data, point, v25);
    }

    f32 v19 = v25[0] + v22;
    f32 v20 = v25[1] + v23;
    f32 v21 = v25[2] + v24;
    v20 = h.terrainHeight(0, 0) + cs.baseHeight + (cs.spanHeight - cs.baseHeight) * cs.zoomT;
    v28 = v19 - v25[0];
    v29 = v20 - v25[1];
    v30 = v21 - v25[2];

    h.meshHeightRange(0, lo, hi);
    v29 = (hi[0] - lo[0]) * kZO_flt_61DE90 + v29;
    f32 v38 = v23 / v29;
    v19 = (f32)((double)v19 - ((double)v22 * v38 - (double)v22 * kZO_dbl_61DE98));
    v21 = (f32)((double)v21 - ((double)v24 * v38 - (double)v24 * kZO_dbl_61DE98));

    f32 v31[3] = {obj.worldX, obj.worldY, obj.worldZ}; // +132/+136/+140

    h.animFree(&obj, a3, a4);
    cs.altMoveMode = 0;                           // dword_62D4E8 = v8 (==0)
    cs.disableMove = 0;                           // dword_62D4E4 = v8

    v20 = h.terrainHeight(0, 0) + cs.baseHeight + (cs.spanHeight - cs.baseHeight) * cs.zoomT;
    v28 = v19 - obj.posX;
    v29 = v20 - obj.posY;
    v30 = v21 - obj.posZ;
    f32 v37 = (f32)cs.minZoomDist;
    f32 v38b = (f32)(std::sqrt((double)v28 * v28 + (double)v29 * v29 + (double)v30 * v30)
                     * (double)v37 * (double)kZO_flt_61DEA0);
    f32 v10 = (v37 > (double)v38b) ? v37 : v38b;
    hi[2] = v10;

    double v11 = (double)(3 * cs.minZoomDist);
    f32 v34 = (f32)v11;
    f32 v13;
    if (v11 < (double)v10) {
        v13 = v34;
    } else {
        double v12 = (double)cs.minZoomDist;
        f32 v35 = (f32)v12;
        if (v12 <= (double)v38b)
            v13 = v38b;
        else
            v13 = v35;
        hi[1] = v13;                              // v33[1] = v13
    }
    double v14 = (double)v13;
    // VIBE_Coord_ConvertX() truncates v14 -> the listener distance.
    i32 dist = trunc_toward_zero(v14);
    return h.sound3dSetListener(&obj, /*posVec*/ v31, dist, /*angVec*/ v31, 104);
}

// ===========================================================================
// 0x4b562c — VIBE_Camera_OrientToTarget
// Computes a yaw/pitch pair pointing the camera at a target derived from the
// frame's local -Z axis, then sets the 3D-sound listener (kind 40).
// ===========================================================================
i32 Camera_OrientToTarget(CameraObject& obj, CameraState& cs, Camera2State& st,
                          const Camera2Hooks& h, f32* a1Frame) {
    (void)st;
    f32 v13[3];
    {
        // PointThroughBoneChain(a1, a1+19floats, &v13).
        f32 point[3] = {a1Frame[19], a1Frame[20], a1Frame[21]};
        guild::util::PointThroughBoneChain(a1Frame, point, v13);
    }
    // local dir = (0,0,-1) rotated by the frame 3x3 (a1[99..109]).
    f32* v1 = a1Frame;
    double v2 = 0.0 * v1[100] + 0.0 * v1[104] + -1.0 * v1[108];
    double v3 = 0.0 * v1[101] + 0.0 * v1[105] + -1.0 * v1[109];
    f32 v16 = (f32)(0.0 * v1[99] + 0.0 * v1[103] + -1.0 * v1[107]);
    f32 v17 = (f32)v2;
    f32 v18 = (f32)v3;
    v16 = v16 * kOT_flt_61DE74;
    v17 = v17 * kOT_flt_61DE74;
    v18 = kOT_flt_61DE74 * v18;

    f32 v7 = v13[0] + v16;
    f32 v8 = v13[1] + v17;
    f32 v9 = v13[2] + v18;
    v8 = v8 + kOT_flt_61DE78;

    // yaw: angle of (target - eye) projected onto forward axis.
    f32 v19 = v13[0] - v7;
    f32 v20 = 0.0f;
    f32 v21 = v13[2] - v9;
    f32 vn[3] = {v19, v20, v21};
    guild::util::VectorNormalize(vn);
    v19 = vn[0]; v20 = vn[1]; v21 = vn[2];
    double v4 = (double)kAxis0 * v19 + (double)kAxis1 * v20 + (double)kAxis2 * v21;
    f32 v34 = (f32)v4;
    double v30 = v4;
    double v26;
    if (kOT_dbl_61DE80 <= v4 && v30 >= 1.0) {
        v26 = 1.0;
    } else {
        double v29 = (double)v34;
        double v28 = (kOT_dbl_61DE80 <= (double)v34) ? v29 : -1.0;
        v26 = v28;
    }
    f32 v23 = (f32)guild::util::AcosGuarded(v26);
    if (v19 > 0.0f) {
        u32 bits; std::memcpy(&bits, &v23, sizeof(u32));
        bits ^= 0x80000000u;             // HIBYTE(v23) ^= 0x80 (sign flip)
        std::memcpy(&v23, &bits, sizeof(f32));
    }

    // pitch: angle including Y.
    v19 = v13[0] - v7;
    v20 = v13[1] - v8;
    f32 v10 = v19;
    v21 = v13[2] - v9;
    f32 v12 = v21;
    f32 v11 = 0.0f;
    f32 vn2[3] = {v19, v20, v21};
    guild::util::VectorNormalize(vn2);
    v19 = vn2[0]; v20 = vn2[1]; v21 = vn2[2];
    f32 vn3[3] = {v10, v11, v12};
    guild::util::VectorNormalize(vn3);
    v10 = vn3[0]; v11 = vn3[1]; v12 = vn3[2];
    double v5 = (double)v10 * v19 + (double)v11 * v20 + (double)v12 * v21;
    f32 v33 = (f32)v5;
    double v32 = v5;
    double v27;
    if (kOT_dbl_61DE80 > v5 || v32 < 1.0) {
        double v31 = (double)v33;
        double v25 = (kOT_dbl_61DE80 <= (double)v33) ? v31 : -1.0;
        v27 = v25;
    } else {
        v27 = 1.0;
    }
    f32 v22 = (f32)guild::util::AcosGuarded(v27);
    if (v20 < 0.0f) {
        u32 bits; std::memcpy(&bits, &v22, sizeof(u32));
        bits ^= 0x80000000u;
        std::memcpy(&v22, &bits, sizeof(f32));
    }
    v8 = v8 + kOT_flt_61DE88;

    f32 posVec[3] = {v7, v8, v9};
    f32 angVec[3] = {v22, v23, 0.0f};    // &v22 -> {v22, v23, v24(=0)}
    return h.sound3dSetListener(&obj, posVec, cs.minZoomDist, angVec, 40);
}

// ===========================================================================
// 0x5e967c — VIBE_Camera_UpdateTrackTargetFromMouse
// Drives the active track object by mouse delta. Two edge-tracked drag modes
// (672220 pan, 672234 tilt). The accumulated deltas are routed to position /
// world-translation axes per the axis-lock bytes, then committed through
// VIBE_Object_ApplyTransformConstraints. Returns whether anything moved.
// ===========================================================================
bool Camera_UpdateTrackTargetFromMouse(Camera2Input& in, const Camera2Hooks& h,
                                       TrackTargetCtx& ctx) {
    char v0 = 0;     // tilt-moved
    char v34 = 0;    // pan-moved
    int v3 = 0;

    if (!ctx.active)
        return false;

    // basis selector -> v2 (the transform basis frame).
    f32* v2;
    switch (ctx.basisSel) {
        case 1:  v2 = ctx.frame649EF0; break;
        case 2:  v2 = ctx.frame649EF4; break;
        case 3:  v2 = ctx.frame649EF8; break;
        default: v2 = ctx.frame649EFC; break;
    }

    f32 v8[4]  = {0,0,0,0};
    f32 v15[4] = {0,0,0,0};
    f32 v9 = 0, v10 = 0, v11 = 0;     // dpos accumulator
    f32 v12 = 0, v13 = 0, v14 = 0;    // dworld accumulator
    f32 v29 = 0, v30 = 0, v31 = 0, v32 = 0;

    // --- pan drag (672220) ---
    if (!in.d672220 || ctx.latchPan) {
        if (in.d672220
            && ctx.activeTrackable
            && ctx.active != reinterpret_cast<CameraObject*>(ctx.frame649EF0)
            && ctx.active != reinterpret_cast<CameraObject*>(ctx.frame649EF4)
            && ctx.active != reinterpret_cast<CameraObject*>(ctx.frame649EF8)) {
            i32 dxv;
            if (ctx.btnX) { dxv = in.d67220E - ctx.prevX; v34 = 1; } else { dxv = 0; }
            i32 dyv;
            if (ctx.btnY) { v34 = 1; dyv = in.d672210 - ctx.prevY; } else { dyv = 0; }
            ctx.prevX = in.d67220E;
            ctx.prevY = in.d672210;
            v32 = (f32)((double)dxv / (double)ctx.screenW * kTT_flt_62BF2C);
            v29 = (f32)(kTT_flt_62BF2C * ((double)dyv / (double)ctx.screenH));
            v3 = dxv;
        }
    } else {
        ctx.latchPan = 1;
        ctx.prevX = in.d67220E;
        ctx.prevY = in.d672210;
    }

    // --- tilt drag (672234) ---
    if (!in.d672234 || ctx.latchTilt) {
        if (in.d672234) {
            i32 v7;
            if (ctx.btnX) { v0 = 1; v7 = in.d67220E - ctx.prevX; } else { v7 = 0; }
            i32 dyv;
            if (ctx.btnY) { dyv = in.d672210 - ctx.prevY; v0 = 1; } else { dyv = 0; }
            double v27 = 1.0 / (double)ctx.screenW;
            double v20 = std::fabs((double)v7);
            ctx.prevX = in.d67220E;
            double v22 = (double)dyv / (double)ctx.screenH;
            ctx.prevY = in.d672210;
            double v23 = (double)v7 * v27;
            double v28 = (v20 + 1.0) * kTT_flt_62BF30 * v27;
            f32 v24 = (kTT_flt_62BF34 >= v28) ? (f32)v28 : 3.0f;
            double v18 = (double)ctx.screenH;
            double v19 = std::fabs((double)dyv);
            double v21 = 1.0 / v18;
            double v25 = (v19 + 1.0) * kTT_flt_62BF30 * v21;
            f32 v26 = (kTT_flt_62BF34 >= v25) ? (f32)v25 : 3.0f;
            v31 = (f32)(v22 * kTT_flt_62BF3C * v26);
            v30 = (f32)(v23 * kTT_flt_62BF38 * v24);
        }
    } else {
        ctx.prevX = in.d67220E;
        v3 = 1;
        ctx.latchTilt = 1;
        ctx.prevY = in.d672210;
    }

    // --- route deltas per axis-lock + commit ---
    if (v0 || v34) {
        if (reinterpret_cast<CameraObject*>(ctx.frame649EFC) == ctx.active) {
            if (ctx.lock671D8A || ctx.lock671D7D) {
                v14 = v32;
            } else {
                v12 = v29;
                v13 = v32;
            }
            if (!ctx.lock671D8A && !ctx.lock671D7D) {
                v9 = v30;
                v10 = v31;
                goto LABEL_16;
            }
            // LABEL_15
            v11 = v31;
        } else {
            if (ctx.alt64A024) {
                if (ctx.lock671D8A) {
                    v13 = v32;
                } else if (ctx.lock671D7D) {
                    v12 = v32;
                } else {
                    v14 = v32;
                }
                if (!ctx.lock671D8A && !ctx.lock671D7D) {
                    v9 = v30;
                    v10 = v31;
                    goto LABEL_16;
                }
                v11 = v31; // LABEL_15
                goto AFTER_ROUTE;
            }
            if (!ctx.lock671D8A) {
                if (ctx.lock671D7D) {
                    v12 = v32;
                    if (ctx.lock671D8A) { v11 = v31; goto AFTER_ROUTE; } // LABEL_15
                } else {
                    v14 = v32;
                    if (ctx.lock671D8A) { v11 = v31; goto AFTER_ROUTE; } // LABEL_15
                }
                if (!ctx.lock671D7D) {
                    v9 = v30;
                    v10 = v31;
                    goto LABEL_16;
                }
                v11 = v31; // LABEL_15
                goto AFTER_ROUTE;
            }
            v13 = v32;
            v11 = v31; // LABEL_15
        }
    AFTER_ROUTE:
    LABEL_16:
        {
            f32 dpos[3]   = {v9, v10, v11};
            f32 dworld[3] = {v12, v13, v14};
            u8 r = h.applyConstraints(ctx.active, dpos, v2, dworld, v8, v15);
            v0  = (char)(r & 1);
            v34 = (char)((int)r >> 1);
        }
    }

    if (v0)
        h.setPosition(ctx.active, v8);
    if (v34
        && ctx.active != reinterpret_cast<CameraObject*>(ctx.frame649EF0)
        && ctx.active != reinterpret_cast<CameraObject*>(ctx.frame649EF4)
        && ctx.active != reinterpret_cast<CameraObject*>(ctx.frame649EF8))
        h.setWorldTranslation(ctx.active, v15);

    // latched-release light updates.
    if (ctx.latchPan && in.d67221C) {
        ctx.latchPan = 0;
        if (h.sceneGraphWalk(ctx.active, 14))
            h.lightRequestCache(ctx.active);
        else
            h.lightRefreshAll(1u);
        v0 = 1;
    }
    if (ctx.latchTilt && in.d672230) {
        ctx.latchTilt = 0;
        if (h.sceneGraphWalk(ctx.active, 6))
            h.lightRequestCache(ctx.active);
        else
            h.lightRefreshAll(1u);
        v0 = 1;
    }

    // VIBE_Scene_HandleDebugKeyToggle((void*)v3) — v3 is the leftover edge value.
    bool dbg = h.sceneDebugToggle(v3) != 0;
    return (dbg | (v0 != 0)) || (v34 != 0);
}

} // namespace guild::render
