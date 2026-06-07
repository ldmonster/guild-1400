#include "render/thumbnail_capture.h"

#include "render/bmp.h"
#include "util/coord.h"        // ConvertX (trunc-toward-zero) — REUSED

#include <cstdio>
#include <cstring>

namespace guild::render {

// gilde.exe word_13CED78 — the engine-wide 160x120 16bpp savegame preview buffer.
// One definition for the whole link (CONVENTIONS ODR): the save/load thumbnail
// (de)serialiser, CaptureScreenThumbnail and BlitThumbnailToSurface share it.
u16* ThumbnailBuffer() {
    static u16 s_thumb[kThumbPixels] = {0};   // 160*120 = 19200 words
    return s_thumb;
}

// flt_62529C — the screen->scratch sample-step factor (1/320 == 0.003125f). The
// original multiplies the screen WIDTH by it for both the X and Y step (v30/v8).
static const float kSampleStep = 0.003125f;   // 0x3B4CCCCD

// gilde.exe 0x56D48C — VIBE_Render_CaptureScreenThumbnail
//
// Original (Hex-Rays, condensed):
//   v30 = (double)SHIWORD(dword_69FFBC) * flt_62529C;      // xStep = screenW/320
//   if ( !VIBE_DecompressState_Blob(dword_62D210, a2) ) return 0;   // surface gate
//   scratch = AllocDebug(0x25800);                          // 320*240*2 bytes
//   v27 = 0.0;                                              // fractional row coord
//   do {                                                    // 240 rows
//     v25 = (int)v27;  v6 = *(stride);  v29 = v6 * v25;     // src row base (pixels)
//     v7 = 0.0;  v8 = v30;                                  // fractional col, step
//     do {                                                  // 320 cols
//       scratch[col] = src16[ (int)v7 + v29 ];              // nearest sample
//       v7 += v8;
//     } while ( col+1 < 320 );
//     ++rowIdx;  v27 += v30;                                // step row by xStep too
//   } while ( rowIdx < 240 );
//   Decompression_Finalize(dword_62D210);
//   // build dst desc v24 = 320x240 16bpp @ scratch, src desc v23 = 120-row,
//   //   320-wide(?) 16bpp @ word_13CED78  -> StretchSurfaceDispatch(v23, v24).
//
// The two zero-init'd descriptors (the unrolled memset(124) blocks) are the
// StretchSurfaceDesc records; the named index writes set:
//   v24: [0]=124 [2]=240(h) [3]=320(w) [4]=640(pitch) [9]=scratch [21]=16(bpp)
//        [22/23/24] = present masks (dword_7626D8/C4/CC)
//   v23: [0]=124 [2]=120(h) [3]=320?? [4]=320(pitch) [9]=word_13CED78 [21]=16
// (the original's v23[3]=v13 is the thumbnail dst width = 160; the dispatch
//  box-averages 320x240 down to 160x120).
bool CaptureScreenThumbnail(const CaptureSource& src) {
    // 0x56d49a: v30 = SHIWORD(dword_69FFBC) * flt_62529C  (sample step, both axes).
    const double step = static_cast<double>(src.screenWidth) * kSampleStep;

    // 0x56d4ab: if ( !VIBE_DecompressState_Blob(dword_62D210, a2) ) return 0;
    // The DecompressState_Blob/Finalize bracket only locks the working surface; the
    // portable gate is "do we have a source surface to read".
    if (!src.pixels)
        return false;

    // 0x56d4b8: scratch = AllocDebug(0x25800)  (320*240 16bpp = 153600 bytes).
    std::vector<u16> scratch(static_cast<std::size_t>(kCapInterW) * kCapInterH, 0);

    // 0x56d4c5..0x56d52f: the nearest-neighbour resample into the 320x240 scratch.
    double rowCoord = 0.0;                              // v27
    for (int row = 0; row < kCapInterH; ++row) {       // v26 < 240
        int srcRow = static_cast<int>(util::ConvertX(rowCoord));   // v25 = (int)v27
        int rowBase = src.stridePx * srcRow;           // v29 = v6 * v25
        double colCoord = 0.0;                          // v7
        for (int col = 0; col < kCapInterW; ++col) {   // v10+1 < 320
            int srcCol = static_cast<int>(util::ConvertX(colCoord));  // v25 = (int)v7
            // scratch[col + 320*row] = src16[ srcCol + stride*srcRow ]
            scratch[static_cast<std::size_t>(col) +
                    static_cast<std::size_t>(kCapInterW) * row] =
                src.pixels[srcCol + rowBase];
            colCoord += step;                           // v7 += v8 (== v30)
        }
        rowCoord += step;                               // v27 = v30 + v27
    }
    // 0x56d535: VIBE_Decompression_Finalize(dword_62D210) — release the surface lock.

    // Channel masks for the descriptors: the original copies the present masks
    // (dword_7626D8/C4/CC) into the scratch desc; if those are zero (static image)
    // derive RGB565 masks from `fmt` so the box-average has a valid format.
    u32 rMask = src.rMask, gMask = src.gMask, bMask = src.bMask;
    if (rMask == 0 && gMask == 0 && bMask == 0) {
        rMask = 0xF800; gMask = 0x07E0; bMask = 0x001F;   // RGB565
    }

    // 0x56d53b..0x56d5b3: dst desc v24 = the 320x240 scratch (src of the dispatch).
    StretchSurfaceDesc scratchDesc{};
    scratchDesc.height = kCapInterH;                   // v24[2] = 240
    scratchDesc.width  = kCapInterW;                   // v24[3] = 320
    scratchDesc.pitch  = 2 * kCapInterW;               // v24[4] = 640 (bytes/row)
    scratchDesc.pixels = reinterpret_cast<u8*>(scratch.data());  // v24[9]
    scratchDesc.bpp    = 16;                            // v24[21] = 16
    scratchDesc.rMask  = rMask;                         // v24[22] = dword_7626D8
    scratchDesc.gMask  = gMask;                         // v24[23] = dword_7626C4
    scratchDesc.bMask  = bMask;                         // v24[24] = dword_7626CC

    // 0x56d5c6..0x56d5fe: src desc v23 = the 160x120 thumbnail (dst of the dispatch).
    // v23[3] (width) = v13 (the thumbnail width 160); v23[2]=120; pitch=2*160=320.
    StretchSurfaceDesc thumbDesc{};
    thumbDesc.height = kThumbHeight;                   // v23[2] = 120
    thumbDesc.width  = kThumbWidth;                    // v23[3] = 160 (v13)
    thumbDesc.pitch  = 2 * kThumbWidth;                // v23[4] = 320 (bytes/row)
    thumbDesc.pixels = reinterpret_cast<u8*>(ThumbnailBuffer());  // v23[9] = word_13CED78
    thumbDesc.bpp    = 16;                              // v23[21] = 16
    thumbDesc.rMask  = rMask;
    thumbDesc.gMask  = gMask;
    thumbDesc.bMask  = bMask;

    // 0x56d603: VIBE_Render_StretchSurfaceDispatch(v23, v24)  (dst=thumb, src=scratch).
    // 320x240 16bpp box-averaged down to 160x120 (StretchAverage16, dst smaller).
    StretchSurfaceDispatch(thumbDesc, scratchDesc);

    // 0x56d610: VIBE_Memory_FreeDebug(scratch)  (RAII on `scratch`).
    return true;
}

// gilde.exe 0x4FF6D4 — VIBE_Render_CaptureScreenshot
//
// Original (Hex-Rays, condensed):
//   SetStatusBanner(byte_620B28);                            // "saving screenshot"
//   RunFrameLoop(dword_11BC2D0, ...);                        // pump one frame
//   v7 = dword_634480++;                                     // screenshot serial
//   sprintf(name, "gamedata/screenshots/gilde%04i.bmp", v7);
//   w = dword_69FFBC>>16; h = (dword_69FFB8+2)>>16;          // screen WxH
//   rgb = AllocDebug(3 * w * h);                             // packed 24-bit RGB
//   VIBE_Render_BeginFrameLock();                            // lock present target
//   VIBE_Surface_CopyRegionRgb(h, w, dword_7626F0, 0,0, rgb);// 16bpp -> 24-bit RGB
//   VIBE_Render_UnlockBackBuffer(status);                    // unlock
//   VIBE_Bmp_Save24Bit(name, w, rgb, h);                     // encode + write
//   FreeDebug(rgb);
//   SetStatusBanner(byte_620B94);                            // restore banner
//
// The banner set + frame pump are GUI/frame leaves (owned elsewhere; the save
// spine pumps the frame before calling). Here we reproduce the capture+encode core
// through the REAL present path: BeginFrameLock -> CopyRegionRgb -> UnlockBackBuffer
// -> Bmp_Save24Bit. CopyRegionRgb's first two args are (rows, cols) = (h, w).
ScreenshotResult CaptureScreenshot(PresentGlobals& g, const ColorFormat& fmt,
                                   int screenWidth, int screenHeight, int* serial) {
    ScreenshotResult out;

    // 0x4ff710: v7 = dword_634480++;  sprintf("...gilde%04i.bmp", v7).
    int n = serial ? *serial : 0;
    if (serial) *serial = n + 1;
    char name[64];
    std::snprintf(name, sizeof name, "gamedata/screenshots/gilde%04i.bmp", n);
    out.path = name;

    const int w = screenWidth;   // dword_69FFBC>>16
    const int h = screenHeight;  // (dword_69FFB8+2)>>16
    if (w <= 0 || h <= 0)
        return out;

    // 0x4ff738: rgb = AllocDebug(3 * w * h)  (packed 24-bit RGB readback buffer).
    out.rgb.assign(static_cast<std::size_t>(3) * w * h, 0);

    // 0x4ff756: VIBE_Render_BeginFrameLock()  — lock the present draw target. The
    // locked 16bpp base is g.targetBase (dword_7626F0); the row stride g.strideWords.
    if (!BeginFrameLock(g) || g.targetBase == 0)
        return out;

    // 0x4ff766: VIBE_Surface_CopyRegionRgb(h, w, dword_7626F0, 0, 0, rgb) — read the
    // locked 16bpp framebuffer back as packed 24-bit RGB (rows=h, cols=w).
    const u16* fb = reinterpret_cast<const u16*>(g.targetBase);
    CopyRegionRgb(static_cast<u32>(h), static_cast<u32>(w), fb,
                  out.rgb.data(), fmt, g);

    // 0x4ff77f: VIBE_Render_UnlockBackBuffer(status).
    UnlockBackBuffer(g, 0);

    // 0x4ff79f: VIBE_Bmp_Save24Bit(name, w, rgb, h) — encode the 24-bit BMP. The io
    // bmp codec returns the encoded bytes; the original streamed them to `name`.
    out.bmp = BmpSave24Bit(w, h, out.rgb.data());
    out.ok = !out.bmp.empty();
    return out;
}

} // namespace guild::render
