// E2E flow across the HUD/Menu/MapMarker/PlayerBar batch-2 module:
//   build a minimap from a mixed object list (trees/towers/bridges), then build a HUD
//   slider+training panel for the selected object, enable its object list, and finally
//   tear down the player bar — exercising the marker, layout, enable and destroy paths
//   end-to-end with deterministic golden expectations.
#include "gui/hud_menu2.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild::gui;

namespace {
struct E2EEnable : EnableHooks {
    int calls = 0;
    int SetEnabled(int objId, int) override { ++calls; return objId; }
};
struct E2EPlayerBar : PlayerBarHooks {
    int sequence = 0;
    int destroyOrder = 0, dragOrder = 0, visOrder = 0, infoOrder = 0;
    void FormDestroy(int) override { destroyOrder = ++sequence; }
    void DragSlotResetTable() override { dragOrder = ++sequence; }
    void FormSetObjectsVisible(int, int) override { visOrder = ++sequence; }
    void InfoPanelUpdate() override { infoOrder = ++sequence; }
};
} // namespace

TEST(HudMenu2E2E, MinimapToHudToTeardown) {
    // --- 1. Build the minimap marker table from a mixed object set ---
    MarkerTable map;
    MapMarker_ClassifyTree(map, 10, /*lbaum*/true, false);            // kind 23
    MapMarker_ClassifyTree(map, 11, false, /*other*/true);            // kind 24
    MapMarker_ClassifyTower(map, 20, true);                           // kind 22
    MapMarker_ClassifyTerrainFeature(map, 30, TerrainFeature::Bridge, 0.5);  // bucket 27
    MapMarker_ClassifyTerrainFeature(map, 31, TerrainFeature::Bridge, 2.0);  // bucket 29
    MapMarker_ClassifyTerrainFeature(map, 32, TerrainFeature::Name2, 0.0);   // kind 30

    CHECK_EQ((int)map.count, 6);
    std::vector<int> wantKinds = {23, 24, 22, 27, 29, 30};
    for (size_t i = 0; i < wantKinds.size() && i < map.kinds.size(); ++i)
        CHECK_EQ(map.kinds[i], wantKinds[i]);

    // --- 2. Gold label for the HUD ---
    CHECK(MapView_FormatGoldLabel(4096) == "4096");

    // --- 3. Build a HUD slider panel + training row for the selected object ---
    std::vector<Widget> widgets;
    SliderPanelResult sp = Hud_BuildSliderPanel(widgets, 160, 60, /*win*/4, /*grp*/2, 0);
    CHECK_EQ(sp.upId, 0);
    CHECK_EQ(sp.labelId, 2);
    if (sp.labelId < (int)widgets.size())
        CHECK_EQ((int)widgets[sp.labelId].editFlags(), 67);

    // border colour quads
    BorderColorCell sliderColors;
    Window cur, src;
    src.at<u16>(80) = 0x0A0B;
    src.at<u16>(82) = 0x0C0D;
    int synced = Hud_SyncWindowColors(cur, src, /*link*/1, sliderColors);
    CHECK_EQ(synced, 1);
    CHECK_EQ(sliderColors.c2, 27);
    CHECK_EQ((int)cur.at<u16>(8), 0x0A0B);

    auto buttons = Hud_ObjectActionPanelButtons(1709, false, true);
    // build/transport flag true + special caption -> extra 9
    CHECK_EQ((int)buttons.size(), 8);
    if (buttons.size() == 8) {
        CHECK_EQ(buttons[3], 9);
        CHECK_EQ(buttons[7], 424);
    }

    // --- 4. Enable the selected object's child list (56-byte records, id at +8) ---
    E2EEnable en;
    Hud_SetEnableHooks(&en);
    std::vector<std::vector<guild::u8>> recs;
    for (int i = 0; i < 4; ++i) {
        std::vector<guild::u8> r(kEnableRecStride, 0);
        int id = 500 + i;
        std::memcpy(r.data() + 8, &id, sizeof(id));
        recs.push_back(r);
    }
    int last = Hud_EnableObjectList(recs, 4);
    Hud_SetEnableHooks(nullptr);
    CHECK_EQ(en.calls, 4);
    CHECK_EQ(last, 503);

    // --- 5. Mission name scratch (menu path reused mid-flow) ---
    MissionNameScratch ms;
    int btype = Menu_FormatMissionBuildingName("Markt", "Stadt", ms);
    CHECK_EQ(btype, 51);
    CHECK(ms.name == "Markt");

    // --- 6. Tear down the player bar, verifying ordered side effects ---
    DragSlotTables drag;
    PlayerBar_ResetDragSlots(drag);
    CHECK_EQ(drag.t708[0], 0xFFFF);

    E2EPlayerBar pb;
    PlayerBar_SetHooks(&pb);
    int form = 77;
    PlayerBar_Destroy(form, /*prevForm*/9);
    PlayerBar_SetHooks(nullptr);
    CHECK_EQ(form, -1);
    // ordering: destroy -> dragReset -> setVisible -> infoUpdate
    CHECK_EQ(pb.destroyOrder, 1);
    CHECK_EQ(pb.dragOrder, 2);
    CHECK_EQ(pb.visOrder, 3);
    CHECK_EQ(pb.infoOrder, 4);
}
