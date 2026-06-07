#include "test.h"
#include "render/types.h"
#include "render/colorformat.h"
#include "render/surface.h"
#include "render/bmp.h"
#include "render/hicoltab.h"
#include "render/quant.h"
#include "render/font.h"
#include "render/paintbox.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// Colour format: pack/unpack vs Python-computed golden 565 values.
// ---------------------------------------------------------------------------
TEST(RenderColor, Pack565Golden) {
    ColorFormat f = Format565();
    CHECK_EQ(PackColor(f, 255, 0, 0),   0xF800u);
    CHECK_EQ(PackColor(f, 0, 255, 0),   0x07E0u);
    CHECK_EQ(PackColor(f, 0, 0, 255),   0x001Fu);
    CHECK_EQ(PackColor(f, 255, 255, 255), 0xFFFFu);
    CHECK_EQ(PackColor(f, 8, 4, 8),     0x0821u);
    CHECK_EQ(PackColor(f, 128, 128, 128), 0x8410u);
    CHECK_EQ(PackColor(f, 33, 77, 200), 0x2279u);
}

TEST(RenderColor, Unpack565Golden) {
    ColorFormat f = Format565();
    u8 r, g, b;
    UnpackColor(f, 0xF800, r, g, b); CHECK_EQ((int)r, 248); CHECK_EQ((int)g, 0); CHECK_EQ((int)b, 0);
    UnpackColor(f, 0x07E0, r, g, b); CHECK_EQ((int)r, 0); CHECK_EQ((int)g, 252); CHECK_EQ((int)b, 0);
    UnpackColor(f, 0x001F, r, g, b); CHECK_EQ((int)r, 0); CHECK_EQ((int)g, 0); CHECK_EQ((int)b, 248);
    UnpackColor(f, 0xFFFF, r, g, b); CHECK_EQ((int)r, 248); CHECK_EQ((int)g, 252); CHECK_EQ((int)b, 248);
}

TEST(RenderColor, ComputeShiftsMatch565And555) {
    ColorFormat c565 = ComputeChannelShifts(0xF800, 0x07E0, 0x001F);
    ColorFormat e565 = Format565();
    CHECK_EQ((int)c565.rPos, (int)e565.rPos);   CHECK_EQ((int)c565.rPrec, (int)e565.rPrec);
    CHECK_EQ((int)c565.gPos, (int)e565.gPos);   CHECK_EQ((int)c565.gPrec, (int)e565.gPrec);
    CHECK_EQ((int)c565.bPos, (int)e565.bPos);   CHECK_EQ((int)c565.bPrec, (int)e565.bPrec);
    ColorFormat c555 = ComputeChannelShifts(0x7C00, 0x03E0, 0x001F);
    ColorFormat e555 = Format555();
    CHECK_EQ((int)c555.rPos, (int)e555.rPos);   CHECK_EQ((int)c555.gPos, (int)e555.gPos);
    CHECK_EQ((int)c555.bPos, (int)e555.bPos);   CHECK_EQ((int)c555.rPrec, (int)e555.rPrec);
}

// ---------------------------------------------------------------------------
// Surface: create, set/get pixel, hline, line, rect, clone for several bpp.
// ---------------------------------------------------------------------------
TEST(RenderSurface, CreateGeometry) {
    Surface* s = SurfaceCreate(64, 48, 16);
    CHECK(s != nullptr);
    CHECK_EQ(s->width, 64);
    CHECK_EQ(s->height, 48);
    CHECK_EQ((int)s->bpp, 16);
    CHECK_EQ(s->pitch, 128);    // 2 bytes * 64
    CHECK_EQ(s->widthPx, 64);
    CHECK_EQ(s->clipX1, 64);
    CHECK_EQ(s->clipY1, 48);
    SurfaceDestroy(s);
}

TEST(RenderSurface, SetGetPixel16) {
    Surface* s = SurfaceCreate(16, 16, 16);
    SurfaceSetPixelRgb(s, 3, 5, 255, 0, 0);
    u8 px[3];
    SurfaceGetPixelRgb(s, 3, 5, px);
    CHECK_EQ((int)px[0], 248); // 565 red rounds to 248
    CHECK_EQ((int)px[1], 0);
    CHECK_EQ((int)px[2], 0);
    // raw pixel value
    u16 raw = ((u16*)s->pixels)[5 * 16 + 3];
    CHECK_EQ(raw, (u16)0xF800);
    SurfaceDestroy(s);
}

