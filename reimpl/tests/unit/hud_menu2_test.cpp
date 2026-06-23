// Unit tests for the HUD/Menu/MapMarker/PlayerBar batch-2 module (Wave 14):
//   - minimap marker classifiers (tree 23/24, tower 22, bridge-angle 26..29, feature 30/31)
//   - map-view gold label format
//   - HUD slider panel + training row layout
//   - HUD window-colour sync + action-panel border quad + action button sequence
//   - HUD enable-object list (56-byte stride, id at +8)
//   - player bar drag-slot reset (320/10) + destroy flow
//   - mission building-name scratch prep
#include "gui/hud_menu2.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild::gui;

TEST(HudMenu2_Marker, TreeLbaumKind23) {
    MarkerTable t;
    bool more = MapMarker_ClassifyTree(t, 111, /*isLbaum*/true, false);
    CHECK_EQ((int)t.count, 1);
    CHECK_EQ(t.ids[0], 111);
    CHECK_EQ(t.kinds[0], 23);
    CHECK(more); // count 1 < 48
}

TEST(HudMenu2_Marker, TreeOtherKind24) {
    MarkerTable t;
    MapMarker_ClassifyTree(t, 7, /*isLbaum*/false, /*isOtherTree*/true);
    CHECK_EQ(t.kinds[0], 24);
    // neither name matches -> no push, still room
    MarkerTable t2;
    bool more = MapMarker_ClassifyTree(t2, 7, false, false);
    CHECK_EQ((int)t2.count, 0);
    CHECK(more);
}

TEST(HudMenu2_Marker, TowerKind22AndCap) {
    MarkerTable t;
    MapMarker_ClassifyTower(t, 99, true);
    CHECK_EQ(t.kinds[0], 22);
    CHECK_EQ(t.ids[0], 99);
    // non-match leaves table empty but still returns room
    MarkerTable t2;
    bool more = MapMarker_ClassifyTower(t2, 5, false);
    CHECK_EQ((int)t2.count, 0);
    CHECK(more);
    // fill to cap and confirm the <128 gate flips
    MarkerTable t3;
    bool last = true;
    for (int i = 0; i < 128; ++i) last = MapMarker_ClassifyTower(t3, i, true);
    CHECK(!last); // count == 128, not < 128
}

TEST(HudMenu2_Marker, BridgeAngleBuckets) {
    // golden vectors (python oracle over the recovered double table)
    CHECK_EQ(MapMarker_BridgeAngleKind(0.0), 26);
    CHECK_EQ(MapMarker_BridgeAngleKind(0.1), 26);
    CHECK_EQ(MapMarker_BridgeAngleKind(0.5), 27);
    CHECK_EQ(MapMarker_BridgeAngleKind(1.5), 28);
    CHECK_EQ(MapMarker_BridgeAngleKind(2.0), 29);
    CHECK_EQ(MapMarker_BridgeAngleKind(2.8), 26);
    CHECK_EQ(MapMarker_BridgeAngleKind(3.6), 27);
    CHECK_EQ(MapMarker_BridgeAngleKind(4.4), 28);
    CHECK_EQ(MapMarker_BridgeAngleKind(5.2), 28);
    CHECK_EQ(MapMarker_BridgeAngleKind(6.0), 26);
    CHECK_EQ(MapMarker_BridgeAngleKind(6.28), 26);
}

TEST(HudMenu2_Marker, TerrainFeatureBranches) {
    MarkerTable t;
    MapMarker_ClassifyTerrainFeature(t, 1, TerrainFeature::Bridge, 2.0);
    CHECK_EQ(t.kinds[0], 29);     // angle 2.0 -> bucket 29
    CHECK_EQ(t.ids[0], 1);
    MapMarker_ClassifyTerrainFeature(t, 2, TerrainFeature::Name2, 0.0);
    CHECK_EQ(t.kinds[1], 30);
    MapMarker_ClassifyTerrainFeature(t, 3, TerrainFeature::Name3, 0.0);
    CHECK_EQ(t.kinds[2], 31);
    bool more = MapMarker_ClassifyTerrainFeature(t, 4, TerrainFeature::None, 0.0);
    CHECK_EQ((int)t.count, 3);    // None pushes nothing
    CHECK(more);
}

