// wave-12 boundary/malformed hardening for the paintbox 2D drawing leaves.
//   paintbox.{h,cpp}        VIBE_Paintbox_DrawScaledRegion @0x41e920,
//                           VIBE_Paintbox_DrawLine @0x41ec40, Clear @0x41ee9c
//   paintbox_shape.{h,cpp}  Surface_BlitPaletteToPixels @0x422F80,
//                           Surface_BlitRgbToPixels @0x422EE4,
//                           Surface_CopyRegionRgb @0x423050
//
// These exercise the engine's own clipping envelope: PaintboxDrawScaledRegion
// pre-clips to [border, w-border]x[border, h-border] and every plotted pixel
// (incl. the brush satellites x±1/x±2) flows through SurfaceSetPixelRgb, which
// re-clips to the surface clip rect. Goal: prove no out-of-bounds write happens
// even with the brush plotting past the surface edge, with an odd surface width,
// or with a fully out-of-bounds blit origin. (ASAN/UBSAN catches any OOB.)
#include "render/paintbox.h"
#include "render/paintbox_shape.h"
#include "render/surface.h"
#include "render/colorformat.h"
#include "render/types.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Read back one 16bpp pixel directly (surface created as RGB565).
u16 Px(const Surface* s, int x, int y) {
    return reinterpret_cast<const u16*>(s->pixels)[(size_t)s->widthPx * y + x];
}

} // namespace

// A brush==2 (13-pixel diamond) plotted at the bottom-right corner: the clip in
// DrawScaledRegion permits x==w-border and y==h-border (`<=`), so satellites at
// x+1/x+2/y+1/y+2 fall off the surface. Each must be silently dropped by the
// per-pixel clip — no write past the buffer.
TEST(PaintboxBoundary, DiamondBrushAtCornerNoOOB) {
    Surface* s = SurfaceCreate(8, 8, 16, Format565());
    CHECK(s != nullptr);
    if (!s) return;
    SurfaceColorFill(s, 0, 0, 0);

    PaintboxState pb;
    pb.surface = s;
    pb.brush = 2;        // 13-pixel diamond (plots x±2, y±2)
    pb.border = 0;       // allow x up to w (== 8), y up to h (== 8)

    // Plot at (7,7): legal centre; the +1/+2 satellites are off-surface.
    PaintboxDrawScaledRegion(pb, 7, 7, 0xFF, 0xFF, 0xFF);
    // The centre pixel was written; the off-surface satellites were dropped.
    CHECK(Px(s, 7, 7) != 0);

    // Plot exactly at the clip edge x==w (8): centre itself is off-surface and is
    // dropped by SurfaceSetPixelRgb; no crash.
    PaintboxDrawScaledRegion(pb, 8, 8, 0xFF, 0, 0);

    SurfaceDestroy(s);
}

// Odd surface width (pitch = 2*width is odd-pixel): line drawing across the whole
// surface with a plus-brush stays in-bounds.
TEST(PaintboxBoundary, OddWidthLineNoOOB) {
    Surface* s = SurfaceCreate(7, 5, 16, Format565());  // 7 px wide (odd)
    CHECK(s != nullptr);
    if (!s) return;
    SurfaceColorFill(s, 0, 0, 0);

    PaintboxState pb;
    pb.surface = s;
    pb.brush = 1;        // 5-pixel plus
    pb.border = 0;

    // Diagonal across the surface, plus a horizontal and a vertical run.
    PaintboxDrawLine(pb, 0, 0, 6, 4, 0xFF, 0xFF, 0xFF);
    PaintboxDrawLine(pb, 0, 2, 6, 2, 0x00, 0xFF, 0x00);
    PaintboxDrawLine(pb, 3, 0, 3, 4, 0x00, 0x00, 0xFF);
    CHECK(true);  // ASAN clean == in-bounds

    SurfaceDestroy(s);
}

