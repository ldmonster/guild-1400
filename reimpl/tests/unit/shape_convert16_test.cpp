// Golden-vector unit tests for the 16bpp shape converters (render/shape_convert16):
//   ShapeConvertRgbTo16  — gilde.exe 0x5d7c0c VIBE_Shape_ConvertRgbTo16
//   ShapeConvert8To16    — gilde.exe 0x5d7924 VIBE_Shape_Convert8To16
//   ShapeBankConvertNew  — gilde.exe 0x5d80a8 VIBE_ShapeBank_ConvertNew
//   (Shape_ConvertToNew  — 0x5d8080 — exercised through the leaves9 hook wiring)
// Every expected buffer is hand-derived from the decompile semantics: byte-exact
// whole-blob compares plus 565-packing edge cases (zero-pack -> (5,5,5), the
// half-bright (px>>1)&word_1406944 path, the unsigned 2*skip/3 re-encode).
// Suite prefix: ShapeConvert16.
#include "tests/framework/test.h"

#include "render/shape_convert16.h"
#include "render/render_leaves9.h"
#include "render/shapebank.h"
#include "render/colorformat.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using guild::u8;
using guild::u16;
using guild::u32;
using namespace guild::render;

namespace {

void W16(std::vector<u8>& b, size_t off, u16 v) { std::memcpy(&b[off], &v, 2); }
void W32(std::vector<u8>& b, size_t off, u32 v) { std::memcpy(&b[off], &v, 4); }
u16  R16(const u8* b, size_t off) { u16 v; std::memcpy(&v, b + off, 2); return v; }
u32  R32(const u8* b, size_t off) { u32 v; std::memcpy(&v, b + off, 4); return v; }

void Push32(std::vector<u8>& b, u32 v) { size_t o = b.size(); b.resize(o + 4); W32(b, o, v); }
void Push16(std::vector<u8>& b, u16 v) { size_t o = b.size(); b.resize(o + 2); W16(b, o, v); }
void Push8 (std::vector<u8>& b, u8 v)  { b.push_back(v); }

ShapeConvertState State565() {
    ShapeConvertState st;
    st.fmt      = Format565();
    st.darkMask = ShapeConvertDarkMask(st.fmt);
    return st;
}

// Build a 24bpp RLE source shape. rows = list of rows; each row = list of runs;
// each run = {skipBytes, pixels(list of RGB triples)}. The blob carries its own
// row table (like the Grab* outputs do) so size@0 is the true total the alloc
// formula consumes.
struct Run24 { u32 skip; std::vector<std::array<u8,3>> px; };
std::vector<u8> BuildRleShape24(u16 w, u16 h, u8 typeFlag,
                                const std::vector<std::vector<Run24>>& rows) {
    std::vector<u8> s(50, 0);
    W16(s, 6, w);  W16(s, 10, h);
    s[12] = 2;     s[13] = typeFlag;
    u32 pix = 0;
    std::vector<u32> rowOff;
    for (const auto& row : rows) {
        rowOff.push_back((u32)s.size());
        Push32(s, (u32)row.size());                  // runCount
        for (const Run24& r : row) {
            Push32(s, r.skip);
            Push32(s, (u32)r.px.size());
            for (const auto& p : r.px) { Push8(s, p[0]); Push8(s, p[1]); Push8(s, p[2]); }
            pix += (u32)r.px.size();
        }
    }
    const u32 rowTab = (u32)s.size();
    for (u32 o : rowOff) Push32(s, o);
    W32(s, 0, (u32)s.size());                        // total size
    W32(s, 38, (u32)rows.size());                    // spanFlag != -1 (any non -1)
    W32(s, 42, rowTab);
    W32(s, 46, pix);
    return s;
}

} // namespace

