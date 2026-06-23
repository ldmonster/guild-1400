// Unit tests for guild::render menu_widgets: DrawThreeSliceButton +
// DrawWindowFrame against a SYNTHETIC in-memory gilde.gfx archive (real on-disk
// format).  We build a 6-shape _BUTTON_RED-shaped record (caps + centre, two
// states) and a single-panel frame record, then assert the composed pixel
// pattern: caps at the ends, stretched centre in the middle; frame borders on
// all four edges.
#include "test.h"

#include "render/gfx_archive.h"
#include "render/menu_widgets.h"

#include <array>
#include <cstdint>
#include <vector>

using guild::render::GfxArchive;
using guild::render::DecodedShape;
using guild::render::DrawThreeSliceButton;
using guild::render::DrawWindowFrame;
using guild::render::ButtonSliceInfo;
using guild::render::FrameDrawInfo;
using guild::u8;
using guild::u16;
using guild::u32;

namespace {

void PutU16(std::vector<u8>& b, std::size_t at, u16 v) {
    if (b.size() < at + 2) b.resize(at + 2, 0);
    b[at] = (u8)(v & 0xFF); b[at + 1] = (u8)(v >> 8);
}
void PutU32(std::vector<u8>& b, std::size_t at, u32 v) {
    if (b.size() < at + 4) b.resize(at + 4, 0);
    b[at] = (u8)(v & 0xFF); b[at + 1] = (u8)((v >> 8) & 0xFF);
    b[at + 2] = (u8)((v >> 16) & 0xFF); b[at + 3] = (u8)((v >> 24) & 0xFF);
}

// One solid-colour shape (w x h, every pixel = (R,G,B), fully opaque).
struct ShapeSpec { int w, h; u8 R, G, B; };

// Build a SHAPBANK blob holding `specs` shapes, each a solid colour rectangle.
std::vector<u8> BuildBlob(const std::vector<ShapeSpec>& specs) {
    std::vector<u8> blob(0x80, 0);
    PutU16(blob, 0x2A, (u16)specs.size());     // shapeCount

    std::vector<std::size_t> shapeOffsets;
    for (const ShapeSpec& sp : specs) {
        const std::size_t shBase = blob.size();
        shapeOffsets.push_back(shBase);
        blob.resize(shBase + 0x32, 0);
        PutU16(blob, shBase + 6, (u16)sp.w);
        PutU16(blob, shBase + 0x0A, (u16)sp.h);

        // Emit each row: one run of w opaque pixels, no skip.
        std::vector<u32> rowOffsets(sp.h, 0);
        for (int r = 0; r < sp.h; ++r) {
            rowOffsets[r] = (u32)(blob.size() - shBase);
            std::size_t at = blob.size();
            PutU32(blob, at, 1); at += 4;           // runCount = 1
            PutU32(blob, at, 0); at += 4;           // skipBytes = 0
            PutU32(blob, at, (u32)sp.w); at += 4;   // lenPixels = w
            for (int c = 0; c < sp.w; ++c) {
                blob.resize(at + 3);
                blob[at] = sp.R; blob[at + 1] = sp.G; blob[at + 2] = sp.B;
                at += 3;
            }
        }
        const u32 rowTabRel = (u32)(blob.size() - shBase);
        PutU32(blob, shBase + 0x2A, rowTabRel);
        for (int r = 0; r < sp.h; ++r) PutU32(blob, blob.size(), rowOffsets[r]);
    }

    // Patch the per-shape offset table (relative to blob) at 0x45.
    for (std::size_t i = 0; i < shapeOffsets.size(); ++i)
        PutU32(blob, 0x45 + i * 4, (u32)shapeOffsets[i]);
    return blob;
}

// Wrap blobs into a flat gilde.gfx archive image (4-byte count + 84-byte records
// + concatenated blobs).
std::vector<u8> BuildArchive(const std::vector<std::pair<std::string,
                             std::vector<u8>>>& recs) {
    const u32 count = (u32)recs.size();
    std::size_t headerEnd = 4 + (std::size_t)count * 84;
    std::vector<u8> img(headerEnd, 0);
    PutU32(img, 0, count);

    std::size_t dataAt = headerEnd;
    for (u32 i = 0; i < count; ++i) {
        const std::size_t base = 4 + (std::size_t)i * 84;
        const std::string& name = recs[i].first;
        for (std::size_t k = 0; k < name.size() && k < 47; ++k) img[base + k] = name[k];
        const std::vector<u8>& blob = recs[i].second;
        PutU32(img, base + 48, (u32)dataAt);     // dataOffset
        PutU32(img, base + 56, (u32)blob.size()); // dataSize
        img.insert(img.end(), blob.begin(), blob.end());
        dataAt += blob.size();
    }
    return img;
}

// Build a synthetic archive mirroring _BUTTON_RED (6 shapes) + a frame panel.
GfxArchive MakeArchive() {
    // _BUTTON_RED: caps 4 wide, centre 8 wide, height 5 (scaled-down stand-in).
    // State A: 0 left(red) 1 right(green) 2 centre(blue)
    // State B: 3 left      4 right        5 centre  (distinct shades)
    std::vector<u8> button = BuildBlob({
        {4, 5, 200, 0, 0},   // 0 left  cap  (red)
        {4, 5, 0, 200, 0},   // 1 right cap  (green)
        {8, 5, 0, 0, 200},   // 2 centre     (blue)
        {4, 5, 100, 0, 0},   // 3 left  cap  (dark red)
        {4, 5, 0, 100, 0},   // 4 right cap  (dark green)
        {8, 5, 0, 0, 100},   // 5 centre     (dark blue)
    });
    // _MAIN_MENU_RAHMEN stand-in: a single solid 24x24 panel (one shape).
    std::vector<u8> panel = BuildBlob({{24, 24, 222, 180, 90}});

    GfxArchive arc;
    arc.LoadFromMemory(BuildArchive({
        {"_BUTTON_RED", button},
        {"_MAIN_MENU_RAHMEN", panel},
    }));
    return arc;
}

inline u32 At(const std::vector<u32>& fb, int W, int x, int y) {
    return fb[(std::size_t)y * W + x];
}

} // namespace

