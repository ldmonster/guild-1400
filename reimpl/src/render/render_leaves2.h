#pragma once
#include "guild/common/types.h"
#include "render/colorformat.h"  // ColorFormat (per-channel pos/prec table)

// =============================================================================
// guild::render — render-math leaves, batch 2 (gilde.exe gfx/light/texture/mesh).
//
// A second slice of self-contained, deterministic VIBE_Render_*/Mesh_*/Light_*/
// Texture_*/Surface_* leaves translated 1:1. Everything here is pure arithmetic
// over caller-supplied surface/record structs — no DDraw/GDI/device state — so it
// is golden-testable.
//
// Cross-module callees that are NOT yet reconstructed (frame surface lock, the
// scene-graph colour walk, the texture-record clone, light-refresh, the vendor
// blit) are routed through an installable RenderLeaves2Hooks struct with inert
// defaults defined in render_leaves2.cpp. Reconstructed callees are reused:
//   guild::render::SetVertexColors (0x428928 VIBE_Mesh_SetVertexColors)
//   guild::util::RandomFloatScaled (0x58b910 VIBE_Math_RandomFloatScaled)
//
// Translated functions:
//   0x435748  VIBE_Render_PutPixel             (frame-target pixel write)
//   0x4351d8  VIBE_Render_DrawHLine            (16bpp Bresenham line)
//   0x423ab8  VIBE_Surface_BlitClipped         (blit-rect origin clip math)
//   0x5dae38  VIBE_Texture_PackColorFlags      (per-texel flag bitfield pack)
//   0x4289f0  VIBE_Mesh_SetVertexColorRgb      (reorder -> SetVertexColors)
//   0x428a10  VIBE_Mesh_SetGlobalColorTemp     (per-node colour walk)
//   0x5d329c  VIBE_Mesh_GetBoundingRadius      (mesh +468 accessor)
//   0x4b24b0  VIBE_Light_SetSunHeight          (RNG-scaled sun pitch)
//   0x43ea0c  VIBE_Light_SetGlobalDirection    (store global light dir + refresh)
//   0x5db694  VIBE_Texture_SetTransparencyFlag (record +104 transparency bit)
//   0x5dbde0  VIBE_Texture_CloneIfPaletteMatch (palette-match clone predicate)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Frame render target as accessed by VIBE_Render_PutPixel / VIBE_Render_DrawHLine.
// The originals read a bank of present-state globals; we gather the deterministic
// pixel-math ones into a struct:
//   dword_7626E0 width        cy/dword_7626DC height
//   dword_7626F0 pixel base   dword_7626F8 pitch(16/32 path, pixels)
//   dword_7626E8 byteStride   dword_7626FC pitchBytes(24bpp path)
//   dword_62D590 bitsPerPixel
//   byte_762719..76271E       per-channel pos/prec (the ColorFormat below)
// ---------------------------------------------------------------------------
struct FrameTarget {
    u8*  base       = nullptr;  // dword_7626F0  (0 => writes skipped)
    i32  width      = 0;        // dword_7626E0
    i32  height     = 0;        // cy / dword_7626DC
    i32  pitch      = 0;        // dword_7626F8 (pixels per row for 16/32)
    i32  byteStride = 0;        // dword_7626E8 (24bpp byte stride per pixel)
    i32  pitchBytes = 0;        // dword_7626FC (24bpp byte stride per row)
    i32  bpp        = 0;        // dword_62D590
    ColorFormat fmt;            // byte_762719..76271E
};

// gilde.exe 0x435748 — VIBE_Render_PutPixel (__userpurge: x@eax, y@edx, ctx@ecx,
//   r@bl, g via LOBYTE(ctx), b@stack). Clip-tests x/y against [0,width)/[0,height),
//   packs the colour from the per-channel pos/prec table, then writes per
//   bitsPerPixel (8 grey / 15,16 / 24 / 32). The original first calls
//   VIBE_Render_BeginFrameLock(ctx) (routed through the hooks); a false return
//   aborts (returns 0). The green channel arrives as ctx's low byte in the
//   original; we pass it explicitly as `g`. Out-of-bounds returns the input x.
i32 PutPixel(FrameTarget& t, i32 x, i32 y, void* ctx, u8 g, u8 r, u8 b);