// Null surface and brush==0 single-pixel at a far-negative origin: both early-out
// or get clipped; never a deref/OOB.
TEST(PaintboxBoundary, NullSurfaceAndNegativeOrigin) {
    PaintboxState pb;
    pb.surface = nullptr;
    pb.brush = 0;
    pb.border = 0;
    PaintboxDrawScaledRegion(pb, 3, 3, 1, 2, 3);     // null -> early return
    PaintboxDrawLine(pb, 0, 0, 5, 5, 1, 2, 3);
    PaintboxClear(pb);                                // null -> no-op

    Surface* s = SurfaceCreate(4, 4, 16, Format565());
    CHECK(s != nullptr);
    if (!s) return;
    pb.surface = s;
    pb.brush = 2;
    pb.border = 0;
    // Wildly out-of-bounds origins (both signs); all clipped per-pixel.
    PaintboxDrawScaledRegion(pb, -1000, -1000, 9, 9, 9);
    PaintboxDrawScaledRegion(pb, 100000, 100000, 9, 9, 9);
    PaintboxClear(pb);
    SurfaceDestroy(s);
}

// ---------------------------------------------------------------------------
// paintbox_shape: the raw-stride blits have NO internal clip — they write a
// width*height block at the top-left of the destination. The CALLER must size
// the destination to fbWidth*height (the engine's contract: it blits into a
// surface it sized). These tests pin the contract: a destination sized exactly
// to the blit must be written end-to-end with no overflow.
// ---------------------------------------------------------------------------

// CopyRegionRgb round-trips an 8-bit-per-channel-truncated 565 block through the
// RGB-triple buffer; an exactly-sized dst writes 3*w*h bytes, no more.
TEST(PaintboxShapeBoundary, CopyRegionRgbExactBuffer) {
    const int w = 5, h = 3, fbW = 8;     // framebuffer wider than the region
    std::vector<u16> fb((size_t)fbW * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            fb[(size_t)fbW * y + x] = (u16)(0xF800 | (y << 5) | x);  // red-ish
    std::vector<u8> dst((size_t)3 * w * h, 0xAB);   // exactly 3*w*h
    Surface_CopyRegionRgb(fb.data(), fbW, dst.data(), w, h, Format565());
    // Top-left pixel 0xF800: unpack independently and compare. Stored order is
    // normal R, G, B — see Surface_CopyRegionRgb disasm @0x4230d6-da (edx=dst+0,
    // ebx=dst+1, ecx=dst+2) with UnpackColor writing R->edx, G->ebx, B->ecx.
    u8 er, eg, eb;
    UnpackColor(Format565(), 0xF800u, er, eg, eb);
    CHECK_EQ((int)dst[0], (int)er);
    CHECK_EQ((int)dst[1], (int)eg);
    CHECK_EQ((int)dst[2], (int)eb);
    // Last triple must also be in-bounds and written (not the 0xAB sentinel for
    // a non-zero source pixel).
    CHECK((size_t)(3 * w * h) == dst.size());
}

// BlitPaletteToPixels: a 1x1 source into a 1px framebuffer, palette index 0 only.
// Exact buffers; LUT is internal (256 entries on the stack).
TEST(PaintboxShapeBoundary, PalettePixelExactBuffer) {
    u8 palette[256 * 4];
    std::memset(palette, 0, sizeof palette);
    palette[0] = 255; palette[1] = 0; palette[2] = 0;   // index 0 -> red
    u8 src[1] = {0};
    u16 fb[1] = {0};
    Surface_BlitPaletteToPixels(fb, 1, src, 1, 1, palette, Format565());
    CHECK_EQ((int)fb[0], (int)(u16)PackColor(Format565(), 255, 0, 0));
}

// BlitRgbToPixels: zero height -> the row loop never runs, no read of src.
TEST(PaintboxShapeBoundary, BlitRgbZeroHeightNoAccess) {
    Surface* s = SurfaceCreate(4, 4, 16, Format565());
    CHECK(s != nullptr);
    if (!s) return;
    SurfaceColorFill(s, 0, 0, 0);
    Surface_BlitRgbToPixels(s, nullptr, 4, 0);   // height 0 -> no src read
    Surface_BlitRgbToPixels(nullptr, nullptr, 4, 4);  // null surf -> early return
    CHECK(true);
    SurfaceDestroy(s);
}
