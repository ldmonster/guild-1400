// Unit tests for the in-game HUD + menus + info/player panels + map view module.
//   - HUD hit-test: slot-table resolution + click-mode flag dispatch
//   - HUD label x/align layout, button-row even-spacing layout, mode-index scan
//   - status-text / damage-label slot tables (de-dup + LRU eviction)
//   - on-screen clock: tick accumulator -> hh:mm:ss
//   - map view: world->screen marker projection (+ roundtrip), scroll-offset step,
//     corner placement table
//   - menu options select transitions
//   - player-bar slot layout + assignment + output-ratio percent
//   - info-panel builder-select dispatch + change detection
#include "gui/hud.h"
#include "gui/infopanel.h"
#include "gui/mapview.h"
#include "gui/menu.h"
#include "gui/playerbar.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

using namespace guild::gui;

namespace {
bool nearf(double a, double b, double eps = 0.01) { return std::fabs(a - b) <= eps; }
} // namespace

// ===========================================================================
// HUD hit-test: slot table.
// ===========================================================================
TEST(GuiHudHitTest, FindSlotForWidget) {
    ResetHudSlots();
    g_hudSlots[0].widget = 100;
    g_hudSlots[1].widget = 205;
    g_hudSlots[2].widget = 17;
    CHECK_EQ(Hud_FindSlotForWidget(205), 1);
    CHECK_EQ(Hud_FindSlotForWidget(100), 0);
    CHECK_EQ(Hud_FindSlotForWidget(17), 2);
    CHECK_EQ(Hud_FindSlotForWidget(999), -1); // not present
    CHECK_EQ(Hud_FindSlotForWidget(-1), -1);  // free marker never matches
}

TEST(GuiHudHitTest, FindFreeSlot) {
    ResetHudSlots();
    CHECK_EQ(Hud_FindFreeSlot(), 0); // all free -> slot 0
    g_hudSlots[0].widget = 5;
    g_hudSlots[1].widget = 6;
    CHECK_EQ(Hud_FindFreeSlot(), 2);
    for (int i = 0; i < kHudSlotCount; ++i)
        g_hudSlots[i].widget = i + 1;
    CHECK_EQ(Hud_FindFreeSlot(), -1); // full
}

// ===========================================================================
// HUD click-mode flag dispatch.
// ===========================================================================
namespace {
struct RecordingHudSink : HudCommandSink {
    int sel = 0, info = 0, status = 0;
    void OnSelection() override { ++sel; }
    void OnInfoPanel() override { ++info; }
    void OnStatusBanner() override { ++status; }
};
} // namespace

TEST(GuiHudClick, ClassifyPriority) {
    CHECK(Hud_ClassifyClick(kClickFlagSelection) == HudClickAction::kSelection);
    CHECK(Hud_ClassifyClick(kClickFlagInfoPanel) == HudClickAction::kInfoPanel);
    CHECK(Hud_ClassifyClick(kClickFlagStatusBanner) == HudClickAction::kStatusBanner);
    CHECK(Hud_ClassifyClick(0) == HudClickAction::kNone);
    // Selection (0x800) outranks info (0x100) and status (0x8).
    CHECK(Hud_ClassifyClick(0x800 | 0x100 | 0x8) == HudClickAction::kSelection);
    CHECK(Hud_ClassifyClick(0x100 | 0x8) == HudClickAction::kInfoPanel);
}

TEST(GuiHudClick, DispatchFiresSink) {
    ResetHudSlots();
    RecordingHudSink sink;
    Hud_SetCommandSink(&sink);
    g_hudSlots[3].widget = 42;
    CHECK(Hud_DispatchClick(kClickFlagSelection, 42) == HudClickAction::kSelection);
    CHECK_EQ(sink.sel, 1);
    CHECK(Hud_DispatchClick(kClickFlagInfoPanel, 42) == HudClickAction::kInfoPanel);
    CHECK_EQ(sink.info, 1);
    CHECK(Hud_DispatchClick(kClickFlagStatusBanner, 42) == HudClickAction::kStatusBanner);
    CHECK_EQ(sink.status, 1);
    CHECK(Hud_DispatchClick(0, 42) == HudClickAction::kNone);
    Hud_SetCommandSink(nullptr); // restore default
}

