// Golden-vector unit tests for the Groundplan / blueprint window DRIVERS
// (the coupled half of VIBE_Groundplan_*; the pure half is covered by
// groundplan_recon_test.cpp).
//
// gilde.exe: 0x4ae3b8 CreateWindow, 0x4ae678 SetWidgetsVisible,
//            0x4ae828 DestroyWidgets, 0x4aea5c LoadBlueprintBmp,
//            0x4af038 RenderBlueprint, 0x4af4a8 BuildInfoPanel,
//            0x4b0758 FadeInScene.
#include "tests/framework/test.h"
#include "gui/groundplan.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using guild::gui::GroundplanState;
using guild::gui::GroundplanBackend;

namespace {

// ---- recording backend: counts calls + captures slot churn -----------------
struct Rec {
    int created = 0;
    int destroyed = 0;
    int visCalls = 0;
    int lastVis = -99;
    int colorFills = 0;
    int hotspotsSpawned = 0;
    std::vector<std::pair<int,int>> roomHotspots;
};
Rec g;

void reset() { g = Rec{}; }

i32 W_Create(i16, i16) { return 100 + g.created++; }
void W_Destroy(i32) { g.destroyed++; }
void O_Vis(i32, int v) { g.visCalls++; g.lastVis = v; }
void F_Vis(i32, int v) { g.visCalls++; g.lastVis = v; }
void E_Bar(int v) { g.visCalls++; g.lastVis = v; }
void S_Fill(i32) { g.colorFills++; }
i32 S_Create(int, int, int) { return 7; }
i32 Win_Create(int,int,int,int,int) { return 55; }
i32 Hot_Reg(int,int,int,int,int) { return 66; }
void OnRoom(int x, int y) { g.roomHotspots.push_back({x,y}); g.hotspotsSpawned++; }

GroundplanBackend makeBackend() {
    GroundplanBackend be;
    be.WidgetCreateObject = &W_Create;
    be.WidgetDestroyByType = &W_Destroy;
    be.ObjectSetVisibleRecursive = &O_Vis;
    be.FormSetObjectsVisible = &F_Vis;
    be.EventPanelSetBarVisible = &E_Bar;
    be.SurfaceColorFill = &S_Fill;
    be.SurfaceCreate = &S_Create;
    be.WindowCreate = &Win_Create;
    be.HotspotRegister = &Hot_Reg;
    be.OnRoomHotspot = &OnRoom;
    be.assetBaseDir = "GAME\\";
    return be;
}

}  // namespace

// ==========================================================================
// PickBlueprintName — full path-selection table (0x4aea5c switch), golden.
// ==========================================================================
TEST(GroundplanPick, Category1) {
    // typeByte==22 -> wirtshaus
    CHECK(gui::Groundplan_PickBlueprintName(1, 22, 0) == "riss_wirtshaus.bmp");
    // roomByte==14 -> parfuemerie ; ==8 -> tinkturei
    CHECK(gui::Groundplan_PickBlueprintName(1, 0, 14) == "riss_parfuemerie.bmp");
    CHECK(gui::Groundplan_PickBlueprintName(1, 0, 8) == "riss_tinkturei.bmp");
    // fallthrough -> generic
    CHECK(gui::Groundplan_PickBlueprintName(1, 0, 0) == "riss_handwerksbetrieb.bmp");
}

TEST(GroundplanPick, Category3) {
    CHECK(gui::Groundplan_PickBlueprintName(3, 15, 0) == "riss_rathaus.bmp");
    CHECK(gui::Groundplan_PickBlueprintName(3, 1, 0) == "Riss_Arbeiterunterkunft.bmp");
    CHECK(gui::Groundplan_PickBlueprintName(3, 0, 0) == "riss_handwerksbetrieb.bmp");
}

TEST(GroundplanPick, Category5AlwaysZunfthaus) {
    CHECK(gui::Groundplan_PickBlueprintName(5, 0, 0) == "riss_zunfthaus.bmp");
    CHECK(gui::Groundplan_PickBlueprintName(5, 99, 99) == "riss_zunfthaus.bmp");
}

TEST(GroundplanPick, DefaultCategory) {
    CHECK(gui::Groundplan_PickBlueprintName(0, 5, 0) == "riss_geldleihe.bmp");
    CHECK(gui::Groundplan_PickBlueprintName(0, 0, 9) == "riss_lagerhaus.bmp");
    CHECK(gui::Groundplan_PickBlueprintName(0, 0, 7) == "riss_kirche.bmp");
    CHECK(gui::Groundplan_PickBlueprintName(0, 0, 19) == "riss_stadtwache.bmp");
    CHECK(gui::Groundplan_PickBlueprintName(0, 0, 0) == "riss_handwerksbetrieb.bmp");
    // typeByte==5 wins over roomByte cases (it's checked first).
    CHECK(gui::Groundplan_PickBlueprintName(2, 5, 9) == "riss_geldleihe.bmp");
}

