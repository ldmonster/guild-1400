// Unit tests — play::SessionHud (the in-game session HUD overlay) over a
// SYNTHETIC 16bpp RGB565 framebuffer: caption glyphs, player-bar fills, the
// status line/table, map markers and the real ShapeShowFromBank sprite blits
// all land as pixel deltas; renders are deterministic; degenerate targets are
// safe no-ops. No assets required (Init(nullptr) exercises the fallback path).
#include "test.h"

#include "play/session_hud.h"
#include "play/hud_render.h"
#include "play/wire_hud_bridge.h"
#include "gui/hud.h"
#include "gui/playerbar.h"

#include <cstring>
#include <vector>

using guild::i64;
using guild::u16;
using guild::play::SessionHud;
using guild::play::SessionHudResult;
using guild::play::HudBarObject;
using guild::play::HudMarker;
using guild::play::MarkerXY;
namespace gui = guild::gui;

namespace {

// kH must cover the player bar's SECOND slot sub-window: barY(48) +
// slot-1 subWinY(141) + subWinH(80) = 269 (PlayerBar_SlotLayout) — a 240-tall
// framebuffer made BarFillScalesWithProductionRatio read out of bounds
// (the long-standing suite segfault).
constexpr int kW = 320, kH = 320, kPitch = kW * 2;

std::vector<u16> MakeFb() { return std::vector<u16>((size_t)kW * kH, 0); }

int NonZero(const std::vector<u16>& fb) {
    int n = 0;
    for (u16 p : fb) if (p) ++n;
    return n;
}

// Count non-zero pixels inside [x0,x0+w) x [y0,y0+h).
int NonZeroIn(const std::vector<u16>& fb, int x0, int y0, int w, int h) {
    int n = 0;
    for (int y = y0; y < y0 + h && y < kH; ++y)
        for (int x = x0; x < x0 + w && x < kW; ++x)
            if (y >= 0 && x >= 0 && fb[(size_t)y * kW + x]) ++n;
    return n;
}

guild::sim::GameTime Clock(int day, int hour, int minute) {
    guild::sim::GameTime t{};
    t.day = day; t.hour = (guild::u16)hour; t.minute = minute; t.second = 0;
    return t;
}

SessionHud::Inputs FullInputs(const guild::sim::GameTime* clk,
                              const HudBarObject* bars, int nBars,
                              const HudMarker* mks, int nMks) {
    SessionHud::Inputs in;
    in.money = 1234567;
    in.moneyRate = 1;
    in.clock = clk;
    in.clockTick = 80;
    in.selectedId = 7;
    in.selectedName = "MARKTSTAND";
    in.barObjects = bars;
    in.barObjectCount = nBars;
    in.markers = mks;
    in.markerCount = nMks;
    return in;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(SessionHud, InitWithoutFsInstallsBridgeAndReportsNoGfx) {
    SessionHud hud;
    CHECK(!hud.Init(nullptr));                       // no fs -> no real gfx
    CHECK(guild::play::RealHudBridgeInstalled());    // fallback bridge wired
    CHECK(!hud.gfxLoaded());
    CHECK_EQ(hud.gfxObjectCount(), 0);
    CHECK(!hud.realShapesUsable());
    // The named gap is pinned to its address.
    CHECK(std::strstr(SessionHud::missingDecode(), "0x5d7c0c") != nullptr);
    CHECK(std::strstr(SessionHud::missingDecode(), "0x5d80a8") != nullptr);
}

// ---------------------------------------------------------------------------
TEST(SessionHud, RenderDrawsCaptionBarStatusAndMarkers) {
    SessionHud hud;
    hud.Init(nullptr);

    guild::sim::GameTime clk = Clock(12, 9, 30);
    HudBarObject bars[2] = {{3, 0.5}, {4, 1.0}};
    HudMarker mks[1] = {{0.0f, 0.0f}};
    // Anchor the map region so the projected marker lands inside the frame.
    MarkerXY xy = guild::play::HudMarkerScreenXY(mks[0], 0, 0, 0);
    SessionHud::Inputs in = FullInputs(&clk, bars, 2, mks, 1);

    hud.layout.mapX = 200 - xy.x;
    hud.layout.mapY = 200 - xy.y;

    std::vector<u16> fb = MakeFb();
    hud.Render(fb.data(), kW, kH, kPitch, in);
    const SessionHudResult& r = hud.lastResult();

    CHECK_EQ(r.barSlotsDrawn, 2);
    CHECK(r.barFillRows > 0);
    CHECK(r.captionGlyphs > 0);
    CHECK(r.statusGlyphs > 0);
    CHECK(r.statusSlot >= 0);
    CHECK_EQ(r.markersDrawn, 1);
    CHECK(r.spriteBlits > 0);             // real ShapeShowFromBank blits landed
    CHECK(NonZero(fb) > 100);             // the overlay is decidedly non-blank

    // Caption glyph pixels land in the money line's 7px row band.
    CHECK(NonZeroIn(fb, hud.layout.captionX, hud.layout.captionY, 200, 7) > 0);
    // Date line one 9px step below.
    CHECK(NonZeroIn(fb, hud.layout.captionX, hud.layout.captionY + 9, 200, 7) > 0);
    // Status line pixels.
    CHECK(NonZeroIn(fb, hud.layout.statusX, hud.layout.statusY, 200, 7) > 0);
    // The marker dot (4x4 + outline) at its anchored position.
    CHECK(NonZeroIn(fb, 200, 200, 4, 4) > 0);
}

// ---------------------------------------------------------------------------
TEST(SessionHud, BarFillScalesWithProductionRatio) {
    SessionHud hud;
    hud.Init(nullptr);
    guild::sim::GameTime clk = Clock(1, 0, 0);

    // Slot 0 nearly empty, slot 1 full: the full slot's sub-window must carry
    // strictly more fill-coloured pixels.
    HudBarObject bars[2] = {{10, 0.0}, {11, 1.0}};
    SessionHud::Inputs in;
    in.clock = &clk;
    in.barObjects = bars;
    in.barObjectCount = 2;

    std::vector<u16> fb = MakeFb();
    hud.Render(fb.data(), kW, kH, kPitch, in);

    // Recompute each slot's sub-window position with the same real layout leaf.
    gui::ResetPlayerBar();
    (void)gui::PlayerBar_AssignSlot(10);
    (void)gui::PlayerBar_AssignSlot(11);
    guild::gui::PlayerBarLayout L0 = guild::gui::PlayerBar_SlotLayout(0);
    guild::gui::PlayerBarLayout L1 = guild::gui::PlayerBar_SlotLayout(1);

    // Count pixels that are NOT the track colour (40,40,40 -> 565 0x2104) and
    // NOT zero inside each sub-window interior (skip the 1px frame).
    auto countFill = [&](const guild::gui::PlayerBarLayout& L) {
        int n = 0;
        const u16 track = (u16)(((40 >> 3) << 11) | ((40 >> 2) << 5) | (40 >> 3));
        for (int y = hud.layout.barY + L.subWinY + 1;
             y < hud.layout.barY + L.subWinY + L.subWinH - 1; ++y)
            for (int x = hud.layout.barX + L.subWinX + 1;
                 x < hud.layout.barX + L.subWinX + L.subWinW - 1; ++x) {
                u16 p = fb[(size_t)y * kW + x];
                if (p && p != track) ++n;
            }
        return n;
    };
    CHECK(countFill(L1) > countFill(L0));

    // The fill-pixel math itself: the real ratio->percent->pixels chain.
    CHECK_EQ(guild::play::HudBarFillPixels(0.0), 0);
    CHECK_EQ(guild::play::HudBarFillPixels(1.0), guild::gui::kPlayerBarSubWinW);
    CHECK_EQ(guild::play::HudBarFillPixels(0.5), guild::gui::kPlayerBarSubWinW / 2);
}

// ---------------------------------------------------------------------------
TEST(SessionHud, StatusLineRegistersSelectionInRealTable) {
    SessionHud hud;
    hud.Init(nullptr);
    gui::ResetStatusText();

    guild::sim::GameTime clk = Clock(3, 12, 0);
    SessionHud::Inputs in;
    in.clock = &clk;
    in.selectedId = 42;
    in.selectedName = "SCHMIEDE";

    std::vector<u16> fb = MakeFb();
    hud.Render(fb.data(), kW, kH, kPitch, in);

    const int slot = hud.lastResult().statusSlot;
    CHECK(slot >= 0);
    CHECK(gui::g_statusText[slot].inUse);
    CHECK_EQ(gui::g_statusText[slot].key, 42);

    // Re-render: the real StatusText_Register de-dups by key -> same slot.
    hud.Render(fb.data(), kW, kH, kPitch, in);
    CHECK_EQ(hud.lastResult().statusSlot, slot);
}

// ---------------------------------------------------------------------------
TEST(SessionHud, RenderIsDeterministic) {
    SessionHud hud;
    hud.Init(nullptr);
    guild::sim::GameTime clk = Clock(5, 18, 45);
    HudBarObject bars[3] = {{1, 0.25}, {2, 0.75}, {3, 1.0}};
    HudMarker mks[2] = {{10.0f, 20.0f}, {-5.0f, 7.5f}};
    SessionHud::Inputs in = FullInputs(&clk, bars, 3, mks, 2);

    std::vector<u16> a = MakeFb(), b = MakeFb();
    hud.Render(a.data(), kW, kH, kPitch, in);
    hud.Render(b.data(), kW, kH, kPitch, in);
    CHECK(NonZero(a) > 0);
    CHECK(std::memcmp(a.data(), b.data(), a.size() * 2) == 0);
}

// ---------------------------------------------------------------------------
TEST(SessionHud, MoneyClampAndNegativeAreSafe) {
    SessionHud hud;
    hud.Init(nullptr);
    SessionHud::Inputs in;
    in.money = (i64)1 << 40;          // beyond i32 -> clamped at the boundary
    std::vector<u16> a = MakeFb();
    hud.Render(a.data(), kW, kH, kPitch, in);
    CHECK(hud.lastResult().captionGlyphs > 0);
    CHECK(NonZero(a) > 0);

    in.money = -987654;               // negative money renders the '-' form
    std::vector<u16> b = MakeFb();
    hud.Render(b.data(), kW, kH, kPitch, in);
    CHECK(hud.lastResult().captionGlyphs > 0);
    CHECK(NonZero(b) > 0);
    // Different money -> different caption pixels.
    CHECK(std::memcmp(a.data(), b.data(), a.size() * 2) != 0);
}

// ---------------------------------------------------------------------------
TEST(SessionHud, DegenerateTargetsAreNoOps) {
    SessionHud hud;
    hud.Init(nullptr);
    guild::sim::GameTime clk = Clock(1, 0, 0);
    SessionHud::Inputs in;
    in.clock = &clk;
    in.money = 100;

    std::vector<u16> fb = MakeFb();
    hud.Render(nullptr, kW, kH, kPitch, in);                 // null target
    CHECK_EQ(hud.lastResult().captionGlyphs, 0);
    hud.Render(fb.data(), 0, kH, kPitch, in);                // zero width
    CHECK_EQ(hud.lastResult().captionGlyphs, 0);
    hud.Render(fb.data(), kW, -1, kPitch, in);               // bad height
    CHECK_EQ(hud.lastResult().captionGlyphs, 0);
    hud.Render(fb.data(), kW, kH, kW, in);                   // pitch < 2*w
    CHECK_EQ(hud.lastResult().captionGlyphs, 0);
    hud.Render(fb.data(), kW, kH, kPitch + 1, in);           // odd pitch
    CHECK_EQ(hud.lastResult().captionGlyphs, 0);
    CHECK_EQ(NonZero(fb), 0);                                // nothing painted
}

// ---------------------------------------------------------------------------
TEST(SessionHud, SpriteBlitsComeFromTheRealLeafAndCanBeDisabled) {
    SessionHud hud;
    hud.Init(nullptr);                       // installs the real bridge
    guild::sim::GameTime clk = Clock(1, 0, 0);
    HudBarObject bars[1] = {{9, 0.0}};
    SessionHud::Inputs in;
    in.clock = &clk;
    in.barObjects = bars;
    in.barObjectCount = 1;

    std::vector<u16> withSprites = MakeFb();
    hud.Render(withSprites.data(), kW, kH, kPitch, in);
    CHECK(hud.lastResult().spriteBlits > 0);

    // Uninstall the bridge -> the hook is inert -> zero sprite blits, and the
    // sprite-anchor pixels that the REAL ShapeShowFromBank painted disappear.
    guild::play::UninstallRealHudBridge();
    std::vector<u16> noSprites = MakeFb();
    hud.Render(noSprites.data(), kW, kH, kPitch, in);
    CHECK_EQ(hud.lastResult().spriteBlits, 0);
    CHECK(NonZero(withSprites) > NonZero(noSprites));

    guild::play::InstallRealHudBridge();     // restore for other tests
}

// ---------------------------------------------------------------------------
// Wave-2 panel extension: Inputs.panels (ADDITIVE).
// ---------------------------------------------------------------------------
#include "play/session_panels.h"

TEST(SessionHud, PanelsNullKeepsLegacyFrameAndZeroPanelCounters) {
    SessionHud hud;
    hud.Init(nullptr);
    guild::sim::GameTime clk = Clock(3, 10, 0);
    HudBarObject bars[1] = {{4, 0.5}};
    SessionHud::Inputs in = FullInputs(&clk, bars, 1, nullptr, 0);
    // in.panels stays null (the legacy call shape sdl_session uses today).

    std::vector<u16> a = MakeFb(), b = MakeFb();
    hud.Render(a.data(), kW, kH, kPitch, in);
    CHECK(!hud.lastResult().tooltipVisible);
    CHECK(!hud.lastResult().panelVisible);
    CHECK_EQ(hud.lastResult().tooltipTextOps, 0);
    CHECK_EQ(hud.lastResult().panelTextOps, 0);

    SessionHud hud2;                        // fresh instance, same inputs
    hud2.Init(nullptr);
    hud2.Render(b.data(), kW, kH, kPitch, in);
    CHECK(std::memcmp(a.data(), b.data(), a.size() * 2) == 0);
}

TEST(SessionHud, PanelsInputsCompositeTooltipAndInfoPanel) {
    SessionHud hud;
    hud.Init(nullptr);
    hud.panels().layout.panelY = 150;       // keep the panel inside 240px
    guild::sim::GameTime clk = Clock(3, 10, 0);
    SessionHud::Inputs in = FullInputs(&clk, nullptr, 0, nullptr, 0);

    // Hovered person (tooltip) + selected building (info panel).
    guild::play::PanelPersonHover ph;
    ph.view.id = 9;
    gui::InfoBuildingRecord bld;
    bld.code = 30;
    bld.upgradeLevel = 40;
    guild::play::SessionPanelsInputs pin;
    pin.hoveredTooltipId = 42;
    pin.cursorX = 100;
    pin.cursorY = 60;
    pin.hoverKind = gui::TooltipKind::kPerson;
    pin.hoverPersonId = 9;
    pin.person = &ph;
    pin.selection.building = 5;
    pin.selBuilding = &bld;
    in.panels = &pin;

    std::vector<u16> withPanels = MakeFb();
    hud.Render(withPanels.data(), kW, kH, kPitch, in);
    CHECK(hud.lastResult().tooltipVisible);
    CHECK(hud.lastResult().panelVisible);
    CHECK(hud.lastResult().tooltipTextOps > 0);
    CHECK(hud.lastResult().panelTextOps > 0);
    CHECK(hud.lastResult().panelIconOps > 0);
    // Pixel bands: the tooltip box near the cursor and the panel box band
    // are non-blank.
    const guild::play::SessionPanelsResult& pr = hud.panels().lastResult();
    CHECK(NonZeroIn(withPanels, pr.tooltipX, pr.tooltipY, pr.tooltipW,
                    pr.tooltipH) > 0);
    CHECK(NonZeroIn(withPanels, hud.panels().layout.panelX,
                    hud.panels().layout.panelY, hud.panels().layout.panelW,
                    hud.panels().layout.panelH) > 0);

    // Without panels the same HUD frame leaves both bands at their legacy
    // content (strictly fewer painted pixels).
    SessionHud hud2;
    hud2.Init(nullptr);
    in.panels = nullptr;
    std::vector<u16> noPanels = MakeFb();
    hud2.Render(noPanels.data(), kW, kH, kPitch, in);
    CHECK(NonZero(withPanels) > NonZero(noPanels));
}

TEST(SessionHud, PanelsRenderIsDeterministicAcrossFreshInstances) {
    guild::sim::GameTime clk = Clock(3, 10, 0);
    guild::play::PanelPersonHover ph;
    ph.view.id = 9;
    gui::InfoBuildingRecord bld;
    bld.code = 30;
    bld.upgradeLevel = 25;
    guild::play::SessionPanelsInputs pin;
    pin.hoveredTooltipId = 42;
    pin.cursorX = 100;
    pin.cursorY = 60;
    pin.hoverKind = gui::TooltipKind::kPerson;
    pin.hoverPersonId = 9;
    pin.person = &ph;
    pin.selection.building = 3;
    pin.selBuilding = &bld;

    std::vector<u16> a = MakeFb(), b = MakeFb();
    {
        SessionHud hud;
        hud.Init(nullptr);
        hud.panels().layout.panelY = 150;
        SessionHud::Inputs in = FullInputs(&clk, nullptr, 0, nullptr, 0);
        in.panels = &pin;
        hud.Render(a.data(), kW, kH, kPitch, in);
    }
    {
        SessionHud hud;
        hud.Init(nullptr);
        hud.panels().layout.panelY = 150;
        SessionHud::Inputs in = FullInputs(&clk, nullptr, 0, nullptr, 0);
        in.panels = &pin;
        hud.Render(b.data(), kW, kH, kPitch, in);
    }
    CHECK(std::memcmp(a.data(), b.data(), a.size() * 2) == 0);
    CHECK(NonZero(a) > 0);
}

// ---------------------------------------------------------------------------
// SELECTION-STATE SIDEBAR BUTTONS (live-capture pin): with the real chrome
// loaded, the button band renders Стройка/Обзор when NOTHING is selected and
// Информация/Транспорт when a building is (the original's two states). Gated
// on GUILD_GAME_DIR (clean skip without the assets). The pin is pixel-level:
// idle and selected frames must DIFFER in the button band, idle must be
// deterministic, and both must be non-blank (the chrome actually drew).
// ---------------------------------------------------------------------------
#include "shim_impl/disk_filesystem.h"
TEST(SessionHud, SidebarButtonsFollowSelectionState) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir)
        return;                                  // clean skip (no assets)
    guild::shim::DiskFileSystem fs(dir);