// ===========================================================================
// HUD label layout.
// ===========================================================================
TEST(GuiHudLabel, AlignmentLayout) {
    // centered: x = anchor - width/2, +88 = 1
    HudLabelLayout c = Hud_LabelLayout(HudLabelAlign::kCentered, 100, 40);
    CHECK_EQ(c.x, 80);
    CHECK_EQ((int)c.width, 40);
    CHECK_EQ(c.flag88, 1);
    CHECK_EQ(c.flag92, 0);
    // right: x = anchor - width, +92 = 1
    HudLabelLayout r = Hud_LabelLayout(HudLabelAlign::kRight, 100, 40);
    CHECK_EQ(r.x, 60);
    CHECK_EQ(r.flag92, 1);
    // left: x = anchor, +92 = 0
    HudLabelLayout l = Hud_LabelLayout(HudLabelAlign::kLeft, 100, 40);
    CHECK_EQ(l.x, 100);
    CHECK_EQ(l.flag92, 0);
}

// ===========================================================================
// HUD button row even-spacing layout.
// ===========================================================================
TEST(GuiHudButtonRow, EvenSpreadWhenButtonsFit) {
    // window 400, 3 buttons each width 40. cell = 400/4 = 100; maxW(40) <= cell so
    // pitch = 100, v26 = 100, startX = -40/2 + 100 = 80, then +100 each.
    int widths[3] = {40, 40, 40};
    int x[3];
    int pitch = Hud_ButtonRowLayout(widths, 3, 400, x);
    CHECK_EQ(pitch, 100);
    CHECK_EQ(x[0], 80);
    CHECK_EQ(x[1], 180);
    CHECK_EQ(x[2], 280);
}

TEST(GuiHudButtonRow, FixedPitchWhenButtonsWide) {
    // window 200, 3 buttons; widest 100. cell = 200/4 = 50; maxW(100) > cell.
    // pitch = 100+8 = 108; total = 108*3 = 324; spread = |200-324| = 124.
    // v26 = 108/2 + 124/2 = 54 + 62 = 116; startX = -100/2 + 116 = 66.
    int widths[3] = {30, 100, 60};
    int x[3];
    int pitch = Hud_ButtonRowLayout(widths, 3, 200, x);
    CHECK_EQ(pitch, 108);
    CHECK_EQ(x[0], 66);
    CHECK_EQ((short)x[1], (short)(108 + 66));
    CHECK_EQ((short)x[2], (short)(108 + 108 + 66));
}

// ===========================================================================
// HUD mode-index scan.
// ===========================================================================
TEST(GuiHudMode, FindModeIndexDepth) {
    // stride-2 table; entry[2*i] holds the mode tag (4*modeId).
    int table[8] = {/*0*/ 4 * 7, 0, /*1*/ 4 * 3, 0, /*2*/ 4 * 5, 0, /*3*/ 4 * 9, 0};
    // curIndex=3, look for modeId 5 (tag 20) -> found at index 2 -> depth 3-2 = 1.
    CHECK_EQ(Hud_FindModeIndex(5, 3, table), 1);
    // modeId 7 (tag 28) at index 0 -> depth 3.
    CHECK_EQ(Hud_FindModeIndex(7, 3, table), 3);
    // modeId 9 (tag 36) at index 3 -> depth 0.
    CHECK_EQ(Hud_FindModeIndex(9, 3, table), 0);
    // not found (scan falls off the bottom) -> 0x4bea4f returns `result * 4` where
    // `result` is the negative loop counter at exit: 6->4->2->0->-2, so -2*4 = -8.
    CHECK_EQ(Hud_FindModeIndex(2, 3, table), -8);
    // curIndex < 0 -> eax (result) is still modeId, so returns 4*modeId.
    CHECK_EQ(Hud_FindModeIndex(5, -1, table), 20);
}

