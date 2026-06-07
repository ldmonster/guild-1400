// Unit tests for the tooltip lifecycle dispatcher (0x4f7424) and the recoverable
// builder cores (0x4f78e4 / 0x4f83e8 / 0x4f8154).
#include "test.h"
#include "gui/tooltip_dispatch.h"
#include "gui/tooltip_build.h"

#include <cstring>
#include <vector>

using namespace guild::gui;

namespace {

// A recording sink: logs destroy/build/raise calls and hands out incrementing handles.
struct RecSink : TooltipDispatchSink {
    std::vector<int> destroyed;
    std::vector<TooltipKind> built;
    std::vector<int> raised;
    int nextHandle = 100;
    int anchor = 7;
    bool buildFails = false;

    void Destroy(int h) override { destroyed.push_back(h); }
    int Build(TooltipKind kind, const TooltipSubject&, int& anchorId) override {
        built.push_back(kind);
        if (buildFails) { anchorId = 0; return -1; }
        anchorId = anchor;
        return nextHandle++;
    }
    void Raise(int h) override { raised.push_back(h); }
};

// Build tables with a single object whose class byte selects Object vs Upgrade.
TooltipTables tablesWithObject(const u8* objBase) {
    TooltipTables t;
    t.objectBase = objBase;
    return t;
}

} // namespace

// -- builder cores -------------------------------------------------------------------

TEST(GuiTooltipBuild, ContactKeyUppercasesAndFormats) {
    char buf[64];
    int n = Tooltip_BuildContactKey("Hans", buf, sizeof(buf));
    CHECK(std::strcmp(buf, "_HILFE_HANS+0") == 0);
    CHECK_EQ(n, (int)std::strlen("_HILFE_HANS+0"));
}

TEST(GuiTooltipBuild, ContactKeyEmptyName) {
    char buf[64];
    Tooltip_BuildContactKey("", buf, sizeof(buf));
    CHECK(std::strcmp(buf, "_HILFE_+0") == 0);
}

static int findHansOnly(const char* key) {
    return std::strcmp(key, "_HILFE_HANS+0") == 0 ? 4242 : -1;
}

TEST(GuiTooltipBuild, ResolveContactHit) {
    ContactTooltip c = Tooltip_ResolveContact("hans", &findHansOnly);
    CHECK(c.hasText);
    CHECK_EQ(c.textIndex, 4242);
}

TEST(GuiTooltipBuild, ResolveContactMiss) {
    ContactTooltip c = Tooltip_ResolveContact("fritz", &findHansOnly);
    CHECK(!c.hasText);
    CHECK_EQ(c.textIndex, -1);
}

TEST(GuiTooltipBuild, BuildingColorsByteExact) {
    CHECK_EQ(kBuildingColors[0], 0x00000000u);
    CHECK_EQ(kBuildingColors[1], 0x00000049u);
    CHECK_EQ(kBuildingColors[2], 0x00004949u);
    CHECK_EQ(kBuildingColors[3], 0x00494949u);
    CHECK_EQ(kBuildingColors[4], 0x00005649u);
    CHECK_EQ(kBuildingColors[5], 0x00000056u);
    CHECK_EQ(kBuildingColors[6], 0x00004956u);
}

TEST(GuiTooltipBuild, BuildingLayoutValues) {
    u8 rec[600];
    std::memset(rec, 0, sizeof(rec));
    rec[583] = 3;                                  // colour selector -> kBuildingColors[3]
    *reinterpret_cast<std::int32_t*>(rec + 579) = 12345; // extra field (id 41)
    BuildingTooltipLayout l = Tooltip_BuildingLayout(rec, /*code*/5, /*salePrice*/999);
    CHECK_EQ(l.titleColor, 0x00494949u);
    CHECK_EQ(l.nameTextId, 14 * 5 + 1078);
    CHECK_EQ(l.iconObjectId, 5 + 1010);
    CHECK_EQ(l.descColor, (int)0x00494949u);
    CHECK_EQ(l.salePrice, 999);
    CHECK_EQ(l.extraField, 12345);
}

TEST(GuiTooltipBuild, UpgradeAppliesSkipsClass29) {
    u8 obj[65 * 4];
    std::memset(obj, 0, sizeof(obj));
    obj[65 * 2] = 29;   // class 29 at code 2 -> skip
    obj[65 * 3] = 7;    // class 7  at code 3 -> applies
    CHECK(!Tooltip_UpgradeApplies(obj, 2));
    CHECK(Tooltip_UpgradeApplies(obj, 3));
}

// -- dispatch lifecycle --------------------------------------------------------------

