#pragma once
#include "guild/common/types.h"

namespace guild::render { struct Surface; struct Texture; struct RgbzVertex; }

// =============================================================================
// guild::render::fxrecon3 — particle billboard rasterizer prep.
//
// Faithful 1:1 reconstruction (Hex-Rays is the reference of record) of the one
// VIBE_Particle_* leaf that the existing fxrecon / particle.* / particle_spawn.*
// modules did NOT yet cover:
//
//   0x5e1278  VIBE_Particle_RenderSystem  (__usercall, eax=a1, esi=a2)
//
// This is the per-system render routine: it runs the optional "should render"
// callback, computes the world matrix for the system's bone, then for every live
// particle slot it (a) transforms the slot center into camera/eye space, (b)
// derives a distance-fade alpha, (c) builds the screen-space billboard half-size
// and the four projected quad corners, (d) culls against the active scissor /
// near-far planes, (e) writes the clip-flag bits the rasterizer consumes, and
// finally (f) appends the visible slots to the engine display list (sorted by an
// integer depth key).
//
// SCOPE / RULE 8 + RULE 3 NOTE. The transform/fade/projection/cull arithmetic and
// the clip-flag bit packing are reconstructed EXACTLY (same float op-order, same
// constants, same integer truncation). The *coupled leaves* this routine reaches
// are GPU/scene-graph bound and are surfaced as inert hooks (function pointers),
// exactly like src/render/fxrecon_particle_mirror_shadow.*:
//
//   - VIBE_Render_FreeObjectNode   (0x5e0f30)  scene-graph node recycle
//   - VIBE_Transform_ComputeBoneWorldMatrix (0x5c8fac) bone->world 4x4
//   - VIBE_Coord_ConvertX          (0x5c6b08)  fpu->int coord rounding helper
//   - VIBE_Texture_FindGroupMember (0x5daec0)  texture atlas lookup
//   - the display-list append (dword_13FC570 / function-ptr slots
//     dword_1408100 / dword_1408118 / dword_13DCD98) — GPU draw queue (rule 3)
//
// Each hook defaults to inert/identity; the live backend installs the real leaf.
// Nothing here is faked: the visibility math is 1:1; the hooks are merely the
// seam where the live call tree plugs the genuine callee back in.
//
// The mutable projection globals (flt_13FCD0C/10/18, flt_13FCAF8, flt_13FC544,
// flt_13FC5AC/58C, flt_13FCAFC, flt_13FC76C, the scissor dword_13ECE58..64) live
// in the original .bss as 0 in the static image and are written by the camera
// setup elsewhere; they are modelled here as a ProjState struct the caller fills.
// =============================================================================