// gilde.exe 0x4351d8 — VIBE_Render_DrawHLine (__userpurge: x0@eax, y0@edx, y1@ecx,
//   x1@ebx, color@stack). A 16bpp integer line raster: horizontal run when
//   y0==y1, vertical line when x0==x1, else a Bresenham line. Writes 16-bit
//   `color` into base[2*(y*pitch+x)]. Returns the final running pixel index.
i32 DrawHLine(FrameTarget& t, i32 x0, i32 y0, i32 y1, i32 x1, u16 color);

// ---------------------------------------------------------------------------
// Blit-rectangle clip math (VIBE_Surface_BlitClipped, 0x423ab8). The original
// clamps a dest origin (a1=dstX, a2=dstY), span (a4=w, a3=h) and a source origin
// (a6=srcX, a7=srcY) against zero, then issues a vendor Blt through *(a8+32). We
// expose the pure clip math; the vendor call is routed through the blit hook.
// ---------------------------------------------------------------------------
struct BlitClipRect {
    i32 dstX, dstY, w, h;   // clamped dest rect (x,y,w,h)
    i32 srcX, srcY;         // clamped source origin
    bool issued;            // true if a non-degenerate blit was issued
};

// gilde.exe 0x423ab8 — VIBE_Surface_BlitClipped. Returns 0 if the clamped span
// collapses (w==0 || h==0), else 1. `out` receives the clamped rectangle. When
// `srcSurface` (a5) is non-null and the blit is non-degenerate, the blit hook is
// invoked with the recovered dest/src rects [x0,y0,x1,y1].
i32 BlitClipped(BlitClipRect& out, i32 dstX, i32 dstY, i32 h, i32 w,
                void* srcSurface, i32 srcX, i32 srcY, void* dstCtx);

// gilde.exe 0x5dae38 — VIBE_Texture_PackColorFlags (__usercall: rec@eax, out0@edx,
//   out1@ebx). Pure bitfield repack of a texture record's flag bytes (rec[104..106],
//   rec[114]) into two output bytes. No-op when any pointer is null.
//   *out0 = (((4*rec[104])>>7)<<6) | (4*(rec[114]&0xF)) | (rec[104]&1)
//                                   | (2*((8*rec[104])>>7))
//   *out1 = (16*(rec[105]&0x1F)) | (rec[106]&0x1F)
void PackColorFlags(const u8* rec, u8* out0, u8* out1);

// gilde.exe 0x4289f0 — VIBE_Mesh_SetVertexColorRgb (__usercall: obj@eax, rgb@edx).
//   Thin shim: SetVertexColors(obj, b=rgb[0], g=rgb[2], r=rgb[1]). Returns 1.
//   Reuses the reconstructed SetVertexColors.
i8 SetVertexColorRgb(void* obj, const u8* rgb);

// gilde.exe 0x5d329c — VIBE_Mesh_GetBoundingRadius (__fastcall: a1, obj).
//   Returns *(float*)(*(void**)(obj+16) + 468) when obj and obj+16 are non-null,
//   else 0. `obj` is a node whose +16 is the mesh-asset pointer.
double GetBoundingRadius(const void* obj);

// ---------------------------------------------------------------------------
// Cross-module hooks (unreconstructed callees) + recovered globals.
// ---------------------------------------------------------------------------
struct RenderLeaves2Hooks {
    // VIBE_Render_BeginFrameLock(0x434508): lock the frame surface for ctx.
    // Default: returns true so pure-math tests proceed.
    bool (*beginFrameLock)(void* ctx) = nullptr;