TEST(RenderSurface, SetPixel24Bgr) {
    Surface* s = SurfaceCreate(8, 8, 24);
    SurfaceSetPixelRgb(s, 2, 1, 0x11, 0x22, 0x33);
    u8* p = s->pixels + (s->widthPx * 1) * 3 + 2 * 3;
    CHECK_EQ((int)p[0], 0x11); // R
    CHECK_EQ((int)p[1], 0x22); // G
    CHECK_EQ((int)p[2], 0x33); // B
    u8 px[3];
    SurfaceGetPixelRgb(s, 2, 1, px);
    CHECK_EQ((int)px[0], 0x11); CHECK_EQ((int)px[1], 0x22); CHECK_EQ((int)px[2], 0x33);
    SurfaceDestroy(s);
}

TEST(RenderSurface, SetPixel8Luma) {
    Surface* s = SurfaceCreate(8, 8, 8);
    SurfaceSetPixelRgb(s, 1, 1, 30, 60, 90); // (90+30+60)/3 = 60
    CHECK_EQ((int)s->pixels[s->widthPx * 1 + 1], 60);
    SurfaceDestroy(s);
}

TEST(RenderSurface, ClipRejectsOutOfBounds) {
    Surface* s = SurfaceCreate(8, 8, 16);
    CHECK_EQ(SurfaceSetPixelRgb(s, -1, 0, 1, 1, 1), 0);
    CHECK_EQ(SurfaceSetPixelRgb(s, 8, 0, 1, 1, 1), 0);
    CHECK_EQ(SurfaceSetPixelRgb(s, 0, 8, 1, 1, 1), 0);
    CHECK_EQ(SurfaceSetPixelRgb(s, 0, 0, 1, 1, 1), 1);
    SurfaceDestroy(s);
}

TEST(RenderSurface, HLineEndpoints) {
    Surface* s = SurfaceCreate(16, 4, 16);
    SurfaceDrawHLine(s, 2, 1, 5, 0, 255, 0); // x = 2..6 inclusive
    for (int x = 0; x < 16; ++x) {
        u16 raw = ((u16*)s->pixels)[1 * 16 + x];
        if (x >= 2 && x <= 6) CHECK_EQ(raw, (u16)0x07E0);
        else CHECK_EQ(raw, (u16)0);
    }
    SurfaceDestroy(s);
}

TEST(RenderSurface, LineHorizontalVertical) {
    Surface* s = SurfaceCreate(16, 16, 16);
    SurfaceDrawLine(s, 0, 8, 10, 8, 255, 255, 255); // horizontal
    SurfaceDrawLine(s, 4, 0, 4, 9, 255, 255, 255);  // vertical
    // endpoints of horizontal: (0,8) and (10,8) is the far one drawn? original
    // draws x=0..9 (stops at !=x1). Check a midpoint and start.
    CHECK_EQ(((u16*)s->pixels)[8 * 16 + 0], (u16)0xFFFF);
    CHECK_EQ(((u16*)s->pixels)[8 * 16 + 5], (u16)0xFFFF);
    CHECK_EQ(((u16*)s->pixels)[0 * 16 + 4], (u16)0xFFFF);
    CHECK_EQ(((u16*)s->pixels)[5 * 16 + 4], (u16)0xFFFF);
    SurfaceDestroy(s);
}

TEST(RenderSurface, LineDiagonal) {
    Surface* s = SurfaceCreate(16, 16, 16);
    SurfaceDrawLine(s, 0, 0, 7, 7, 255, 255, 255);
    for (int i = 0; i <= 7; ++i)
        CHECK_EQ(((u16*)s->pixels)[i * 16 + i], (u16)0xFFFF);
    SurfaceDestroy(s);
}

TEST(RenderSurface, RectOutline) {
    Surface* s = SurfaceCreate(16, 16, 16);
    // NOTE: the original gates the whole routine on the 3rd colour channel (b)
    // being nonzero (a7 != 0). We therefore use a blue value so the outline is
    // actually drawn (faithful behaviour: a pure-red call would draw nothing).
    SurfaceDrawRectOutline(s, 2, 3, 5, 4, 255, 255, 255); // x=2,y=3,w=5,h=4 white
    auto get = [&](int x, int y) { return ((u16*)s->pixels)[y * 16 + x]; };
    // corners
    CHECK_EQ(get(2, 3), (u16)0xFFFF);
    CHECK_EQ(get(6, 3), (u16)0xFFFF);   // x+w-1
    CHECK_EQ(get(2, 6), (u16)0xFFFF);   // y+h-1
    CHECK_EQ(get(6, 6), (u16)0xFFFF);
    // interior empty
    CHECK_EQ(get(4, 4), (u16)0);
    SurfaceDestroy(s);
}

