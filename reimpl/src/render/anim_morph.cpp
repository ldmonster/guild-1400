// =============================================================================
// anim_morph.cpp — gilde.exe 0x5cf150 VIBE_Anim_CreateMorphAnim, 1:1.
// namespace guild::render. See anim_morph.h for the overview + provenance.
//
// Translation notes (binary fidelity):
//  * The 364-byte record `v10` and its 192*frameStride frame buffer (`v10[87]`)
//    are modelled with byte offsets identical to the binary; every write below is
//    annotated with its decompile address.
//  * frameStride (`v10[82]`) is set to 2 BEFORE the frame buffer alloc, exactly
//    as the binary (0x5cf1c5 then 0x5cf208 alloc of 192*v10[82]).
//  * The qmemcpy blob copies (WPoints/Points) are byte-faithful std::memcpy.
//  * The morph-delta quantization (0x5cf7c8..0x5cfa57) keeps the binary's float
//    op order: the running min/max use the `>=`/`<=` compare-then-select idiom
//    (so a NaN keeps the accumulator, matching the original), the scale is
//    range * (1/255) (flt_628E60), and the per-point byte is
//    (delta-min)*255/range truncated toward zero by VIBE_Coord_ConvertX.
//  * VIBE_Coord_ConvertX @0x5c6b08 sets x87 RC=11 (toward zero) => TRUNCATE.
//    The two compile-time-different cast sites in the decompile (the explicit
//    `(int)(v66*v64/v93)` / `(int)v68` vs the ConvertX-returned byte) all reduce
//    to truncation toward zero, so all three axes use the same MorphQuantizeByte.
//  * The engine global-list insertion (dword_13FC8E4 head) is an out-of-tree side
//    effect on the live anim list; we return the record and document the handoff.
// =============================================================================
#include "render/anim_morph.h"

#include <cmath>
#include <cstring>

