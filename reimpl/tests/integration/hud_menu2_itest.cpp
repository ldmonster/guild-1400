#include "test.h"

// Integration: drive hud_menu2's VIBE_PlayerBar_Destroy and VIBE_Hud_EnableObjectList
// against REAL reconstructed gui siblings — no mock form/widget cluster:
//
//  * PlayerBar_Destroy's PlayerBarHooks.FormDestroy / FormSetObjectsVisible slots
//    ARE VIBE_Form_Destroy (0x41da04) and VIBE_Form_SetObjectsVisible (0x41d634)
//    in the live binary (the header names that exact wiring). We forward them into
//    the real form_lifecycle.cpp functions operating on the real g_forms / g_widgets
//    globals, and assert the destroy clears the player-bar form's valid flag while
//    the previous form's widgets are made visible by the real Form_SetObjectsVisible.
//
//  * Hud_EnableObjectList's EnableHooks.SetEnabled slot is the widget enable path;
//    we forward it into the real VIBE_RadioGroup_SetEnabled (radiogroup.cpp 0x4128a4)
//    over the real g_radioGroups / g_widgets tables, and assert the listed groups'
//    buttons are really enabled (disabledA cleared) by the sibling.
//
// (the marker-classification / gold-label leaves below have no cross-module callee
// and run on the module's inert defaults — noted at that test.)
#include "gui/hud_menu2.h"
#include "gui/form_lifecycle.h"   // REAL siblings: Form_Destroy, Form_SetObjectsVisible
#include "gui/form.h"             // g_forms
#include "gui/object.h"           // g_widgets
#include "gui/radiogroup.h"       // REAL sibling: RadioGroup_SetEnabled, g_radioGroups

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {

// PlayerBarHooks forwarding FormDestroy / FormSetObjectsVisible into the REAL
// form_lifecycle functions; the other two side effects are recorded (no exported
// reconstructed sibling for the drag-table reset / info-panel refresh).
struct RealFormPlayerBarHooks : PlayerBarHooks {
    int destroyedForm = -99;
    int visibleForm   = -99;
    int dragResets    = 0;
    int infoUpdates   = 0;
    void FormDestroy(int form) override {
        destroyedForm = form;
        gui::Form_Destroy(form);                 // -> real VIBE_Form_Destroy
    }
    void FormSetObjectsVisible(int form, int visible) override {
        visibleForm = form;
        gui::Form_SetObjectsVisible(form, visible); // -> real VIBE_Form_SetObjectsVisible
    }
    void DragSlotResetTable() override { ++dragResets; }
    void InfoPanelUpdate() override   { ++infoUpdates; }
};

// EnableHooks forwarding SetEnabled into the REAL RadioGroup_SetEnabled (the object
// id in the enable-list record is used as the group id, exactly as the radio-group
// enabler keys its tables).
struct RealRadioEnableHooks : EnableHooks {
    int calls = 0;
    int SetEnabled(int objId, int enabled) override {
        ++calls;
        gui::RadioGroup_SetEnabled(objId, enabled); // -> real VIBE_RadioGroup_SetEnabled
        return objId;
    }
};

// Build a 56-byte enable record carrying `objId` at +8.
std::vector<guild::u8> MakeEnableRecord(int objId) {
    std::vector<guild::u8> rec(kEnableRecStride, 0);
    std::memcpy(rec.data() + 8, &objId, sizeof(objId));
    return rec;
}

} // namespace

