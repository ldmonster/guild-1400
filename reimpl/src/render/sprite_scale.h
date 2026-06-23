#pragma once
#include "guild/common/types.h"
#include "render/shape_blit.h"        // ColorBlitTarget16 (+0x10 widthPx, +0x1C pixels)
#include "render/animation_decode.h"  // FrameBlitState (clip rect + remap table globals)
#include <cstddef>

// =============================================================================
// guild::render — scaled 2D sprite/shape compositors (ts_texture.c / shp.c).
//
// The screen-space sprite draw path that blits one shape of an animation/shape
// bank at a fractional/integer scale. Three per-pixel leaves plus their bank
// dispatcher, distinct from the unscaled shape_blit.cpp / animation_decode.cpp
// blitters already translated:
//
//   0x5D72F0  VIBE_Shape_BlitScaled16    — UNCOMPRESSED 16bpp sprite, sampled
//                                          through 16.16-style fixed-point X/Y
//                                          step deltas (nearest-neighbour scale).
//   0x5D6A08  VIBE_Shape_BlitRleScaled   — RLE sprite, integer DOWN-scale by `scale`
//                                          (every `scale`-th source column/row kept),
//                                          Y/X clipped against the global blit rect.
//   0x5D6D74  VIBE_Shape_BlitRleLightTable — as BlitRleScaled, but each opaque pixel
//                                          is recoloured through remapTable (the
//                                          dword_64A1C4 light/colour table).
//   0x5D86C4  VIBE_Shape_ShowFromBankScaled ("shp_ShowShapeFromBank" scaled variant)
//                                          — validate the shape index against the
//                                          bank, install the dest stride, and dispatch
//                                          the RLE scaled or light-table blitter.
//   0x559D60  VIBE_Render_EncodeSpriteDrawFlags — pack a sprite's draw flags.
//
// THE SHAPE / FRAME RECORD (byte offsets; same family as animation_decode.h)
// -----------------------------------------------------------------------------
//   +0x06  u16  width        (pixels per row)
//   +0x0A  u16  height       (rows)
//   +0x0C  u8   colorDepth   (0 / 1 = RLE shape, 2 = uncompressed marker)
//   +0x32  …    for the SCALED-16 path: a flat u16 pixel block (width*height)
//   +0x32  u32  for the RLE paths: row[0] runCount; then runs of
//                {u32 skip; u32 nPixels; u16 px[nPixels]}; the next row's runCount
//                follows immediately (skip>>1 = transparent pixels to advance).
//
// THE SHAPE BANK RECORD (for ShowFromBankScaled)
// -----------------------------------------------------------------------------
//   +0x0A  char name[]        (bank name, for the error sprintf)
//   +0x2A  u16  shapeCount    (max valid index)
//   +0x45  u32  offset[i]     (bank-relative byte offset of shape i)
//
// THE GLOBAL BLIT STATE — reused from animation_decode.h FrameBlitState:
//   clipX0 dword_64A1B4  clipX1 dword_64A1BC  clipY0 dword_64A1B8  clipY1 dword_64A1C0
//   remapTable dword_64A1C4   destStridePx dword_64A1C8
// =============================================================================
namespace guild::render {

// gilde.exe 0x5D72F0 — VIBE_Shape_BlitScaled16 (__userpurge edx:eax = fn(x@eax,
//   y@edx, shape@ecx, surface@ebx, xStep, yStep, useColorKey)). Blits the
//   uncompressed 16bpp block at shape+0x32 to (x, y), advancing the source by the
//   fixed-point `xStep`/`yStep` per dest pixel/row (sampled via the high byte of a
//   running fixed-point accumulator). When `useColorKey` is true, source value 0 is
//   transparent; otherwise every sample is copied. No screen clipping (callers
//   pre-clip). Returns the last computed source row offset (the original's edx:eax).
u64 ShapeBlitScaled16(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                      int xStep, int yStep, bool useColorKey);

// gilde.exe 0x5D6A08 — VIBE_Shape_BlitRleScaled (__userpurge eax = fn(x@eax, y@edx,
//   shape@ecx, surface@ebx, scale@bl)). Integer down-scale of the row-RLE shape:
//   only every `scale`-th source row/column survives. Clips against the global blit
//   rect (clipX0/X1/Y0/Y1). Returns 1 if drawn, 0 if fully clipped.
int ShapeBlitRleScaled(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                       u8 scale, const FrameBlitState& st);

// gilde.exe 0x5D6D74 — VIBE_Shape_BlitRleLightTable. As BlitRleScaled, but each
//   opaque source index is remapped: dst = remapTable[srcPixel] (st.remapTable =
//   dword_64A1C4). Returns 1 if drawn, 0 if fully clipped.
int ShapeBlitRleLightTable(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                           u8 scale, const FrameBlitState& st);

// gilde.exe 0x5D86C4 — VIBE_Shape_ShowFromBankScaled. Looks up shape `shapeIndex`
//   in `bank` (count @+0x2A, offset table @+0x45). `scale` is the integer down-scale
//   factor (the original derives it as abs(BYTE4 of the packed 64-bit arg)); when
//   `doScaledBlit` is set the RLE blitter runs (light-table form when `lightTable`
//   is non-zero), otherwise only the clip rect is recomputed. `bankStridePx` is the
//   dest surface stride installed into st.destStridePx (original: *(surface+16)).
//   Returns 1 on success / valid, 0 when the index is out of range or depth==0.
//   `highColorMode` is the original's byte_140694B (default 0). When set, the
//   binary skips the stride-install + depth-dispatch block entirely and falls
//   through to the clip-extent recompute (see impl note re: dword_64A1A2/word_64A1A6).
int ShapeShowFromBankScaled(int x, int y, const u8* bank, int shapeIndex, u8 scale,
                            bool doScaledBlit, bool lightTable,
                            const ColorBlitTarget16& dst, FrameBlitState& st,
                            bool highColorMode = false);

// gilde.exe 0x5D861C — VIBE_Shape_ShowFromBank ("shp_ShowShapeFromBank", unscaled).
//   Validates `shapeIndex` against the bank's shape count (+0x2A), then, when the
//   global scaled-draw gate is clear, installs the dest stride and — for an RLE
//   shape (depth in 1..) — runs the recolouring blit VIBE_Shape_BlitColored16
//   (reused from shape_blit.cpp). depth 0 -> fail, depth 2 -> no-op success,
//   depth > 2 -> restore stride, success. Returns 1 on success, 0 on failure.
//   `fmt` is the destination colour format the BlitColored16 (un)pack needs.
//   `highColorMode` is the original's byte_140694B (default 0). When set, the
//   binary skips the stride-install + BlitColored16 block entirely and returns 1.
int ShapeShowFromBank(int x, int y, const u8* bank, int shapeIndex,
                      const ColorBlitTarget16& dst, const ColorFormat& fmt,
                      FrameBlitState& st, bool highColorMode = false);

// gilde.exe 0x559D60 — VIBE_Render_EncodeSpriteDrawFlags (__usercall eax=fn(mode@edx,
//   value@ebx)). Returns 0 when mode==1, else (value<<22)|0x8000000.
int RenderEncodeSpriteDrawFlags(int mode, int value);

// =============================================================================
// WORLD-SPACE SPRITES / BILLBOARDS — gilde.exe 0x5AC970 VIBE_Particle_UpdateBillboards.
//
// The camera-facing 2D-shape-in-3D path. The 3D scene-node walk
// (VIBE_Render_ProcessSceneNode @0x5ADD1C) calls this for every visible node
// that carries a billboard set (smoke/flame/water/flare/icon sprites whose
// quads always face the camera). For each live billboard VERTEX (camera-space
// x/y/z) it performs the perspective project + depth scale that decides WHERE
// and at what apparent size the sprite quad lands on screen, plus a per-vertex
// depth-fade alpha; then it computes per-QUAD back-face/visibility flags from
// the projected winding. The projected screen coords/invZ/alpha it writes are
// what the downstream textured-quad rasteriser (RasterizeMeshList) consumes —
// i.e. this is the "project world point -> screen + scale-by-depth" stage of
// the world sprite, the unscaled-shape blit (ShapeShowFromBank*) being the 2D
// sibling for UI/entity sprites.
//
// THE PROJECTION (per live vertex; the perspective divide IS the depth scale):
//   invZ     = 1.0f / cz                       ; stored at vtx+0x1C
//   screenX  = projScaleX * cx * invZ + centerX ; stored at vtx+0x10
//   screenY  = projScaleY * cy * invZ + centerY ; stored at vtx+0x14
// projScaleX = flt_13FCD0C  projScaleY = flt_13FCAF8  (camera-derived, per frame)
// centerX    = flt_13FCD18  centerY    = flt_13FCD10  (viewport centre offset)
//
// THE DEPTH FADE (only when BillboardParams.depthFade is set):
//   distSq = cx*cx + cy*cy + cz*cz
//   alpha  = (distSq <= fadeMinSq) ? 255
//          : 255 - clamp((sqrt(distSq) - fadeNear) * fadeScale, 0, 255)
//   stored (truncated toward zero) at vtx+0x4F.
// fadeMinSq = flt_13FC544  fadeNear = flt_13FC5AC  fadeScale = flt_13FC58C.
//
// THE NO-FADE / no-billboards paths only refresh screen coords (+invZ) and the
// per-vertex copies at +0x40 (= +0x44) / +0x42 (= (+0x46)>>2), leaving alpha.
//
// THE NODE-LEVEL EFFECT TINT (only when the node's type byte +0x215 >= 5; runs
// AFTER both project arms, BEFORE the quad pass, only on the billboards-enabled
// branch — gilde.exe 0x5ACAB0): broadcasts ONE packed 0x00RRGGBB colour into
// every vertex's colorOut (+0x40), overwriting the per-vertex colorSrc copy the
// project arm wrote. The colour is:
//   nodeType == 8  -> constant 0x1F1FFF (R=0x1F, G=0x1F, B=0xFF — a fixed sky/
//                     glow tint; `mov esi, 1F1FFFh` at 0x5ACAC2).
//   nodeType in 5..7 (i.e. >=5 and !=8) -> packed from the node's three tint
//                     floats, each rounded toward zero to a byte and clamped to
//                     8 bits (VIBE_Coord_ConvertX @0x5C6B08 frndint + fistp + mov
//                     al): R = (u8)trunc(node+0x5C), G = (u8)trunc(node+0x60),
//                     B = (u8)trunc(node+0x64); packed (R<<16)|(G<<8)|B.
// nodeType < 5 -> no tint (the project arm's per-vertex colorSrc copy stands).
// See BillboardEffectTintColor / BillboardApplyEffectTint.
//
// THE PER-QUAD VISIBILITY pass (always, after the vertex pass): for each quad
// with flag bit7 set, if bit4 set -> also set bit6; else compute the signed
// projected area of (v0,v1,v2) and, when front-facing (area test true) and
// quad+0x26 bit2 clear, clear bit7 (cull). See QuadVisibilityPass.
// =============================================================================

// Per-frame camera projection + fade parameters the original kept in globals.
// (flt_13FCD0C / flt_13FCAF8 / flt_13FCD18 / flt_13FCD10 / flt_13FC544 /
//  flt_13FC5AC / flt_13FC58C). Passed explicitly so the math is re-entrant/testable.
struct BillboardParams {
    float projScaleX = 0.0f;  // flt_13FCD0C
    float projScaleY = 0.0f;  // flt_13FCAF8
    float centerX    = 0.0f;  // flt_13FCD18
    float centerY    = 0.0f;  // flt_13FCD10
    float fadeMinSq  = 0.0f;  // flt_13FC544  (distSq at/under which alpha = 255)
    float fadeNear   = 0.0f;  // flt_13FC5AC
    float fadeScale  = 0.0f;  // flt_13FC58C
    bool  enabled    = true;  // byte_649D70 (billboards globally enabled)
    bool  depthFade  = true;  // byte_649DD8 (per-vertex depth fade enabled)
};

// One billboard vertex record (gilde.exe 0x50 = 80 bytes). Modelled as a raw
// 80-byte block with byte-exact accessors so the layout matches the original
// (several fields the original reads/writes overlap a 4-byte colour word at
// +0x40 / +0x44, which a plain struct can't express). The downstream rasteriser
// sees an identical record. Construct via memset(0)+field sets through accessors.
#pragma pack(push, 1)
struct BillboardVertex {
    u8 raw[0x50] = {};

