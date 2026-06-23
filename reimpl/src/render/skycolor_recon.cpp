// Sky/time-of-day band-colour reconstruction for Die Gilde (gilde.exe).
// Translated 1:1 from the Hex-Rays decompile (reference of record).  See
// skycolor_recon.h for the cluster map and the modelling of coupled leaves.

#include "render/skycolor_recon.h"

#include <cmath>
#include <cstring>

namespace guild {
namespace render {

namespace {

// gilde.exe constants (exact IEEE-754 bytes recovered with get_bytes):
constexpr double kMinusOneD = -1.0;       // dbl_628718 = 0xBFF0000000000000
constexpr float  kMinusOneF = -1.0f;      // flt_628720 = 0xBF800000
constexpr float  kHalf      = 0.5f;       // flt_628724 = 0x3F000000

// Truncate toward zero (frndint with rounding mode forced to chop, i.e.
// VIBE_Coord_ConvertX 0x5c6b08).  fistp then yields the truncated integer.
inline i64 TruncTowardZero(double v) {
    return static_cast<i64>(v >= 0.0 ? std::floor(v) : std::ceil(v));
}

} // namespace

// gilde.exe 0x5b80ac — VIBE_SkyColor_InterpolateBand (__userpurge, al)
char SkyColor_InterpolateBand(SkyObject* obj, unsigned band, float frac,
                              SkyColorHooks& hooks) {
    unsigned v4 = band;                                   // /*0x5b80b5*/
    if (!obj)                          return 0;          // /*0x5b80b9*/
    if (band >= 7)                     return 0;          // /*0x5b80c2*/
    if (frac < 0.0f)                   return 0;          // /*0x5b80d1*/
    // SLODWORD(a3) > 1065353216  <=>  frac (as signed bit-pattern) > 1.0f.
    // Since the frac<0 case is already rejected, this is just frac > 1.0f.
    if (frac > 1.0f)                   return 0;          // /*0x5b80df*/
    if (obj->kind < 5)                 return 0;          // /*0x5b80ec*/
    SkyBandRecord* v5 = obj->bands;                       // *(a1+488) /*0x5b80f2*/
    if (!v5)                           return 0;          // /*0x5b838d*/

    // v21 = frac + band-phase  (original: *(float*)(v5+420))         /*0x5b810a*/
    float v21 = frac + hooks.ReadBandPhase();
    if (v21 > 1.0f) {                                     // /*0x5b8116*/
        v4 = (band + 1) % 7;                              // /*0x5b82ec*/
        v21 = v21 + kMinusOneF;                           // /*0x5b82f2*/
    } else if (v21 < static_cast<float>(kMinusOneD)) {    // !(v21 >= -1.0) /*0x5b8129*/
        v4 = (band - 1) % 7;                              // /*0x5b8135*/
        v21 = v21 + 1.0f;                                 // /*0x5b8139*/
    }
    // LABEL_11
    unsigned v8 = (v4 + 1) % 7;                           // /*0x5b8143*/

    auto band_at = [&](unsigned idx) -> float* { return v5[idx].f; };

    // Light presence for prev/cur/next bands (field +36 == f[9]).
    const bool kind8 = (obj->kind == 8);
    // (*(_DWORD*)(v5+56*((v4-1)%7)+36) & 0x7FFFFFFF) != 0  ||  kind==8
    auto has_mag = [&](unsigned idx) -> bool {
        u32 bits;
        std::memcpy(&bits, &band_at(idx)[9], 4);
        return (bits & 0x7FFFFFFFu) != 0;
    };
    bool v20 = has_mag((v4 - 1) % 7) || kind8;            // /*0x5b82fd*/
    bool v9  = has_mag(v4)           || kind8;            // /*0x5b830a*/
    bool v10 = has_mag(v8)           || kind8;            // /*0x5b8314*/

    float* cur  = band_at(v4);
    float* next = band_at(v8);

    if (v20 && v9 && v10) {                               // /*0x5b8200*/
        // obj+148 = (next[9]-cur[9])*v21 + cur[9]
        obj->out_148 = (next[9] - cur[9]) * v21 + cur[9]; // /*0x5b8234*/
    } else if (v9 && v21 < kHalf) {                       // /*0x5b832f*/
        obj->out_148 = cur[9];                            // /*0x5b8343*/
    } else if (v10 && v21 >= kHalf) {                     // /*0x5b835f*/
        obj->out_148 = next[9];                           // /*0x5b8373*/
    } else {
        // *(_DWORD*)(a1+148) = 0  (write the integer 0, i.e. +0.0f)
        obj->out_148 = 0.0f;                              // /*0x5b837e*/
    }

    // obj+144 = (next[10]-cur[10])*v21 + cur[10]                     /*0x5b8268*/
    obj->out_144 = (next[10] - cur[10]) * v21 + cur[10];
    // obj+152 = v21*(next[11]-cur[11]) + cur[11]                     /*0x5b827d*/
    obj->out_152 = v21 * (next[11] - cur[11]) + cur[11];

    // The do/while interpolates the orientation triple (band[0..2]) into v19,
    // the translation triple (band[3..5]) into v18[1..3], and writes obj+92
    // from band[6] on the first iteration's sibling field.  v15 = v21.
    float v19[3];                 // euler triple -> SetPosition
    float v18[4];                 // [1..3] translation triple -> SetWorldTranslation
    float* v11 = next;            // float* into next band                /*0x5b8249*/
    float* v12 = cur;             // float* into cur  band                /*0x5b8255*/
    int    v13 = 0;               // /*0x5b8276*/
    // v14 starts at obj and advances 4 bytes (one float) per iteration, so the
    // loop writes obj+92, obj+96, obj+100 (== out_92[0..2]).
    int    v14 = 0;               // index into out_92[]                   /*0x5b827b*/
    const float v15 = v21;        // /*0x5b8283*/
    do {                                                  // /*0x5b82be*/
        // v19[v13] = (v11[0]-v12[0])*v15 + v12[0]                    /*0x5b8290*/
        v19[v13] = (v11[0] - v12[0]) * v15 + v12[0];
        // (v14+92) = (v11[6]-v12[6])*v15 + v12[6]                    /*0x5b829f*/
        obj->out_92[v14] = (v11[6] - v12[6]) * v15 + v12[6];
        // v16 = (v11[3]-v12[3])*v15                                  /*0x5b82a8*/
        float v16 = (v11[3] - v12[3]) * v15;
        ++v12;                                            // /*0x5b82aa*/
        ++v14;                                            // v14 += 4 bytes /*0x5b82ad*/
        ++v13;                                            // /*0x5b82b0*/
        ++v11;                                            // /*0x5b82b4*/
        // v18[v13] = v16 + v12[2]  (v12 already advanced => original cur[old+3])
        v18[v13] = v16 + v12[2];                          // /*0x5b82b7*/
    } while (v13 < 3);                                    // /*0x5b82be*/

    hooks.SetPosition(obj, v19);                          // /*0x5b82c8*/
    hooks.SetWorldTranslation(obj, &v18[1]);              // /*0x5b82d1*/
    return 1;                                             // /*0x5b82d8*/
}

// gilde.exe 0x5b83b0 — VIBE_SkyColor_SetTimeOfDay (__fastcall)
char SkyColor_SetTimeOfDay(SkyObject* obj, const float* time_of_day,
                           SkyColorHooks& hooks) {
    float v7 = *time_of_day;                              // /*0x5b83b6*/
    double v2 = *time_of_day;                             // /*0x5b83bc*/
    // VIBE_Coord_ConvertX(): chop mode; `fistp [var_10]` writes ONLY the low
    // dword as a signed-32 truncation; `HIDWORD(v6) = ecx = 0` zero-extends. So
    // v6 is a 64-bit value whose low half is the (signed) truncated int and whose
    // high half is 0 — i.e. the UNSIGNED 32-bit truncation widened to 64 bits.
    u32 trunc32 = static_cast<u32>(static_cast<i32>(TruncTowardZero(v2))); // /*0x5b83c5*/
    i64 v6 = static_cast<i64>(static_cast<u64>(trunc32));                  // HIDWORD=0 /*0x5b83cb..cf*/
    // v8 = v7 - (double)v6 (fild reads the full 64-bit, so negative inputs widen
    // to a huge positive — but those are rejected by InterpolateBand band>=7).
    float v8 = v7 - static_cast<float>(static_cast<double>(v6)); // /*0x5b83d9*/
    // InterpolateBand(eax=obj, edx=v6_low, a3=v8): band index = the low 32 bits.
    SkyColor_InterpolateBand(obj, trunc32, v8, hooks);   // /*0x5b83e1*/
    return 1;                                             // /*0x5b83ec*/
}

// gilde.exe 0x5b83f0 — VIBE_SkyColor_StoreBandColors (__usercall, al)
char SkyColor_StoreBandColors(SkyStoreSource* src, unsigned band) {
    if (band > 6 || !src->bands)       return 1;          // /*0x5b83f9*/
    // 56*band byte offset; the destination dwords are at byte offsets
    // {0,4,8,12,16,20,24,28,32,36,40,44} of the band record.
    float* dst = src->bands[band].f;                      // a1[122] + 56*band
    auto store = [&](int byte_off, u32 idx) {
        std::memcpy(reinterpret_cast<char*>(dst) + byte_off, &src->d[idx], 4);
    };
    store(40, 36);                                        // /*0x5b841d*/
    store(36, 37);                                        // /*0x5b842d*/
    store(44, 38);                                        // /*0x5b843d*/
    store(0,  19);                                        // /*0x5b844a*/
    store(4,  20);                                        // /*0x5b8456*/
    store(8,  21);                                        // /*0x5b8463*/
    store(24, 23);                                        // /*0x5b8470*/
    store(28, 24);                                        // /*0x5b847d*/
    store(32, 25);                                        // /*0x5b848a*/
    store(12, 33);                                        // /*0x5b849a*/
    store(16, 34);                                        // /*0x5b84aa*/
    store(20, 35);                                        // /*0x5b84ba*/
    return 1;                                             // /*0x5b8404*/
}

// gilde.exe 0x5b89e8 — VIBE_SkyColor_BandHasLight (__usercall, al)
char SkyColor_BandHasLight(const SkyObject* obj, int band, bool* out_has_light) {
    // HARDENING (wave-10): the original dereferences *(v3 + 56*band) with no
    // null/range check — the callers always pass a live object and an in-range
    // band (0..6, the sky has 7 bands). A null `bands` pointer or an out-of-range
    // `band` was never an in-bounds path in the binary, so guarding them is
    // faithful: it cannot change any value the original produced (it only avoids
    // an OOB read on inputs the original would have crashed on). Return the
    // LABEL_14 "no light" default (1), matching the function's catch-all exit.
    if (!obj || !obj->bands || (unsigned)band >= 7u)
        return 1;
    const float* rec = obj->bands[band].f;                // v3 + 56*band
    auto fbits = [&](int idx) -> u32 {
        u32 b; std::memcpy(&b, &rec[idx], 4); return b;
    };
    auto fval = [&](int idx) -> float { return rec[idx]; };

    switch (obj->kind) {                                  // *(_BYTE*)(a1+533) /*0x5b8a04*/
        case 5:
        case 6: {
            // (+24>0 || +28&7FFFFFFF || +32&7FFFFFFF) && (+36>0 || +40>0)
            const bool a = fval(6) > 0.0f                  // +24 == f[6]
                        || (fbits(7) & 0x7FFFFFFFu) != 0   // +28 == f[7]
                        || (fbits(8) & 0x7FFFFFFFu) != 0;  // +32 == f[8]
            const bool b = fval(9) > 0.0f                  // +36 == f[9]
                        || fval(10) > 0.0f;                // +40 == f[10]
            if (a && b) {
                if (out_has_light) *out_has_light = true;  // /*0x5b8a54*/
                return 0;                                  // /*0x5b8a52*/
            }
            return 1;                                      // /*0x5b8a6f*/
        }
        case 7: {
            // ( +24<=0 && +28&7FFFFFFF==0 && +32&7FFFFFFF==0 ) || +36<=0  -> no light
            const bool no_dir = fval(6) <= 0.0f
                             && (fbits(7) & 0x7FFFFFFFu) == 0
                             && (fbits(8) & 0x7FFFFFFFu) == 0;
            if (no_dir || fval(9) <= 0.0f)                 // /*0x5b8aaa*/
                return 1;                                  // LABEL_14
            if (out_has_light) *out_has_light = true;      // /*0x5b8aac*/
            return 0;                                      // /*0x5b8ab3*/
        }
        default:
            return 1;                                      // LABEL_14 /*0x5b8aca*/
    }
}

// gilde.exe 0x5b84c4 — VIBE_SkyColor_CopyGradientEntry (__usercall, eax)
float* SkyColor_CopyGradientEntry(SkyGradient* g, int band) {
    // HARDENING (wave-10): `band` indexes the 7-entry gradient table and
    // `pending_index` indexes the 6-entry scratch; both are in range on every
    // live path (band 0..6 from SetBandGradient's `band < 7` gate; pending_index
    // is the ApplyAmbientBlend band a1 in 0..5, or -1 disabled). Clamping the
    // out-of-range cases to the no-op / first slot only affects inputs the
    // original would have indexed OOB — it never changes an in-bounds result.
    if ((unsigned)band >= (unsigned)SkyGradient::kBands)
        return g->entry[0];                               // OOB band: no valid row
    if (g->pending_index >= 0 &&
        g->pending_index < 6) {                           // dword_649F08 in [0,6) /*0x5b84d3*/
        // v2 = 3*idx (triple stride 3 floats == 12 bytes):
        //   flt_13FD174[v2] (+4) = flt_13FC5FC   (pending_a)
        //   flt_13FD178[v2] (+8) = flt_13FC5F8   (pending_b, v3)
        //   dword_13FD170[v2](+0)= dword_649DD4  (pending_packed, v4)
        SkyGradient::Triple& t = g->scratch[g->pending_index];
        t.a      = g->pending_a;                          // /*0x5b84ec*/
        t.b      = g->pending_b;                          // /*0x5b84f9*/
        t.packed = g->pending_packed;                     // /*0x5b8500*/
    }
    // result = &flt_13FD1B8[24*band]; copy 72 bytes from the scratch triples
    // (dword_13FD170 region) into result+6.
    float* result = g->entry[band];                       // /*0x5b8518*/
    std::memcpy(result + 6, g->scratch, 72);              // qmemcpy 72 bytes /*0x5b853d*/
    return result;                                        // /*0x5b8543*/
}

// gilde.exe 0x5b8548 — VIBE_SkyColor_SetBandGradient (__usercall, eax)
unsigned SkyColor_SetBandGradient(SkyGradient* g, unsigned band, bool finalize) {
    unsigned v3 = band;                                   // /*0x5b854d*/
    if (band < 7) {                                       // /*0x5b8555*/
        // Original: VIBE_SceneGraph_WalkAndInvoke(..., StoreBandColors, 30?, band)
        // — the scene walk is the coupled leaf; the pure store below is exact.
        unsigned result = 96 * v3;                        // byte offset /*0x5b859b*/
        // flt_13FD1B8[result/4 + i] = staged[i] (flt_64A074..64A08C)
        for (int i = 0; i < 6; ++i)
            g->entry[v3][i] = g->staged[i];               // /*0x5b85a6..0x5b85c8*/
        if (finalize) {                                   // a3 /*0x5b85d2*/
            // return (unsigned)VIBE_SkyColor_CopyGradientEntry(v3); pointer is
            // ignored by all callers; we run the flush for its side effects.
            SkyColor_CopyGradientEntry(g, static_cast<int>(v3)); // /*0x5b85d6*/
            return result;
        }
        return result;                                    // /*0x5b8557*/
    }
    return band;                                          // result (unchanged) /*0x5b8557*/
}

// gilde.exe 0x43f444 — VIBE_SkyColor_ApplyDefaultLighting (__usercall, eax)
int SkyColor_ApplyDefaultLighting(const unsigned* band_index,
                                  DefaultLightingHook& hook) {
    // VIBE_SkyColor_BlendBandLighting(*a1, 1.0, 1065353216, 0)
    //   a2 = 1.0f (alpha), a3 = 0x3F800000 == 1.0f (scale), a4 = 0.
    hook.BlendBandLighting(*band_index, 1.0f, 1.0f, 0);   // /*0x43f452*/
    return 1;                                             // /*0x43f45c*/
}

// gilde.exe 0x5b85e4 inner do-while @0x5b87c1..0x5b88c1 — build the 6 fog/shade
// scratch triples (dword_13FD170/174/178) by cross-fading band `a1` -> band
// (a1+1)%7 over each of the 6 keyframes.  This is the band-table build the live
// sky/fog clear (VIBE_SkyColor_ApplyAmbientBlend @0x5b8b04) then indexes.
bool SkyColor_BuildFogScratch(const SkySceneTable& src, unsigned band, float t,
                              SkyFogScratch& out) {
    // Early-out matches the function head at 0x5b8604: a1>=7 || t<0 || t>1.
    if (band >= 7u || t < 0.0f || t > 1.0f)               // /*0x5b8604*/
        return false;                                     // /*0x5b8608*/

    const unsigned a = band;                              // source band a1
    const unsigned b = (band + 1u) % 7u;                  // v6 = (a1+1)%7 /*0x5b8661*/
    // v47 = 1.0 - a2 is computed in x87 then `fstp [var_24]` STORES IT AS A
    // 4-BYTE FLOAT (0x5b8783) and is `fld`'d back (0x5b87bd) as the per-channel
    // weight st(1) — so the (1-t) weight is float-ROUNDED before the products.
    const float  v47 = 1.0f - t;                          // 1.0 - a2 /*0x5b8783*/
    const double v21 = (double)v47;                       // float-rounded (1-t) weight

    // Per-keyframe (v17 = 0..5): the disasm advances ecx/edx by 12 bytes (3 dwords
    // = one keyframe) per iteration, so it walks src[a].fog[i] / src[b].fog[i].
    for (int i = 0; i < kSkyKeyframes; ++i) {             // /*0x5b88c1*/
        const SkyFogKeyframe& A = src.fog[a][i];
        const SkyFogKeyframe& B = src.fog[b][i];

        // Channel bytes taken as signed-16 of the byte value (fild word, byte 0..255).
        auto byteN = [](u32 p, int n) -> i16 {
            return (i16)((p >> (8 * n)) & 0xFFu);
        };
        // out.B2 (0x5b8817), B1 (0x5b8860), B0 (0x5b88b8) — trunc toward zero.
        int c2 = (int)TruncTowardZero((double)byteN(A.packed, 2) * v21
                                    + (double)byteN(B.packed, 2) * (double)t);
        int c1 = (int)TruncTowardZero((double)byteN(A.packed, 1) * v21
                                    + (double)byteN(B.packed, 1) * (double)t);
        int c0 = (int)TruncTowardZero((double)byteN(A.packed, 0) * v21
                                    + (double)byteN(B.packed, 0) * (double)t);
        // Reassembled exactly as the byte stores: only the low byte of each result
        // is written (mov byte ptr); B2<<16 | B1<<8 | B0.
        out.triple[i].packed = ((u32)(c0 & 0xFF))
                             | ((u32)(c1 & 0xFF) << 8)
                             | ((u32)(c2 & 0xFF) << 16);
        // near (flt_13FD174 @0x5b889c) = A.near*(1-t) + B.near*t ; far likewise.
        out.triple[i].near_ = (float)((double)A.near_ * v21 + (double)B.near_ * (double)t);
        out.triple[i].far_  = (float)((double)A.far_  * v21 + (double)B.far_  * (double)t);
    }
    return true;
}

} // namespace render
} // namespace guild