TEST(MenuWidgets, ThreeSliceCapsAndStretch) {
    GfxArchive arc = MakeArchive();
    CHECK(arc.ok());
    const int btn = arc.FindByName("_BUTTON_RED");
    CHECK_EQ(arc.ShapeCount(btn), 6);

    const int W = 64, H = 16;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    const int x = 5, y = 2, widthPx = 40;  // caps 4 + 4 -> centre span 32

    ButtonSliceInfo info =
        DrawThreeSliceButton(fb.data(), W, H, x, y, widthPx, arc, btn, false);
    CHECK(info.real);
    CHECK_EQ(info.capLeft, 4);
    CHECK_EQ(info.capRight, 4);
    CHECK_EQ(info.center, widthPx - 8);  // 32
    CHECK_EQ(info.height, 5);

    const int midY = y + 2;
    // Left cap region (first 4 cols) = red (200,0,0) => 0xFFC80000.
    CHECK_EQ(At(fb, W, x + 0, midY), 0xFFC80000u);
    CHECK_EQ(At(fb, W, x + 3, midY), 0xFFC80000u);
    // Centre region = blue (0,0,200) => 0xFF0000C8.
    CHECK_EQ(At(fb, W, x + 4, midY), 0xFF0000C8u);
    CHECK_EQ(At(fb, W, x + widthPx / 2, midY), 0xFF0000C8u);
    CHECK_EQ(At(fb, W, x + widthPx - 5, midY), 0xFF0000C8u);
    // Right cap (last 4 cols) = green (0,200,0) => 0xFF00C800.
    CHECK_EQ(At(fb, W, x + widthPx - 4, midY), 0xFF00C800u);
    CHECK_EQ(At(fb, W, x + widthPx - 1, midY), 0xFF00C800u);
    // Outside the button stays transparent.
    CHECK_EQ(At(fb, W, x + widthPx, midY), 0u);
    CHECK_EQ(At(fb, W, x - 1, midY), 0u);
}

