#include "render/paintbox_shape.h"
#include <cstddef>

// =============================================================================
// guild::render — Paintbox / Surface shape-blit (implementation). The colour
// (un)packing goes through PackColor/UnpackColor (render/colorformat); the
// originals used VIBE_Result_Handler_Final (pack) / VIBE_Render_UnpackColor.
// =============================================================================
namespace guild::render {

// gilde.exe 0x422F80 — VIBE_Surface_BlitPaletteToPixels.
// First a 256-entry LUT is built: LUT[i] = Pack(pal[4i+0], pal[4i+1], pal[4i+2]).
// (The original stores it at v16[i+1] and reads v16[idx+1]; the +1 cancels — see
// the disasm at 0x422FB4 where esi pre-increments by 2.) Then each source index
// is written as LUT[idx] into the framebuffer at (x, y) with stride fbWidth.
void Surface_BlitPaletteToPixels(u16* dstFb, int fbWidth,
                                 const u8* src, int width, int height,
                                 const u8* palette, const ColorFormat& fmt) {
    u16 lut[256];
    const u8* p = palette;
    for (int i = 0; i < 256; ++i) {
        lut[i] = (u16)PackColor(fmt, p[0], p[1], p[2]);   // R,G,B (X at p[3])
        p += 4;
    }
    for (int y = 0; y < height; ++y) {
        const u8* srcRow = src + (size_t)width * y;       // v19 += a2 (width)
        u16* dstRow = dstFb + (size_t)y * fbWidth;
        for (int x = 0; x < width; ++x)
            dstRow[x] = lut[srcRow[x]];
    }
}

// gilde.exe 0x422EE4 — VIBE_Surface_BlitRgbToPixels.
void Surface_BlitRgbToPixels(Surface* surf, const u8* src, int width, int height) {
    if (!surf)
        return;
    int stride = surf->widthPx;                           // *(a6+16)
    u16* pixels = (u16*)surf->pixels;                     // *(a6+28)
    for (int y = 0; y < height; ++y) {
        const u8* srcRow = src + (size_t)3 * width * y;   // v11 += 3*a2
        for (int x = 0; x < width; ++x) {
            u8 r = srcRow[3 * x + 0];
            u8 g = srcRow[3 * x + 1];
            u8 b = srcRow[3 * x + 2];
            pixels[(size_t)y * stride + x] = (u16)PackColor(surf->fmt, r, g, b);
        }
    }
}

// gilde.exe 0x423050 — VIBE_Surface_CopyRegionRgb.
void Surface_CopyRegionRgb(const u16* srcFb, int fbWidth, u8* dstRgb,
                           int width, int height, const ColorFormat& fmt) {
    for (int y = 0; y < height; ++y) {
        u8* dstRow = dstRgb + (size_t)3 * width * y;      // a6 + 3*(x + a2*y)
        for (int x = 0; x < width; ++x) {
            u16 pix = srcFb[(size_t)y * fbWidth + x];     // a3 + 2*(x + fbWidth*y)
            u8 r, g, b;
            UnpackColor(fmt, pix, r, g, b);
            // Faithful byte order from the original call: UnpackColor writes
            //   r -> dst+0 (edx), g -> dst+2 (ecx=v8), b -> dst+1 (ebx=v7).
            // i.e. the dest triple is stored R, B, G (an intentional quirk).
            dstRow[3 * x + 0] = r;
            dstRow[3 * x + 1] = b;
            dstRow[3 * x + 2] = g;
        }
    }
}

} // namespace guild::render
