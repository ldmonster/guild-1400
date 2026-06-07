// End-to-end test for the in-game HUD + map view + panels flow.
//
// Scenario: a small game state with a 1024x768 world map, three HUD action slots, two
// player-bar objects and two map markers.  We:
//   1. simulate clicks on HUD elements and verify the resolved slot + dispatched action
//      against the click-mode flags;
//   2. project the two markers world->screen, pan the map, and check the resulting
//      screen coords against a hand-computed reference;
//   3. drive the info panel through a selection change and confirm the builder choice;
//   4. select menu options and confirm the transitions.
#include "gui/hud.h"
#include "gui/infopanel.h"
#include "gui/mapview.h"
#include "gui/menu.h"
#include "gui/playerbar.h"
#include "tests/framework/test.h"

#include <cmath>
#include <vector>

using namespace guild::gui;

namespace {
bool nearf(double a, double b, double eps = 0.5) { return std::fabs(a - b) <= eps; }

struct E2EHudSink : HudCommandSink {
    std::vector<HudClickAction> log;
    void OnSelection() override { log.push_back(HudClickAction::kSelection); }
    void OnInfoPanel() override { log.push_back(HudClickAction::kInfoPanel); }
    void OnStatusBanner() override { log.push_back(HudClickAction::kStatusBanner); }
};

struct E2EMenuSink : MenuCommandSink {
    std::vector<OptionsItem> log;
    bool lastNet = false;
    void RunLoad() override { log.push_back(OptionsItem::kLoad); }
    void RunSave(bool net) override { lastNet = net; log.push_back(OptionsItem::kSave); }
    void RunGameOptions() override { log.push_back(OptionsItem::kGame); }
    void RunGfxOptions() override { log.push_back(OptionsItem::kGfx); }
    void RunSfxOptions() override { log.push_back(OptionsItem::kSfx); }
    void RunQuitConfirm() override { log.push_back(OptionsItem::kQuit); }
    void Resume() override { log.push_back(OptionsItem::kResume); }
};

struct E2EInfoSink : InfoPanelCommandSink {
    std::vector<InfoBuilder> log;
    void Build(InfoBuilder b) override { log.push_back(b); }
};
} // namespace