TEST(RenderSurface, RectOutlineGatedOnBlue) {
    // Faithful quirk: when the blue/3rd channel is 0 the routine draws nothing.
    Surface* s = SurfaceCreate(16, 16, 16);
    SurfaceDrawRectOutline(s, 2, 3, 5, 4, 255, 0, 0);
    CHECK_EQ(((u16*)s->pixels)[3 * 16 + 2], (u16)0);
    SurfaceDestroy(s);
}

TEST(RenderSurface, Clone) {
    Surface* s = SurfaceCreate(8, 8, 16);
    SurfaceSetPixelRgb(s, 3, 3, 255, 0, 0);
    Surface* c = SurfaceClone(s);
    CHECK(c != nullptr);
    CHECK_EQ(c->width, 8);
    CHECK_EQ(((u16*)c->pixels)[3 * 8 + 3], (u16)0xF800);
    SurfaceDestroy(s);
    SurfaceDestroy(c);
}

// ---------------------------------------------------------------------------
// Font glyph table golden.
// ---------------------------------------------------------------------------
TEST(RenderFont, GlyphTableGolden) {
    u8 t[256];
    std::memset(t, 0xAA, sizeof(t));
    FontInitGlyphTable(t);
    CHECK_EQ((int)t[32], 1);
    CHECK_EQ((int)t[65], 2);     // 'A'
    CHECK_EQ((int)t[90], 27);    // 'Z'
    CHECK_EQ((int)t[97], 28);    // 'a'
    CHECK_EQ((int)t[100], 31);   // 'd'
    CHECK_EQ((int)t[33], 69);    // overwritten by block2 'E'
    CHECK_EQ((int)t[64], 78);    // 'N'
    CHECK_EQ((int)t[101], 32);   // block1 ' '
    CHECK_EQ((int)t[122], 53);   // block1 '5'
    CHECK_EQ((int)t[91], 64);
    CHECK_EQ((int)t[92], 55);
    CHECK_EQ((int)t[93], 65);
    CHECK_EQ((int)t[95], 79);
    CHECK_EQ((int)t[126], 90);
    CHECK_EQ((int)t[0], 0);      // zero-initialised
}

// ---------------------------------------------------------------------------
// Quant lookup tables + nearest-colour search.
// ---------------------------------------------------------------------------
TEST(RenderQuant, LookupTablesGolden) {
    QuantState q;
    QuantInitLookupTables(q);
    CHECK_EQ((int)q.spreadHi[0], 0);
    CHECK_EQ((int)q.spreadHi[8], 4);    CHECK_EQ((int)q.spreadMid[8], 1);   CHECK_EQ((int)q.spreadLo[8], 2);
    CHECK_EQ((int)q.spreadHi[16], 32);  CHECK_EQ((int)q.spreadMid[16], 8);  CHECK_EQ((int)q.spreadLo[16], 16);
    CHECK_EQ((int)q.spreadHi[128], 16384);
    CHECK_EQ((int)q.spreadHi[255], 18724);
    CHECK_EQ((int)q.spreadMid[255], 4681);
    CHECK_EQ((int)q.spreadLo[255], 9362);
    CHECK_EQ(q.sqBase[-255], 65025);
    CHECK_EQ(q.sqBase[255], 65025);
    CHECK_EQ(q.sqBase[0], 0);
    CHECK_EQ(q.sqBase[3], 9);
}