namespace guild::render::fxrecon3 {

using f32 = float;
using f64 = double;
using guild::u8;
using guild::u16;
using guild::u32;
using guild::i16;
using guild::i32;

// ---------------------------------------------------------------------------
// Static float/double constants recovered via get_bytes.
// ---------------------------------------------------------------------------
constexpr f32 kHalf          = 0.5f;          // flt_62BA68
constexpr f32 kLumaB         = 0.30000001192092896f; // flt_62BA6C (point luma .B coef)
constexpr f32 kLumaG         = 0.5899999737739563f;  // flt_62BA70
constexpr f32 kLumaR         = 0.10999999940395355f; // flt_62BA74
constexpr f32 kLumaScale     = 0.25f;          // flt_62BA78
constexpr f32 k2p24          = 16777216.0f;    // flt_62BA7C (2^24, poly depth scale)
constexpr f64 kDbl3          = 3.0;            // dbl_62BA84
constexpr f64 kDbl255        = 255.0;          // dbl_62BA8C (alpha clamp ceiling)

// ---------------------------------------------------------------------------
// Particle slot record — 84-byte stride, base = system[+40] (a1[10]).
// Offsets straight from the decompile (byte view).
//   +56 cx  +60 cy  +64 cz   : slot center (model space)  float
//   +72 size                 : sprite radius              float
//   +76 colR +77 colG +78 colB +79 alpha : RGBA bytes
//   +80 textureSubIndex      : 0 => use system default tex
//   +81 flags  (bit0 0x1 => active)
// ---------------------------------------------------------------------------
struct ParticleSlot {
    u8 raw[84];
    f32  cx()    const { return *reinterpret_cast<const f32*>(raw + 56); }
    f32  cy()    const { return *reinterpret_cast<const f32*>(raw + 60); }
    f32  cz()    const { return *reinterpret_cast<const f32*>(raw + 64); }
    f32  size()  const { return *reinterpret_cast<const f32*>(raw + 72); }
    u8   colR()  const { return raw[76]; }
    u8   colG()  const { return raw[77]; }
    u8   colB()  const { return raw[78]; }
    u8   alpha() const { return raw[79]; }
    u8   texSub()const { return raw[80]; }
    u8   flags() const { return raw[81]; }
};

// 4x4 row-major world matrix as written by ComputeBoneWorldMatrix (v94[16]):
//   v94[0..3] row0, [4..7] row1, [8..11] row2, [12..15] translation row.
// Eye-space mapping in the decompile:
//   X = cx*m0 + cy*m4 + cz*m8  + m12   (-> v95)
//   Y = cx*m1 + cy*m5 + cz*m9  + m13   (-> v8 = v96)
//   Z = cx*m2 + cy*m6 + cz*m10 + m14   (-> v9 = v97)
struct Mat4 { f32 m[16]; };

// Mutable projection state (engine .bss; 0 in static image).
struct ProjState {
    f32 sx;        // flt_13FCD0C  screen X scale
    f32 ox;        // flt_13FCD18  screen X offset
    f32 sy;        // flt_13FCAF8  screen Y scale  (note: applied as -sy for half-w)
    f32 oy;        // flt_13FCD10  screen Y offset
    f32 minDistSq; // flt_13FC544  below => full alpha (255)
    f32 fadeBias;  // flt_13FC5AC  subtracted from sqrt(distSq)
    f32 fadeScale; // flt_13FC58C  multiplied to give fade amount
    f32 zFar;      // flt_13FCAFC  cull if z >= zFar
    f32 zNear;     // flt_13FC76C  cull if z <  zNear
    // Scissor bounds — names follow the original compare polarity (0x5e1c56):
    //   cull if scTop  <= cxScr-halfW  (ECE60 is the RIGHT  X bound)
    //   cull if scLeft >  cxScr+halfW  (ECE58 is the LEFT   X bound)
    //   cull if scBottom <= cyScr-halfH(ECE64 is the BOTTOM Y bound)
    //   cull if scRight  >  cyScr+halfH(ECE5C is the TOP    Y bound)
    i32 scLeft;    // dword_13ECE58
    i32 scTop;     // dword_13ECE60
    i32 scRight;   // dword_13ECE5C
    i32 scBottom;  // dword_13ECE64
};

// Result of projecting one slot. Mirrors what RenderSystem stores into the two
// 80-byte poly halves + the per-particle scratch span; we surface the meaningful
// numbers (the rest is rasterizer plumbing).
struct SlotProjection {
    bool visible;       // v7: passed cull & active
    f32  ex, ey, ez;    // eye-space center (v95, v96, v97)
    f32  invZ;          // 1/ez (v118)
    f32  halfW;         // screen half-width  (v115)
    f32  halfH;         // screen half-height (v117)
    f32  cxScr;         // projected center x (v116)
    f32  cyScr;         // projected center y (v114)
    i32  fadeAlpha;     // distance-fade alpha 0..255 (v93 truncated, v129)
    u8   colR, colG, colB, colA; // resolved color (after alpha modulation)
    f32  distSq;        // flt_13FC548
    // four projected quad corners (x,y) — TL, TR, BL, BR as the engine lays them
    f32  qx[4], qy[4];

