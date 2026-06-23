#include "test.h"

// Unit tests — the reconstructed VIBE_Quant_* 24-bit palettizer
// (render/texture_palettize.h; gilde.exe 0x6029f0/0x602aa4/0x602d2c/0x603068/
// 0x603180/0x6033b4/0x603a30/0x602a70). Golden vectors derived from the
// algorithm's pinned-down structure:
//
//   * the histogram buckets every channel to 5 bits; leaf sums accumulate the
//     BUCKET CENTRE ((c & 0xF8) + 4), so a solid colour yields exactly its
//     bucket centre as the palette entry;
//   * TreeCollectPalette recurses children bit 7 FIRST (0x6031e1), so the
//     palette order over {black, white} is white(0), black(1);
//   * the dither arm is serpentine Floyd–Steinberg in 16ths with a +-20
//     per-channel error clamp — a regression vector pins it byte-for-byte.
#include "render/texture_bin.h"
#include "render/texture_palettize.h"

#include <cstdio>
#include <vector>

using namespace guild;

namespace {

// Run QuantBuildPalette over a raw RGB triple buffer.
struct QOut {
    std::vector<u8> idx;
    u8 pal[768];
};
QOut Quant(const std::vector<u8>& rgb, i32 w, i32 h, i32 dither) {
    QOut o;
    o.idx.assign((std::size_t)w * h, 0xAA);
    CHECK(render::QuantBuildPalette(rgb.data(), w, h, 256, dither, o.idx.data(),
                                    o.pal));
    return o;
}

} // namespace

TEST(RenderTexturePalettize, SolidColorYieldsBucketCenter) {
    // 4x4 solid (0,200,0): one occupied histogram cell -> one leaf; palette
    // entry = ((c&0xF8)+4) per channel = (4, 204, 4); every index 0.
    std::vector<u8> rgb;
    for (int i = 0; i < 16; ++i) {
        rgb.push_back(0);
        rgb.push_back(200);
        rgb.push_back(0);
    }
    QOut o = Quant(rgb, 4, 4, /*dither=*/1);
    CHECK_EQ((int)o.pal[0], 4);          // planar R[0]
    CHECK_EQ((int)o.pal[256], 204);      // planar G[0]
    CHECK_EQ((int)o.pal[512], 4);        // planar B[0]
    for (int i = 0; i < 16; ++i) CHECK_EQ((int)o.idx[(std::size_t)i], 0);
}

TEST(RenderTexturePalettize, CollectOrderIsHighChildFirst) {
    // {black, white} image: TreeCollectPalette walks child bit 7 before bit 0
    // (0x6031e1 "v5 = 7; ... --v5"), so WHITE (the all-ones code) collects
    // first: palette[0] = (252,252,252), palette[1] = (4,4,4).
    std::vector<u8> rgb;
    for (int i = 0; i < 8; ++i) {        // top row(s) white, bottom black
        rgb.push_back(255); rgb.push_back(255); rgb.push_back(255);
    }
    for (int i = 0; i < 8; ++i) {
        rgb.push_back(0); rgb.push_back(0); rgb.push_back(0);
    }
    QOut o = Quant(rgb, 4, 4, /*dither=*/1);
    CHECK_EQ((int)o.pal[0], 252);
    CHECK_EQ((int)o.pal[256], 252);
    CHECK_EQ((int)o.pal[512], 252);
    CHECK_EQ((int)o.pal[1], 4);
    CHECK_EQ((int)o.pal[257], 4);
    CHECK_EQ((int)o.pal[513], 4);
    for (int i = 0; i < 8; ++i)  CHECK_EQ((int)o.idx[(std::size_t)i], 0);  // white
    for (int i = 8; i < 16; ++i) CHECK_EQ((int)o.idx[(std::size_t)i], 1);  // black
}

TEST(RenderTexturePalettize, UnditheredArmMapsThroughHistogramTable) {
    // dither=0 — the 0x6033dc arm: per-occupied-cell closest-colour table.
    // Two colours in the same buckets as above map identically.
    std::vector<u8> rgb;
    for (int i = 0; i < 8; ++i) {
        rgb.push_back(250); rgb.push_back(250); rgb.push_back(250);
    }
    for (int i = 0; i < 8; ++i) {
        rgb.push_back(3); rgb.push_back(3); rgb.push_back(3);
    }
    QOut o = Quant(rgb, 4, 4, /*dither=*/0);
    CHECK_EQ((int)o.pal[0], 252);        // 250 -> bucket 248..255 centre 252
    CHECK_EQ((int)o.pal[1], 4);          // 3   -> bucket 0..7 centre 4
    for (int i = 0; i < 8; ++i)  CHECK_EQ((int)o.idx[(std::size_t)i], 0);
    for (int i = 8; i < 16; ++i) CHECK_EQ((int)o.idx[(std::size_t)i], 1);
}