TEST(HudMenu2_GoldLabel, Decimal) {
    CHECK(MapView_FormatGoldLabel(0) == "0");
    CHECK(MapView_FormatGoldLabel(12345) == "12345");
    CHECK(MapView_FormatGoldLabel(-7) == "-7");
}

TEST(HudMenu2_Slider, PanelLayout) {
    std::vector<Widget> w;
    SliderPanelResult r = Hud_BuildSliderPanel(w, /*baseX*/200, /*baseY*/40,
                                               /*win*/3, /*group*/5, /*user*/99);
    // three distinct widgets allocated 0,1,2
    CHECK_EQ(r.upId, 0);
    CHECK_EQ(r.downId, 1);
    CHECK_EQ(r.labelId, 2);
    if (r.upId >= 0 && r.upId < (int)w.size()) {
        CHECK_EQ((int)w[r.upId].btnFlagB(), 1);
        CHECK_EQ((int)w[r.upId].radioFlag(), 3);
        CHECK_EQ((int)w[r.upId].at<i32>(476), 5);
    }
    if (r.downId >= 0 && r.downId < (int)w.size()) {
        CHECK_EQ((int)w[r.downId].btnFlagB(), 1);
        CHECK_EQ((int)w[r.downId].at<i32>(476), 5);
    }
    if (r.labelId >= 0 && r.labelId < (int)w.size()) {
        CHECK_EQ((int)w[r.labelId].x(), 200 - 19);
        CHECK_EQ((int)w[r.labelId].y(), 40 + 25);
        CHECK_EQ((int)w[r.labelId].at<i16>(112), 67); // 0x4bd564 writes +112 (text color)
        CHECK_EQ((int)w[r.labelId].at<i32>(88), 1);
        CHECK_EQ((int)w[r.labelId].at<i16>(20), 48);
    }
}

TEST(HudMenu2_Color, SyncWindowColors) {
    Window cur, src;
    src.at<u16>(80) = 0x1234;
    src.at<u16>(82) = 0x5678;
    BorderColorCell cell;
    int r = Hud_SyncWindowColors(cur, src, /*srcLink*/0, cell);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)cur.at<u16>(8), 0x1234);
    CHECK_EQ((int)cur.at<u16>(10), 0x5678);
    CHECK_EQ(cell.c0, 24);
    CHECK_EQ(cell.c1, 24);
    CHECK_EQ(cell.c2, 27);
    CHECK_EQ(cell.c3, 11);
    // link == -1 -> no copy, returns 0
    Window cur2, src2;
    BorderColorCell cell2;
    int r2 = Hud_SyncWindowColors(cur2, src2, -1, cell2);
    CHECK_EQ(r2, 0);
    CHECK_EQ(cell2.c0, 0);
}

TEST(HudMenu2_Color, ActionPanelBorderAndButtons) {
    BorderColorCell cell;
    Hud_BuildObjectActionPanel_Colors(cell);
    CHECK_EQ(cell.c0, 24);
    CHECK_EQ(cell.c1, 24);
    CHECK_EQ(cell.c2, 24);
    CHECK_EQ(cell.c3, 24);

    // normal panel (no flags, non-special caption)
    auto normal = Hud_ObjectActionPanelButtons(100, false, false);
    std::vector<int> wantN = {9, 9, 9, 0, 400, 77, 77};
    CHECK_EQ((int)normal.size(), (int)wantN.size());
    for (size_t i = 0; i < wantN.size() && i < normal.size(); ++i)
        CHECK_EQ(normal[i], wantN[i]);

    // 1709 special case with a build flag -> extra 9 then 67/67/424
    auto special = Hud_ObjectActionPanelButtons(1709, true, false);
    std::vector<int> wantS = {9, 9, 9, 9, 0, 67, 67, 424};
    CHECK_EQ((int)special.size(), (int)wantS.size());
    for (size_t i = 0; i < wantS.size() && i < special.size(); ++i)
        CHECK_EQ(special[i], wantS[i]);
}

namespace {
struct CountingEnable : EnableHooks {
    std::vector<int> enabled;
    int SetEnabled(int objId, int) override { enabled.push_back(objId); return objId; }
};
} // namespace

