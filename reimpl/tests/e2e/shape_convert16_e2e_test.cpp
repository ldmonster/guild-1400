// e2e (GUARDED on the real gfx/gilde.gfx): the 16bpp shape-converter chain over
// REAL shipped artwork — the proof that the named gap of progress/session-hud.md
// is CLOSED.
//
//   * Loads the real gilde.gfx directory (render::GfxArchive) and pulls the raw
//     _WIN_BORDER SHAPBANK blob (the depth-2 bank session_hud_e2e proved to be a
//     no-op in the depth-1 blitter).
//   * Converts it with the REAL chain: render::ShapeBankConvertNew @0x5d80a8 ->
//     render_leaves9 Shape_ConvertToNew @0x5d8080 -> ShapeConvertRgbTo16
//     @0x5d7c0c -> ShapeBankAddShape @0x5d8330.
//   * Blits shape 0 of the CONVERTED bank through the REAL ShapeShowFromBank
//     @0x5d861c and asserts non-trivial deterministic pixels — flipping the
//     previous behavioral proof (rv==1 && zero pixels painted).
//   * Cross-checks the converted run pixels against the independently
//     reconstructed depth-2 decode (render::DecodeShapeBlob, 0x5fbb24) +
//     PackColor (0x434f30), and drives the play::SetHudSpriteBankFromGfx bridge
//     feed end-to-end.
//
// Skips cleanly when the asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "render/shape_convert16.h"
#include "render/render_leaves9.h"
#include "render/gfx_archive.h"
#include "render/sprite_scale.h"
#include "render/shape_blit.h"
#include "render/animation_decode.h"
#include "render/colorformat.h"
#include "play/wire_hud_bridge.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using guild::u8;
using guild::u16;
using guild::u32;
using namespace guild::render;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

inline u16 R16(const u8* b, size_t off) { u16 v; std::memcpy(&v, b + off, 2); return v; }
inline u32 R32(const u8* b, size_t off) { u32 v; std::memcpy(&v, b + off, 4); return v; }

int NonZero(const std::vector<u16>& fb) {
    int n = 0;
    for (u16 p : fb) if (p) ++n;
    return n;
}