// ===========================================================================
// Status-text table.
// ===========================================================================
TEST(GuiHudStatusText, RegisterDedupAndOverflow) {
    ResetStatusText();
    CHECK_EQ(StatusText_Register(1001, 7), 0);
    CHECK_EQ(StatusText_Register(1002, 8), 1);
    // Re-register the same key -> existing slot.
    CHECK_EQ(StatusText_Register(1001, 99), 0);
    CHECK_EQ(g_statusText[0].tag, 7); // tag not overwritten on dedup
    // Fill the rest.
    for (int i = 2; i < kStatusTextCount; ++i)
        CHECK_EQ(StatusText_Register(2000 + i, i), i);
    CHECK_EQ(StatusText_Register(9999, 0), -1); // overflow
}

// ===========================================================================
// Damage-label table with LRU eviction.
// ===========================================================================
TEST(GuiHudDamageLabel, RegisterDedupAndEvictOldest) {
    ResetDamageLabels();
    CHECK_EQ(DamageLabel_Register(/*src*/ 500, /*amt*/ 12, /*now*/ 10), 0);
    CHECK_EQ(DamageLabel_Register(501, 20, 11), 1);
    // Dedup by source -> same slot, no new slot.
    CHECK_EQ(DamageLabel_Register(500, 99, 12), 0);
    CHECK_EQ(g_damageLabels[0].amount, 12); // not overwritten on dedup

    // Fill all 64 slots with distinct sources and increasing timestamps.
    ResetDamageLabels();
    for (int i = 0; i < kDamageLabelCount; ++i)
        CHECK_EQ(DamageLabel_Register(1000 + i, i, 100 + i), i);
    // Now full; a new source at a later time evicts the oldest (smallest ts) slot 0.
    int slot = DamageLabel_Register(9999, 7, 1000);
    CHECK_EQ(slot, kDamageLabelCount - 1); // last index with ts < now is slot 63
    CHECK_EQ(g_damageLabels[slot].source, 9999);
}

// ===========================================================================
// On-screen clock.
// ===========================================================================
TEST(GuiHudClock, TimeOfDayGoldenVectors) {
    g_dayLengthSeconds = 100; // dword_63CC60 recovered value
    // golden: (tick*0.00625 + 0.5) * 100 truncated -> seconds.
    ClockTime t0 = Clock_ComputeTimeOfDay(0);
    CHECK_EQ(t0.totalSeconds, 50);
    CHECK_EQ(t0.h, 0); CHECK_EQ(t0.m, 0); CHECK_EQ(t0.s, 50);

    ClockTime t1 = Clock_ComputeTimeOfDay(1000);
    CHECK_EQ(t1.totalSeconds, 675);
    CHECK_EQ(t1.h, 0); CHECK_EQ(t1.m, 11); CHECK_EQ(t1.s, 15);

    ClockTime t2 = Clock_ComputeTimeOfDay(16000);
    CHECK_EQ(t2.totalSeconds, 10050);
    CHECK_EQ(t2.h, 2); CHECK_EQ(t2.m, 47); CHECK_EQ(t2.s, 30);
}

// ===========================================================================
// Map view world->screen transform + roundtrip + scroll step + corners.
// ===========================================================================
TEST(GuiMapViewTransform, OriginRoundtrip) {
    g_mapWidth = 1024;
    g_mapHeight = 768;
    // When worldX == worldZ == cameraOrigin*0.5, dx=dz=0 -> screen is exactly the
    // viewport centre plus the pan offset (no perspective bow).
    int cam = 40; // origin*0.5 = 20
    MapMarker m{};
    m.worldX = 20.0f; // == cam*0.5
    m.worldZ = 20.0f;
    int half = MapView_ComputeMarkerScreenPos(m, /*panX*/ 7, /*panY*/ 9, cam);
    CHECK_EQ(half, 384);
    CHECK(nearf(m.screenX, 1024 / 2 + 7));
    CHECK(nearf(m.screenY, 768 / 2 + 9));
}

