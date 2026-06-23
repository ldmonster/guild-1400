#pragma once
#include "guild/common/types.h"
#include <cstdint>

// =============================================================================
// guild::render — recon5 ScaleBlitMip pixel core (gilde.exe ts_texture.c / gfx.c).
//
//   0x5b8d18  VIBE_TextureCache_ScaleBlitMip   (16-bit colour 2x2 box downscale)
//
// This is the bilinear (4-tap) mip downscale that produces one 16-bit native
// destination pixel from a weighted blend of four source samples, then ORs the
// three per-channel native bit-fields together via the runtime channel LUTs.
//
// The channel LUTs (dword_1404260 = R, dword_1404660 = G, dword_1404A60 = B; each
// 256 dwords) are the SAME native-format remap tables built by
// VIBE_Render_BuildChannelLUT (already reconstructed in render/texture_mip.cpp as
// BuildChannelLUT) from the active 16-bit DD surface masks. Here they are inputs:
// the surface/DDraw lock that *produces* the dest pointer is the vendor-coupled
// half (rule 3) and is NOT modelled — the pure pixel arithmetic is.
//
// Strides (engine globals, in BYTES, set by the caller per blit):
//   dword_1404E60  srcRowStride  (a2 advance per output row, "v_src" pitch)
//   dword_1404E64  srcColStride  (per-tap column advance: v9/v12.. += v4)
//   dword_1404E68  weightAdvance  (per-output-col advance of the weight quad set)
//   dword_1404E70  dstRowStride  (a1 advance per output row, in BYTES)
// =============================================================================
namespace guild::render {

using f32 = float;

// 16-bit native channel remap LUTs (R/G/B), each 256 entries. Mirrors
// dword_1404260 / dword_1404660 / dword_1404A60.
struct ChannelLuts {
    const u32* r = nullptr;   // dword_1404260
    const u32* g = nullptr;   // dword_1404660
    const u32* b = nullptr;   // dword_1404A60
};

// Blit stride configuration (BYTES). Mirrors the dword_1404E6x / E70 globals.
struct BlitStrides {
    i32 srcColStride = 0;   // dword_1404E64 (v4) per-tap column advance
    i32 weightAdvance = 0;  // dword_1404E68 advance of the 4 weight rows per out col
    i32 srcRowStride = 0;   // dword_1404E60 a2 advance per output row
    i32 dstRowStride = 0;   // dword_1404E70 a1 advance per output row (bytes)
};

// The four source-sample row cursors (a4[0..3]). Each points at a packed-RGB
// triple (3 bytes: [0]=R,[1]=G,[2]=B) for one tap; advanced by srcColStride per
// output column and by weightAdvance per output row (the general path mutates
// them in place exactly like the original mutates *a4 = a4[0..3]).
struct BlitTaps {
    const u8* tap[4] = {nullptr, nullptr, nullptr, nullptr};
};

// ---------------------------------------------------------------------------
// gilde.exe 0x5b8d18 — VIBE_TextureCache_ScaleBlitMip (__usercall).
// Produces `dim`x`dim` (a3 x a3) destination pixels into `dst` (a1), reading the
// weight quads from `weights` (a2) and the four source taps from `taps` (a4).
//
// Fast path (taps all equal, the original's `a4[1]==*a4 && ...` test): a straight
// per-pixel channel remap of each tap row's RGB triple, advancing by srcColStride.
//
// General path: for each output pixel, read a packed weight dword from `weights`,
// take its four bytes (w0..w3) as the bilinear weights, compute per channel
//   chan = (w0*tap0[c] + w1*tap1[c] + w2*tap2[c] + w3*tap3[c]) >> 8
// and OR the native-remapped channels: lutB[chanB] | lutG[chanG] | lutR[chanR].
// Returns the final `weightAdvance` (the original returns `result`=dword_1404E68).
// ---------------------------------------------------------------------------
i32 ScaleBlitMip(u16* dst, const u32* weights, i32 dim, BlitTaps& taps,
                 const ChannelLuts& luts, const BlitStrides& strides);

} // namespace guild::render
