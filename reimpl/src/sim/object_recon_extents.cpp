// gilde.exe object/scene-record helpers — strict 1:1 reconstruction.
// See object_recon_extents.h for cluster scope and provenance.
#include "object_recon_extents.h"

#include <cmath>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Local helpers mirroring pure math leaves (used as inert defaults).
// ---------------------------------------------------------------------------
namespace {

// 0x5caa4c VIBE_Math_VectorWithinTolerance — exact translation.
bool VectorWithinTolerance_impl(const f32* a1, const f32* a2, f32 a3) {
    double v4 = a3;
    return std::fabs((double)(a2[0] - a1[0])) <= (double)a3
        && std::fabs((double)(a2[1] - a1[1])) <= v4
        && std::fabs((double)(a2[2] - a1[2])) <= v4;
}

void identityMatrix(f32* m) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void computeBoneWorldMatrix_inert(u32, const f32*, f32* outMat) { identityMatrix(outMat); }
f32  vectorAngleWrapped_inert(const f32*, const f32*) { return 0.0f; }
void matrixFromEuler_inert(const f32*, f32* outMat) { identityMatrix(outMat); }
void matrixMul_inert(const f32*, const f32* b, f32* out) { std::memcpy(out, b, 16 * sizeof(f32)); }
void matrixToEuler_inert(f32*) {}

void project_inert(f32 worldX, f32 worldY, i32* outX, i32* outY) {
    *outX = (i32)worldX;
    *outY = (i32)worldY;
}

// ---- record field accessors (match the original byte arithmetic) ----
// The original is 32-bit: pointer-bearing fields are dwords. On a 64-bit host
// we read those as native pointer width (uintptr_t) so handles are not
// truncated; integer/float fields keep their original 32-bit width.
inline f32&  ObjF32(u8* base, int off) { return *reinterpret_cast<f32*>(base + off); }
inline u32&  ObjU32(u8* base, int off) { return *reinterpret_cast<u32*>(base + off); }
inline i8&   ObjI8 (u8* base, int off) { return *reinterpret_cast<i8*>(base + off); }
inline u8&   ObjU8 (u8* base, int off) { return *reinterpret_cast<u8*>(base + off); }
// Pointer field at an original dword offset that need not be pointer-aligned on
// a 64-bit host (the original is 32-bit). Read alignment-safe via memcpy.
inline uintptr_t ObjPtr(u8* base, int off) {
    uintptr_t p;
    std::memcpy(&p, base + off, sizeof(p));
    return p;
}

} // namespace

TransformConstraintHooks ApplyTransformConstraints_DefaultHooks() {
    TransformConstraintHooks h;
    h.computeBoneWorldMatrix = computeBoneWorldMatrix_inert;
    h.vectorWithinTolerance  = VectorWithinTolerance_impl;
    h.vectorAngleWrapped     = vectorAngleWrapped_inert;
    h.matrixFromEuler        = matrixFromEuler_inert;
    h.matrixMul              = matrixMul_inert;
    h.matrixToEuler          = matrixToEuler_inert;
    return h;
}

BoneExtentHooks ComputeBoneScreenExtents_DefaultHooks() {
    BoneExtentHooks h;
    h.project = project_inert;
    return h;
}