TEST(MenuWidgets, ThreeSlicePressedState) {
    GfxArchive arc = MakeArchive();
    const int btn = arc.FindByName("_BUTTON_RED");
    const int W = 64, H = 16;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    ButtonSliceInfo info =
        DrawThreeSliceButton(fb.data(), W, H, 0, 0, 40, arc, btn, /*pressed*/true);
    CHECK(info.real);
    // Pressed centre = dark blue (0,0,100) => 0xFF000064.
    CHECK_EQ(At(fb, W, 20, 2), 0xFF000064u);
    // Pressed left cap = dark red (100,0,0) => 0xFF640000.
    CHECK_EQ(At(fb, W, 0, 2), 0xFF640000u);
}

TEST(MenuWidgets, ThreeSliceInertFallback) {
    GfxArchive empty;  // not ok()
    const int W = 64, H = 40;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    ButtonSliceInfo info =
        DrawThreeSliceButton(fb.data(), W, H, 2, 2, 40, empty, 174, false);
    CHECK(!info.real);                       // inert path used (no archive)
    CHECK(info.center > 0);
    // Something was drawn (non-transparent fill).
    CHECK(At(fb, W, 20, 10) != 0u);
}

TEST(MenuWidgets, FrameBordersAllFourEdges) {
    GfxArchive arc = MakeArchive();
    const int frame = arc.FindByName("_MAIN_MENU_RAHMEN");
    CHECK_EQ(arc.ShapeCount(frame), 1);

    const int W = 48, H = 48;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    const int x = 4, y = 4, w = 40, h = 40;
    FrameDrawInfo info =
        DrawWindowFrame(fb.data(), W, H, x, y, w, h, arc, frame);
    CHECK(info.real);
    CHECK(!info.tiled);                       // single-panel ring path
    CHECK(info.thickness >= 1);

    const u32 panelColor = 0xFFDEB45Au;       // (222,180,90)
    // All four edges have real border pixels.
    CHECK_EQ(At(fb, W, x + w / 2, y), panelColor);            // top
    CHECK_EQ(At(fb, W, x + w / 2, y + h - 1), panelColor);    // bottom
    CHECK_EQ(At(fb, W, x, y + h / 2), panelColor);            // left
    CHECK_EQ(At(fb, W, x + w - 1, y + h / 2), panelColor);    // right
    // Corners drawn.
    CHECK_EQ(At(fb, W, x, y), panelColor);
    CHECK_EQ(At(fb, W, x + w - 1, y + h - 1), panelColor);
    // Interior centre is NOT filled by the frame (border-only ring).
    CHECK_EQ(At(fb, W, x + w / 2, y + h / 2), 0u);
}

TEST(MenuWidgets, FrameInertFallback) {
    GfxArchive empty;
    const int W = 40, H = 40;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    FrameDrawInfo info =
        DrawWindowFrame(fb.data(), W, H, 2, 2, 30, 30, empty, 1776);
    CHECK(!info.real);                        // inert outline path
    // Outline pixel drawn at the rect border.
    CHECK(At(fb, W, 2, 2) != 0u);
}

// ===========================================================================
// wave-12 boundary / out-of-range hardening
// ===========================================================================