TEST(GuiMapViewTransform, ProjectionGolden) {
    g_mapWidth = 1024;
    g_mapHeight = 768;
    MapMarker m{};
    m.worldX = 100.0f;
    m.worldZ = 50.0f;
    MapView_ComputeMarkerScreenPos(m, 0, 0, /*cam*/ 0);
    // golden (double ref): screenX ~ 1098.0918, screenY ~ 685.8945 (float32 rounding
    // tolerated).
    CHECK(nearf(m.screenX, 1098.09, 0.5));
    CHECK(nearf(m.screenY, 685.89, 0.5));
}

TEST(GuiMapViewScroll, StepClampsToBounds) {
    g_mapWidth = 1024;
    g_mapHeight = 768;
    // No cursor pan (mouse within band, equal to bounds), edge-left held -> x -= 10.
    ScrollOffset off{100, 100};
    // bounds chosen so cursor pan does nothing: mouseX <= boundLo so x -= 10? Avoid by
    // mouseX > boundLo and < boundHi -> no change from cursor.
    int changed = MapView_StepScrollOffset(off, /*mx*/ 50, /*my*/ 50,
                                           /*loX*/ 0, /*hiX*/ 100, /*loY*/ 0, /*hiY*/ 100,
                                           /*up*/ false, /*down*/ false, /*left*/ true, /*right*/ false);
    CHECK_EQ(changed, 1);
    CHECK_EQ(off.x, 90); // 100 -10 (left key); cursor band gave no change
    CHECK_EQ(off.y, 100);

    // Clamp at low edge.
    ScrollOffset off2{5, 5};
    MapView_StepScrollOffset(off2, 50, 50, 0, 100, 0, 100, true, false, true, false);
    // up: y -= 10 -> -5 -> clamp 0 ; left key not reached (up has priority) so x stays.
    CHECK_EQ(off2.y, 0);

    // Clamp at high edge: world-512 = 512 for x, world-360 = 408 for y.
    ScrollOffset off3{510, 405};
    MapView_StepScrollOffset(off3, 50, 50, 0, 100, 0, 100, false, false, false, true);
    CHECK_EQ(off3.x, 512); // 510 +10 = 520 -> clamp to 1024-512 = 512
}

TEST(GuiMapViewCorners, PlacementTable) {
    CHECK_EQ((int)kMapCornerPlacements[0].x, 0);
    CHECK_EQ((int)kMapCornerPlacements[0].y, 0);
    CHECK_EQ((int)kMapCornerPlacements[1].x, 0);
    CHECK_EQ((int)kMapCornerPlacements[1].y, 418);
    CHECK_EQ((int)kMapCornerPlacements[2].x, 0);
    CHECK_EQ((int)kMapCornerPlacements[2].y, 120);
    CHECK_EQ((int)kMapCornerPlacements[3].x, 579);
    CHECK_EQ((int)kMapCornerPlacements[3].y, 120);
}

// ===========================================================================
// Menu options select transitions.
// ===========================================================================
TEST(GuiMenuOptions, SelectByIndex) {
    CHECK(Menu_OptionsSelect(0) == OptionsItem::kLoad);
    CHECK(Menu_OptionsSelect(1) == OptionsItem::kSave);
    CHECK(Menu_OptionsSelect(2) == OptionsItem::kGame);
    CHECK(Menu_OptionsSelect(3) == OptionsItem::kGfx);
    CHECK(Menu_OptionsSelect(4) == OptionsItem::kSfx);
    CHECK(Menu_OptionsSelect(5) == OptionsItem::kQuit);
    CHECK(Menu_OptionsSelect(6) == OptionsItem::kResume);
    CHECK(Menu_OptionsSelect(99) == OptionsItem::kResume); // out of range
}