// -----------------------------------------------------------------------------
// 565 packing edge cases of the converter pipeline itself.
TEST(ShapeConvert16, DarkMaskMatchesInitColorMasksFormula) {
    CHECK_EQ((int)ShapeConvertDarkMask(Format565()), 0x7BEF);  // word_1406944 @565
    CHECK_EQ((int)ShapeConvertDarkMask(Format555()), 0x3DEF);  // ... @555
    // PackColor sanity for the vectors below.
    CHECK_EQ((int)PackColor(Format565(), 255, 0, 0), 0xF800);
    CHECK_EQ((int)PackColor(Format565(), 0, 255, 0), 0x07E0);
    CHECK_EQ((int)PackColor(Format565(), 0, 0, 255), 0x001F);
    CHECK_EQ((int)PackColor(Format565(), 8, 4, 8),   0x0821);
    CHECK_EQ((int)PackColor(Format565(), 0, 0, 7),   0x0000);  // packs to ZERO
    CHECK_EQ((int)PackColor(Format565(), 5, 5, 5),   0x0020);  // the replacement
}

// -----------------------------------------------------------------------------
// RLE branch: byte-exact whole-blob golden vector.
//   row0: 1 run  {skip 6,  px (255,0,0) (0,255,0)}
//   row1: 2 runs {skip 0,  px (0,0,255)} {skip 3, px (8,4,8)}
TEST(ShapeConvert16, RgbTo16RleGoldenBlob) {
    std::vector<u8> src = BuildRleShape24(4, 2, 0, {
        { {6, {{{255,0,0}}, {{0,255,0}}}} },
        { {0, {{{0,0,255}}}}, {3, {{{8,4,8}}}} },
    });
    CHECK_EQ((int)R32(src.data(), 0), 102);          // 50+4+14+4+11+11+8
    CHECK_EQ((int)R32(src.data(), 46), 4);

    u8* dst = ShapeConvertRgbTo16(State565(), src.data());
    CHECK(dst != nullptr);

    // Hand-built expected blob (alloc = 102 - 4 = 98 bytes, fully written).
    std::vector<u8> exp(src.begin(), src.begin() + 50);   // header copy (0x32)
    exp.resize(50);
    W32(exp, 0, 0);                                  // patched below
    exp[12] = 1;                                     // depth 1
    W32(exp, 38, 3);                                 // total runs
    W32(exp, 42, 0);                                 // patched below
    W32(exp, 46, 4);                                 // total pixels
    // row0 @50: runCount 1; run {skip 2*6/3=4, n 2, 0xF800 0x07E0}
    Push32(exp, 1);  Push32(exp, 4);  Push32(exp, 2);
    Push16(exp, 0xF800); Push16(exp, 0x07E0);
    // row1 @66: runCount 2; {0,1,0x001F} {2*3/3=2,1,0x0821}
    Push32(exp, 2);
    Push32(exp, 0); Push32(exp, 1); Push16(exp, 0x001F);
    Push32(exp, 2); Push32(exp, 1); Push16(exp, 0x0821);
    CHECK_EQ((int)exp.size(), 90);
    W32(exp, 42, 90);                                // rowTableOffset
    Push32(exp, 50); Push32(exp, 66);                // row offsets
    W32(exp, 0, 98);                                 // final size

    CHECK_EQ((int)R32(dst, 0), 98);
    CHECK(std::memcmp(dst, exp.data(), 98) == 0);
    std::free(dst);
}

// A pixel that PACKS to zero is replaced by Pack(5,5,5) = 0x0020 (RLE branch only).
TEST(ShapeConvert16, RgbTo16ZeroPackBecomes555) {
    std::vector<u8> src = BuildRleShape24(1, 1, 0, {
        { {0, {{{0,0,7}}}} },                        // (0,0,7) -> 565 packs to 0
    });
    u8* dst = ShapeConvertRgbTo16(State565(), src.data());
    CHECK(dst != nullptr);
    CHECK_EQ((int)R16(dst, 50 + 4 + 8), 0x0020);     // row hdr + run hdr, then px
    std::free(dst);
}

// typeFlag @+13 == 1: AFTER pack (and after the 0->(5,5,5) replacement) the pixel
// is halved: (px >> 1) & 0x7BEF.
TEST(ShapeConvert16, RgbTo16HalfBrightAppliesAfterZeroReplacement) {
    std::vector<u8> src = BuildRleShape24(2, 1, 1, {
        { {0, {{{255,0,0}}, {{0,0,7}}}} },
    });
    u8* dst = ShapeConvertRgbTo16(State565(), src.data());
    CHECK(dst != nullptr);
    // 0xF800>>1 = 0x7C00; & 0x7BEF = 0x7800.
    CHECK_EQ((int)R16(dst, 62), 0x7800);
    // zero-pack -> 0x0020 (green LSB); >>1 = 0x0010 lands on the masked-out
    // inter-channel bit, so & 0x7BEF darkens it all the way back to 0 — the
    // half-bright path CAN reintroduce the transparent slot, faithfully.
    CHECK_EQ((int)R16(dst, 64), 0x0000);
    std::free(dst);
}