TEST(HudMenu2_Enable, FirstNObjects) {
    CountingEnable hk;
    Hud_SetEnableHooks(&hk);
    std::vector<std::vector<guild::u8>> recs;
    for (int i = 0; i < 5; ++i) {
        std::vector<guild::u8> r(kEnableRecStride, 0);
        int id = 1000 + i;
        std::memcpy(r.data() + 8, &id, sizeof(id)); // id at +8
        recs.push_back(r);
    }
    int last = Hud_EnableObjectList(recs, 3);
    Hud_SetEnableHooks(nullptr);
    CHECK_EQ((int)hk.enabled.size(), 3);
    if (hk.enabled.size() == 3) {
        CHECK_EQ(hk.enabled[0], 1000);
        CHECK_EQ(hk.enabled[1], 1001);
        CHECK_EQ(hk.enabled[2], 1002);
    }
    CHECK_EQ(last, 1002);
    // count <= 0 -> no calls
    CountingEnable hk2;
    Hud_SetEnableHooks(&hk2);
    CHECK_EQ(Hud_EnableObjectList(recs, 0), 0);
    CHECK_EQ((int)hk2.enabled.size(), 0);
    Hud_SetEnableHooks(nullptr);
}

TEST(HudMenu2_DragSlots, ResetTable) {
    DragSlotTables t;
    PlayerBar_ResetDragSlots(t);
    int touched = 0;
    // 0x4b1d17: the `i += 10` runs at the top of the body, so the writes land on
    // i = 10, 20, ..., 320 (slot 0 is NOT reset; slot 320 IS).
    for (int i = kDragSlotStep; i <= kDragSlotLoopEnd; i += kDragSlotStep) {
        CHECK_EQ(t.t6F8[i], -1);
        CHECK_EQ(t.t700[i], -1);
        CHECK_EQ(t.t704[i], -1);
        CHECK_EQ(t.t708[i], 0xFFFF);
        CHECK_EQ(t.t70C[i], -1);
        CHECK_EQ(t.t710[i], -1);
        CHECK_EQ(t.b714[i], 0);
        ++touched;
    }
    CHECK_EQ(touched, 32);          // indices 10..320 step 10
    // slot 0 is never reset by the loop (stays 0), and entries between strides too.
    CHECK_EQ(t.t6F8[0], 0);
    CHECK_EQ(t.t6F8[1], 0);
    CHECK_EQ(t.t708[5], 0);
}

namespace {
struct PbTrace : PlayerBarHooks {
    int destroyed = -999, visForm = -999, visVal = -999;
    int dragReset = 0, infoUpdate = 0;
    void FormDestroy(int f) override { destroyed = f; }
    void DragSlotResetTable() override { ++dragReset; }
    void FormSetObjectsVisible(int f, int v) override { visForm = f; visVal = v; }
    void InfoPanelUpdate() override { ++infoUpdate; }
};
} // namespace

TEST(HudMenu2_PlayerBar, DestroyWithPrevForm) {
    PbTrace tr;
    PlayerBar_SetHooks(&tr);
    int form = 42;
    PlayerBar_Destroy(form, /*prevForm*/7);
    PlayerBar_SetHooks(nullptr);
    CHECK_EQ(tr.destroyed, 42);
    CHECK_EQ(form, -1);                 // dword_631764 = -1
    CHECK_EQ(tr.dragReset, 1);
    CHECK_EQ(tr.visForm, 7);
    CHECK_EQ(tr.visVal, 1);
    CHECK_EQ(tr.infoUpdate, 1);
}

TEST(HudMenu2_PlayerBar, DestroyNoPrevForm) {
    PbTrace tr;
    PlayerBar_SetHooks(&tr);
    int form = 13;
    PlayerBar_Destroy(form, /*prevForm*/-1);
    PlayerBar_SetHooks(nullptr);
    CHECK_EQ(form, -1);
    CHECK_EQ(tr.visForm, -999);         // SetObjectsVisible NOT called
    CHECK_EQ(tr.infoUpdate, 1);
}

TEST(HudMenu2_Mission, BuildingNameScratch) {
    MissionNameScratch s;
    int type = Menu_FormatMissionBuildingName("Rathaus", "City", s);
    CHECK(s.name == "Rathaus");
    CHECK(s.subName == "City");
    CHECK_EQ(s.hdr0, 856692811);
    CHECK_EQ(s.hdr4, 1342);
    CHECK_EQ(s.hdr528, 1555);
    CHECK_EQ((int)s.flagA8, 0);
    CHECK_EQ((int)s.flagA9, 0);
    CHECK_EQ(type, 51);                 // SHIBYTE(0x3310184B) = 0x33 = 51
}