TEST(RenderTexturePalettize, SerpentineDitherRegressionVector) {
    // An 8x8 horizontal red ramp (r = 16*x + 8, g = b = 64): the leaf budget
    // keeps all 8 buckets, so the dither only spreads the +-20-clamped bucket
    // errors. The full index map is a REGRESSION PIN of the serpentine
    // Floyd–Steinberg arm (verbatim translation of 0x6033b4; any drift in the
    // error layout, the clamp tables, or the row turnaround changes it).
    const int w = 8, h = 8;
    std::vector<u8> rgb;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            rgb.push_back((u8)(16 * x + 8));
            rgb.push_back(64);
            rgb.push_back(64);
        }
    QOut o = Quant(rgb, w, h, /*dither=*/1);
    // 8 distinct red buckets, collected high-child-first: palette R descends.
    for (int k = 0; k < 8; ++k) {
        CHECK_EQ((int)o.pal[k], 8 * (2 * (7 - k)) + 4 + 8);  // 124,108,..,12
        CHECK_EQ((int)o.pal[256 + k], 68);
        CHECK_EQ((int)o.pal[512 + k], 68);
    }
    // The exact serpentine-FS index map, CROSS-VALIDATED by an independent
    // word-level reference implementation written directly from the 0x6033b4
    // decompile (identical output): row 0 (forward, no incoming error) maps
    // each column to its own bucket; the following rows oscillate exactly as
    // the 16ths error propagation dictates.
    static const u8 kExpect[64] = {
        7, 6, 5, 4, 3, 2, 1, 0,
        7, 6, 6, 4, 3, 3, 1, 0,
        7, 6, 5, 4, 4, 2, 1, 0,
        7, 6, 6, 4, 3, 2, 2, 0,
        7, 6, 5, 4, 4, 2, 1, 0,
        7, 6, 6, 4, 3, 2, 1, 0,
        7, 7, 5, 4, 4, 2, 2, 0,
        7, 6, 6, 4, 3, 2, 1, 0,
    };
    for (int i = 0; i < w * h; ++i)
        CHECK_EQ((int)o.idx[(std::size_t)i], (int)kExpect[i]);
}

TEST(RenderTexturePalettize, ManyColorsReduceTo256) {
    // 32x32 image with 1024 distinct bucket colours -> HeapReduceColors
    // (0x603068) must merge leaves up the octree to <= 256; every index stays
    // within the collected palette and maps to a colour close to the source.
    const int w = 32, h = 32;
    std::vector<u8> rgb;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            rgb.push_back((u8)(8 * x + 4));
            rgb.push_back((u8)(8 * y + 4));
            rgb.push_back((u8)(8 * ((x + y) % 32) + 4));
        }
    QOut o = Quant(rgb, w, h, /*dither=*/1);
    int maxIdx = 0;
    for (u8 i : o.idx)
        if (i > maxIdx) maxIdx = i;
    CHECK(maxIdx <= 255);
    // Every pixel's palette colour is within the merge radius of its source
    // (one octree level = 16 per channel, + the dither clamp 20).
    int worst = 0;
    for (int p = 0; p < w * h; ++p) {
        int i = o.idx[(std::size_t)p];
        int dr = (int)o.pal[i] - rgb[(std::size_t)p * 3];
        int dg = (int)o.pal[256 + i] - rgb[(std::size_t)p * 3 + 1];
        int db = (int)o.pal[512 + i] - rgb[(std::size_t)p * 3 + 2];
        int d = dr * dr + dg * dg + db * db;
        if (d > worst) worst = d;
    }
    std::printf("  [palettize] 1024-colour reduce: worst sqdist %d\n", worst);
    CHECK(worst <= 3 * 48 * 48);
}