// Pull one record's raw SHAPBANK blob straight from the file (the same bytes
// VIBE_State_Helper @0x40e014 reads from the +48/+56 offset/size pair).
bool ReadBankBlob(guild::shim::DiskFileSystem& fs, const GfxRecord& rec,
                  std::vector<u8>& out) {
    guild::shim::IFile* f = fs.open("gfx/gilde.gfx", "rb");
    if (!f) return false;
    out.resize(rec.dataSize);
    f->seek((long)rec.dataOffset, 0);
    const bool ok = f->read(out.data(), out.size()) == out.size();
    fs.close(f);
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(ShapeConvert16Real, RealDepth2BankConvertsAndBlitsRealArtwork) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    GfxArchive ar;
    CHECK(ar.LoadFromFile(fs, "gfx/gilde.gfx"));
    const int idx = ar.FindByName("_WIN_BORDER");
    CHECK(idx >= 0);
    if (idx < 0) return;

    std::vector<u8> raw;
    CHECK(ReadBankBlob(fs, ar.record((size_t)idx), raw));
    CHECK(std::memcmp(raw.data(), "SHAPBANK", 8) == 0);
    CHECK_EQ((int)raw[52], 2);                        // the depth-2 bank of the gap

    // The REAL conversion chain (rule-13 wiring through the leaves9 hooks).
    SetActiveShapeConvertState([]{
        ShapeConvertState st;
        st.fmt = Format565();
        st.darkMask = ShapeConvertDarkMask(st.fmt);
        return st;
    }());
    InstallShapeConvertersIntoLeaves9();
    u8* conv = ShapeBankConvertNew(raw.data(), 1, /*keepSource=*/true);
    CHECK(conv != nullptr);
    CHECK(conv != raw.data());
    if (!conv) return;

    // Converted bank: pixel format 1, same shape count, every shape depth 1.
    CHECK_EQ((int)conv[52], 1);
    const int count = (int)R16(conv, 42);
    CHECK_EQ(count, (int)R16(raw.data(), 42));
    CHECK(count > 0);
    for (int i = 0; i < count; ++i) {
        const u32 off = R32(conv, 69 + 4u * (u32)i);
        CHECK_EQ((int)conv[off + 12], 1);
    }

    // Cross-check shape 0 against the independent depth-2 decode (@0x5fbb24):
    // the converted opaque-pixel count must match, and the first opaque pixel
    // must be its PackColor (with the 0 -> (5,5,5) slot replacement).
    DecodedShape ds;
    CHECK(DecodeShapeBlob(raw.data(), raw.size(), 0, ds));
    const u32 shp0 = R32(conv, 69);
    CHECK_EQ((int)R32(conv, shp0 + 46), ds.opaque);
    // First run of first non-empty row: locate via the converted row stream.
    {
        const u8* shp = conv + shp0;
        const u16 h = R16(shp, 10);
        const u8* p = shp + 50;
        u16 firstPx = 0;
        bool found = false;
        for (int r = 0; r < (int)h && !found; ++r) {
            const u32 runs = R32(p, 0); p += 4;
            for (u32 k = 0; k < runs; ++k) {
                const u32 n = R32(p, 4);
                if (n) { firstPx = R16(p, 8); found = true; break; }
                p += 8 + 2u * n;
            }
        }
        CHECK(found);
        // The matching source pixel is the first opaque ARGB of the decode.
        u32 argb = 0;
        for (u32 px : ds.argb) if (px) { argb = px; break; }
        const u8 R = (u8)(argb >> 16), G = (u8)(argb >> 8), B = (u8)argb;
        u16 expect = (u16)PackColor(Format565(), R, G, B);
        if (expect == 0) expect = (u16)PackColor(Format565(), 5, 5, 5);
        CHECK_EQ((int)firstPx, (int)expect);
    }

    // THE FLIP: the same REAL leaf that was a proven no-op on the raw bank
    // (session_hud_e2e RealDepth2BankIsANoOpInTheBlitter) now paints REAL pixels.
    std::vector<u16> px(64 * 64, 0);
    ColorBlitTarget16 dst{64, px.data()};
    FrameBlitState st;
    const int rv = ShapeShowFromBank(4, 4, conv, 0, dst, Format565(), st);
    CHECK_EQ(rv, 1);
    CHECK(NonZero(px) > 0);                           // previously == 0
    CHECK(NonZero(px) <= ds.opaque);                  // only opaque runs land

    // Deterministic: a second convert is byte-identical; a second blit too.
    u8* conv2 = ShapeBankConvertNew(raw.data(), 1, true);
    CHECK(conv2 != nullptr);
    if (conv2) {
        CHECK_EQ((int)R32(conv2, 48), (int)R32(conv, 48));
        CHECK(std::memcmp(conv, conv2, R32(conv, 48)) == 0);
        std::free(conv2);
    }
    std::vector<u16> px2(64 * 64, 0);
    ColorBlitTarget16 dst2{64, px2.data()};
    ShapeShowFromBank(4, 4, conv, 0, dst2, Format565(), st);
    CHECK(px == px2);

    std::free(conv);
}

// ---------------------------------------------------------------------------
// The HUD bridge feed: play::SetHudSpriteBankFromGfx converts the real blob and
// the bridge's own blit entry paints real artwork — NO SessionHud edit involved.
TEST(ShapeConvert16Real, HudBridgeFeedsRealConvertedBank) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    GfxArchive ar;
    CHECK(ar.LoadFromFile(fs, "gfx/gilde.gfx"));
    const int idx = ar.FindByName("_WIN_BORDER");
    CHECK(idx >= 0);
    if (idx < 0) return;
    std::vector<u8> raw;
    CHECK(ReadBankBlob(fs, ar.record((size_t)idx), raw));

    const u8* bank = guild::play::SetHudSpriteBankFromGfx(raw.data(), raw.size());
    CHECK(bank != nullptr);
    if (!bank) return;
    CHECK_EQ((int)bank[52], 1);                       // depth-1 feed installed

    std::vector<u16> px(64 * 64, 0);
    const int rv = guild::play::BlitHudSprite(4, 4, 0, px.data(), 64,
                                              Format565());
    CHECK_EQ(rv, 1);
    CHECK(NonZero(px) > 0);                           // REAL artwork pixels

    // Revert: the bridge releases its converted bank.
    guild::play::SetHudSpriteBankFromGfx(nullptr, 0);
    CHECK(true);
}