    SessionHud hud;
    if (!hud.Init(&fs))
        return;                                  // gfx absent -> skip
    if (!hud.DecodePanelChrome("_PANEL_STEIN", "_STADTWAPPEN_KOELN"))
        return;                                  // chrome absent -> skip

    // 800x600 frame; the button band lives at (700..792, 404..454) design.
    const int W = 800, H = 600;
    std::vector<u16> idleFb((size_t)W * H, 0), selFb((size_t)W * H, 0),
        idle2((size_t)W * H, 0);
    guild::sim::GameTime clk = Clock(0, 12, 0);
    SessionHud::Inputs in;
    in.clock = &clk;
    in.selectedId = 0;
    hud.Render(idleFb.data(), W, H, W * 2, in);
    hud.Render(idle2.data(), W, H, W * 2, in);
    in.selectedId = 42;                          // a building selection
    hud.Render(selFb.data(), W, H, W * 2, in);

    int diff = 0, nonzero = 0;
    for (int y = 404; y < 454; ++y)
        for (int x = 700; x < 792; ++x) {
            const size_t i = (size_t)y * W + x;
            if (idleFb[i] != selFb[i]) ++diff;
            if (idleFb[i]) ++nonzero;
            CHECK_EQ((int)idleFb[i], (int)idle2[i]);   // deterministic
        }
    CHECK(nonzero > 500);                        // the band drew chrome text
    CHECK(diff > 100);                           // the two states differ
}
