#include "tests/framework/test.h"

// ===========================================================================
// Pins for the SIDEBAR CARD mechanisms (suite prefix: SessionCard):
//
//  * The KEYED localized building-name resolve: with a scene TYPE NAME fed
//    (SessionPanelsInputs.selBuildingTypeName), the building panel line
//    resolves "_GEB_<TYPE>_NAME+0" from the TextDb KEY namespace (the engine's
//    own per-type name records), '~' soft-wrap hints become spaces, and the
//    custom name still appends as >name< (the gold-font low-quote glyphs).
//  * The card-text HOOK seam: with drawBox off and a hook installed, panel
//    text lines route through the hook (the small-gold-face renderer); a
//    declining hook falls back to the built-in 5x7 face.
//  * The HUD sprite-id mapping (wire_hud_bridge SetHudSpriteIdBase): legacy
//    mode blits shape 0 for every id; id-mapped mode blits shape (id - base)
//    through the PLAIN COLOUR RLE leaf (ShapeDecodeRle @0x5d70cc — colours
//    preserved, not the grey BlitColored16), and declines out-of-range ids.
// ===========================================================================
#include "gui/infopanel_build.h"
#include "gui/text/textdb.h"
#include "play/hud_render.h"
#include "play/session_panels.h"
#include "play/wire_hud_bridge.h"
#include "render/colorformat.h"
#include "render/types.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using guild::play::SessionPanels;
using guild::play::SessionPanelsInputs;

namespace {

constexpr int kW = 320, kH = 240, kPitch = kW * 2;

std::vector<u16> MakeFb() { return std::vector<u16>((size_t)kW * kH, 0); }

render::Surface Surf(std::vector<u16>& fb) {
    render::Surface s{};
    s.width = kW; s.height = kH; s.pitch = kPitch; s.widthPx = kPitch / 2;
    s.bpp = 16;
    s.pixels = reinterpret_cast<u8*>(fb.data());
    s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = kW; s.clipY1 = kH;
    s.fmt = render::Format565();
    return s;
}

// A capturing card-text hook: records every line routed to the card renderer.
std::vector<std::string> g_cardLines;
int CaptureCardText(render::Surface&, int, int, int, const char* text, void*) {
    g_cardLines.emplace_back(text ? text : "");
    return 1;
}
int DeclineCardText(render::Surface&, int, int, int, const char*, void*) {
    return 0;
}

// Building-selection inputs (the InfoPanelRebuildOnlyOnSelectionChange shape).
struct CardFixture {
    gui::InfoBuildingRecord bld;
    gui::text::TextDb db;
    SessionPanelsInputs in;
    CardFixture() {
        bld.code = 30;
        bld.upgradeLevel = 40;
        bld.item = 0xFFFF;
        bld.customName = "Zum Test";
        in.hoveredTooltipId = -1;
        in.selection.building = 5;
        in.selBuilding = &bld;
        in.textDb = &db;
    }
};

} // namespace

// Keyed name resolve: type name fed -> "_GEB_<TYPE>_NAME+0" text, '~' -> ' ',
// custom name appended as >name<.
TEST(SessionCard, KeyedBuildingNameResolves) {
    CardFixture f;
    f.db.Add("Steinmetz~huette", "_GEB_MAUREREI_NAME+0");
    f.in.selBuildingTypeName = "MAUREREI";

    SessionPanels sp;
    sp.layout.drawBox = false;              // sidebar-card mode
    sp.cardText.draw = &CaptureCardText;
    g_cardLines.clear();

    auto fb = MakeFb();
    auto s = Surf(fb);
    sp.Frame(s, f.in);
    CHECK(sp.lastResult().panelVisible);
    CHECK(!g_cardLines.empty());
    bool found = false;
    for (const std::string& l : g_cardLines)
        if (l.find("Steinmetz huette") != std::string::npos &&
            l.find(">Zum Test<") != std::string::npos)
            found = true;
    CHECK(found);
}

// Without a type name the resolve falls back to the numeric formula (T<id>
// fallback text on an empty db) — the pre-keyed behaviour, unchanged.
TEST(SessionCard, NoTypeNameFallsBackToNumericId) {
    CardFixture f;                          // db empty, no type name

    SessionPanels sp;
    sp.layout.drawBox = false;
    sp.cardText.draw = &CaptureCardText;
    g_cardLines.clear();

    auto fb = MakeFb();
    auto s = Surf(fb);
    sp.Frame(s, f.in);
    bool numeric = false;
    const int wantId = gui::kNameTextStride * 30 + gui::kNameTextBias;
    const std::string tag = "T" + std::to_string(wantId);
    for (const std::string& l : g_cardLines)
        if (l.find(tag) != std::string::npos) numeric = true;
    CHECK(numeric);
}