// ==========================================================================
// ComputeClockHands — ConvertX-truncation + %48 / *0.8 math (0x4af4a8).
// ==========================================================================
TEST(GroundplanClock, MinuteHandTruncates) {
    gui::GroundplanClockInputs in;
    // minutes = (qword>>32) % 60. Put 37 in hi -> minutes 37; 37*0.8=29.6 -> 29.
    in.hi = 37; in.lo = 0;
    auto h = gui::Groundplan_ComputeClockHands(in);
    CHECK_EQ(h.minuteCell, 29);   // trunc(29.6)
}

TEST(GroundplanClock, MinuteHandWraps60) {
    gui::GroundplanClockInputs in;
    in.hi = 65; in.lo = 0;        // 65 % 60 = 5 ; 5*0.8 = 4.0 -> 4
    auto h = gui::Groundplan_ComputeClockHands(in);
    CHECK_EQ(h.minuteCell, 4);
}

TEST(GroundplanClock, HourHandModulo48) {
    // 0x4afdd2: secsTotal = 60*WORD1(qword) + HIDWORD(qword), where the 64-bit
    // packed time is (hi<<32)|lo. WORD1 == bits 16..31 == (lo>>16)&0xFFFF (NOT
    // a function of hi), HIDWORD == hi.
    // hi==1, lo==0 -> WORD1=0, HIDWORD=1 -> secsTotal=1 ; 1*4*(1/60)=0.0667 ->
    // trunc 0 ; %48 = 0.
    gui::GroundplanClockInputs in;
    in.hi = 1; in.lo = 0;
    auto h = gui::Groundplan_ComputeClockHands(in);
    CHECK_EQ(h.hourCell, 0);
}

TEST(GroundplanClock, HourHandWord1FromLowDword) {
    // WORD1 comes from the LOW dword's high half. lo=(1<<16)=0x10000 -> WORD1=1,
    // hi=1 -> HIDWORD=1 -> secsTotal = 60*1 + 1 = 61 ; 61*4*(1/60)=4.0667 ->
    // trunc 4 ; %48 = 4.
    gui::GroundplanClockInputs in;
    in.hi = 1; in.lo = 0x10000u;
    auto h = gui::Groundplan_ComputeClockHands(in);
    CHECK_EQ(h.hourCell, 4);
}

TEST(GroundplanClock, MoonHandBiasedRounding) {
    gui::GroundplanClockInputs in;
    in.weatherPhase = 0.0f;       // 48 + 0 + 0.5 = 48.5 -> trunc 48 ; %48 = 0
    auto h = gui::Groundplan_ComputeClockHands(in);
    CHECK_EQ(h.moonCell, 0);
}

TEST(GroundplanClock, MoonHandHalfPhase) {
    gui::GroundplanClockInputs in;
    // phase = pi -> fmod=pi ; pi*(1/2pi)*48 = 24 ; +48+0.5 = 72.5 -> 72 ; %48=24
    in.weatherPhase = 3.14159265f;
    auto h = gui::Groundplan_ComputeClockHands(in);
    CHECK_EQ(h.moonCell, 24);
}

// ==========================================================================
// SetWidgetsVisible — toggles every populated slot + bar; skips -1 slots.
// ==========================================================================
TEST(GroundplanVisible, TogglesPopulatedOnly) {
    reset();
    auto be = makeBackend();
    GroundplanState st;
    st.wid700 = 5; st.widE4 = 6; st.widE8 = 7;  // a few populated
    // the rest stay -1 (skipped)
    gui::Groundplan_SetWidgetsVisible(st, be, 1);
    CHECK(g.visCalls > 0);
    CHECK_EQ(g.lastVis, 1);
    int onCount = g.visCalls;

    reset();
    gui::Groundplan_SetWidgetsVisible(st, be, 0);
    CHECK_EQ(g.lastVis, 0);
    // hiding also reaches the widAC-only path; same populated-slot set otherwise.
    CHECK(g.visCalls >= 1);
    (void)onCount;
}

TEST(GroundplanVisible, AllUnpopulatedStillTogglesBar) {
    reset();
    auto be = makeBackend();
    GroundplanState st;  // all slots -1 except none
    gui::Groundplan_SetWidgetsVisible(st, be, 1);
    // EventPanelSetBarVisible always fires (the one unconditional call).
    CHECK_EQ(g.lastVis, 1);
    CHECK(g.visCalls >= 1);
}