// The skip re-encode is UNSIGNED 2*skip/3 (matches `div` on edx:eax in the
// original) — a non-multiple-of-3 skip truncates.
TEST(ShapeConvert16, RgbTo16SkipReencodeIsUnsignedTruncatingDiv) {
    std::vector<u8> src = BuildRleShape24(3, 1, 0, {
        { {7, {{{1,2,3}}}} },                        // 2*7/3 = 4 (trunc)
    });
    u8* dst = ShapeConvertRgbTo16(State565(), src.data());
    CHECK(dst != nullptr);
    CHECK_EQ((int)R32(dst, 54), 4);
    std::free(dst);
}

// An empty row (runCount 0) still costs 4 bytes and a row-table entry.
TEST(ShapeConvert16, RgbTo16EmptyRowKeepsRowTableSync) {
    std::vector<u8> src = BuildRleShape24(2, 3, 0, {
        { {0, {{{255,255,255}}}} },
        { },                                          // empty row
        { {0, {{{255,255,255}}}} },
    });
    u8* dst = ShapeConvertRgbTo16(State565(), src.data());
    CHECK(dst != nullptr);
    const u32 rowTab = R32(dst, 42);
    CHECK_EQ((int)R32(dst, rowTab + 0), 50);          // row0
    CHECK_EQ((int)R32(dst, rowTab + 4), 50 + 4 + 8 + 2);   // row1 after 1-px run
    CHECK_EQ((int)R32(dst, rowTab + 8), 50 + 4 + 8 + 2 + 4); // row2 after empty row
    CHECK_EQ((int)R32(dst, 38), 2);                   // two runs total
    CHECK_EQ((int)R32(dst, 46), 2);                   // two pixels total
    std::free(dst);
}

// -----------------------------------------------------------------------------
// RAW branch (spanFlag == -1): full-bitmap convert; NO zero replacement; the
// header keeps spanFlag = -1, rowTableOffset = 0, opaque = 0.
TEST(ShapeConvert16, RgbTo16RawFullBitmapGoldenBlob) {
    // 2x2 raw 24bpp shape: (255,0,0) (0,0,0) / (0,0,7) (8,4,8).
    std::vector<u8> src(50, 0);
    W16(src, 6, 2); W16(src, 10, 2);
    src[12] = 2;
    W32(src, 38, 0xFFFFFFFFu);
    const u8 px[12] = {255,0,0,  0,0,0,  0,0,7,  8,4,8};
    src.insert(src.end(), px, px + 12);
    W32(src, 0, (u32)src.size());                    // 62
    W32(src, 46, 4);                                 // pixel count (alloc input)

    u8* dst = ShapeConvertRgbTo16(State565(), src.data());
    CHECK(dst != nullptr);
    CHECK_EQ((int)R32(dst, 0), 58);                  // 50 + 2 rows * 2*2 bytes
    CHECK_EQ((int)dst[12], 1);
    CHECK_EQ(R32(dst, 38), 0xFFFFFFFFu);             // stays raw
    CHECK_EQ((int)R32(dst, 42), 0);
    CHECK_EQ((int)R32(dst, 46), 0);
    CHECK_EQ((int)R16(dst, 50), 0xF800);
    CHECK_EQ((int)R16(dst, 52), 0x0000);             // black stays 0 (no remap)
    CHECK_EQ((int)R16(dst, 54), 0x0000);             // zero-pack stays 0 (no remap)
    CHECK_EQ((int)R16(dst, 56), 0x0821);
    std::free(dst);
}