// A declining hook falls back to the built-in 5x7 face (text ops still drawn).
TEST(SessionCard, DecliningHookFallsBackTo5x7) {
    CardFixture f;
    SessionPanels sp;
    sp.layout.drawBox = false;
    sp.cardText.draw = &DeclineCardText;

    auto fb = MakeFb();
    auto s = Surf(fb);
    sp.Frame(s, f.in);
    CHECK(sp.lastResult().panelVisible);
    CHECK(sp.lastResult().panelTextOps >= 1);   // 5x7 fallback drew the line
}

// ---------------------------------------------------------------------------
// The sprite-id mapping. A synthetic two-shape depth-1 bank in the exact
// ShowFromBank layout: count @+0x2A, offsets @+0x45, shape header @+6/+0xA/
// +0xC, run table @+0x32 (one solid run per row).
// ---------------------------------------------------------------------------
namespace {

void PutU16(std::vector<u8>& b, size_t off, u16 v) { std::memcpy(&b[off], &v, 2); }
void PutU32(std::vector<u8>& b, size_t off, u32 v) { std::memcpy(&b[off], &v, 4); }

// One 2x1 shape: a single row with one 2-pixel run of `px`.
size_t EmitShape(std::vector<u8>& bank, u16 px) {
    const size_t off = bank.size();
    bank.resize(off + 0x32 + 4 + 4 + 4 + 2 * 2, 0);
    PutU16(bank, off + 0x06, 2);          // width
    PutU16(bank, off + 0x0A, 1);          // height
    bank[off + 0x0C] = 1;                 // depth 1 (row-RLE)
    PutU32(bank, off + 0x32, 1);          // runCount
    PutU32(bank, off + 0x36, 0);          // skip
    PutU32(bank, off + 0x3A, 2);          // nPixels
    PutU16(bank, off + 0x3E, px);
    PutU16(bank, off + 0x40, px);
    return off;
}

std::vector<u8> TwoShapeBank(u16 pxA, u16 pxB) {
    std::vector<u8> bank(0x80, 0);
    const size_t offA = EmitShape(bank, pxA);
    const size_t offB = EmitShape(bank, pxB);
    PutU16(bank, 0x2A, 1);                // max valid shape index = 1
    PutU32(bank, 0x45, (u32)offA);
    PutU32(bank, 0x49, (u32)offB);
    return bank;
}

} // namespace

TEST(SessionCard, SpriteIdBaseMapsAndDeclines) {
    const std::vector<u8> bank = TwoShapeBank(/*A=*/0xF800, /*B=*/0x07E0);
    play::InstallRealHudBridge();
    play::SetHudSpriteBank(bank.data());
    play::SetHudSpriteIdBase(100);

    const play::HudRenderHooks& hooks = play::GetHudRenderHooks();
    CHECK(hooks.drawSprite != nullptr);

    auto fb = MakeFb();
    auto s = Surf(fb);
    // id 101 -> shape 1 (green), PLAIN colour (ShapeDecodeRle keeps 0x07E0).
    CHECK(hooks.drawSprite(&s, 10, 10, 101, hooks.userData));
    CHECK_EQ((int)fb[(size_t)10 * kW + 10], 0x07E0);
    CHECK_EQ((int)fb[(size_t)10 * kW + 11], 0x07E0);
    // id 100 -> shape 0 (red).
    CHECK(hooks.drawSprite(&s, 20, 20, 100, hooks.userData));
    CHECK_EQ((int)fb[(size_t)20 * kW + 20], 0xF800);
    // Out-of-range ids decline (no pixels).
    CHECK(!hooks.drawSprite(&s, 30, 30, 99, hooks.userData));
    CHECK(!hooks.drawSprite(&s, 30, 30, 102, hooks.userData));
    CHECK_EQ((int)fb[(size_t)30 * kW + 30], 0);

    // Legacy mode (base < 0): every id maps to shape 0 (the grey-luma leaf —
    // red 0xF800 recolours to a non-zero grey, never the raw colour).
    play::SetHudSpriteIdBase(-1);
    auto fb2 = MakeFb();
    auto s2 = Surf(fb2);
    CHECK(hooks.drawSprite(&s2, 40, 40, 12345, hooks.userData));
    CHECK(fb2[(size_t)40 * kW + 40] != 0);

    // Cleanup: restore the default bank + legacy mapping + inert hooks.
    play::SetHudSpriteBank(nullptr);
    play::UninstallRealHudBridge();
}