TEST(RenderTexturePalettize, PalettizeDecodedBmpFillsIndices) {
    // The resolve-layer wiring point: a 24-bit DecodedBmp gains indices +
    // an interleaved 768-byte palette; 8-bit sources are untouched.
    render::DecodedBmp bmp;
    bmp.ok = true;
    bmp.bpp = 24;
    bmp.width = bmp.height = 4;
    bmp.square = true;
    bmp.rgba.assign(4 * 4 * 4, 0);
    for (int i = 0; i < 16; ++i) {
        bmp.rgba[(std::size_t)i * 4 + 0] = 100;
        bmp.rgba[(std::size_t)i * 4 + 1] = 50;
        bmp.rgba[(std::size_t)i * 4 + 2] = 25;
        bmp.rgba[(std::size_t)i * 4 + 3] = 255;
    }
    CHECK(render::PalettizeDecodedBmp(bmp));
    CHECK_EQ((int)bmp.indices.size(), 16);
    CHECK_EQ((int)bmp.palette.size(), 768);
    // Interleaved triples; bucket centres of (100,50,25) = (100,52,28).
    u8 i0 = bmp.indices[0];
    CHECK_EQ((int)bmp.palette[(std::size_t)i0 * 3 + 0], 100);
    CHECK_EQ((int)bmp.palette[(std::size_t)i0 * 3 + 1], 52);
    CHECK_EQ((int)bmp.palette[(std::size_t)i0 * 3 + 2], 28);
    // Second call: no-op (already palettized).
    CHECK(!render::PalettizeDecodedBmp(bmp));
    // 8-bit guard.
    render::DecodedBmp b8;
    b8.ok = true;
    b8.bpp = 8;
    b8.width = b8.height = 2;
    CHECK(!render::PalettizeDecodedBmp(b8));
}

// ---------------------------------------------------------------------------
// wave-10 W10-TEX hardening — degenerate / edge inputs (ASAN+UBSAN drives the
// bounds of the histogram, heap, palette, dither buffers and the colour-key
// swap). Each test exercises a real entry on a boundary the shipped content
// never hits but the public API admits; ASAN catches any OOB/UB.
// ---------------------------------------------------------------------------

// A 1x1 image: the smallest histogram (one leaf), the smallest dither row
// buffer (3*(1+2) words), the heapify base case. Must not OOB.
TEST(RenderTexturePalettizeEdge, OnePixel) {
    std::vector<u8> rgb = {17, 200, 9};
    QOut o = Quant(rgb, 1, 1, /*dither=*/1);
    CHECK_EQ((int)o.idx[0], 0);
    // bucket centres of (17,200,9): (17&0xF8)+4=20, (200&0xF8)+4=204, (9&0xF8)+4=12
    CHECK_EQ((int)o.pal[0], 20);
    CHECK_EQ((int)o.pal[256], 204);
    CHECK_EQ((int)o.pal[512], 12);
    // The undithered arm on the same 1x1.
    QOut o2 = Quant(rgb, 1, 1, /*dither=*/0);
    CHECK_EQ((int)o2.idx[0], 0);
}

// An all-black image: a single near-black leaf; both arms collapse every pixel
// to index 0. Drives the heap with a lone leaf and the closest-colour search
// over a 1-entry palette.
TEST(RenderTexturePalettizeEdge, AllBlack) {
    const int w = 5, h = 5;
    std::vector<u8> rgb((std::size_t)w * h * 3, 0);
    QOut o = Quant(rgb, w, h, /*dither=*/1);
    for (int i = 0; i < w * h; ++i) CHECK_EQ((int)o.idx[(std::size_t)i], 0);
    // bucket centre of (0,0,0) = (4,4,4).
    CHECK_EQ((int)o.pal[0], 4);
    CHECK_EQ((int)o.pal[256], 4);
    CHECK_EQ((int)o.pal[512], 4);
}

// >256 distinct bucket colours: HeapReduceColors must merge leaves until
// leafCount <= 256 BEFORE TreeCollectPalette writes into the 256-entry palette
// arrays. The colour-count boundary: if reduction stopped one short, the
// palette write at index 256 would OOB. Drive enough colours to force it.
TEST(RenderTexturePalettizeEdge, ColorCountBoundaryNoPaletteOverrun) {
    const int w = 64, h = 64;             // 4096 pixels
    std::vector<u8> rgb;
    rgb.reserve((std::size_t)w * h * 3);
    // 512 distinct bucket colours laid out so each occupies its own 15-bit cell.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int c = (y * w + x) % 512;    // 512 distinct codes
            rgb.push_back((u8)(8 * (c & 31) + 4));
            rgb.push_back((u8)(8 * ((c >> 5) & 15) + 4));
            rgb.push_back((u8)(8 * ((c >> 4) & 7) + 4));
        }
    QOut o = Quant(rgb, w, h, /*dither=*/1);
    int maxIdx = 0;
    for (u8 i : o.idx) if (i > maxIdx) maxIdx = i;
    CHECK(maxIdx <= 255);                  // every index inside the 256 palette
}

// A 0x0 image: width*height == 0. The histogram loop, heapify, reduce, collect
// and map all run on an empty image; the dither buffers are 3*(0+2) words. No
// OOB, returns true (the original's allocation paths cannot fail here).
TEST(RenderTexturePalettizeEdge, ZeroSize) {
    std::vector<u8> rgb;                   // empty
    u8 pal[768];
    // outIndices has zero elements; pass a valid (empty) pointer.
    std::vector<u8> idx;
    CHECK(render::QuantBuildPalette(rgb.data(), 0, 0, 256, 1, idx.data(), pal));
    // 0-width, non-zero height (degenerate row stride) also must not OOB.
    CHECK(render::QuantBuildPalette(rgb.data(), 0, 4, 256, 1, idx.data(), pal));
}