// =============================================================================
// 0x5e89b4 — VIBE_Object_ApplyTransformConstraints
//
// Local-name correspondence to the Hex-Rays decompile:
//   v47 = objBase (a1)            v46 = dpos (a2)
//   v45 = basis (a3)              v44 = dworld (a4)
//   a5  = outPos                 a6  = outWorld
//   v41[4] = dword_5E5A80 = {0,0,0,0} (zero vector)
//   v49 = dpos differs from zero (within 0.001)  -> position change requested
//   v48 = dworld differs from zero AND obj+533 != 0 -> world change requested
//   v50 = position-changed result bit; v51 = world-changed result bit
//   v25..v33 = the 3x3 rotation of the resolved frame matrix (MatrixCopy dest),
//              laid out as columns:
//                col0 = (v25,v26,v27)  col1 = (v28,v29,v30)  col2 = (v31,v32,v33)
//   flt_5CA2B0 = {0,0,1} (Z basis);  flt_62BF0C = 0.1f.
// =============================================================================
u8 ApplyTransformConstraints(u8* objBase, const f32* dpos, f32* basis,
                             const f32* dworld, f32* outPos, f32* outWorld,
                             const TransformConstraintEnv& env,
                             const TransformConstraintHooks& h) {
    u8* v47   = objBase;
    const f32* v46 = dpos;
    const f32* v44 = dworld;
    f32* v45  = basis;
    f32* a5   = outPos;
    f32* a6   = outWorld;

    const f32 zero4[4] = {0.0f, 0.0f, 0.0f, 0.0f};        // dword_5E5A80
    const f32 zbasis[3] = {0.0f, 0.0f, 1.0f};             // flt_5CA2B0/4/8
    const f32 kEps = 0.1f;                                // flt_62BF0C

    bool v49 = !h.vectorWithinTolerance(v46, zero4, 0.001f);
    char v50 = 0;
    bool v6 = (!h.vectorWithinTolerance(v44, zero4, 0.001f)) && (ObjU8(v47, 533) != 0);
    bool v48 = v6;
    char v51 = 0;

    f32 v23[16];
    f32 v24[16];
    f32 v34[16];

    // Resolve the basis matrix into v25..v33 (the 3x3 used below).
    f32 v25, v26, v27, v28, v29, v30, v31, v32, v33;
    {
        f32* src;
        if (ObjU32(v47, 504)) {
            f32* localFrame = (ObjI8(v47, 528) >= 0) ? v45 : nullptr;
            h.computeBoneWorldMatrix(ObjU32(v47, 504), localFrame, v23);
            src = v23;
        } else {
            src = v45 + 99;
        }
        // VIBE_Math_MatrixCopy(src, &v25): copy the 3x3 rotation columns.
        v25 = src[0]; v26 = src[1]; v27 = src[2];
        v28 = src[3]; v29 = src[4]; v30 = src[5];
        v31 = src[6]; v32 = src[7]; v33 = src[8];
    }

    if (ObjU8(v47, 533) == 3) {
        if (v48) {
            a6[0] = ObjF32(v47, 132) + v44[0];
            a6[1] = ObjF32(v47, 136) + v44[1];
            a6[2] = ObjF32(v47, 140) + v44[2];
            v51 = 1;
        }
        if (v49) {
            f32 v38, v39, v40, v35, v36, v37;
            if (env.d649EFC != (u32)(uintptr_t)v47 || env.b64A024) {
                double v17 = (double)v46[0] * v26 + (double)v46[1] * v29 + (double)v46[2] * v32;
                double v18 = (double)v46[0] * v27 + (double)v46[1] * v30 + (double)v46[2] * v33;
                v38 = (f32)((double)v46[0] * v25 + (double)v46[1] * v28 + (double)v46[2] * v31);
                v39 = (f32)v17;
                v40 = (f32)v18;
                a5[0] = ObjF32(v47, 76) + v38;
                a5[1] = ObjF32(v47, 80) + v39;
                a5[2] = ObjF32(v47, 84) + v40;
            } else {
                v38 = (f32)((double)zbasis[0] * v25 + (double)zbasis[1] * v28 + (double)zbasis[2] * v31);
                v40 = (f32)((double)zbasis[0] * v27 + (double)zbasis[1] * v30 + (double)zbasis[2] * v33);
                v39 = 0.0f;
                if (std::sqrt((double)v38 * v38 + 0.0 * 0.0 + (double)v40 * v40) >= (double)kEps) {
                    double v14 = h.vectorAngleWrapped(zbasis, &v38);
                    v35 = v46[0];
                    v36 = 0.0f;
                    double v15 = -v14;
                    v37 = v46[1];
                    double v16 = std::sin(v15);
                    double v42 = std::cos(v15);
                    double v43 = v42;
                    v39 = 0.0f;
                    v38 = (f32)((double)v37 * v16 + (double)v35 * v42);
                    v40 = (f32)((double)v16 * -(double)v35 + (double)v37 * v43);
                } else {
                    v35 = v46[0];
                    v36 = v46[1];
                    v37 = 0.0f;
                    v38 = (f32)((double)v35 * v25 + (double)v36 * v28 + 0.0 * v31);
                    v40 = (f32)((double)v35 * v27 + (double)v36 * v30 + 0.0 * v33);
                    v39 = 0.0f;
                }
                a5[0] = ObjF32(v47, 76) + v38;
                a5[1] = ObjF32(v47, 80) + v39;
                a5[2] = ObjF32(v47, 84) + v40;
                v35 = 0.0f;
                v36 = v46[2];
                v37 = 0.0f;
                double v11 = a5[2];
                a5[1] = a5[1] + v36;
                a5[2] = (f32)(v11 + v37);
            }
            v50 = 1;
        }
    } else {
        if (v49) {
            double v19 = (double)v46[0] * v26 + (double)v46[1] * v29 + (double)v46[2] * v32;
            double v20 = (double)v46[0] * v27 + (double)v46[1] * v30 + (double)v46[2] * v33;
            f32 v38 = (f32)((double)v46[0] * v25 + (double)v46[1] * v28 + (double)v46[2] * v31);
            f32 v39 = (f32)v19;
            f32 v40 = (f32)v20;
            a5[0] = ObjF32(v47, 76) + v38;
            a5[1] = ObjF32(v47, 80) + v39;
            a5[2] = ObjF32(v47, 84) + v40;
            v50 = 1;
        }
        if (v48) {
            f32* v22 = (ObjI8(v47, 528) >= 0) ? v45 : nullptr;
            h.computeBoneWorldMatrix((u32)(uintptr_t)v47, v22, v23);
            h.matrixFromEuler(v44, v24);
            h.matrixMul(v23, v24, v34);
            if (ObjI8(v47, 528) < 0) {
                h.matrixToEuler(v34);
            } else {
                // The original reuses &v25 as a contiguous 16-float matrix here.
                f32 tmp[16];
                tmp[0] = v25; tmp[1] = v26; tmp[2] = v27;
                tmp[3] = v28; tmp[4] = v29; tmp[5] = v30;
                tmp[6] = v31; tmp[7] = v32; tmp[8] = v33;
                for (int i = 9; i < 16; ++i) tmp[i] = 0.0f;
                h.matrixMul(v34, tmp, v23);
                h.matrixToEuler(v23);
            }
            v51 = 1;
        }
    }

    return (u8)(v50 | (2 * v51));
}