// -----------------------------------------------------------------------------
// 8bpp converter: palette-quad LUT, skip*2 re-encode, no zero replacement.
TEST(ShapeConvert16, Convert8To16GoldenBlob) {
    std::vector<u8> pal(1024, 0);
    pal[4 * 7 + 0] = 255;                            // index 7 -> (255,0,0)
    pal[4 * 9 + 2] = 7;                              // index 9 -> (0,0,7): packs 0
    ShapeConvertState st = State565();
    st.palette1024 = pal.data();

    // 1 row, 2 runs: {skip 5, idx 7} {skip 1, idx 9 idx 0}.
    std::vector<u8> src(50, 0);
    W16(src, 6, 3); W16(src, 10, 1);
    src[12] = 0;                                     // depth 0 (8bpp)
    W32(src, 38, 1);
    Push32(src, 2);                                  // runCount
    Push32(src, 5); Push32(src, 1); Push8(src, 7);
    Push32(src, 1); Push32(src, 2); Push8(src, 9); Push8(src, 0);
    const u32 rowTab = (u32)src.size();
    Push32(src, 50);
    W32(src, 0, (u32)src.size());
    W32(src, 42, rowTab);
    W32(src, 46, 3);

    u8* dst = ShapeConvert8To16(st, src.data());
    CHECK(dst != nullptr);
    CHECK_EQ((int)dst[12], 1);
    CHECK_EQ((int)R32(dst, 54), 10);                 // skip 5 -> 2*5
    CHECK_EQ((int)R16(dst, 62), 0xF800);             // lut[7]
    CHECK_EQ((int)R32(dst, 64), 2);                  // run1: skip 1 -> 2
    CHECK_EQ((int)R32(dst, 68), 2);
    CHECK_EQ((int)R16(dst, 72), 0x0000);             // lut[9] packs 0; NO (5,5,5)
    CHECK_EQ((int)R16(dst, 74), 0x0000);             // lut[0]
    CHECK_EQ((int)R32(dst, 38), 2);
    CHECK_EQ((int)R32(dst, 46), 3);
    // size: 50 + 4 + (8+2) + (8+4) = 76; +4 row table = 80.
    CHECK_EQ((int)R32(dst, 42), 76);
    CHECK_EQ((int)R32(dst, 0), 80);
    CHECK_EQ((int)R32(dst, 76), 50);                 // row offset entry
    std::free(dst);
}

TEST(ShapeConvert16, Convert8To16HalfBrightUsesDarkMask) {
    std::vector<u8> pal(1024, 0);
    pal[4 * 1 + 0] = 255;                            // (255,0,0) -> 0xF800
    ShapeConvertState st = State565();
    st.palette1024 = pal.data();

    std::vector<u8> src(50, 0);
    W16(src, 6, 1); W16(src, 10, 1);
    src[12] = 0; src[13] = 1;                        // half-bright flag
    W32(src, 38, 1);
    Push32(src, 1);
    Push32(src, 0); Push32(src, 1); Push8(src, 1);
    Push32(src, 50);
    W32(src, 0, (u32)src.size());
    W32(src, 46, 1);

    u8* dst = ShapeConvert8To16(st, src.data());
    CHECK(dst != nullptr);
    CHECK_EQ((int)R16(dst, 62), 0x7800);             // (0xF800>>1)&0x7BEF
    std::free(dst);
}

// RAW 8bpp shapes are rejected with the original's exact message.
TEST(ShapeConvert16, Convert8To16RawIsUnsupportedAndReports) {
    static std::string captured;
    captured.clear();
    ShapeConvertState st = State565();
    st.reportMessage = [](const char* m) { captured = m; };

    std::vector<u8> src(50, 0);
    W32(src, 0, 50);
    W32(src, 38, 0xFFFFFFFFu);
    CHECK(ShapeConvert8To16(st, src.data()) == nullptr);
    CHECK_EQ(captured, std::string("shp_Convert8To16: Converting of NoReadAndSkip "
                                   "Shapes not surported..."));
}