    // VIBE_Light_RefreshAllObjects(0x5c886c): re-light all objects after the
    // global light direction changes. Default: no-op.
    void (*refreshAllObjects)(u32 mode) = nullptr;

    // VIBE_Texture_CloneRecord(0x5dbaa0): clone `rec` under palette `pal`.
    // Default: returns rec unchanged.
    void* (*cloneRecord)(void* rec, i8 pal, i8 a3) = nullptr;

    // VIBE_Surface_BlitClipped's vendor blit (*(a8+32)->+20). Default: no-op.
    void (*blit)(void* dstCtx, void* srcSurface,
                 const i32 dstRect[4], const i32 srcRect[4]) = nullptr;

    // VIBE_SceneGraph_WalkAndInvoke(0x5ac738): walk a node tree, invoking the
    // per-node callback (here SetVertexColorRgb) with the 3-byte colour payload.
    // Returns the walk result byte. Default: returns 1 (no nodes visited).
    i8 (*walkAndInvoke)(void* table, void* root, void* callback,
                        int mask, const u8* payload) = nullptr;
};
void InstallRenderLeaves2Hooks(const RenderLeaves2Hooks& hooks);
const RenderLeaves2Hooks& GetRenderLeaves2Hooks();

// Global light direction (gilde.exe flt_64A074/78/7C). Stored once here.
struct GlobalLightDir { float x, y, z; };
GlobalLightDir& GlobalLight();

// gilde.exe 0x428a10 — VIBE_Mesh_SetGlobalColorTemp (__usercall: obj@eax, b@dl,
//   g@cl, r@bl). Packs a 3-byte payload {b, r, g} (payload[0]=b, [1]=r, [2]=g),
//   then walks the object's scene-graph node tree invoking SetVertexColorRgb on
//   each (via WalkAndInvoke hook, table off_649D64, mask 511). When obj+528 bit0
//   is clear it temporarily zeroes obj+496 around the walk and restores it.
//   No-op (returns obj's low byte unchanged) when obj is null.
i8 SetGlobalColorTemp(void* obj, u8 b, u8 g, u8 r);

// gilde.exe 0x4b24b0 — VIBE_Light_SetSunHeight (__fastcall: light, raise).
//   raise : pitch = -0.3 - RandomFloatScaled()*0.6   (recovered dbl_61DD40/38)
//   else  : pitch =  0.3 + RandomFloatScaled()*0.6   (recovered dbl_61DD48/38)
//   When `sunPitchSlot` (the sun object's +420 float, reached via light+488) is
//   non-null, stores pitch there. Returns 1.
i8 SetSunHeight(float* sunPitchSlot, int raise);

// gilde.exe 0x43ea0c — VIBE_Light_SetGlobalDirection (__usercall: x@eax, y@edx,
//   z@ebx; each an int* whose value is cast to float). Stores into the global
//   light dir then calls RefreshAllObjects(1). Returns 1.
i32 SetGlobalDirection(const i32* x, const i32* y, const i32* z);

// gilde.exe 0x5db694 — VIBE_Texture_SetTransparencyFlag (__usercall: rec@eax,
//   on@dl, a3@edi). Sets/clears bit 2 (0x04) of rec[104] to `on&1`. The original
//   also redirects clones to their root and short-circuits on device frame-state
//   globals + a release call (device state, omitted); this faithful core does the
//   bit math. Returns 1 if the bit changed, else 0. No-op returning 0 if rec null.
i8 SetTransparencyFlag(u8* rec, i8 on);

// gilde.exe 0x5dbde0 — VIBE_Texture_CloneIfPaletteMatch (__usercall: rec@eax,
//   pal@dl, a3@bl). If rec[108]==0xFF and (rec[110]&1)==0 and (pal!=-1 or
//   (rec[110]&1)!=0), clone via the hook. Returns the clone pointer, else `rec`.
void* CloneIfPaletteMatch(void* rec, i8 pal, i8 a3);

} // namespace guild::render
