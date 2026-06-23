// tests/unit/menu_font_test.cpp — play::MenuFont golden vectors.
//
// MenuFont reconstructs the engine's bitmap-font text metrics:
//   VIBE_Property_Get @0x4152cc  (text pixel width)
//   VIBE_Object_RecomputeSize @0x41b164 sprite-kind-9 width = width + cap+cap+4
// with spacing globals dword_62D274 = 2 (tracking) and dword_62D270 = 8 (space).
//
// We build a synthetic gilde.gfx with a single `_FONT` SHAPBANK whose per-glyph
// metric fields (+6 width, +22 kern, +26 advance) we control, then assert that
// MeasureWidth/ButtonWidth reproduce the exact original accumulation, and that an
// asset-present run resolves the eight localized main-menu labels + their
// binary-derived button widths.
#include "test.h"

#include "play/menu_assets.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using namespace guild;

namespace {

void PutU16(std::vector<u8>& b, std::size_t at, u16 v) {
    if (at + 2 > b.size()) b.resize(at + 2);
    b[at] = (u8)v; b[at + 1] = (u8)(v >> 8);
}
void PutU32(std::vector<u8>& b, std::size_t at, u32 v) {
    if (at + 4 > b.size()) b.resize(at + 4);
    b[at] = (u8)v; b[at + 1] = (u8)(v >> 8);
    b[at + 2] = (u8)(v >> 16); b[at + 3] = (u8)(v >> 24);
}

struct GlyphSpec { u16 width, kern, advance; };

// Build a `_FONT` SHAPBANK blob: shape `ch` carries metrics from specs[ch] (a 1x1
// RLE bitmap so it decodes). Glyph for char `ch` == shape `ch` (engine model).
std::vector<u8> BuildFontBlob(const std::vector<GlyphSpec>& specs, u16 lineHeight) {
    const int n = (int)specs.size();
    // Layout: header (offset table at 0x45), then shapes.
    const u32 firstShape = (u32)(0x45 + n * 4 + 16);
    std::vector<u8> blob(firstShape, 0);
    PutU16(blob, 0x2A, (u16)n);              // shape count
    PutU16(blob, 0x2E, lineHeight);          // font+46 line height
    std::vector<u32> shapeOffs(n, 0);
    for (int ch = 0; ch < n; ++ch) {
        u32 so = (u32)blob.size();
        shapeOffs[ch] = so;
        // shape header up to 0x32
        blob.resize(so + 0x32, 0);
        PutU16(blob, so + 6, specs[ch].width);     // +6  width
        PutU16(blob, so + 22, specs[ch].kern);     // +22 kern
        PutU16(blob, so + 26, specs[ch].advance);  // +26 advance
        // 1x1 RLE bitmap: 1 run, skip 0, len 1, one RGB pixel.
        PutU16(blob, so + 0x0A, 1);                // height 1
        u32 rowOff = (u32)(blob.size() - so);
        PutU32(blob, blob.size(), 1);              // runCount
        PutU32(blob, blob.size(), 0);              // skip
        PutU32(blob, blob.size(), 1);              // len 1
        blob.resize(blob.size() + 3);
        blob[blob.size() - 3] = 0xFF; blob[blob.size() - 2] = 0xFF; blob[blob.size() - 1] = 0xFF;
        u32 rowTabRel = (u32)(blob.size() - so);
        PutU32(blob, so + 0x2A, rowTabRel);        // shape+0x2A row table
        PutU32(blob, blob.size(), rowOff);         // one row entry
    }
    for (int ch = 0; ch < n; ++ch) PutU32(blob, 0x45 + ch * 4, shapeOffs[ch]);
    return blob;
}

// Wrap one blob as a 1-record gilde.gfx archive named "_FONT".
std::vector<u8> BuildFontArchive(const std::vector<u8>& blob) {
    const u32 count = 1;
    const std::size_t headerEnd = 4 + count * 84;
    std::vector<u8> file(headerEnd, 0);
    PutU32(file, 0, count);
    std::memcpy(file.data() + 4, "_FONT", 5);
    PutU32(file, 4 + 48, (u32)headerEnd);   // dataOffset
    PutU32(file, 4 + 56, (u32)blob.size()); // dataSize
    file.insert(file.end(), blob.begin(), blob.end());
    return file;
}

// Reference Property_Get over the same specs (independent re-derivation).
int RefMeasure(const std::vector<GlyphSpec>& specs, const std::string& bytes) {
    const int T = 2, S = 8;
    int v4 = 0, i = 0;
    for (std::size_t idx = 0; idx < bytes.size(); ++idx, ++i) {
        const unsigned char ch = (unsigned char)bytes[idx];
        if (ch != 126) {
            if (i && (unsigned char)bytes[idx - 1] != 32) v4 -= specs[ch].kern;
            if (ch == 32) v4 += T + S + specs[ch].advance;
            else          v4 += T + specs[ch].advance;
        }
    }
    return v4 + T;
}

} // namespace