// -----------------------------------------------------------------------------
// The 0x5d8080 driver through the leaves9 hooks (rule-13 wiring): depth 2 ->
// RgbTo16, depth 0 -> 8To16, anything else / target != 1 -> null.
TEST(ShapeConvert16, ConvertToNewDispatchThroughLeaves9) {
    SetActiveShapeConvertState(State565());
    InstallShapeConvertersIntoLeaves9();

    std::vector<u8> s24 = BuildRleShape24(1, 1, 0, { { {0, {{{255,0,0}}}} } });
    u8* viaHook = static_cast<u8*>(Shape_ConvertToNew(s24.data(), 1));
    CHECK(viaHook != nullptr);
    u8* direct = ShapeConvertRgbTo16(State565(), s24.data());
    CHECK_EQ((int)R32(viaHook, 0), (int)R32(direct, 0));
    CHECK(std::memcmp(viaHook, direct, R32(direct, 0)) == 0);
    std::free(viaHook);
    std::free(direct);

    // depth 1 / target mismatch -> null (no converter touched).
    std::vector<u8> s16 = s24;
    s16[12] = 1;
    CHECK(Shape_ConvertToNew(s16.data(), 1) == nullptr);
    CHECK(Shape_ConvertToNew(s24.data(), 2) == nullptr);
}

// -----------------------------------------------------------------------------
// ShapeBankConvertNew @0x5d80a8: full bank drive — header copy, per-shape convert
// + AddShape, sequence-data re-append, exact write cursor.
TEST(ShapeConvert16, BankConvertNewConvertsADepth2Bank) {
    SetActiveShapeConvertState(State565());
    InstallShapeConvertersIntoLeaves9();

    std::vector<u8> shp0 = BuildRleShape24(2, 1, 0,
        { { {0, {{{255,0,0}}, {{0,255,0}}}} } });
    std::vector<u8> shp1 = BuildRleShape24(1, 2, 0,
        { { {3, {{{0,0,255}}}} }, { {0, {{{8,4,8}}}} } });

    const u32 s0 = R32(shp0.data(), 0), s1 = R32(shp1.data(), 0);
    const u32 seqOff = 2117 + s0 + s1;
    std::vector<u8> bank(seqOff + 8, 0);
    std::memcpy(&bank[0], "SHAPBANK", 8);
    bank[8] = 1;
    W16(bank, 42, 2);                                 // shapeCount
    W16(bank, 44, 2); W16(bank, 46, 2);               // maxW / maxH
    W32(bank, 48, (u32)bank.size());                  // writeCursor
    bank[52] = 2;                                     // pixel format 2
    W32(bank, 62, seqOff);                            // seq data offset
    W16(bank, 67, 1);                                 // 1 seq record
    W32(bank, 69, 2117); W32(bank, 73, 2117 + s0);    // offset table
    std::memcpy(&bank[2117], shp0.data(), s0);
    std::memcpy(&bank[2117 + s0], shp1.data(), s1);
    const u8 seq[8] = {1,2,3,4,5,6,7,8};
    std::memcpy(&bank[seqOff], seq, 8);

    u8* nb = ShapeBankConvertNew(bank.data(), 1, /*keepSource=*/true);
    CHECK(nb != nullptr);
    CHECK(nb != bank.data());

    // New bank header: format 1, count restored by AddShape, exact cursor.
    CHECK(std::memcmp(nb, "SHAPBANK", 8) == 0);
    CHECK_EQ((int)nb[52], 1);
    CHECK_EQ((int)R16(nb, 42), 2);
    const u32 pixSum = R32(shp0.data(), 46) + R32(shp1.data(), 46);  // 2 + 2
    CHECK_EQ((int)pixSum, 4);
    const u32 expCursor = 2117 + (s0 - 2) + (s1 - 2);  // each shape shrinks by pix
    CHECK_EQ((int)R32(nb, 48), (int)expCursor);

    // Shapes land at 2117 / 2117+(s0-2) and equal the direct converter output.
    CHECK_EQ((int)R32(nb, 69), 2117);
    CHECK_EQ((int)R32(nb, 73), (int)(2117 + s0 - 2));
    u8* c0 = ShapeConvertRgbTo16(State565(), shp0.data());
    u8* c1 = ShapeConvertRgbTo16(State565(), shp1.data());
    CHECK(std::memcmp(nb + 2117, c0, R32(c0, 0)) == 0);
    CHECK(std::memcmp(nb + 2117 + s0 - 2, c1, R32(c1, 0)) == 0);
    std::free(c0); std::free(c1);

    // Sequence data re-appended at the new cursor, offset field updated.
    CHECK_EQ((int)R32(nb, 62), (int)expCursor);
    CHECK(std::memcmp(nb + expCursor, seq, 8) == 0);

    // The alloc formula writeCursor - 3*pix + 2*pix covers exactly cursor + seq.
    CHECK_EQ((int)(R32(bank.data(), 48) - pixSum), (int)(expCursor + 8));

    std::free(nb);
}