TEST(RenderQuant, FindClosestColor) {
    QuantState q;
    QuantInitLookupTables(q);
    // palette: 0=black, 1=red, 2=green, 3=blue, 4=white
    q.palCount = 5;
    u8 R[5] = {0, 255, 0, 0, 255};
    u8 G[5] = {0, 0, 255, 0, 255};
    u8 B[5] = {0, 0, 0, 255, 255};
    std::memcpy(q.palR, R, 5);
    std::memcpy(q.palG, G, 5);
    std::memcpy(q.palB, B, 5);
    CHECK_EQ(QuantFindClosestColor(q, 250, 5, 5), 1u);   // near red
    CHECK_EQ(QuantFindClosestColor(q, 5, 250, 5), 2u);   // near green
    CHECK_EQ(QuantFindClosestColor(q, 5, 5, 250), 3u);   // near blue
    CHECK_EQ(QuantFindClosestColor(q, 250, 250, 250), 4u); // near white
    CHECK_EQ(QuantFindClosestColor(q, 5, 5, 5), 0u);     // near black
}

TEST(RenderQuant, CopyPaletteEntriesPlanar) {
    QuantState q;
    q.palR[0] = 10; q.palG[0] = 20; q.palB[0] = 30;
    q.palR[255] = 1; q.palG[255] = 2; q.palB[255] = 3;
    u8 out[768];
    QuantCopyPaletteEntries(q, out);
    CHECK_EQ((int)out[0], 10);
    CHECK_EQ((int)out[256], 20);
    CHECK_EQ((int)out[512], 30);
    CHECK_EQ((int)out[255], 1);
    CHECK_EQ((int)out[511], 2);
    CHECK_EQ((int)out[767], 3);
}

// ---------------------------------------------------------------------------
// HiColTab: add entries, direct + ramp values.
// ---------------------------------------------------------------------------
TEST(RenderHiColTab, AddAndDirect) {
    HiColTab tab;
    u8 i0 = HiColTabAddEntry(tab, 255, 0, 0);
    u8 i1 = HiColTabAddEntry(tab, 0, 255, 0);
    u8 i0b = HiColTabAddEntry(tab, 255, 0, 0); // dup -> same index
    CHECK_EQ((int)i0, 0);
    CHECK_EQ((int)i1, 1);
    CHECK_EQ((int)i0b, 0);
    CHECK_EQ(tab.freeCount, 254);
    CHECK_EQ(HiColTabDirect(tab, 0), (u16)0xF800);
    CHECK_EQ(HiColTabDirect(tab, 1), (u16)0x07E0);
}

TEST(RenderHiColTab, RampEndpoints) {
    HiColTab tab;
    HiColTabAddEntry(tab, 255, 0, 0); // red, index 0
    // step 0 -> black; step 62 -> full red.
    CHECK_EQ(HiColTabRamp(tab, 0, 0), (u16)0x0000);
    CHECK_EQ(HiColTabRamp(tab, 0, 62), (u16)0xF800);
    // mid step ~31 -> ~half red intensity (round(255/62*31)=128 -> 565 red bits)
    u8 r, g, b;
    UnpackColor(tab.fmt, HiColTabRamp(tab, 0, 31), r, g, b);
    CHECK(r >= 120 && r <= 136);
    CHECK_EQ((int)g, 0);
    CHECK_EQ((int)b, 0);
}

// ---------------------------------------------------------------------------
// BMP save (8-bit + 24-bit) byte-layout + load roundtrip.
// ---------------------------------------------------------------------------
TEST(RenderBmp, Save24MatchesReference) {
    // top-down R,G,B source 2x2: red, green / blue, white
    u8 src[12] = {255,0,0, 0,255,0,  0,0,255, 255,255,255};
    std::vector<u8> out = BmpSave24Bit(2, 2, src);
    CHECK_EQ((int)out.size(), 66);
    CHECK_EQ((int)out[0], 'B'); CHECK_EQ((int)out[1], 'M');
    // dataOffset
    CHECK_EQ((int)out[10], 54);
    // info bpp at file offset 14+14 = 28
    CHECK_EQ((int)out[28], 24);
    // first on-disk pixel = bottom-up row0 = source row1 col0 = blue -> B,G,R
    CHECK_EQ((int)out[54], 255); // B
    CHECK_EQ((int)out[55], 0);
    CHECK_EQ((int)out[56], 0);   // R
}

TEST(RenderBmp, Save24LoadRoundtrip) {
    u8 src[12] = {255,0,0, 0,255,0,  0,0,255, 255,255,255};
    std::vector<u8> file = BmpSave24Bit(2, 2, src);
    int w, h;
    std::vector<u8> back = BmpLoadBuffer(file, 24, w, h);
    CHECK_EQ(w, 2);
    CHECK_EQ(h, 2);
    CHECK_EQ((int)back.size(), 12);
    for (int i = 0; i < 12; ++i) CHECK_EQ((int)back[i], (int)src[i]);
}