// PlayerBar_Destroy forwards into the real form siblings. Set up a real previous
// form with one in-use widget whose parentClip points at that form, then destroy
// the player-bar form. Assert: (1) Form_Destroy cleared the player-bar form's valid
// flag, (2) Form_SetObjectsVisible (called with hide=1) set the prev form's widget
// renderPtr to 0 and cached hide=1, (3) the player-bar form handle was reset to -1.
TEST(HudMenu2Itest, PlayerBarDestroyDrivesRealFormSiblings) {
    const int barFormId  = 5;
    const int prevFormId = 9;

    // Reset the two real form records we touch.
    g_forms[barFormId]  = Form{};   // ctor zero-fills the record
    g_forms[prevFormId] = Form{};

    // Player-bar form: valid, no child windows (so Form_Destroy just clears valid).
    g_forms[barFormId].valid()       = 1;
    g_forms[barFormId].windowCount() = 0;

    // Previous form: valid; give it one in-use widget keyed to its form record.
    g_forms[prevFormId].valid()       = 1;
    g_forms[prevFormId].windowCount() = 0;
    i32 prevFormKey =
        static_cast<i32>(reinterpret_cast<std::intptr_t>(&g_forms[prevFormId]));

    const int wIdx = 3;
    g_widgets[wIdx] = Widget{};
    g_widgets[wIdx].type()       = 7;             // in-use (type != 0)
    g_widgets[wIdx].parentClip() = prevFormKey;   // owned by the previous form
    g_widgets[wIdx].renderPtr()  = 1;             // currently rendered

    RealFormPlayerBarHooks hooks;
    PlayerBar_SetHooks(&hooks);

    int playerBarForm = barFormId;
    PlayerBar_Destroy(playerBarForm, prevFormId);

    // (3) the player-bar form handle was reset.
    CHECK_EQ(playerBarForm, kPlayerBarInvalidForm);
    // hook ordering side effects fired.
    CHECK_EQ(hooks.destroyedForm, barFormId);
    CHECK_EQ(hooks.visibleForm, prevFormId);
    CHECK_EQ(hooks.dragResets, 1);
    CHECK_EQ(hooks.infoUpdates, 1);

    // (1) real Form_Destroy cleared the player-bar form's valid flag.
    CHECK_EQ(g_forms[barFormId].valid(), 0);
    // (2) real Form_SetObjectsVisible ran with hide=1: cached value + widget hidden.
    CHECK_EQ(g_forms[prevFormId].objectsVisible(), 1);
    CHECK_EQ(g_widgets[wIdx].renderPtr(), 0);   // (hide==0) -> 0

    PlayerBar_SetHooks(nullptr);
}

// Hud_EnableObjectList forwards SetEnabled into the real RadioGroup_SetEnabled.
// Seed two real radio groups with real widgets (disabled), feed their group ids
// through the enable-list, and assert the real sibling enabled (disabledA=0) every
// button of the listed groups.
TEST(HudMenu2Itest, EnableObjectListDrivesRealRadioGroupSetEnabled) {
    const int grpA = 1, grpB = 2;
    const int a0 = 10, a1 = 11, b0 = 12;

    // Real radio groups + their button widgets, all initially disabled.
    std::memset(&g_radioGroups[grpA], 0, sizeof(RadioGroup));
    std::memset(&g_radioGroups[grpB], 0, sizeof(RadioGroup));
    g_radioGroups[grpA].count = 2;
    g_radioGroups[grpA].button[0] = a0;
    g_radioGroups[grpA].button[1] = a1;
    g_radioGroups[grpB].count = 1;
    g_radioGroups[grpB].button[0] = b0;
    for (int w : {a0, a1, b0}) {
        g_widgets[w] = Widget{};
        g_widgets[w].disabledA() = 1;   // start disabled
    }

    RealRadioEnableHooks hooks;
    Hud_SetEnableHooks(&hooks);

    // Two enable records carrying the group ids at +8.
    std::vector<std::vector<guild::u8>> records;
    records.push_back(MakeEnableRecord(grpA));
    records.push_back(MakeEnableRecord(grpB));

    int last = Hud_EnableObjectList(records, 2);

    CHECK_EQ(hooks.calls, 2);
    CHECK_EQ(last, grpB);   // returns last record's object id
    // The real RadioGroup_SetEnabled cleared disabledA on every listed group's button.
    CHECK_EQ(g_widgets[a0].disabledA(), 0);
    CHECK_EQ(g_widgets[a1].disabledA(), 0);
    CHECK_EQ(g_widgets[b0].disabledA(), 0);

    Hud_SetEnableHooks(nullptr);
}

// Pure-leaf path (no reconstructed sibling): the minimap marker classifiers and the
// gold-label formatter run entirely on the module's own deterministic math — no
// hooks are installed/touched here (inert default state).
TEST(HudMenu2Itest, MarkerAndGoldLabelPureLeaves) {
    MarkerTable tbl;
    // Tower (kind 22).
    bool room = MapMarker_ClassifyTower(tbl, 100, /*isSlateTower=*/true);
    CHECK(room);
    if (!tbl.kinds.empty()) CHECK_EQ(tbl.kinds[0], kMarkerKindTower);
    if (!tbl.ids.empty())   CHECK_EQ(tbl.ids[0], 100);

    // Tree LBAUM (kind 23).
    MarkerTable trees;
    MapMarker_ClassifyTree(trees, 200, /*isLbaum=*/true, /*isOtherTree=*/false);
    if (!trees.kinds.empty()) CHECK_EQ(trees.kinds[0], kMarkerKindTreeLbaum);

    // Bridge angle bucket 0 -> kind 26.
    CHECK_EQ(MapMarker_BridgeAngleKind(0.0), kMarkerKindBridgeEW);

    // Gold label format.
    std::string label = MapView_FormatGoldLabel(1234);
    CHECK(label.find("1234") != std::string::npos);
}
