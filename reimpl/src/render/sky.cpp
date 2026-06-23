#include "render/sky.h"
#include "render/particle.h"   // TruncToward
#include "render/types.h"      // Surface
#include "render/scene_load.h" // SceneHeader / SceneLight (read-only handoff)
#include <cstddef>             // size_t

namespace guild::render {

// gilde.exe 0x5b85e4 (ambient-light core of VIBE_SkyColor_BlendBandLighting).
// flt_64A074/78/7C = the lerped band RGB (all scaled by `scale`); the luma
// flt_64A070 = r*0.30 + g*0.59 + b*0.11. The original lerps the gradient row
// floats (rowB - rowA)*t + rowA, then multiplies each by `scale`.
SkyAmbient BlendBandLighting(const SkyBandColor bands[kSkyBands], int a, float t, float scale) {
    SkyAmbient out{0.0f, 0.0f, 0.0f, 0.0f};
    if ((unsigned)a >= 7u || t < 0.0f || t > 1.0f)
        return out;

    int b = (a + 1) % 7;
    // x87 precision model (DISASM @0x5b8677..0x5b876f is the reference):
    //   * (rowB-rowA) is `fsub` then `fstp` to a 4-byte stack float (var_28/1C/14)
    //     -> the band DIFFERENCE is float-ROUNDED before the multiply.
    //   * t*(B-A) is kept in the x87 register (80-bit); rowA is added at 80-bit.
    //   * R and B: the lerp result is `fstp`'d to flt_64A074/A07C (float-ROUNDED),
    //     then `fld`'d back to scale -> rLerp/bLerp ARE float-rounded.
    //   * G: gLerp is NEVER stored before the scale (`fmul arg_4` @0x5b872f acts on
    //     the 80-bit register directly) -> gLerp is NOT float-rounded. This asymmetry
    //     is real in the disasm and must be preserved.
    //   * the scale multiply (* a3) `fst`s the float-rounded value to flt_64A07x BUT
    //     KEEPS the 80-bit product in the register for the luma weighting (so the
    //     luma uses the unrounded scaled channel, not the stored float).
    const float dr = bands[b].r - bands[a].r;            // var_28  /*0x5b868b*/
    const float dg = bands[b].g - bands[a].g;            // var_1C  /*0x5b86a5*/
    const float db = bands[b].b - bands[a].b;            // var_14  /*0x5b86bd*/
    const float  rLerp = (float)((double)t * (double)dr + (double)bands[a].r); // fstp /*0x5b86ff*/
    const double gLerp = (double)t * (double)dg + (double)bands[a].g;          // kept 80-bit
    const float  bLerp = (float)((double)t * (double)db + (double)bands[a].b); // fstp /*0x5b8735*/

    const double rScaled = (double)rLerp * (double)scale; // v13 (80-bit kept) /*0x5b8753*/
    const double gScaled = gLerp        * (double)scale;  // v12 (80-bit)      /*0x5b8733*/
    const double bScaled = (double)bLerp * (double)scale; // v15 (80-bit kept) /*0x5b876b*/

    out.r = (float)rScaled;   // flt_64A074 (float store)
    out.g = (float)gScaled;   // flt_64A078
    out.b = (float)bScaled;   // flt_64A07C
    // luma = g*0.59 + r*0.30 + b*0.11 using the 80-bit scaled channels (the regs,
    // not the float-rounded flt_64A07x). order matches v16=v12*0.59+v14; +v18.
    out.luma = (float)(gScaled * (double)kSkyLumaG
                     + rScaled * (double)kSkyLumaR
                     + bScaled * (double)kSkyLumaB);
    return out;
}

// gilde.exe 0x43f460 — VIBE_SkyColor_ApplyScaledBlend alpha.
float ScaledBlendAlpha(int brightness) {
    float v = (float)((double)brightness * kBrightScale);
    if (v <= 0.0f) {
        // SLODWORD(v6) < 1.0f && v6 <= 0 -> alpha 0
        return 0.0f;
    }
    if (v >= 1.0f)
        return 1.0f;
    return v;
}

// out = trunc( a*(1-t) + b*t ) — the signed-16 channel blend used for the 6
// shade colours (VIBE_Coord_ConvertX truncates toward zero).
int LerpChannelTrunc(int a, int b, float t) {
    double blended = (double)(i16)a * (1.0f - t) + (double)(i16)b * t;
    return TruncToward(blended);
}

// gilde.exe 0x5b8b04 — VIBE_SkyColor_ApplyAmbientBlend (__userpurge, al).
//
// The original walks three parallel runtime arrays indexed by triple-stride 3
// dwords:  dword_13FD170 (packed sky/clear colour), flt_13FD174 (fog near),
// flt_13FD178 (fog far).  It cross-fades band `a1` -> band `a2` by `a3` (frac):
//
//   * each colour byte (B2 @0x5b8ba0, B1 @0x5b8bfd, B0 @0x5b8c52) is
//       trunc( (byte[a2]-byte[a1])*frac + byte[a1] )   (ConvertX = chop)
//     reassembled into the packed dword v20.
//   * near'  = (flt_13FD174[a2]-flt_13FD174[a1])*frac + flt_13FD174[a1]   (v18)
//     far'   = (flt_13FD178[a2]-flt_13FD178[a1])*frac + flt_13FD178[a1]   (v16)
//     near   = near' * flt_64A018  (v12, ConfigureFog arg1)              /*0x5b8bf9*/
//     far    = far'  * flt_64A018  (v17, ConfigureFog arg2)              /*0x5b8c6a*/
//   * VIBE_Render_ConfigureFog(near, far, packed) — sets dword_649DD4 = packed
//     (the sky/clear colour) — then dword_649F08 = a1.
//
// Early-out (return 0, nothing written) when a1>=6 || a2>=6 || frac<0 || frac>1.
SkyFog BlendAmbientFog(const SkyFogBand bands[6], unsigned a, unsigned b, float frac) {
    SkyFog out{0u, 0.0f, 0.0f, false};
    // cmp eax,6 / cmp edx,6 / fldz fcomp ja / cmp arg0,3F800000h jle  /*0x5b8b27*/
    if (a >= 6u || b >= 6u || frac < 0.0f || frac > 1.0f)
        return out;

    const SkyFogBand& A = bands[a];
    const SkyFogBand& B = bands[b];

    // Per-byte colour lerp, truncate toward zero. Byte layout of the packed
    // dword: bits 16..23 = B2 (first ConvertX), 8..15 = B1, 0..7 = B0.
    //
    // 1:1 NOTE (the algebraic form matters): 0x5b8b04 does NOT use the
    // a*(1-t)+b*t form (that is BlendBandLighting @0x5b85e4); it uses
    //   v6 = (double)(BYTE2(b) - BYTE2(a)) * a3 + (double)BYTE2(a)   /*0x5b8b9e*/
    // i.e. (B-A)*frac + A, with B,A the UNSIGNED packed bytes (0..255), the byte
    // difference taken as int. Those two forms truncate to different integers in
    // floating point (e.g. A==B==1, frac==0.05: (0)*t+1==1 -> 1, but
    // 1*(1-t)+1*t==0.9999.. -> 0), so this must match the binary exactly.
    auto byteN = [](u32 p, int n) -> int { return (int)((p >> (8 * n)) & 0xFFu); };
    auto lerpDiffTrunc = [&](int av, int bv) -> int {
        double v = (double)(bv - av) * frac + (double)av;       // (B-A)*frac + A
        return TruncToward(v);                                  // ConvertX = chop
    };
    int c2 = lerpDiffTrunc(byteN(A.packed, 2), byteN(B.packed, 2)); // /*0x5b8b9e*/
    int c1 = lerpDiffTrunc(byteN(A.packed, 1), byteN(B.packed, 1)); // /*0x5b8bfb*/
    int c0 = lerpDiffTrunc(byteN(A.packed, 0), byteN(B.packed, 0)); // /*0x5b8c50*/
    // var_10 reassembled: B2<<16 | B1<<8 | B0. Bytes are 0..255 (lerp of two
    // bytes), so the byte stores match the original's `mov byte ptr` writes.
    u32 packed = ((u32)(c0 & 0xFF))
               | ((u32)(c1 & 0xFF) << 8)
               | ((u32)(c2 & 0xFF) << 16);

    // Fog distances: (b - a)*frac + a. The disasm does `fld B; fsub A; fmul frac;
    // fadd A` ALL in the x87 register (80-bit, the diff is NOT stored to a float),
    // then `fstp [var_14]` float-rounds the lerp (0x5b8bdf near / 0x5b8c3c far)
    // before reloading it for the * flt_64A018 scale. Model: whole lerp in double,
    // one float round, then the scale at double (the scaled product is kept 80-bit
    // through ConfigureFog's push). frac is the float arg promoted to double.
    float nearLerp = (float)(((double)B.near_ - (double)A.near_) * (double)frac
                             + (double)A.near_);                      // /*0x5b8bdf*/
    float farLerp  = (float)(((double)B.far_  - (double)A.far_)  * (double)frac
                             + (double)A.far_);                       // /*0x5b8c3c*/

    out.color   = packed;
    out.near_   = nearLerp * kFogRangeScale;                          // v12 /*0x5b8bf9*/
    out.far_    = farLerp  * kFogRangeScale;                          // v17 /*0x5b8c6a*/
    out.applied = true;
    return out;
}

u32 SkyFogColor(const SkyFogBand bands[6], unsigned a, unsigned b, float frac) {
    return BlendAmbientFog(bands, a, b, frac).color;
}

// Fill the universe surface with the time-of-day sky colour — the software
// equivalent of BeginUniverseFrame's pre-terrain clear to dword_649DD4
// (VIBE_Render_ClearViewport @0x5dd464 / VIBE_Render_ClearRect @0x434728, which
// blit/memset the whole back buffer with the device colour). Respects the
// surface clip rect; writes 16bpp or 32bpp pixels per surf->bpp.
void RenderSky(Surface* surf, u32 packedColor) {
    if (!surf || !surf->pixels)
        return;

    // Clip rect (Create defaults: x0=y0=0, x1=width, y1=height). Clamp to bounds.
    i32 x0 = surf->clipX0, y0 = surf->clipY0;
    i32 x1 = surf->clipX1, y1 = surf->clipY1;
    if (x1 <= x0 || y1 <= y0) { x0 = 0; y0 = 0; x1 = surf->width; y1 = surf->height; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > surf->width)  x1 = surf->width;
    if (y1 > surf->height) y1 = surf->height;
    if (x1 <= x0 || y1 <= y0)
        return;

    const i32 stridePx = surf->widthPx ? surf->widthPx : surf->width;

    if (surf->bpp <= 16) {
        u16 px = (u16)(packedColor & 0xFFFFu);
        for (i32 y = y0; y < y1; ++y) {
            u16* row = reinterpret_cast<u16*>(surf->pixels) + (size_t)stridePx * y;
            for (i32 x = x0; x < x1; ++x)
                row[x] = px;
        }
    } else if (surf->bpp == 24) {
        u8 b0 = (u8)(packedColor & 0xFF);
        u8 b1 = (u8)((packedColor >> 8) & 0xFF);
        u8 b2 = (u8)((packedColor >> 16) & 0xFF);
        for (i32 y = y0; y < y1; ++y) {
            u8* row = surf->pixels + (size_t)stridePx * 3 * y;
            for (i32 x = x0; x < x1; ++x) {
                row[x * 3 + 0] = b0;
                row[x * 3 + 1] = b1;
                row[x * 3 + 2] = b2;
            }
        }
    } else { // 32bpp
        for (i32 y = y0; y < y1; ++y) {
            u32* row = reinterpret_cast<u32*>(surf->pixels) + (size_t)stridePx * y;
            for (i32 x = x0; x < x1; ++x)
                row[x] = packedColor;
        }
    }
}

// gilde.exe 0x5e7e38 (light-rig portion) — adapt the scene_load-parsed header into
// the runtime SkySceneTable.  This mirrors, field-for-field, how the loader writes
// the engine globals (flt_13FD1B8.. / flt_13FD1C4.. / dword_13FD1D0/D4/D8):
//   light[i].pos        -> ambient[i]              (ReadVec3 -> flt_13FD1B8/1BC/1C0)
//   light[i].color      -> secondary[i]            (ReadVec3 -> flt_13FD1C4/1C8/1CC, >=0xB5)
//   light[i].keyframe[k]-> fog[i][k]               (id->dword_13FD1D0, a->13FD1D4, b->13FD1D8)
// The keyframe id dword is the packed fog/shade colour (bytes B2/B1/B0); near=a, far=b.
SkySceneTable LoadSkyBands(const SceneHeader& header) {
    SkySceneTable t{};
    int n = (int)header.lights.size();
    if (n > kSkyBandCount) n = kSkyBandCount;   // file count is 4/6/7; clamp to 7
    t.band_count = n;
    for (int i = 0; i < n; ++i) {
        const SceneLight& L = header.lights[i];
        t.ambient[i][0] = L.pos.x;              // flt_13FD1B8[24*i]
        t.ambient[i][1] = L.pos.y;              // flt_13FD1BC[24*i]
        t.ambient[i][2] = L.pos.z;              // flt_13FD1C0[24*i]
        if (L.hasColor) {                        // version >= 0x3A6C00B5
            t.secondary[i][0] = L.color.x;      // flt_13FD1C4[24*i]
            t.secondary[i][1] = L.color.y;      // flt_13FD1C8[24*i]
            t.secondary[i][2] = L.color.z;      // flt_13FD1CC[24*i]
        }
        int k = (int)L.keyframes.size();
        if (k > kSkyKeyframes) k = kSkyKeyframes;
        for (int j = 0; j < k; ++j) {
            t.fog[i][j].packed = L.keyframes[j].id;   // dword_13FD1D0[24*i + 3*j]
            t.fog[i][j].near_  = L.keyframes[j].a;    // flt_13FD1D4 [24*i + 3*j]
            t.fog[i][j].far_   = L.keyframes[j].b;    // flt_13FD1D8 [24*i + 3*j]
        }
    }
    return t;
}

// Full time-of-day sky/fog colour from a loaded scene table: build the 6 scratch
// triples for the current band/blend (step 2), then cross-fade two of them by the
// time-of-day fraction (step 3).  Mirrors the live chain
// BlendBandLighting @0x5b85e4 -> ApplyAmbientBlend @0x5b8b04 -> dword_649DD4.
SkyFog ComputeSkyFog(const SkySceneTable& table, unsigned band, float blend,
                     unsigned fogA, unsigned fogB, float frac) {
    SkyFogScratch scratch{};
    if (!SkyColor_BuildFogScratch(table, band, blend, scratch)) {
        return SkyFog{0u, 0.0f, 0.0f, false};
    }
    // The scratch triples (dword_13FD170/174/178) are exactly the SkyFogBand[6]
    // table BlendAmbientFog indexes; reinterpret in place (same {packed,near,far}
    // layout).  Build a contiguous SkyFogBand[6] to honour the public API.
    SkyFogBand bands[6];
    for (int i = 0; i < 6; ++i) {
        bands[i].packed = scratch.triple[i].packed;
        bands[i].near_  = scratch.triple[i].near_;
        bands[i].far_   = scratch.triple[i].far_;
    }
    return BlendAmbientFog(bands, fogA, fogB, frac);
}

} // namespace guild::render