// Out-of-range gfxRecord indices (negative and beyond the archive count) must
// hit the inert fallback, never index a non-existent record. No OOB into the
// archive's record table.
TEST(MenuWidgets, ButtonOutOfRangeRecordInert) {
    GfxArchive arc = MakeArchive();   // 2 records (indices 0,1)
    const int W = 64, H = 40;
    std::vector<u32> fb((std::size_t)W * H, 0u);

    // gfxRecord = -1 (negative guard).
    ButtonSliceInfo n =
        DrawThreeSliceButton(fb.data(), W, H, 2, 2, 40, arc, -1, false);
    CHECK(!n.real);
    // gfxRecord far beyond the count.
    ButtonSliceInfo big =
        DrawThreeSliceButton(fb.data(), W, H, 2, 2, 40, arc, 9999, false);
    CHECK(!big.real);
    // Frame with both out-of-range record ids -> inert too.
    FrameDrawInfo fn = DrawWindowFrame(fb.data(), W, H, 2, 2, 30, 30, arc, -5);
    CHECK(!fn.real);
    FrameDrawInfo fb2 = DrawWindowFrame(fb.data(), W, H, 2, 2, 30, 30, arc, 9999);
    CHECK(!fb2.real);
}

// Degenerate sizes: widthPx<=0 / w<=0 / h<=0 must early-return with an inert
// (default-constructed) info and write nothing — no negative-extent loops.
TEST(MenuWidgets, DegenerateSizesEarlyReturn) {
    GfxArchive arc = MakeArchive();
    const int W = 64, H = 40;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    const int btn = arc.FindByName("_BUTTON_RED");
    const int frame = arc.FindByName("_MAIN_MENU_RAHMEN");

    ButtonSliceInfo b0 =
        DrawThreeSliceButton(fb.data(), W, H, 0, 0, 0, arc, btn, false);
    CHECK(!b0.real);
    ButtonSliceInfo bneg =
        DrawThreeSliceButton(fb.data(), W, H, 0, 0, -10, arc, btn, false);
    CHECK(!bneg.real);
    FrameDrawInfo f0 = DrawWindowFrame(fb.data(), W, H, 0, 0, 0, 10, arc, frame);
    CHECK(!f0.real);
    FrameDrawInfo fh = DrawWindowFrame(fb.data(), W, H, 0, 0, 10, -3, arc, frame);
    CHECK(!fh.real);
    // The framebuffer must be untouched by the degenerate calls.
    bool clean = true;
    for (u32 p : fb) if (p != 0u) { clean = false; break; }
    CHECK(clean);
}

// Narrow button: widthPx smaller than the two caps combined forces the overlap
// path (centreSpan<0 -> clamped). Caps are clipped, no centre, no OOB write.
TEST(MenuWidgets, ButtonTooNarrowCapsClamp) {
    GfxArchive arc = MakeArchive();
    const int btn = arc.FindByName("_BUTTON_RED");  // caps 4+4 = 8 px
    const int W = 64, H = 16;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    // widthPx = 5 < 8: overlap path.
    ButtonSliceInfo info =
        DrawThreeSliceButton(fb.data(), W, H, 3, 2, 5, arc, btn, false);
    CHECK(info.real);
    CHECK_EQ(info.center, 0);
    CHECK(info.capLeft <= 5);
    CHECK(info.capRight <= 5);
}

// Fully off-buffer placement (origin far negative AND far positive): every blit
// pixel is clipped by the InBounds tests; nothing is written, no OOB.
TEST(MenuWidgets, OffBufferPlacementClipped) {
    GfxArchive arc = MakeArchive();
    const int btn = arc.FindByName("_BUTTON_RED");
    const int frame = arc.FindByName("_MAIN_MENU_RAHMEN");
    const int W = 32, H = 24;
    std::vector<u32> fb((std::size_t)W * H, 0u);

    DrawThreeSliceButton(fb.data(), W, H, -1000, -1000, 40, arc, btn, false);
    DrawThreeSliceButton(fb.data(), W, H, 100000, 100000, 40, arc, btn, false);
    // Frame larger than the buffer, origin off-screen: tiled/ring loops clipped.
    DrawWindowFrame(fb.data(), W, H, -50, -50, 200, 200, arc, frame);
    bool clean = true;
    for (u32 p : fb) if (p != 0u) { clean = false; break; }
    CHECK(clean);   // all off-buffer -> nothing drawn, ASAN-clean
}
