#pragma once
// =============================================================================
// guild::render — savegame-thumbnail capture + screenshot grab (gilde.exe gfx.c).
//
// The top of the "grab what's on screen and shrink/encode it" path. Two captures:
//
//   0x56D48C  VIBE_Render_CaptureScreenThumbnail — resample the live working
//             surface down to the fixed 160x120 16bpp savegame preview that lives
//             in the global thumbnail buffer (word_13CED78). Two stages, exactly
//             as the original: nearest-sample the screen into a 320x240 16bpp
//             scratch, then box-average (StretchSurfaceDispatch -> StretchAverage16)
//             that scratch down to 160x120 into word_13CED78.
//   0x4FF6D4  VIBE_Render_CaptureScreenshot — pump one frame, lock the present
//             back buffer (BeginFrameLock), read the locked 16bpp framebuffer back
//             as packed 24-bit RGB (Surface_CopyRegionRgb), unlock, then encode a
//             24-bit BMP (Bmp_Save24Bit) named "gilde%04i.bmp". This routes the
//             REAL present path: render/surface_present.{Begin,Unlock} + CopyRegionRgb.
//
// The 160x120 16bpp savegame thumbnail buffer (gilde.exe word_13CED78, 0x9600 =
// 38400 bytes = 160*120 u16) is OWNED here — one definition for the whole engine
// (the save_drivers thumbnail write/read and CaptureScreenThumbnail all read/write
// it). It is exposed via ThumbnailBuffer() so the io thumbnail (de)serialiser and
// the capture share the same storage (CONVENTIONS ODR: a recovered game global
// lives once, in its owning module).
//
// The runtime surface bases/strides/masks (dword_62D210 working surface, the
// present framebuffer dword_7626F0 / screen dims dword_69FFB8/BC) were process
// globals filled at display-mode init; here they are passed in explicitly through
// small descriptor structs so the captures are re-entrant and testable headless.
// =============================================================================
#include "guild/common/types.h"
#include "render/colorformat.h"
#include "render/surface_present.h"
#include "render/surface_stretch.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::render {

// The fixed savegame-thumbnail geometry the original hard-codes everywhere.
constexpr int kThumbWidth  = 160;   // v23[3]/v23[? ] thumbnail width
constexpr int kThumbHeight = 120;   // v23[2] thumbnail height
constexpr int kThumbPixels = kThumbWidth * kThumbHeight;         // 19200
// The 320x240 16bpp intermediate the screen is nearest-sampled into first.
constexpr int kCapInterW   = 320;   // v24[3] / inner loop bound (< 320)
constexpr int kCapInterH   = 240;   // v24[2] / outer loop bound (< 240)

// gilde.exe word_13CED78 — the engine-wide 160x120 16bpp savegame preview buffer.
// CaptureScreenThumbnail writes it; the save/load thumbnail (de)serialiser and
// BlitThumbnailToSurface read it. One owner for the whole link.
u16* ThumbnailBuffer();

// ---------------------------------------------------------------------------
// The live working surface CaptureScreenThumbnail samples from. The original read
// it off the decompressed-state record dword_62D210: pixel base at +28, the row
// stride (pixels) at +16. Modelled explicitly here.
// ---------------------------------------------------------------------------
struct CaptureSource {
    const u16* pixels = nullptr;  // *(dword_62D210 + 28)  16bpp base
    i32 stridePx = 0;             // *(dword_62D210 + 16)  pixels per row
    // The on-screen size that maps onto the 320x240 scratch. screenWidth>>16 and
    // screenHeight>>16 in the original (dword_69FFBC / dword_69FFB8+2). The X/Y
    // sample step is screenW * (1/320) and screenH * (1/240) — but the original
    // uses the SAME flt_62529C (=1/320) factor for both axes (v30), so both steps
    // are derived from screenWidth/320 and screenHeight is folded via the 320x240
    // scratch dimensions. We keep the original's exact arithmetic (see .cpp).
    i32 screenWidth  = 0;         // SHIWORD(dword_69FFBC)
    i32 screenHeight = 0;         // SHIWORD(dword_69FFB8+2)
    ColorFormat fmt{5, 2, 3, 0, 11, 3};  // scratch + thumbnail pixel format
    // The present-format channel masks the original copies into the 320x240 scratch
    // descriptor (dword_7626D8/C4/CC). Default 0 (static image) => derived from fmt.
    u32 rMask = 0; u32 gMask = 0; u32 bMask = 0;
};

// gilde.exe 0x56D48C — VIBE_Render_CaptureScreenThumbnail.
//   Stage 1: alloc a 320x240 16bpp scratch (0x25800 bytes); for each of 240 rows,
//            for each of 320 cols, sample src[ (int)(col*xStep) + stride*(int)(row*
//            yStep) ] into the scratch (nearest-neighbour, trunc-toward-zero on the
//            fractional source coords — the original's Coord_ConvertX idiom).
//   Stage 2: build two StretchSurfaceDesc (scratch 320x240 -> thumbnail 160x120),
//            both 16bpp, and StretchSurfaceDispatch (box-average down-sample) into
//            word_13CED78. Returns true on success (false if `src.pixels` is null,
//            mirroring the original's `if (DecompressState_Blob(...))` gate).
bool CaptureScreenThumbnail(const CaptureSource& src);

// ---------------------------------------------------------------------------
// The present framebuffer CaptureScreenshot reads back. After BeginFrameLock the
// engine's draw base is g.targetBase (dword_7626F0) with stride g.strideWords; the
// screen size comes from the same dword_69FFB8/BC. Modelled explicitly so the lock
// goes through the real surface_present globals.
// ---------------------------------------------------------------------------
struct ScreenshotResult {
    std::vector<u8>  rgb;     // width*height*3 packed RGB (CopyRegionRgb output)
    std::vector<u8>  bmp;     // the encoded 24-bit BMP bytes (Bmp_Save24Bit)
    std::string      path;    // "gamedata/screenshots/gilde%04i.bmp"
    bool ok = false;
};

// gilde.exe 0x4FF6D4 — VIBE_Render_CaptureScreenshot.
//   The original: SetStatusBanner("saving"); RunFrameLoop; format the gildeNNNN.bmp
//   name (dword_634480++); alloc a w*h*3 RGB buffer; BeginFrameLock; CopyRegionRgb
//   the locked 16bpp framebuffer into it; UnlockBackBuffer; Bmp_Save24Bit; free;
//   SetStatusBanner("done"). Here the banner/frame-pump are GUI/frame leaves owned
//   elsewhere; this reproduces the capture+encode core through the REAL present
//   path. `g` is the live PresentGlobals (its primary/ppvBits already set up); `fmt`
//   the present pixel format. `serial` is dword_634480 (the screenshot counter); it
//   is post-incremented (returned via *serial). Returns the capture + encoded BMP.
ScreenshotResult CaptureScreenshot(PresentGlobals& g, const ColorFormat& fmt,
                                   int screenWidth, int screenHeight, int* serial);

} // namespace guild::render