    // SCREEN-space billboard corners — the geometry the original ACTUALLY writes
    // into the four poly verts (a2+16/+20 .. a2+256/+260) and that the engine
    // textured-triangle rasterizer consumes. Recovered exactly from the decompile
    // (0x5e179b/0x5e17b4/0x5e1937/0x5e1947):
    //   v99 = v103 - v102 = cxScr - halfW   (left X)
    //   v97 = v103 + v102 = cxScr + halfW   (right X)
    //   v98 = v101 - v104 = cyScr - halfH   (top Y)   (halfH = v104 is negative)
    //   v94 = v101 + v104 = cyScr + halfH   (bottom Y)
    // Vert layout (the engine's two 80-byte poly halves -> a 4-corner quad):
    //   sv[0] = (left ,top )  sv[1] = (right,bottom)
    //   sv[2] = (left ,bottom) sv[3] = (right,top )
    f32  sxc[4], syc[4];
};

// ---------------------------------------------------------------------------
// PARTICLE BLEND MODE — which span path the engine's textured-triangle leaf uses
// for a particle quad. The display-list poly node carries one of two draw vtbl
// pointers (&dword_1408100 / &dword_1408118, runtime-bound to the engine's
// translucent textured-triangle entries). Particles are drawn AFTER opaque
// objects, blended (rule-3 note in the .cpp). The two faithful blend leaves
// already reconstructed (render/raster_blend.{h,cpp}):
//   kBlendAlpha : 0x5F728A FillSpanTexturedBlend   dst=(src>>1)&m + (dst>>1)&m
//   kBlendAdd   : 0x5F739C FillSpanTexturedOr      dst |= src   (additive-ish)
// Masked variants skip source-index-0 texels (colour key) for both.
// ---------------------------------------------------------------------------
enum ParticleBlend : u8 {
    kBlendAlpha = 0,   // 50/50 translucent (smoke / spray / dust)
    kBlendAdd   = 1,   // additive OR (fire / sparks)
};

// One blended textured triangle through the 50/50 / OR span leaves (the body
// RasterizeTexturedTriangleRgbzWith uses, driving the blend inner spans).
// `masked` selects the colour-key variants (skip source index 0) — the alpha
// path FOLIAGE materials take (mesh material +194 bit 1, the "flag0 BYTE2 |= 2"
// alpha route; bit 0 adds the key). `palette` is the 256-entry row the span
// resolves texels through (callers with a 256-row shade ramp pass row base).
int RasterizeBlendTriangle(Surface* fb, const RgbzVertex v[3], const Texture& tex,
                           const u16* palette, ParticleBlend blend,
                           bool masked = false);

// ---------------------------------------------------------------------------
// A live particle system header view, sufficient for rendering. Mirrors the
// fields VIBE_Particle_RenderSystem reads off the 0x310 system block:
//   +208 (count)     particle slot count          -> slotCount
//   +40  (particles) Particle slot array base      -> slots
//   +212 (defTex)    default texture record        -> defTex
// (the bone-world matrix is computed per frame; we take it explicitly.)
// ---------------------------------------------------------------------------
struct ParticleSystemView {
    const ParticleSlot* slots = nullptr; // a1+40
    int slotCount = 0;                   // a1+208
    const Texture* defTex = nullptr;     // a1+212 (group base)
    const u16* palette = nullptr;        // bound HiColTab for defTex
};

// ---------------------------------------------------------------------------
// render_system_to_surface — the per-system particle billboard RENDER into the
// 16bpp software Surface. This is the visible-output completion of
// VIBE_Particle_RenderSystem: for every active slot it projects (project_slot,
// 1:1), then — for the visible ones — builds the two billboard triangles from
// the screen-space corners with a full [0,1]x[0,1] quad UV mapping, modulates the
// slot colour by the distance-fade alpha, and rasterizes BOTH triangles into
// `fb` through the engine's translucent textured-triangle path (the blend span
// reconstructed in raster_blend.cpp) selected by `blend`. Particles draw with the
// blend enabled, after objects — see the CityView3D handoff note in the .cpp.
//
// `colorReplaced` mirrors project_slot's alpha-modulation branch (chosen when the
// bound texture's +110 bit0 is set). Returns the number of slots drawn.
// ---------------------------------------------------------------------------
int render_system_to_surface(Surface* fb, const ParticleSystemView& sys,
                             const Mat4& world, const ProjState& ps,
                             ParticleBlend blend, bool colorReplaced);

// ---------------------------------------------------------------------------
// Inert hooks for the coupled (GPU / scene-graph / math-helper) leaves.
// ---------------------------------------------------------------------------
struct RenderHooks {
    // 0x5e0f30 — recycle the system's scene node when not rendered. Inert: return 0.
    i32  (*freeObjectNode)(void* system, void* a2) = nullptr;
    // 0x5c8fac — bone -> world 4x4. Inert: identity matrix.
    void (*computeBoneWorld)(const void* boneData, const void* parent, int flag, Mat4* out) = nullptr;
    // 0x5c6b08 — FPU->int coordinate rounding side effect. Inert: no-op.
    void (*coordConvertX)() = nullptr;
    // 0x5daec0 — texture atlas group member lookup. Inert: return group base.
    void* (*findGroupMember)(void* group, int sub) = nullptr;
};

// Default-installs inert hooks where null.
void install_default_render_hooks(RenderHooks& h);

// ---------------------------------------------------------------------------
// Pure projection of a single particle slot — the genuine inner-loop math of
// VIBE_Particle_RenderSystem (0x5e13ac .. 0x5e15c7), 1:1 with the decompile.
// `world` is the system's bone world matrix; `colorReplaced` selects the
// alpha-modulated color path (decompile branch at 0x5e1696 when the chosen
// texture has bit0 of byte +110 set).
// ---------------------------------------------------------------------------
SlotProjection project_slot(const ParticleSlot& s, const Mat4& world,
                            const ProjState& ps, bool colorReplaced);

} // namespace guild::render::fxrecon3