TEST(GuiMenuOptions, ButtonYTable) {
    int expect[7] = {10, 56, 102, 148, 194, 240, 332};
    for (int i = 0; i < 7; ++i)
        CHECK_EQ(kOptionsButtonY[i], expect[i]);
}

namespace {
struct RecordingMenuSink : MenuCommandSink {
    int load = 0, saveLocal = 0, saveNet = 0, game = 0, gfx = 0, sfx = 0, quit = 0, resume = 0;
    void RunLoad() override { ++load; }
    void RunSave(bool net) override { if (net) ++saveNet; else ++saveLocal; }
    void RunGameOptions() override { ++game; }
    void RunGfxOptions() override { ++gfx; }
    void RunSfxOptions() override { ++sfx; }
    void RunQuitConfirm() override { ++quit; }
    void Resume() override { ++resume; }
};
} // namespace

TEST(GuiMenuOptions, DispatchRunsActionAndNetworkSave) {
    RecordingMenuSink sink;
    Menu_SetCommandSink(&sink);
    CHECK(Menu_DispatchOption(0, 0) == OptionsItem::kLoad);
    CHECK_EQ(sink.load, 1);
    CHECK(Menu_DispatchOption(1, 0) == OptionsItem::kSave); // local save
    CHECK_EQ(sink.saveLocal, 1);
    CHECK(Menu_DispatchOption(1, kFlagNetwork) == OptionsItem::kSave); // network save
    CHECK_EQ(sink.saveNet, 1);
    CHECK(Menu_DispatchOption(5, 0) == OptionsItem::kQuit);
    CHECK_EQ(sink.quit, 1);
    CHECK(Menu_DispatchOption(6, 0) == OptionsItem::kResume);
    CHECK_EQ(sink.resume, 1);
    Menu_SetCommandSink(nullptr);
}

TEST(GuiMenuOptions, OptionEnabledFlags) {
    // Mission mode disables Load + Save.
    CHECK(!Menu_OptionEnabled(OptionsItem::kLoad, kFlagMission));
    CHECK(!Menu_OptionEnabled(OptionsItem::kSave, kFlagMission));
    CHECK(Menu_OptionEnabled(OptionsItem::kGfx, kFlagMission));
    // gilde.exe 0x56dccc: network (bit 0x4) disables Load UNCONDITIONALLY (regardless
    // of 0x10), and disables Save unless the host bit 0x10 is set.
    CHECK(!Menu_OptionEnabled(OptionsItem::kLoad, kFlagNetwork));
    CHECK(!Menu_OptionEnabled(OptionsItem::kLoad, kFlagNetwork | 0x10)); // Load still off
    CHECK(!Menu_OptionEnabled(OptionsItem::kSave, kFlagNetwork));        // Save off (no 0x10)
    CHECK(Menu_OptionEnabled(OptionsItem::kSave, kFlagNetwork | 0x10));  // Save on (host)
}

// ===========================================================================
// Player bar slot layout + assignment + output ratio.
// ===========================================================================
TEST(GuiPlayerBar, SlotLayoutPitch) {
    PlayerBarLayout l0 = PlayerBar_SlotLayout(0);
    CHECK_EQ(l0.rowY, 0);
    CHECK_EQ(l0.iconX, 6);
    CHECK_EQ(l0.spriteX, 8);
    CHECK_EQ(l0.spriteY, 17);
    CHECK_EQ(l0.labelY, 4);
    CHECK_EQ(l0.labelWidth, 95);
    CHECK_EQ(l0.subWinY, 63);

    PlayerBarLayout l2 = PlayerBar_SlotLayout(2);
    CHECK_EQ(l2.rowY, 156); // 78*2
    CHECK_EQ(l2.spriteY, 156 + 17);
    CHECK_EQ(l2.labelY, 156 + 4);
    CHECK_EQ(l2.subWinY, 156 + 63);
}