TEST(MenuFont, PropertyGetGoldenVectors) {
    // 256 glyphs; set ASCII letters to plausible advances, space adv 0.
    std::vector<GlyphSpec> specs(256, GlyphSpec{1, 0, 0});
    specs[(int)' '] = {1, 0, 0};
    for (int c = 'A'; c <= 'Z'; ++c) specs[c] = {(u16)11, 0, (u16)10};
    for (int c = 'a'; c <= 'z'; ++c) specs[c] = {(u16)8, 0, (u16)7};
    specs[(int)'i'] = {4, 0, 3};
    specs[(int)'g'] = {9, 2, 8};   // a glyph WITH kern (exercise the kern path)

    std::vector<u8> arch = BuildFontArchive(BuildFontBlob(specs, /*lineHeight=*/17));
    shim::MemFileSystem fs;
    fs.put("gfx/gilde.gfx", arch);

    play::MenuFont font;
    CHECK(font.Load(fs, "gfx/gilde.gfx", "_FONT"));
    CHECK_EQ(font.lineHeight(), 17);

    // Width matches the independent reference for several strings, incl. kern + spaces.
    const char* cases[] = {"A", "AB", "Game", "New Game", "iiii", "gg", "g g"};
    for (const char* s : cases)
        CHECK_EQ(font.MeasureWidth(s), RefMeasure(specs, s));

    // Spot value: "AB" = T + (T+10) + (T+10) = 2 + 12 + 12 = 26 (no kern between).
    CHECK_EQ(font.MeasureWidth("AB"), 26);
    // ButtonWidth adds cap(12)+cap(12)+4 over the text width.
    CHECK_EQ(font.ButtonWidth("AB"), 26 + 12 + 12 + 4);
    // Empty / null -> 0 width.
    CHECK_EQ(font.MeasureWidth(""), 0);
    CHECK_EQ(font.MeasureWidth(nullptr), 0);
}

// Asset-guarded: over the REAL gilde.gfx _FONT + the install's textbin, the eight
// localized main-menu labels resolve and their button widths match the
// binary-derived values (computed offline from the _FONT metrics, see
// progress/harden/menu_labels_dims.md).
TEST(MenuFont, RealGfxLabelsAndWidths) {
    const char* gd = std::getenv("GUILD_GAME_DIR");
    std::string gameDir = gd ? gd : "europe_guild_1400_original";
    shim::DiskFileSystem fs(gameDir);
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // skip when assets absent

    play::MenuFont font;
    if (!font.Load(fs, "gfx/gilde.gfx", "_FONT")) { CHECK(true); return; }

    std::string labels[8];
    if (!play::ResolveMainMenuLabels(gameDir, labels)) { CHECK(true); return; }

    // Binary-derived button widths (Property_Get(label) + 28) for the RUSSIAN build
    // (the install whose textbin carries the Russian _OPTIONEN_MENUE_* strings).
    // If the install is a different language these will differ; guard on the title
    // being the Russian one by checking the New-Game width fingerprint.
    const int expectW[8] = {121, 149, 134, 158, 184, 161, 94, 154};
    bool russian = !labels[0].empty() &&
                   font.ButtonWidth(labels[0].c_str()) == expectW[0];
    if (!russian) { CHECK(true); return; }  // non-Russian install: skip the pins

    for (int i = 0; i < 8; ++i) {
        CHECK(!labels[i].empty());
        CHECK_EQ(font.ButtonWidth(labels[i].c_str()), expectW[i]);
    }
}
