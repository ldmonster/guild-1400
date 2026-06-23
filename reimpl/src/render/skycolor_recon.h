#pragma once
// Sky/time-of-day band-colour reconstruction for Die Gilde (gilde.exe).
//
// This module reconstructs the "sky colour" cluster: the per-object band-colour
// records, the time-of-day band interpolation, the band light-presence predicate
// and the global gradient table used by the lighting blend.  The reconstruction is
// driven 1:1 from the Hex-Rays decompile (reference of record).
//
//   gilde.exe 0x5b80ac VIBE_SkyColor_InterpolateBand
//   gilde.exe 0x5b83b0 VIBE_SkyColor_SetTimeOfDay
//   gilde.exe 0x5b83f0 VIBE_SkyColor_StoreBandColors
//   gilde.exe 0x5b84c4 VIBE_SkyColor_CopyGradientEntry
//   gilde.exe 0x5b8548 VIBE_SkyColor_SetBandGradient
//   gilde.exe 0x5b89e8 VIBE_SkyColor_BandHasLight
//   gilde.exe 0x43f444 VIBE_SkyColor_ApplyDefaultLighting
//
// Pure band-interpolation / colour math is reconstructed faithfully here.  The
// coupled scene-graph writes (object position/rotation, scene-graph walk, the
// gradient-copy that feeds the GPU lighting blend) are routed through an inert
// SkyColorHooks struct so the integer/float math can be exercised in isolation
// and golden-vector tested.  No third-party libs.

#include "guild/common/types.h"

namespace guild {
namespace render {

// --- Object band-colour record ----------------------------------------------
// The original engine stores the per-band colour table as a flat array of
// 56-byte records.  *(_DWORD*)(obj+488) points at the base of that array; each
// band is `56 * index` bytes from the base.  The 14 dwords/floats per record
// are reconstructed here by value.
//
//   +0x00 (band[0..2])  = euler / orientation triple        (-> object +76..84)
//   +0x0C (band[3..5])  = world translation triple          (-> object +132..140)
//   +0x18 (band[6])     = scalar (a)                         (-> object +92)
//   +0x1C (band[7..8])  = colour g/b component pair          (used by BandHasLight)
//   +0x24 (band[9])     = colour r / light field            (-> object +148)
//   +0x28 (band[10])    = scalar (b)                         (-> object +144)
//   +0x2C (band[11])    = scalar (c)                         (-> object +152)
//   +0x30 (band[12..13]) = remaining 8 bytes of the 56-byte stride
struct SkyBandRecord {       // 56 bytes, gilde.exe band stride
    float f[14];
};

// --- Object surrogate --------------------------------------------------------
// Mirrors the fields of the original 3D object that the sky-colour functions
// read/write.  Field offsets in comments are the original byte offsets.
struct SkyObject {
    SkyBandRecord* bands;    // +0x1E8 (488): base of the 56-byte band array (null => no-op)
    u8   kind;               // +0x215 (533): object kind/state (5,6,7,8 ...)
    float band_phase;        // +0x1A4 (420): per-object band phase offset (read at bands+420?)
    // Interpolation outputs:
    float out_92[3];         // +0x5C  (92,96,100) — written across the 3-iter loop
    float out_144;           // +0x90  (144)
    float out_148;           // +0x94  (148)
    float out_152;           // +0x98  (152)
    float out_pos[3];        // +0x4C  (76..84)  via SetPosition (euler)
    float out_trans[3];      // +0x84  (132..140) via SetWorldTranslation
};

// Inert hooks for the coupled scene-graph writes.  The defaults simply record
// the values; real integration overrides these to drive the scene graph.
struct SkyColorHooks {
    // Reads the per-object band-phase scalar (original: *(float*)(bands_base+420)).
    float band_phase = 0.0f;
    float ReadBandPhase() const { return band_phase; }
    // VIBE_Object_SetPosition surrogate (writes euler triple to obj+76..84).
    void SetPosition(SkyObject* o, const float pos[3]) {
        o->out_pos[0] = pos[0]; o->out_pos[1] = pos[1]; o->out_pos[2] = pos[2];
    }
    // VIBE_Object_SetWorldTranslation surrogate (writes obj+132..140).
    void SetWorldTranslation(SkyObject* o, const float t[3]) {
        o->out_trans[0] = t[0]; o->out_trans[1] = t[1]; o->out_trans[2] = t[2];
    }
};

// gilde.exe 0x5b80ac — VIBE_SkyColor_InterpolateBand (__userpurge, al)
// Interpolates the object's band colour/transform for time-of-day `band`
// (0..6) with fractional `frac` (0..1).  Returns 1 on success, 0 on reject.
char SkyColor_InterpolateBand(SkyObject* obj, unsigned band, float frac,
                              SkyColorHooks& hooks);

// gilde.exe 0x5b83b0 — VIBE_SkyColor_SetTimeOfDay (__fastcall)
// Splits a continuous time-of-day value into (integer band, fractional part)
// and forwards to SkyColor_InterpolateBand.  Returns 1.
char SkyColor_SetTimeOfDay(SkyObject* obj, const float* time_of_day,
                           SkyColorHooks& hooks);

// gilde.exe 0x5b83f0 — VIBE_SkyColor_StoreBandColors (__usercall, al)
// Stores the source object's transient colour/transform fields into band slot
// `band` (0..6) of its band table.  `src` mirrors the original a1[] dword array.
struct SkyStoreSource {      // mirrors a1[] dwords used by StoreBandColors
    u32 d[39];               // need indices 19..38; sized to cover them
    SkyBandRecord* bands;    // a1[122] (offset +488): destination band table
};
char SkyColor_StoreBandColors(SkyStoreSource* src, unsigned band);

// gilde.exe 0x5b89e8 — VIBE_SkyColor_BandHasLight (__usercall, al)
// Returns 0 (and sets the global "has light" flag) if band `band` of the object
// carries light for its kind; otherwise 1.  `out_has_light` receives the global
// flag side effect (original: LOBYTE(dword_13FD460[0]) = 1).
char SkyColor_BandHasLight(const SkyObject* obj, int band, bool* out_has_light);

// --- Global gradient table ---------------------------------------------------
// gilde.exe flt_13FD1B8 : per-band gradient entries, stride 24 floats (96 bytes),
// indices 0..6.  The first 6 floats come from SetBandGradient's staged values;
// floats [6..24) are copied in from the RGB scratch triples by CopyGradientEntry.
struct SkyGradient {
    static constexpr int kBands = 7;
    static constexpr int kStride = 24;       // floats per band
    float entry[kBands][kStride];            // flt_13FD1B8[24*band + i]