// =============================================================================
// 0x5b6ebc — VIBE_Object_ComputeBoneScreenExtents
//
// Walks the posed object's face/bone mesh and keeps four extreme screen-space
// corner points into the caller's extent record `ext` (a2). Indices into `ext`
// mirror the original a2[..] dword indices exactly:
//   ext[33] (a2+132) byte set to 1 when at least one matching face found
//   ext[25],[26] = best "min sum (x+y)" corner X/Y; ext[17],[18] uv; ext[1..3] pos
//   ext[27],[28] = best "max-x / min-y" corner;     ext[19],[20] uv; ext[5..7]
//   ext[29],[30] = best "min-x / max-y" corner;     ext[21],[22] uv; ext[9..11]
//   ext[31],[32] = best far corner;                 ext[23],[24] uv; ext[13..15]
// Geometry walked from objBase+460 (pose): {+4 face base, +12 face count,
//   +16 vertex-id base, +20 face-list}. Pose-node match on face's bone id at
//   (face+20) against ext[0]; flags at face+36 (sign) and face+38 (&2 clear).
// =============================================================================
u8 ComputeBoneScreenExtents(u8* objBase, i32* a2,
                            const BoneExtentEnv& env,
                            const BoneExtentHooks& h) {
    u8* a1 = objBase;
    const BonePose* pose = reinterpret_cast<const BonePose*>(ObjPtr(a1, 460));
    if (!pose) return 1;

    // Locate ext[0] (bone id) among the pose's vertex-id list (must be < count).
    // Original: v6 = *(idBlock+480); scan idList while idList[k] != ext[0].
    int v18 = a2[0];
    int v6 = pose->idBlock[480 / 4];
    const i32* v5 = pose->idList;
    int i;
    for (i = 0; i < v6; ++v5) {
        if (v18 == *v5) break;
        ++i;
    }
    if (i >= v6) return 1;

    int v17 = pose->facesCount;
    const BoneFace* face = pose->facesBase;
    int v20 = 0;
    if (v17 <= 0) return 1;

    do {
        if (face->flagSign < 0 && (face->flag2 & 2) == 0 && v18 == face->boneId) {
            // Original: walk the inline vertex-pointer array face[0..+11], i.e.
            // (12 bytes / 4-byte dword) == 3 entries, stepping uv index v11 by 8.
            const int kVertCount = 12 / 4;   // 3
            *reinterpret_cast<u8*>(a2 + 33) = 1;   // a2[..]+132 byte
            int v11 = 0;
            const i32* faceUv = face->uvBase;     // face+16
            for (int vi = 0; vi < kVertCount; ++vi) {
                const BoneVertex* vert = face->verts[vi];
                int v12 = a2[26];
                f32 v13f = vert->f[5];
                f32 v14f = vert->f[4];
                i32 px, py;
                // VIBE_Coord_ConvertX twice: X from v14, Y from v13.
                h.project(v14f, v13f, &px, &py);
                int v22 = px;   // (int)v14 in original
                int v21 = py;   // (int)v13 in original
                // uv pair at byte offset v11 from uvBase -> uvBase[v11/4 + 0/1].
                const i32* uvp = reinterpret_cast<const i32*>(
                    reinterpret_cast<const u8*>(faceUv) + v11);
                const i32* posp = reinterpret_cast<const i32*>(vert);  // (*v15)[0..2]

                if (v21 + v22 < v12 + a2[25]) {
                    a2[26] = v21;
                    a2[25] = v22;
                    a2[17] = uvp[0];
                    a2[18] = uvp[1];
                    a2[1] = posp[0];
                    a2[2] = posp[1];
                    a2[3] = posp[2];
                }
                if (v22 + env.screenH - 1 - v21 < a2[28] + env.screenH - 1 - a2[27]) {
                    a2[28] = v22;
                    a2[27] = v21;
                    a2[19] = uvp[0];
                    a2[20] = uvp[1];
                    a2[5] = posp[0];
                    a2[6] = posp[1];
                    a2[7] = posp[2];
                }
                if (v21 + env.screenW - 1 - v22 < a2[29] + env.screenW - 1 - a2[30]) {
                    a2[29] = v21;
                    a2[30] = v22;
                    a2[21] = uvp[0];
                    a2[22] = uvp[1];
                    a2[9]  = posp[0];
                    a2[10] = posp[1];
                    a2[11] = posp[2];
                }
                if (env.screenH - 1 - v21 + env.screenW - 1 - v22
                        < env.screenW - 1 - a2[32] + env.screenH - 1 - a2[31]) {
                    a2[32] = v22;
                    a2[31] = v21;
                    a2[23] = uvp[0];
                    a2[24] = uvp[1];
                    a2[13] = posp[0];
                    a2[14] = posp[1];
                    a2[15] = posp[2];
                }
                v11 += 8;
            }
        }
        ++face;
        ++v20;
    } while (v20 < v17);

    return 1;
}

} // namespace guild::sim