TEST(GuiTooltipDispatch, NoSubjectNoChange) {
    TooltipDispatchState s;            // all -1 / 0
    RecSink sink;
    TooltipTables t;
    TooltipAction a = Tooltip_Dispatch(s, sink, t, nullptr);
    CHECK(a == TooltipAction::kNone);
    CHECK_EQ(s.formHandle, -1);
    CHECK(sink.built.empty());
    CHECK(sink.raised.empty());
}

TEST(GuiTooltipDispatch, BuildsObjectTooltipFromId) {
    // object id-fallback range [206,1010): id 210 -> object code 4.
    u8 obj[65 * 16];
    std::memset(obj, 0, sizeof(obj));
    obj[65 * 4] = 32;  // class 32 -> kObject (not upgrade)
    TooltipTables t = tablesWithObject(obj);

    TooltipDispatchState s;
    s.hoveredTooltipId = 210;
    RecSink sink;
    TooltipAction a = Tooltip_Dispatch(s, sink, t, nullptr);

    CHECK(a == TooltipAction::kBuilt);
    CHECK_EQ((int)sink.built.size(), 1);
    CHECK(sink.built[0] == TooltipKind::kObject);
    CHECK_EQ(s.formHandle, 100);
    CHECK_EQ(s.shownForId, 210);
    CHECK_EQ(s.formAnchorId, 7);
    CHECK_EQ(s.visibleFlag, 1);
    CHECK_EQ(s.raiseFlag, 1);
    CHECK_EQ((int)sink.raised.size(), 1);
}

TEST(GuiTooltipDispatch, BuildingIdRangeMapsToBuilding) {
    // building id-fallback range [1010,1082): id 1020 -> building code 1020+14.
    TooltipTables t;
    TooltipDispatchState s;
    s.hoveredTooltipId = 1020;
    RecSink sink;
    TooltipAction a = Tooltip_Dispatch(s, sink, t, nullptr);
    CHECK(a == TooltipAction::kBuilt);
    CHECK(sink.built[0] == TooltipKind::kBuilding);
}

TEST(GuiTooltipDispatch, HideWhenHoverClears) {
    // First build a tooltip, then clear the hover and dispatch again -> torn down.
    TooltipTables t;
    TooltipDispatchState s;
    s.hoveredTooltipId = 1020;            // building
    RecSink sink;
    Tooltip_Dispatch(s, sink, t, nullptr);
    CHECK_EQ(s.formHandle, 100);

    s.hoveredTooltipId = -1;              // hover cleared, shownForId != -1
    TooltipAction a = Tooltip_Dispatch(s, sink, t, nullptr);
    CHECK(a == TooltipAction::kHidden);
    CHECK_EQ(s.formHandle, -1);
    CHECK_EQ(s.shownForId, -1);
    CHECK_EQ(s.visibleFlag, 0);
    CHECK_EQ(s.raiseFlag, 0);
    CHECK_EQ((int)sink.destroyed.size(), 1);
    CHECK_EQ(sink.destroyed[0], 100);
}

TEST(GuiTooltipDispatch, BuildFailureLeavesNoForm) {
    TooltipTables t;
    TooltipDispatchState s;
    s.hoveredTooltipId = 1020;
    RecSink sink;
    sink.buildFails = true;
    TooltipAction a = Tooltip_Dispatch(s, sink, t, nullptr);
    CHECK(a == TooltipAction::kNone);    // built attempted but failed -> handle -1
    CHECK_EQ(s.formHandle, -1);
    CHECK_EQ(s.shownForId, -1);
    CHECK_EQ((int)sink.built.size(), 1);
}

TEST(GuiTooltipDispatch, ContactRequestBuildsContact) {
    // contactReqId set, scenePick active, no record ptr -> contact tooltip from id.
    TooltipTables t;
    TooltipDispatchState s;
    s.contactReqId    = 55;
    s.scenePickActive = 1;
    RecSink sink;
    TooltipAction a = Tooltip_Dispatch(s, sink, t, nullptr);
    CHECK(a == TooltipAction::kBuilt);
    CHECK(sink.built[0] == TooltipKind::kContact);
    CHECK_EQ(s.builtForReqId, 55);
}

TEST(GuiTooltipDispatch, ScenePickKeepsFormAndZerosRedraw) {
    // With scenePick active, a built tooltip clears the redraw flag (dword_62D0D4 = 0).
    TooltipTables t;
    TooltipDispatchState s;
    s.hoveredTooltipId = 1020;
    s.scenePickActive  = 1;
    s.redrawFlag       = 1;
    RecSink sink;
    Tooltip_Dispatch(s, sink, t, nullptr);
    CHECK_EQ(s.redrawFlag, 0);
}