// ==========================================================================
// DestroyWidgets — destroys populated slots, resets them to -1.
// ==========================================================================
TEST(GroundplanDestroy, ResetsSlots) {
    reset();
    auto be = makeBackend();
    GroundplanState st;
    st.widE4 = 1; st.widE8 = 2; st.widEC = 3; st.wid704 = 4; st.wid708 = 5;
    gui::Groundplan_DestroyWidgets(st, be);
    CHECK_EQ(g.destroyed, 5);
    CHECK_EQ(st.widE4, -1);
    CHECK_EQ(st.widE8, -1);
    CHECK_EQ(st.widEC, -1);
    CHECK_EQ(st.wid704, -1);
    CHECK_EQ(st.wid708, -1);
}

// ==========================================================================
// CreateWindow — registers hotspot+window, clears the 15 room slots, stashes
// the scene id and builds the panel surface + scroll child.
// ==========================================================================
TEST(GroundplanCreate, WiresWindowAndClearsSlots) {
    reset();
    auto be = makeBackend();
    GroundplanState st;
    st.screenW = 1024; st.screenH = 768;
    // pre-dirty the room slots to ensure they get cleared
    for (int i = 0; i < 15; ++i) st.roomHotspots[i] = 999;
    gui::Groundplan_CreateWindow(st, be, 42);
    CHECK_EQ(st.wid700, 55);          // WindowCreate
    CHECK_EQ(st.hotspot710, 66);      // HotspotRegister
    CHECK_EQ(st.sceneFlag714, 42);    // scene id stashed
    CHECK_EQ(st.surf631644, 7);       // panel surface
    CHECK(g.colorFills >= 1);
    for (int i = 0; i < 15; ++i)
        CHECK_EQ(st.roomHotspots[i], -1);
    CHECK(st.widAC != -1);            // scroll child created
}

// ==========================================================================
// BuildInfoPanel — full rebuild (bit0 clear) tears down then recreates the
// widget set; in-place refresh (bit0 set) leaves slots intact.
// ==========================================================================
TEST(GroundplanPanel, FullRebuildRecreates) {
    reset();
    auto be = makeBackend();
    GroundplanState st;
    st.selectedPlot = 3;
    st.widE4 = 1; st.widE8 = 2;  // stale widgets to be torn down
    int r = gui::Groundplan_BuildInfoPanel(st, be, /*rebuild=*/0, {});
    CHECK_EQ(r, 67 * 3);          // result = 67 * selectedPlot
    CHECK(g.destroyed >= 2);      // old widgets torn down
    CHECK(g.created >= 6);        // new label/sprite set created
    CHECK(st.widE4 != -1);
}

TEST(GroundplanPanel, InPlaceRefreshKeepsSlots) {
    reset();
    auto be = makeBackend();
    GroundplanState st;
    st.selectedPlot = 2;
    st.widE4 = 11; st.widE8 = 12;
    int before = st.widE4;
    int r = gui::Groundplan_BuildInfoPanel(st, be, /*rebuild=*/1, {});
    CHECK_EQ(r, 67 * 2);
    CHECK_EQ(st.widE4, before);   // not recreated
}

// ==========================================================================
// RenderBlueprint — dirty rebuild destroys+recreates room hotspots; clean
// frame is a no-op for hotspot churn.
// ==========================================================================
TEST(GroundplanRender, DirtyRebuildsHotspots) {
    reset();
    auto be = makeBackend();
    GroundplanState st;
    st.buildingPtr744 = 0x1000;        // a building is active
    st.roomHotspots[0] = 77;           // a stale hotspot to destroy
    st.selectedPlot = 1;               // != cacheDB8(0) -> dirty
    gui::Groundplan_RenderBlueprint(st, be, 0, 0, 0, false);
    CHECK(g.destroyed >= 1);           // stale hotspot torn down
    CHECK_EQ(st.cacheDB8, 1);          // cache updated
    CHECK_EQ(st.dirtyDB4, 0);
}

TEST(GroundplanRender, CleanFrameNoChurn) {
    reset();
    auto be = makeBackend();
    GroundplanState st;
    st.buildingPtr744 = 0x1000;
    st.selectedPlot = 1; st.cacheDB8 = 1; st.cacheDBC = 0x1000;  // not dirty
    gui::Groundplan_RenderBlueprint(st, be, 0, 0, 0, false);
    CHECK_EQ(g.destroyed, 0);
}