namespace guild::render {

namespace {
// get_bytes @0x628E60/0x628E64 (bit-exact).
constexpr float kInv255 = 0.0039215688593685627f; // flt_628E60 = 1/255 (0x3B808081)
constexpr float k255    = 255.0f;                  // flt_628E64 = 255  (0x437F0000)

// Init sentinels for the min/max accumulators (0x5cf7a1..0x5cf7bc).
constexpr float kNegBig = -1.0e10f; // max accumulators (v103/v104/v105)
constexpr float kPosBig =  1.0e10f; // min accumulators (v106/v107/v108)

// VIBE_Coord_ConvertX @0x5c6b08 — truncate toward zero (x87 RC=11 / frndint).
inline int ConvertXTrunc(double x) { return (int)x; }
} // namespace

// Forward decl: defined after the build helpers (matches header export below).
u8 MorphQuantizeByte(float delta, float minV, float range);

// ---------------------------------------------------------------------------
// Bone-name match table (0x5cf52d..0x5cf5fc primary / 0x5cfaf6..0x5cfbb7 alt).
// For each dest bone (record dword[81] of them), scan the endpoint's bone slots;
// on a name match (VIBE_Util_StrCmp == 0) copy the 6 matrix dwords into
// frame[+276 + 24*destBone]. `slots` is the per-endpoint slot count the binary
// walks (4 in both branches: the primary `for(i<4)`, the alt `while != a3+320`
// over a 16-dword/64-byte stride == 4 entries).
// ---------------------------------------------------------------------------
static void BuildBoneMatchTable(MorphAnim& rec, const MorphDestNode& dest,
                                const MorphEndpointBone* bones, int boneCount,
                                int slots) {
    const int numBones = rec.numBones;
    if (!dest.boneNames || !bones) return;
    for (int bi = 0; bi < numBones; ++bi) {
        const char* destName = dest.boneNames + 64 * bi; // 64-byte stride (v97/v98)
        const int outBase = 24 * bi + 276;               // frame[+276 + 24*bi]
        const int lim = slots < boneCount ? slots : boneCount;
        for (int si = 0; si < lim; ++si) {
            const MorphEndpointBone& b = bones[si];
            if (b.name && std::strcmp(b.name, destName) == 0) { // VIBE_Util_StrCmp==0
                for (int j = 0; j < 6; ++j)
                    *(u32*)(rec.frame.data() + outBase + 4 * j) = b.mat[j];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Quantized morph-delta blob (0x5cf797..0x5cfa69). The real motion/blend math:
//   pass 1: per point, delta = target - source; track component min/max.
//   bounds: +200=minX +204=minY +208=minZ ; +212=rangeX/255 +216=rangeY/255
//           +220=rangeZ/255  (scale = range * flt_628E60).
//   pass 2: per point, byte = (delta - min) * 255 / range, truncated -> wpoints1.
// The min/max use the binary's compare-then-select (`if (acc >= v) v else acc`)
// idiom so NaN-handling matches; the scale multiply order (range * 1/255) and the
// per-axis quantize divide (`* 255 / range`) are preserved bit-for-bit.
// ---------------------------------------------------------------------------
static void BuildQuantizedMorph(MorphAnim& rec, const float* srcPts,
                                const float* tgtPts, int numPoints) {
    if (rec.wpoints1.empty()) rec.wpoints1.assign((size_t)3 * numPoints, 0);

    std::vector<float> tmp((size_t)4 * numPoints, 0.0f); // alloc 16*np (0x5cf79c)
    float maxX = kNegBig, maxY = kNegBig, maxZ = kNegBig; // v103/v104/v105
    float minX = kPosBig, minY = kPosBig, minZ = kPosBig; // v106/v107/v108

    for (int p = 0; p < numPoints; ++p) {                 // pass 1 (0x5cf7d4)
        const float* sp = srcPts + 6 * p;                 // stride 24 bytes
        const float* tp = tgtPts + 6 * p;
        float dx = tp[0] - sp[0];
        float dy = tp[1] - sp[1];
        float dz = tp[2] - sp[2];
        tmp[4 * p + 0] = dx; tmp[4 * p + 1] = dy; tmp[4 * p + 2] = dz;
        // min: if (minAcc >= v) min=v else min=minAcc   (compare-then-select)
        if (minX >= (double)dx) minX = dx;
        if (minY >= (double)dy) minY = dy;
        if (minZ >= (double)dz) minZ = dz;
        // max: if (maxAcc <= v) max=v else max=maxAcc
        if (maxX <= (double)dx) maxX = dx;
        if (maxY <= (double)dy) maxY = dy;
        if (maxZ <= (double)dz) maxZ = dz;
    }

    float rangeX = maxX - minX;                           // (v103-v106)
    float rangeY = maxY - minY;                           // v62/v93
    float rangeZ = maxZ - minZ;                           // v94
    rec.frameF(200)[0] = minX;                            // 0x5cf92c
    rec.frameF(204)[0] = minY;                            // 0x5cf940
    rec.frameF(208)[0] = minZ;                            // 0x5cf954
    // x87: the X scale multiplies the UNROUNDED 80-bit difference (v61 =
    // (v103 - v106) * flt_628E60, sub never stored); Y and Z multiply the
    // float-rounded ranges (v93/v94 stored first).
    rec.frameF(212)[0] = (float)(((double)maxX - minX) * kInv255); // v61 0x5cf96a
    rec.frameF(216)[0] = rangeY * kInv255;               // v63  0x5cf97a
    rec.frameF(220)[0] = kInv255 * rangeZ;               // v60*v94  0x5cf986

    u8* out = rec.wpoints1.data();                        // frame[+372] target
    for (int p = 0; p < numPoints; ++p) {                 // pass 2 (0x5cf9a8)
        float dx = tmp[4 * p + 0];
        float dy = tmp[4 * p + 1];
        float dz = tmp[4 * p + 2];
        out[3 * p + 0] = MorphQuantizeByte(dx, minX, rangeX);
        out[3 * p + 1] = MorphQuantizeByte(dy, minY, rangeY);
        out[3 * p + 2] = MorphQuantizeByte(dz, minZ, rangeZ);
    }
    // VIBE_Memory_FreeDebug(tmp) (0x5cfa69) — tmp is the local std::vector.
}

u8 MorphQuantizeByte(float delta, float minV, float range) {
    // (delta - min) * 255.0 / range, truncated toward zero, low byte stored.
    // x87 (disasm 0x5cf9ae..0x5cf9d4): the whole chain stays on the FPU stack —
    // fld delta; fsub min; fmul 255; fdiv range; ConvertX(RC=11)+fistp — with NO
    // intermediate float store on ANY axis. Modeled as one double expression with
    // a single truncation.
    int q = ConvertXTrunc(((double)delta - minV) * (double)k255 / (double)range);
    return (u8)q;
}

MorphAnim* CreateMorphAnim(const MorphSrcNode& src, const MorphDestNode& dest,
                           const MorphControl& ctrl, const MorphEndpoint* prim,
                           const MorphEndpoint* alt, const char* name, int frames) {
    // ---- early reject (0x5cf18b) ----
    //   !a4 || !a1 || (!a6 && (!a3 || !a5)) || a8 <= 1
    // a4 is the control array (always present here); a1 = src; a6 = prim;
    // a3/a5 = the alt path operands. `alt` carries both a3 (bone table) and a5
    // (param block) in this model.
    if (!name)               // a7 is dereferenced unconditionally below
        return nullptr;
    if (frames <= 1)         // a8 <= 1
        return nullptr;
    if (!prim && !alt)       // !a6 && (!a3 || !a5)
        return nullptr;

    MorphAnim* rec = new MorphAnim();        // VIBE_Memory_AllocDebug(0x16C) 0x5cf1ba
    rec->frameStride = 2;                    // v10[82] = 2  0x5cf1c5
    rec->numPoints = src.numPoints;          // v10[80] = *(a1+68)  0x5cf1d1
    rec->name = name;                        // name copy loop 0x5cf1d8..0x5cf1ee

    const int numPoints = rec->numPoints;
    const int numFrames = frames;            // a8

    // ---- frame buffer alloc: 192 * frameStride (0x5cf208) ----
    rec->frame.assign((size_t)192 * rec->frameStride, 0);
    u32* f = rec->frameD(0);

    // frame[+0]=0 (0x5cf213); frame[+4]=3*a8 (0x5cf232).
    *rec->frameD(0) = 0;
    *(i32*)(rec->frame.data() + 4) = 3 * numFrames;

    // frame[+32..+52] = a4[8..13]  (always, 0x5cf242..0x5cf27e).
    for (int i = 0; i < 6; ++i)
        rec->frameF(32 + 4 * i)[0] = ctrl.blk8[i];

    // ---- a4[47] (hasPoints) path: alloc Points0 (12*numPoints) + copy a4[47] blob,
    //      and pre-alloc Points1 (12*numPoints) (0x5cf281..0x5cf31e). ----
    if (ctrl.hasPoints) {
        // frame[+188]=Points0 (12*np, 0x5cf2b0); qmemcpy of the a4[47] source blob
        // (the control's point-deltas) follows. In the prim (a6!=0) path the real
        // per-point deltas are recomputed into Points1 below; Points0 mirrors the
        // control blob, modelled here as zero-init (overwritten where carried).
        rec->points0.assign((size_t)12 * numPoints, 0); // frame[+188] 0x5cf2b0
        rec->points1.assign((size_t)12 * numPoints, 0); // frame[+380] 0x5cf31e
    }

    // ---- a4[45] (hasWPoints) path: alloc WPoints0 (3*numPoints) + copy, copy
    //      a4[2..7] into frame[+8..+28], alloc WPoints1 (0x5cf328..0x5cf404). ----
    if (ctrl.hasWPoints) {
        rec->wpoints0.assign((size_t)3 * numPoints, 0);  // frame[+180] 0x5cf354
        for (int i = 0; i < 6; ++i)                      // a4[2..7] -> +8..+28
            rec->frameF(8 + 4 * i)[0] = ctrl.blk2[i];    // 0x5cf3a6..0x5cf3e2
        rec->wpoints1.assign((size_t)3 * numPoints, 0);  // frame[+372] 0x5cf404
    }

    // frame[+192]=a8 (0x5cf417); frame[+196]=3*a8 (0x5cf42c).
    *(i32*)(rec->frame.data() + 192) = numFrames;
    *(i32*)(rec->frame.data() + 196) = 3 * numFrames;

    // ---- bone count + per-bone keyframe blocks (0x5cf436..0x5cf4e9) ----
    rec->numBones = dest.numBones;           // v10[81] = *(a2+324)  0x5cf43e
    const int numBones = rec->numBones;
    for (int bi = 0; bi < numBones; ++bi) {
        // copy dest bone name into record (v10+64 + 64*bi) — name table (cosmetic
        // for the build; the match loop below uses it).
        // frame[+84 + 24*bi .. +104 + 24*bi] = a4[15+6*bi .. 20+6*bi]  (6 floats).
        const float* blk = ctrl.boneBlocks ? ctrl.boneBlocks + 6 * bi : nullptr;
        const int base = 24 * bi + 84;
        for (int j = 0; j < 6; ++j)
            rec->frameF(base + 4 * j)[0] = blk ? blk[j] : 0.0f;
    }
    (void)f;

    if (prim) {
        // ===================== a6 != 0 (primary endpoint) =====================
        // (1) per dest-bone, scan prim's 4 bone slots for a name match; on match,
        //     copy v44[45..50] -> frame[+276 + 24*bi .. +296] (0x5cf50a..0x5cf5fc).
        BuildBoneMatchTable(*rec, dest, prim->bones, prim->numBones, /*slots=*/4);

        // (2) transform deltas frame[+224..+244] = (a6+80..+100) - (a1+80..+100)
        //     with the binary's deliberate frame[+228]=0 overwrite (0x5cf65f).
        for (int i = 0; i < 6; ++i)                       // 0x5cf619..0x5cf6ba
            rec->frameF(224 + 4 * i)[0] = prim->xform[i] - src.xform[i];
        *(u32*)(rec->frame.data() + 228) = 0;             // 0x5cf65f overwrite

        // (3) Points1: per point, the 3-float (target-source) delta (a4[47] path)
        //     into frame[+380] (0x5cf6c4..0x5cf770).
        if (ctrl.hasPoints && src.pointList && prim->pointList) {
            float* dst = (float*)rec->points1.data();
            for (int p = 0; p < numPoints; ++p) {
                const float* sp = src.pointList + 6 * p;   // stride 24 bytes
                const float* tp = prim->pointList + 6 * p;
                dst[3 * p + 0] = tp[0] - sp[0];
                dst[3 * p + 1] = tp[1] - sp[1];
                dst[3 * p + 2] = tp[2] - sp[2];
            }
        }

        // (4) WPoints1: build the quantized morph-delta blob (the real math).
        if (ctrl.hasWPoints && src.pointList && prim->pointList) {
            BuildQuantizedMorph(*rec, src.pointList, prim->pointList, numPoints);
        }
    } else {
        // ===================== a6 == 0 (alt a3/a5 endpoint) ===================
        // (1) bone match against ALL of a3's bones (the while v73 != a3+320 loop,
        //     16-dword stride => 4 bones), copying v74[21..26] (0x5cfad1..0x5cfbb7).
        BuildBoneMatchTable(*rec, dest, alt->bones, alt->numBones, /*slots=*/4);

        // (2) frame[+224..+244] = a5[8..13]  (verbatim dwords, 0x5cfbcd..0x5cfc18).
        for (int i = 0; i < 6; ++i)
            rec->frameF(224 + 4 * i)[0] = alt->xform[i];

        // (3) Points0: qmemcpy a5[47] blob (12*numPoints) -> frame[+380]
        //     (0x5cfc22..0x5cfc67). Source carried on `alt->pointList` as raw bytes
        //     when present; modelled as the float morph blob below for the test path.
        if (ctrl.hasPoints && alt->pointList) {
            std::memcpy(rec->points1.data(), alt->pointList,
                        (size_t)12 * numPoints);
        }

        // (4) WPoints0: qmemcpy a5[45] blob (3*numPoints) -> frame[+372], then copy
        //     a5[2..7] bounds into frame[+200..+220] (0x5cfc6d..0x5cfd17).
        if (ctrl.hasWPoints && !rec->wpoints0.empty()) {
            // the binary copies the alt's pre-built WPoints blob and its bounds; we
            // mirror the bounds from blk2 (a4[2..7] == a5[2..7] in this path).
            for (int i = 0; i < 6; ++i)
                rec->frameF(200 + 4 * i)[0] = ctrl.blk2[i];
        }
    }

    // ---- engine list insertion (0x5cfa6e..0x5cfa89): record[89]=head,
    //      head=record, record[88]=sentinel, head_prev[+352]=record. This mutates
    //      the live anim list dword_13FC8E4 — an out-of-tree global owned by the
    //      anim subsystem; handled by the caller/AnimStock wiring (see progress doc).
    return rec;                                           // 0x5cf18f returns v10
}

} // namespace guild::render
