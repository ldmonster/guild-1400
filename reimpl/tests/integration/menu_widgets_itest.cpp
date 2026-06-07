// Integration test: compose a 3-slice button AND a window frame into one shared
// 32bpp ARGB buffer (synthetic archive in the real on-disk gilde.gfx format) and
// assert the resulting GEOMETRY matches the decoded slice dims — cap widths,
// centre span, frame thickness — and that two renders are byte-identical.
#include "test.h"

#include "render/gfx_archive.h"
#include "render/menu_widgets.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using guild::render::GfxArchive;
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

struct ShapeSpec { int w, h; u8 R, G, B; };

std::vector<u8> BuildBlob(const std::vector<ShapeSpec>& specs) {
    std::vector<u8> blob(0x80, 0);
    PutU16(blob, 0x2A, (u16)specs.size());
    std::vector<std::size_t> shapeOffsets;
    for (const ShapeSpec& sp : specs) {
        const std::size_t shBase = blob.size();
        shapeOffsets.push_back(shBase);
        blob.resize(shBase + 0x32, 0);
        PutU16(blob, shBase + 6, (u16)sp.w);
        PutU16(blob, shBase + 0x0A, (u16)sp.h);
        std::vector<u32> rowOffsets(sp.h, 0);
        for (int r = 0; r < sp.h; ++r) {
            rowOffsets[r] = (u32)(blob.size() - shBase);
            std::size_t at = blob.size();
            PutU32(blob, at, 1); at += 4;
            PutU32(blob, at, 0); at += 4;
            PutU32(blob, at, (u32)sp.w); at += 4;
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
    for (std::size_t i = 0; i < shapeOffsets.size(); ++i)
        PutU32(blob, 0x45 + i * 4, (u32)shapeOffsets[i]);
    return blob;
}

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
        PutU32(img, base + 48, (u32)dataAt);
        PutU32(img, base + 56, (u32)blob.size());
        img.insert(img.end(), blob.begin(), blob.end());
        dataAt += blob.size();
    }
    return img;
}

// A _WIN_BORDER-style tiled record (>=9 shapes): 4 corners (3x3), h-edge (3x1),
// v-edge (1x3) at the indices DrawWindowFrame samples (0..5,9).
std::vector<u8> BuildTiledBorder() {
    std::vector<ShapeSpec> s(14, ShapeSpec{3, 3, 0, 0, 0});
    s[0] = {3, 3, 255, 0, 0};   // TL
    s[1] = {3, 3, 255, 0, 0};   // TR
    s[2] = {3, 3, 255, 0, 0};   // BL
    s[3] = {3, 3, 255, 0, 0};   // BR
    s[5] = {3, 1, 0, 255, 0};   // horizontal edge
    s[9] = {1, 3, 0, 0, 255};   // vertical edge
    return BuildBlob(s);
}

GfxArchive MakeArchive() {
    std::vector<u8> button = BuildBlob({
        {12, 33, 200, 0, 0}, {12, 33, 0, 200, 0}, {100, 33, 0, 0, 200},
        {12, 33, 100, 0, 0}, {12, 33, 0, 100, 0}, {100, 33, 0, 0, 100},
    });
    std::vector<u8> panel = BuildBlob({{30, 32, 222, 180, 90}});
    GfxArchive arc;
    arc.LoadFromMemory(BuildArchive({
        {"_BUTTON_RED", button},
        {"_MAIN_MENU_RAHMEN", panel},
        {"_WIN_BORDER", BuildTiledBorder()},
    }));
    return arc;
}

void Compose(const GfxArchive& arc, std::vector<u32>& fb, int W, int H,
             ButtonSliceInfo& bi, FrameDrawInfo& fi) {
    fb.assign((std::size_t)W * H, 0u);
    const int frame = arc.FindByName("_MAIN_MENU_RAHMEN");
    const int btn = arc.FindByName("_BUTTON_RED");
    fi = DrawWindowFrame(fb.data(), W, H, 20, 20, 200, 160, arc, frame);
    bi = DrawThreeSliceButton(fb.data(), W, H, 40, 60, 150, arc, btn, false);
}

} // namespace

TEST(MenuWidgetsItest, GeometryMatchesSliceDims) {
    GfxArchive arc = MakeArchive();
    CHECK(arc.ok());
    const int W = 256, H = 256;
    std::vector<u32> fb;
    ButtonSliceInfo bi; FrameDrawInfo fi;
    Compose(arc, fb, W, H, bi, fi);

    // Button geometry: caps 12px each, centre = widthPx - 24.
    CHECK(bi.real);
    CHECK_EQ(bi.capLeft, 12);
    CHECK_EQ(bi.capRight, 12);
    CHECK_EQ(bi.center, 150 - 24);            // 126
    CHECK_EQ(bi.height, 33);

    // Verify the cap/centre/cap transition in the framebuffer at the button row.
    const int by = 60 + 16, bx = 40;
    CHECK_EQ(fb[(std::size_t)by * W + (bx + 0)],  0xFFC80000u);   // left cap red
    CHECK_EQ(fb[(std::size_t)by * W + (bx + 11)], 0xFFC80000u);
    CHECK_EQ(fb[(std::size_t)by * W + (bx + 12)], 0xFF0000C8u);   // centre blue
    CHECK_EQ(fb[(std::size_t)by * W + (bx + 137)], 0xFF0000C8u);
    CHECK_EQ(fb[(std::size_t)by * W + (bx + 138)], 0xFF00C800u);  // right cap green
    CHECK_EQ(fb[(std::size_t)by * W + (bx + 149)], 0xFF00C800u);

    // Frame: real, single-panel ring, nonzero thickness; corners are panel colour.
    CHECK(fi.real);
    CHECK(fi.thickness >= 1);
    CHECK_EQ(fb[(std::size_t)20 * W + 20], 0xFFDEB45Au);          // TL corner
}

TEST(MenuWidgetsItest, TiledBorderThicknessFromTiles) {
    GfxArchive arc = MakeArchive();
    const int W = 128, H = 128;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    const int frame = arc.FindByName("_WIN_BORDER");
    FrameDrawInfo fi =
        DrawWindowFrame(fb.data(), W, H, 10, 10, 80, 60, arc, frame);
    CHECK(fi.real);
    CHECK(fi.tiled);                          // multi-shape tiled path
    CHECK_EQ(fi.thickness, 3);                // 3x3 corner tile dimension
    // Top-left corner tile is red.
    CHECK_EQ(fb[(std::size_t)10 * W + 10], 0xFFFF0000u);
    // A top-edge pixel (between corners) is green (h-edge).
    CHECK_EQ(fb[(std::size_t)10 * W + 40], 0xFF00FF00u);
}

TEST(MenuWidgetsItest, Deterministic) {
    GfxArchive arc = MakeArchive();
    const int W = 256, H = 256;
    std::vector<u32> a, b;
    ButtonSliceInfo bi; FrameDrawInfo fi;
    Compose(arc, a, W, H, bi, fi);
    Compose(arc, b, W, H, bi, fi);
    CHECK(a == b);
}