// PalettizeDecodedBmp degenerate guards: a 0-size bmp, a 1x1 bmp, an all-black
// bmp (colour-key index-0 reservation on a NO-other-colour image), and an image
// that is exactly black + one colour (the index-0 swap path).
TEST(RenderTexturePalettizeEdge, DecodedBmpDegenerate) {
    // 0-size: width/height 0 -> guarded false (no quantize, no OOB).
    {
        render::DecodedBmp b;
        b.ok = true; b.bpp = 24; b.width = 0; b.height = 0;
        CHECK(!render::PalettizeDecodedBmp(b));
    }
    // 1x1 24-bit.
    {
        render::DecodedBmp b;
        b.ok = true; b.bpp = 24; b.width = 1; b.height = 1; b.square = true;
        b.rgba = {10, 20, 30, 255};
        CHECK(render::PalettizeDecodedBmp(b));
        CHECK_EQ((int)b.indices.size(), 1);
        CHECK_EQ((int)b.palette.size(), 768);
    }
    // All-black 2x2: the colour-key reservation pins index 0 to exact black with
    // no swap (the only colour already lands at 0).
    {
        render::DecodedBmp b;
        b.ok = true; b.bpp = 24; b.width = 2; b.height = 2; b.square = true;
        b.rgba.assign(2 * 2 * 4, 0);
        for (int i = 0; i < 4; ++i) b.rgba[(std::size_t)i * 4 + 3] = 255;
        CHECK(render::PalettizeDecodedBmp(b));
        CHECK_EQ((int)b.palette[0], 0);    // index 0 pinned to exact black
        CHECK_EQ((int)b.palette[1], 0);
        CHECK_EQ((int)b.palette[2], 0);
        for (u8 v : b.indices) CHECK_EQ((int)v, 0);
    }
}

// Colour-key index-0 reservation when the source HAS black but the quantizer
// placed the black bucket at a NON-zero index: the swap must relabel both the
// palette and every index without OOB, and pin index 0 to exact black. Also the
// no-black case (reservation untouched).
TEST(RenderTexturePalettizeEdge, ColourKeyIndexZeroReservation) {
    // White top half, black bottom half: TreeCollectPalette walks high child
    // first, so WHITE is index 0 and BLACK is index 1 BEFORE the reservation.
    // The reservation swaps them so black -> index 0.
    render::DecodedBmp b;
    b.ok = true; b.bpp = 24; b.width = 4; b.height = 4; b.square = true;
    b.rgba.assign(4 * 4 * 4, 0);
    for (int i = 0; i < 16; ++i) {
        u8 v = (i < 8) ? 255 : 0;          // top 8 white, bottom 8 black
        b.rgba[(std::size_t)i * 4 + 0] = v;
        b.rgba[(std::size_t)i * 4 + 1] = v;
        b.rgba[(std::size_t)i * 4 + 2] = v;
        b.rgba[(std::size_t)i * 4 + 3] = 255;
    }
    CHECK(render::PalettizeDecodedBmp(b));
    // index 0 == exact black, and every black source pixel carries index 0.
    CHECK_EQ((int)b.palette[0], 0);
    CHECK_EQ((int)b.palette[1], 0);
    CHECK_EQ((int)b.palette[2], 0);
    for (int i = 8; i < 16; ++i) CHECK_EQ((int)b.indices[(std::size_t)i], 0);
    // The white pixels carry a non-zero index whose colour is near-white.
    u8 wi = b.indices[0];
    CHECK(wi != 0);
    CHECK((int)b.palette[(std::size_t)wi * 3 + 0] >= 248);

    // No-black image: reservation leaves index 0 as the quantizer placed it.
    render::DecodedBmp nb;
    nb.ok = true; nb.bpp = 24; nb.width = 2; nb.height = 2; nb.square = true;
    nb.rgba.assign(2 * 2 * 4, 0);
    for (int i = 0; i < 4; ++i) {
        nb.rgba[(std::size_t)i * 4 + 0] = 200;
        nb.rgba[(std::size_t)i * 4 + 1] = 100;
        nb.rgba[(std::size_t)i * 4 + 2] = 50;
        nb.rgba[(std::size_t)i * 4 + 3] = 255;
    }
    CHECK(render::PalettizeDecodedBmp(nb));
    // No exact-black pixel -> palette[0] is the bucket centre (204,100,52), NOT
    // forced black.
    CHECK_EQ((int)nb.palette[0], 204);
}