    // typed views into the raw block (byte offsets identical to gilde.exe).
    float& cx()      { return *reinterpret_cast<float*>(raw + 0x00); }  // camera X
    float& cy()      { return *reinterpret_cast<float*>(raw + 0x04); }  // camera Y
    float& cz()      { return *reinterpret_cast<float*>(raw + 0x08); }  // camera Z
    float& screenX() { return *reinterpret_cast<float*>(raw + 0x10); }  // OUT
    float& screenY() { return *reinterpret_cast<float*>(raw + 0x14); }  // OUT
    float& invZ()    { return *reinterpret_cast<float*>(raw + 0x1C); }  // OUT 1/cz
    u32&   colorOut(){ return *reinterpret_cast<u32*>(raw + 0x40); }    // OUT = colorSrc
    u8&    byteOut() { return raw[0x42]; }   // OUT (= byteSrc>>2) [disabled path]
    u32&   colorSrc(){ return *reinterpret_cast<u32*>(raw + 0x44); }    // source colour
    u8&    byteSrc() { return raw[0x46]; }   // source byte (>>2 into byteOut)
    u8&    flags()   { return raw[0x4C]; }   // bit7 = live/visible
    u8&    alphaOut(){ return raw[0x4F]; }   // OUT depth-fade alpha (fade path)