TEST(RenderBmp, Save8LoadRoundtripGrayscale) {
    // default grayscale palette (R=G=B=index) round-trips through the original's
    // quirky palette layout perfectly.
    u8 idx[6] = {0, 1, 2, 254, 255, 128}; // 3x2
    std::vector<u8> file = BmpSaveIndexed(3, 2, idx, nullptr);
    int w, h;
    u8 pal[768];
    std::vector<u8> back = BmpLoadBuffer(file, 8, w, h, pal);
    CHECK_EQ(w, 3);
    CHECK_EQ(h, 2);
    CHECK_EQ((int)back.size(), 6);
    // Pixel indices round-trip exactly (the key property; palette colours are
    // independent of indices).
    for (int i = 0; i < 6; ++i) CHECK_EQ((int)back[i], (int)idx[i]);
    // The original's save palette layout writes {R_i, G_i, B_{i+1}, 0} on disk
    // while its load reads it back as BGRA — a cross-bug that swaps R/B and
    // shifts B by one. We assert the FAITHFUL result for a mid index: loaded G
    // is exact (G_i), loaded B = R_i = i.
    CHECK_EQ((int)pal[3 * 128 + 1], 128); // G channel exact
    CHECK_EQ((int)pal[3 * 128 + 2], 128); // B(loaded) = R(saved) = i
    // file header magic + dataOffset dword (1078 for 8-bit) at bytes 10..13.
    CHECK_EQ((int)file[0], 'B');
    u32 dataOff = (u32)file[10] | ((u32)file[11] << 8) | ((u32)file[12] << 16) | ((u32)file[13] << 24);
    CHECK_EQ(dataOff, 1078u);
}

TEST(RenderBmp, ReadHeaderInfo) {
    u8 src[12] = {1,2,3, 4,5,6, 7,8,9, 10,11,12};
    std::vector<u8> file = BmpSave24Bit(2, 2, src);
    BmpInfo info = BmpReadHeaderInfo(file);
    CHECK(info.ok);
    CHECK_EQ(info.width, 2);
    CHECK_EQ(info.height, 2);
    CHECK_EQ(info.bitCount, 24);
}

// ---------------------------------------------------------------------------
// Paintbox: brush plotting + line.
// ---------------------------------------------------------------------------
TEST(RenderPaintbox, BrushZeroSinglePixel) {
    Surface* s = SurfaceCreate(16, 16, 16);
    PaintboxState pb; pb.surface = s; pb.brush = 0; pb.border = 0;
    PaintboxDrawScaledRegion(pb, 5, 6, 255, 0, 0);
    CHECK_EQ(((u16*)s->pixels)[6 * 16 + 5], (u16)0xF800);
    CHECK_EQ(((u16*)s->pixels)[6 * 16 + 4], (u16)0);
    SurfaceDestroy(s);
}

TEST(RenderPaintbox, BrushOnePlus) {
    Surface* s = SurfaceCreate(16, 16, 16);
    PaintboxState pb; pb.surface = s; pb.brush = 1; pb.border = 1;
    PaintboxDrawScaledRegion(pb, 8, 8, 0, 255, 0);
    auto g = [&](int x, int y) { return ((u16*)s->pixels)[y * 16 + x]; };
    CHECK_EQ(g(8, 8), (u16)0x07E0);
    CHECK_EQ(g(7, 8), (u16)0x07E0);
    CHECK_EQ(g(9, 8), (u16)0x07E0);
    CHECK_EQ(g(8, 7), (u16)0x07E0);
    CHECK_EQ(g(8, 9), (u16)0x07E0);
    CHECK_EQ(g(7, 7), (u16)0); // diagonal not set for plus
    SurfaceDestroy(s);
}

TEST(RenderPaintbox, LineDraws) {
    Surface* s = SurfaceCreate(16, 16, 16);
    PaintboxState pb; pb.surface = s; pb.brush = 0; pb.border = 0;
    PaintboxDrawLine(pb, 0, 0, 6, 6, 255, 255, 255);
    for (int i = 0; i <= 6; ++i)
        CHECK_EQ(((u16*)s->pixels)[i * 16 + i], (u16)0xFFFF);
    SurfaceDestroy(s);
}