    // RGB scratch triples consumed by CopyGradientEntry (original
    // dword_13FD170 / flt_13FD174 / flt_13FD178, 6 triples, stride 3).
    struct Triple { i32 packed; float a; float b; };  // 12 bytes each
    Triple scratch[6];

    // Staged band colour produced by the lighting blend (flt_64A074..64A08C).
    float staged[6];
    // Index whose scratch is flushed on the *next* CopyGradientEntry call
    // (original dword_649F08; -1 disables the flush).
    int   pending_index = -1;
    // Values flushed into scratch[pending] (flt_13FC5F8/5FC, dword_649DD4).
    float pending_b = 0.0f;     // flt_13FC5F8 -> scratch[..].b
    float pending_a = 0.0f;     // flt_13FC5FC -> scratch[..].a
    i32   pending_packed = 0;   // dword_649DD4 -> scratch[..].packed
};

// gilde.exe 0x5b84c4 — VIBE_SkyColor_CopyGradientEntry (__usercall, eax)
// Flushes any pending scratch entry, then copies the 6 RGB scratch triples (72
// bytes) into floats [6..24) of gradient band `band`.  Returns &entry[band][0].
float* SkyColor_CopyGradientEntry(SkyGradient* g, int band);

// gilde.exe 0x5b8548 — VIBE_SkyColor_SetBandGradient (__usercall, eax)
// Stores the 6 staged band colours into gradient band `band` (0..6).  When
// `finalize` is set, also flushes the RGB scratch via CopyGradientEntry.
// `store_to_object` mirrors the scene-graph StoreBandColors walk (inert here);
// it is invoked once per call when provided.
unsigned SkyColor_SetBandGradient(SkyGradient* g, unsigned band, bool finalize);

// gilde.exe 0x43f444 — VIBE_SkyColor_ApplyDefaultLighting (__usercall, eax)
// Thin wrapper: applies a full-strength (alpha=1.0, scale=1.0) lighting blend
// to the band index held in *band_index.  Returns 1.  The heavy blend itself
// (VIBE_SkyColor_BlendBandLighting, 0x5b85e4) is *not* in this cluster, so the
// wrapper records the requested arguments through the hook below.
struct DefaultLightingHook {
    unsigned last_band = 0;
    float    last_alpha = 0.0f;
    float    last_scale = 0.0f;
    int      last_flag = -1;
    void BlendBandLighting(unsigned band, float alpha, float scale, int flag) {
        last_band = band; last_alpha = alpha; last_scale = scale; last_flag = flag;
    }
};
int SkyColor_ApplyDefaultLighting(const unsigned* band_index,
                                  DefaultLightingHook& hook);

// --- Per-scene runtime band/keyframe source tables ---------------------------
// These are the BSS arrays VIBE_Scene_LoadFromStream @0x5e7e38 POPULATES from the
// scene file (the per-scene day/dusk/night sky-colour data) and that
// VIBE_SkyColor_BlendBandLighting @0x5b85e4 then consumes:
//
//   flt_13FD1B8/1BC/1C0[24*band]  — per-band AMBIENT RGB triple   (light pos vec3
//                                   in the file; the band's sky ambient colour)
//   flt_13FD1C4/1C8/1CC[24*band]  — per-band SECONDARY/diffuse RGB (>= ver 0xB5)
//   dword_13FD1D0[24*band + 3*kf] — per-band, per-keyframe PACKED fog/shade colour
//                                   (the file "id" dword; bytes B2/B1/B0 are the
//                                   colour channels BlendBandLighting reads)
//   flt_13FD1D4 [24*band + 3*kf]  — per-band, per-keyframe fog NEAR distance
//   flt_13FD1D8 [24*band + 3*kf]  — per-band, per-keyframe fog FAR  distance
//
// Scene-load layout (0x5e8011..0x5e8107):
//   for band i in [0, lightCount):                  lightCount = 4 | 6 | 7 (ver)
//     ReadVec3 -> flt_13FD1B8/1BC/1C0[24*i]                          (ambient)
//     if ver>=0xB5: ReadVec3 -> flt_13FD1C4/1C8/1CC[24*i]            (secondary)
//     for kf v39 in [0, 6):
//       v40 = ReadDword  -> dword_13FD1D0[24*i + 3*v39]   (= 12*v39 + 96*i bytes)
//       v42 = ReadDword  -> flt_13FD1D4 [24*i + 3*v39]    (near)
//       v42 = ReadDword  -> flt_13FD1D8 [24*i + 3*v39]    (far)
//
// The first loop (0x5e7fa2..0x5e7fd8) DEFAULT-initialises a sibling 7*24 table
// (flt_13FD158..16C) from the scene ambient (flt_64A074..7C); that table is the
// light-rig staging the lighting refresh uses, not the sky-fog source, so it is
// not modelled here.  The 6 keyframes per band == the 6 fog/shade triples
// BlendBandLighting cross-fades into the dword_13FD170/174/178 scratch.

constexpr int kSkyBandCount = 7;        // bands indexed 0..6, wrap mod 7
constexpr int kSkyKeyframes = 6;        // 6 fog/shade keyframes per band

// One per-band, per-keyframe fog/shade source record (dword_13FD1D0 / flt_13FD1D4
// / flt_13FD1D8 triple).  Reconstructed 1:1 by value.
struct SkyFogKeyframe {
    u32   packed = 0;   // dword_13FD1D0[..] — packed colour (bytes B2,B1,B0)
    float near_  = 0.0f;// flt_13FD1D4[..]   — fog near distance
    float far_   = 0.0f;// flt_13FD1D8[..]   — fog far distance
};

// The full per-scene runtime sky source table (the 13FD1B8.. / 13FD1D0.. arrays
// the scene loader fills).  `band_count` is the file's light count (4/6/7); bands
// past it stay zero (matching the BSS zero-init / the i=7 default).
struct SkySceneTable {
    int   band_count = 0;
    float ambient[kSkyBandCount][3]    = {};  // flt_13FD1B8/1BC/1C0[24*band]
    float secondary[kSkyBandCount][3]  = {};  // flt_13FD1C4/1C8/1CC[24*band]
    SkyFogKeyframe fog[kSkyBandCount][kSkyKeyframes] = {};  // dword_13FD1D0..
};

// --- Output scratch (dword_13FD170 / flt_13FD174 / flt_13FD178) ---------------
// The 6 fog/shade triples VIBE_SkyColor_BlendBandLighting writes and
// VIBE_SkyColor_ApplyAmbientBlend @0x5b8b04 then cross-fades into the framebuffer
// clear colour.  This is exactly the `SkyFogBand[6]` array sky.h's BlendAmbientFog
// indexes (packed colour / near / far per triple).
struct SkyFogScratch {
    SkyFogKeyframe triple[kSkyKeyframes];   // dword_13FD170/174/178[3*i], i 0..5
};

// gilde.exe 0x5b85e4 (inner do-while @0x5b87c1..0x5b88c1) — the per-keyframe
// cross-band fog/shade blend.  Cross-fades band `a` -> band (a+1)%7 by `t` for all
// 6 keyframes, writing the scratch triples (dword_13FD170/174/178):
//
//   for i in [0,6):
//     out.packed.B2 = trunc( srcA[a].kf[i].B2*(1-t) + srcB[b].kf[i].B2*t )   (ConvertX)
//     out.packed.B1 = trunc( srcA[a].kf[i].B1*(1-t) + srcB[b].kf[i].B1*t )
//     out.packed.B0 = trunc( srcA[a].kf[i].B0*(1-t) + srcB[b].kf[i].B0*t )
//     out.near      = srcA[a].kf[i].near*(1-t) + srcB[b].kf[i].near*t
//     out.far       = srcA[a].kf[i].far *(1-t) + srcB[b].kf[i].far *t
//
// where the channel bytes are taken as signed-16 (fild word) of the byte value
// (0..255) — identical to the byte lerp ApplyAmbientBlend uses.  Rejects
// (returns false, scratch untouched) when a>=7 || t<0 || t>1, matching the
// original's early-out at 0x5b8604.  This is the band-table BUILD that turns the
// loaded per-scene keyframes into the 6-entry table the sky/fog clear indexes.
bool SkyColor_BuildFogScratch(const SkySceneTable& src, unsigned band, float t,
                              SkyFogScratch& out);

} // namespace render
} // namespace guild