    float screenX_c() const { return *reinterpret_cast<const float*>(raw + 0x10); }
    float screenY_c() const { return *reinterpret_cast<const float*>(raw + 0x14); }
};
#pragma pack(pop)
static_assert(sizeof(BillboardVertex) == 0x50, "BillboardVertex must be 80 bytes");

// One billboard quad record (gilde.exe 0x28 = 40 bytes). v0/v1/v2 point at the
// projected vertices used by the winding (back-face) test. On a 64-bit host the
// vertex pointers are wider than the original's 4-byte fields, so we keep them in
// dedicated members and a separate flags block (the projection routine only
// touches the three vertex ptrs + the two flag bytes; the rest of the record is
// opaque mesh data). Layout is functional, not byte-identical to the 32-bit
// record (pointers differ in width); the flag offsets within `tail` mirror the
// original (flags @ tail+0x18 == record+0x24, flags2 @ tail+0x1A == record+0x26).
struct BillboardQuad {
    BillboardVertex* v0 = nullptr;   // record +0x00
    BillboardVertex* v1 = nullptr;   // record +0x04
    BillboardVertex* v2 = nullptr;   // record +0x08
    u8   flags  = 0;                 // record +0x24  bit7 visible, bit4 -> set bit6
    u8   flags2 = 0;                 // record +0x26  bit2 = never-cull
};

// gilde.exe 0x5AC970 — VIBE_Particle_UpdateBillboards (vertex projection arm).
//   Projects every live vertex of `verts[0..vertCount)` into screen space using
//   `p`, writing screenX/screenY/invZ (+ depth alpha when p.depthFade). Returns
//   nothing; mutates the vertex records in place. `p.enabled==false` reproduces
//   the original's byte_649D70==0 branch (project + byteOut, NO fade, NO alpha).
void ProjectBillboardVertices(BillboardVertex* verts, unsigned vertCount,
                              const BillboardParams& p);

// gilde.exe 0x5AC970 (node-level effect-tint arm @0x5ACAB0). Computes the packed
//   0x00RRGGBB tint colour the original broadcasts into every vertex when the
//   node's type byte (`nodeType`, +0x215) is >= 5. For nodeType == 8 this is the
//   fixed constant 0x1F1FFF; for nodeType in 5..7 it is packed from the node's
//   three tint floats (R = +0x5C, G = +0x60, B = +0x64), each truncated toward
//   zero to a byte. For nodeType < 5 the function returns false (no tint) and
//   `outColor` is left untouched. The float->byte conversion matches the
//   original's VIBE_Coord_ConvertX (frndint truncate-toward-zero) + fistp + the
//   `mov al`/`movzx`/`and 0xFF` 8-bit narrowing.
bool BillboardEffectTintColor(u8 nodeType, float tintR, float tintG, float tintB,
                              u32& outColor);

// gilde.exe 0x5AC970 (effect-tint broadcast loop @0x5ACAC7). When the node's
//   type byte is >= 5, overwrites colorOut (+0x40) of EVERY vertex in
//   `verts[0..vertCount)` with the packed tint from BillboardEffectTintColor
//   (the loop is unconditional over the count — it does NOT test the per-vertex
//   live bit). For nodeType < 5 it is a no-op. Call AFTER ProjectBillboardVertices
//   and BEFORE BillboardQuadVisibilityPass, only on the billboards-enabled path
//   (the original guards the whole arm under byte_649D70).
void BillboardApplyEffectTint(BillboardVertex* verts, unsigned vertCount,
                              u8 nodeType, float tintR, float tintG, float tintB);

// gilde.exe 0x5AC970 (per-quad visibility/back-face arm @0x5ACAE0). For each
//   quad with flag bit7 set: if bit4 set -> set bit6; else compute the signed
//   projected area of (v0,v1,v2) — when it is front-facing and flags2 bit2 is
//   clear, clear bit7 (cull the quad). Mutates the quad flags in place.
void BillboardQuadVisibilityPass(BillboardQuad* quads, unsigned quadCount);

} // namespace guild::render