TEST(ShapeConvert16, BankConvertNewEdgeReturns) {
    SetActiveShapeConvertState(State565());
    InstallShapeConvertersIntoLeaves9();

    // writeCursor == 0 -> null (source NOT freed).
    std::vector<u8> empty(0x900, 0);
    CHECK(ShapeBankConvertNew(empty.data(), 1, true) == nullptr);

    // Non-convertible format/target combos return the SAME pointer.
    std::vector<u8> b1(0x900, 0);
    W32(b1, 48, 0x845);
    b1[52] = 1;                                       // already format 1
    CHECK(ShapeBankConvertNew(b1.data(), 1, true) == b1.data());
    b1[52] = 2;
    CHECK(ShapeBankConvertNew(b1.data(), 2, true) == b1.data());  // target != 1
}

// ---------------------------------------------------------------------------
// W10-TEX hardening — a MALFORMED shape whose dimensions exceed the converters'
// fixed stack staging arrays (WORD[1152] row, DWORD[864] row table) would have
// overrun the stack in gilde.exe. The faithful guard rejects it (null) instead
// of corrupting the stack; the in-contract paths above stay byte-identical.
// ASAN/UBSAN proves no OOB write occurs.
// ---------------------------------------------------------------------------
TEST(ShapeConvert16, MalformedOversizedShapesRejected) {
    ShapeConvertState st = State565();

    // RAW 24bpp shape claiming width 2000 (> 1152) but carrying NO pixel payload:
    // the width guard must fire BEFORE the per-pixel row staging.
    {
        std::vector<u8> s(50, 0);
        W16(s, 6, 2000); W16(s, 10, 1);          // width 2000, height 1
        s[12] = 2;
        W32(s, 38, 0xFFFFFFFFu);                 // raw branch
        W32(s, 0, (u32)s.size());
        W32(s, 46, 0);                           // 0 pixels (alloc input)
        CHECK(ShapeConvertRgbTo16(st, s.data()) == nullptr);
    }

    // A shape with height 2000 (> 864): the row-table guard fires first.
    {
        std::vector<u8> s(50, 0);
        W16(s, 6, 1); W16(s, 10, 2000);          // height 2000
        s[12] = 2;
        W32(s, 38, 0xFFFFFFFFu);
        W32(s, 0, (u32)s.size());
        W32(s, 46, 0);
        CHECK(ShapeConvertRgbTo16(st, s.data()) == nullptr);
        // The 8bpp converter shares the same height guard.
        W32(s, 38, 1);                           // RLE branch (not -1)
        CHECK(ShapeConvert8To16(st, s.data()) == nullptr);
    }

    // An RLE 24bpp shape whose first run declares n = 5000 pixels (> 1152): the
    // per-run guard rejects after the dst alloc (no leak, no stack overrun).
    {
        std::vector<std::vector<Run24>> rows = {
            { Run24{ 0, std::vector<std::array<u8,3>>(5000, {1, 2, 3}) } }
        };
        std::vector<u8> s = BuildRleShape24(5000, 1, 0, rows);
        CHECK(ShapeConvertRgbTo16(st, s.data()) == nullptr);
    }

    // A within-contract small shape still converts (the guard does not regress
    // the valid path): 4x1 raw shape packs fine.
    {
        std::vector<u8> s(50, 0);
        W16(s, 6, 2); W16(s, 10, 1);
        s[12] = 2;
        W32(s, 38, 0xFFFFFFFFu);
        const u8 px[6] = {255,0,0, 0,8,0};
        s.insert(s.end(), px, px + 6);
        W32(s, 0, (u32)s.size());
        W32(s, 46, 2);
        u8* dst = ShapeConvertRgbTo16(st, s.data());
        CHECK(dst != nullptr);
        std::free(dst);
    }
}
