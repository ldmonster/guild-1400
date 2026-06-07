#pragma once
#include "guild/common/types.h"
#include "render/surface_stretch.h"  // StretchSurfaceDesc (shared raw-surface record)

// =============================================================================
// guild::render — render-math leaves (gilde.exe gfx.c).
//
// Self-contained, deterministic pixel/colour/flag math translated 1:1 from the
// VIBE_Render_* leaves. Everything here is pure arithmetic over caller-supplied
// buffers/descriptors — no DDraw, GDI, or device state — so it is golden-testable.
//
// Translated functions:
//   0x4359b0  VIBE_Render_PackColorToPixel    (mask-driven 8-8-8 -> native pack)
//   0x432770  VIBE_Render_DepthToModeFlag     (bit-depth -> mode-caps bit)
//   0x436078  VIBE_Render_StretchAverage24    (24bpp BGR box-average down-sample)
//   0x436228  VIBE_Render_StretchAverage32    (32bpp box-average down-sample)
//   0x436504  VIBE_Render_StretchInterpolate16(16bpp bilinear up-sample)
//   0x436aa4  VIBE_Render_StretchInterpolate24(24bpp BGR bilinear up-sample)
//   0x436f30  VIBE_Render_StretchInterpolate32(32bpp bilinear up-sample)
//
// The Average/Interpolate routines operate on the same StretchSurfaceDesc record
// as src/render/surface_stretch.* (recovered DDSURFACEDESC-ish layout); see that
// header for the field offsets. `eax` is the *destination*, `edx` the *source*.
// =============================================================================
namespace guild::render {

// gilde.exe 0x4359b0 — VIBE_Render_PackColorToPixel
//   __usercall (value@eax, maskHi@edx, mask0@ecx, mask1@ebx). The original recovers
//   per-channel field-position/precision from each mask via ComputeChannelShifts,
//   then re-deposits `value`'s channels into a 0x00HH1100-style native triple:
//     byte2 = (value & maskHi) >> pos(maskHi) << prec(maskHi)   (HIWORD low byte)
//     byte1 = (value & mask1)  >> pos(mask1)  << prec(mask1)
//     byte0 = (value & mask0)  >> pos(mask0)  << prec(mask0)
//   where pos = trailing-zero count and prec = 8 - set-bit count of the mask.
i32 PackColorToPixel(i32 value, u32 maskHi, u32 mask0, u32 mask1);

// gilde.exe 0x432770 — VIBE_Render_DepthToModeFlag
//   Maps a colour bit-depth to the engine's mode-capability bit. 8->2048,
//   16->1024, 24->512, 32->256, else 0.
i32 DepthToModeFlag(i32 depthBits);

// gilde.exe 0x436078 — VIBE_Render_StretchAverage24
//   24bpp (B,G,R triplet) box-average down-sample. eax=dst, edx=src.
u8* StretchAverage24(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x436228 — VIBE_Render_StretchAverage32
//   32bpp per-channel (mask-decoded) box-average down-sample. eax=dst, edx=src.
i32* StretchAverage32(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x436504 — VIBE_Render_StretchInterpolate16
//   16bpp bilinear up-sample. eax=dst, edx=src.
u16* StretchInterpolate16(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x436aa4 — VIBE_Render_StretchInterpolate24
//   24bpp (B,G,R) bilinear up-sample. eax=dst, edx=src.
u8* StretchInterpolate24(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x436f30 — VIBE_Render_StretchInterpolate32
//   32bpp per-channel (mask-decoded) bilinear up-sample. eax=dst, edx=src.
i32* StretchInterpolate32(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

} // namespace guild::render