TEST(GuiPlayerBar, AssignFindAndDedup) {
    ResetPlayerBar();
    CHECK_EQ(PlayerBar_FindFreeSlot(), 0);
    CHECK_EQ(PlayerBar_AssignSlot(1234), 0);
    CHECK_EQ(PlayerBar_AssignSlot(5678), 1);
    CHECK_EQ(PlayerBar_AssignSlot(1234), 0); // dedup -> same slot
    CHECK_EQ(PlayerBar_FindSlot(5678), 1);
    CHECK_EQ(PlayerBar_FindSlot(9999), -1);
    // Fill the bar.
    for (int i = 2; i < kPlayerBarSlots; ++i)
        CHECK_EQ(PlayerBar_AssignSlot((guild::u16)(100 + i)), i);
    CHECK_EQ(PlayerBar_AssignSlot(424242 & 0xFFFF), -1); // full
}

TEST(GuiPlayerBar, OutputRatioPercent) {
    CHECK_EQ(PlayerBar_OutputRatioPercent(0.0), 0);
    CHECK_EQ(PlayerBar_OutputRatioPercent(0.5), 50);
    CHECK_EQ(PlayerBar_OutputRatioPercent(1.0), 100);
    CHECK_EQ(PlayerBar_OutputRatioPercent(0.999), 99); // truncation
}

// ===========================================================================
// Info panel builder-select dispatch + change detection.
// ===========================================================================
TEST(GuiInfoPanel, SelectBuilderBySubject) {
    // transporter passenger (category 29) -> kTransporter.
    InfoSelection t{};
    t.transporter = 0x800;
    t.transporterCategory = kTransporterPanelCategory;
    CHECK(InfoPanel_SelectBuilder(t) == InfoBuilder::kTransporter);

    // building selected -> kBuilding.
    InfoSelection b{};
    b.building = 0x900;
    CHECK(InfoPanel_SelectBuilder(b) == InfoBuilder::kBuilding);

    // room + sub-object selected -> kObject.
    InfoSelection o{};
    o.room = 0x100;
    o.subObject = 0x200;
    CHECK(InfoPanel_SelectBuilder(o) == InfoBuilder::kObject);

    // person with single-person mode -> kPerson.
    InfoSelection p{};
    p.person = 0x300;
    p.personMode = 1;
    CHECK(InfoPanel_SelectBuilder(p) == InfoBuilder::kPerson);

    // nothing selected -> kStandard.
    InfoSelection n{};
    CHECK(InfoPanel_SelectBuilder(n) == InfoBuilder::kStandard);
}

TEST(GuiInfoPanel, ChangeDetectionAndSnapshot) {
    InfoSelection s{};
    s.building = 0x10;
    InfoSnapshot snap{};
    // invalid snapshot -> always changed.
    CHECK(InfoPanel_SelectionChanged(s, snap));
    snap = InfoPanel_Snapshot(s);
    CHECK(!InfoPanel_SelectionChanged(s, snap)); // unchanged now
    s.person = 0x20;
    CHECK(InfoPanel_SelectionChanged(s, snap)); // person added -> changed
}

namespace {
struct RecordingInfoSink : InfoPanelCommandSink {
    InfoBuilder last = InfoBuilder::kNone;
    int calls = 0;
    void Build(InfoBuilder b) override { last = b; ++calls; }
};
} // namespace

TEST(GuiInfoPanel, UpdateRebuildsOnlyOnChange) {
    RecordingInfoSink sink;
    InfoPanel_SetCommandSink(&sink);
    InfoSelection s{};
    s.building = 0x55;
    InfoSnapshot snap{};
    InfoBuilder b = InfoPanel_Update(s, snap);
    CHECK(b == InfoBuilder::kBuilding);
    CHECK_EQ(sink.calls, 1);
    // No change -> no rebuild.
    InfoBuilder b2 = InfoPanel_Update(s, snap);
    CHECK(b2 == InfoBuilder::kNone);
    CHECK_EQ(sink.calls, 1);
    InfoPanel_SetCommandSink(nullptr);
}