TEST(GuiHudE2E, FullHudMapPanelFlow) {
    // ---- World / HUD setup ------------------------------------------------
    g_mapWidth = 1024;
    g_mapHeight = 768;

    ResetHudSlots();
    ResetPlayerBar();
    ResetStatusText();
    ResetDamageLabels();

    // Three HUD action slots bound to widgets 70 (selection), 71 (info), 72 (status).
    g_hudSlots[0] = HudSlot{/*owner*/ 1, /*widget*/ 70, /*window*/ 9};
    g_hudSlots[1] = HudSlot{2, 71, 9};
    g_hudSlots[2] = HudSlot{3, 72, 9};

    // ---- 1. HUD clicks -> resolved slot + dispatched action ---------------
    E2EHudSink hudSink;
    Hud_SetCommandSink(&hudSink);

    // Click over widget 70 in selection mode (0x800).
    CHECK_EQ(Hud_FindSlotForWidget(70), 0);
    CHECK(Hud_DispatchClick(kClickFlagSelection, 70) == HudClickAction::kSelection);
    // Click over widget 71 in info-panel mode (0x100).
    CHECK_EQ(Hud_FindSlotForWidget(71), 1);
    CHECK(Hud_DispatchClick(kClickFlagInfoPanel, 71) == HudClickAction::kInfoPanel);
    // Click over widget 72 in status mode (0x8).
    CHECK_EQ(Hud_FindSlotForWidget(72), 2);
    CHECK(Hud_DispatchClick(kClickFlagStatusBanner, 72) == HudClickAction::kStatusBanner);
    // Click over an unbound widget -> no slot, but flag still drives the action.
    CHECK_EQ(Hud_FindSlotForWidget(999), -1);

    CHECK_EQ((int)hudSink.log.size(), 3);
    CHECK(hudSink.log[0] == HudClickAction::kSelection);
    CHECK(hudSink.log[1] == HudClickAction::kInfoPanel);
    CHECK(hudSink.log[2] == HudClickAction::kStatusBanner);
    Hud_SetCommandSink(nullptr);

    // ---- 2. Player-bar objects + status / damage tables -------------------
    int s0 = PlayerBar_AssignSlot(2001);
    int s1 = PlayerBar_AssignSlot(2002);
    CHECK_EQ(s0, 0);
    CHECK_EQ(s1, 1);
    // Object 2001 produces at 75% output -> "%s 75%%".
    CHECK_EQ(PlayerBar_OutputRatioPercent(0.75), 75);
    // Register a status entry + a damage label for the same object handle.
    CHECK_EQ(StatusText_Register(2001, 1), 0);
    CHECK_EQ(DamageLabel_Register(2001, 25, /*now*/ 500), 0);
    // Same source again de-dupes.
    CHECK_EQ(DamageLabel_Register(2001, 99, 501), 0);

    // ---- 3. Map markers world->screen, then pan ---------------------------
    // Two markers; cameraOrigin = 0 so origin scale term is 0.
    MapMarker m0{};
    m0.worldX = 100.0f;
    m0.worldZ = 50.0f;
    MapMarker m1{};
    m1.worldX = -80.0f;
    m1.worldZ = 30.0f;

    // Hand-computed reference (double):
    //   m0: dx = 100*5.33 = 533; bow_x = (|533|/(1024*0.5))*51 = (533/512)*51 = 53.094..
    //       sx = 512 + (533 + 53.094) = 1098.094 ; matches golden 1098.09
    //       dz = 50*5.33 = 266.5; bow_z = (266.5/(0.5*768))*51 = (266.5/384)*51=35.394
    //       sy = 384 + (266.5 + 35.394) = 685.894
    MapView_ComputeMarkerScreenPos(m0, /*panX*/ 0, /*panY*/ 0, /*cam*/ 0);
    CHECK(nearf(m0.screenX, 1098.09));
    CHECK(nearf(m0.screenY, 685.89));

    //   m1: dx = -80*5.33 = -426.4 (negative branch); bow = (426.4/512)*51 = 42.473
    //       sx = 512 + (-426.4 - 42.473) = 512 - 468.873 = 43.127
    //       dz = 30*5.33 = 159.9; bow = (159.9/384)*51 = 21.236
    //       sy = 384 + (159.9 + 21.236) = 565.136
    MapView_ComputeMarkerScreenPos(m1, 0, 0, 0);
    CHECK(nearf(m1.screenX, 43.13));
    CHECK(nearf(m1.screenY, 565.14));

    // Panning the viewport shifts both markers by the same pan delta.
    MapMarker m0b = m0;
    m0b.worldX = 100.0f;
    m0b.worldZ = 50.0f;
    MapView_ComputeMarkerScreenPos(m0b, /*panX*/ 25, /*panY*/ -15, 0);
    CHECK(nearf(m0b.screenX, m0.screenX + 25));
    CHECK(nearf(m0b.screenY, m0.screenY - 15));

    // Scroll-offset step toward the right edge then clamp.
    ScrollOffset off{500, 400};
    int changed = MapView_StepScrollOffset(off, /*mx*/ 50, /*my*/ 50,
                                          /*loX*/ 0, /*hiX*/ 100, /*loY*/ 0, /*hiY*/ 100,
                                          false, false, false, /*right*/ true);
    CHECK_EQ(changed, 1);
    CHECK_EQ(off.x, 510); // 500 +10 (right key)
    CHECK_EQ(off.y, 400); // y unchanged, within [0,408]

    // ---- 4. Info panel: selection change drives one rebuild ---------------
    E2EInfoSink infoSink;
    InfoPanel_SetCommandSink(&infoSink);
    InfoSelection sel{};
    sel.building = 0x4000;
    InfoSnapshot snap{};
    CHECK(InfoPanel_Update(sel, snap) == InfoBuilder::kBuilding);
    CHECK(InfoPanel_Update(sel, snap) == InfoBuilder::kNone); // unchanged
    // Now select a transporter (category 29) -> rebuild as transporter panel.
    sel.transporter = 0x5000;
    sel.transporterCategory = kTransporterPanelCategory;
    sel.building = 0; // clear building so transporter branch wins
    CHECK(InfoPanel_Update(sel, snap) == InfoBuilder::kTransporter);
    CHECK_EQ((int)infoSink.log.size(), 2);
    CHECK(infoSink.log[0] == InfoBuilder::kBuilding);
    CHECK(infoSink.log[1] == InfoBuilder::kTransporter);
    InfoPanel_SetCommandSink(nullptr);

    // ---- 5. Menu options transitions --------------------------------------
    E2EMenuSink menuSink;
    Menu_SetCommandSink(&menuSink);
    // Click Load, then Save (in a network session), then Quit, then Resume.
    Menu_DispatchOption(0, 0);
    Menu_DispatchOption(1, kFlagNetwork);
    Menu_DispatchOption(5, 0);
    Menu_DispatchOption(6, 0);
    CHECK_EQ((int)menuSink.log.size(), 4);
    CHECK(menuSink.log[0] == OptionsItem::kLoad);
    CHECK(menuSink.log[1] == OptionsItem::kSave);
    CHECK(menuSink.lastNet == true);
    CHECK(menuSink.log[2] == OptionsItem::kQuit);
    CHECK(menuSink.log[3] == OptionsItem::kResume);
    Menu_SetCommandSink(nullptr);
}
