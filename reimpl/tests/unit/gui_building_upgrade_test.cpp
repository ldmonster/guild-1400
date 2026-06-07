// Unit tests for the building upgrade / expand-room dialogs and the building
// options-menu hub:
//   building_upgrade_dialog : Upgrade gate ladder + cost; ExpandRoom 12-byte-stride
//                             slot table with per-slot price + handler-progress data.
//   building_options_menu   : the hub entry set per building state + the clicked-entry
//                             -> sub-dialog dispatch table (full + Alt variants).
#include "gui/building_upgrade_dialog.h"
#include "gui/building_options_menu.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

namespace {

struct UpgSink : BuildingCommandSink {
    enum Kind { kNone, kUpgrade, kExpand };
    Kind kind = kNone;
    int building = 0, cost = 0, itemId = 0;
    const char* label = nullptr;
    void reset() { kind = kNone; building = cost = itemId = 0; label = nullptr; }
    void Upgrade(int b, int c, const char* l) override {
        kind = kUpgrade; building = b; cost = c; label = l;
    }
    void ExpandRoom(int b, int it, int c, const char* l) override {
        kind = kExpand; building = b; itemId = it; cost = c; label = l;
    }
};

}  // namespace

// ===========================================================================
// Upgrade dialog
// ===========================================================================

TEST(GuiBldUpgrade, UpgradeCostAndConfirm) {
    UpgSink sink; BuildingDialog_SetCommandSink(&sink);
    UpgradeState s{};
    s.handle = 42;
    s.curUpgradeLevel = 2; s.maxUpgradeLevel = 5;
    s.slotsWorth = 1000;          // cost = round(1000 * 0.3) = 300
    s.canAfford = true;
    UpgradeLayout l = BuildingUpgradeDialog_BuildUpgrade(s);
    CHECK(!l.capped);
    CHECK(!l.requirementsFailed);
    CHECK(!l.plotFailed);
    CHECK_EQ(l.cost, 300);
    CHECK_EQ(l.promptText, kTextUpgradePrompt);
    CHECK_EQ(l.reset28Kind, kReset28KindUpgrade);  // 30
    CHECK(std::strcmp(l.actionLabel, kActionBuildingUpgrade) == 0);  // "building_upgrade"

    sink.reset();
    bool done = BuildingUpgradeDialog_DispatchUpgrade(l, s, kClickOK);
    CHECK(done);
    CHECK(sink.kind == UpgSink::kUpgrade);
    CHECK_EQ(sink.building, 42);
    CHECK_EQ(sink.cost, 300);
    CHECK(std::strcmp(sink.label, "building_upgrade") == 0);
}

TEST(GuiBldUpgrade, UpgradeAffordabilityGate) {
    UpgSink sink; BuildingDialog_SetCommandSink(&sink);
    UpgradeState s{};
    s.handle = 7; s.curUpgradeLevel = 0; s.maxUpgradeLevel = 3;
    s.slotsWorth = 500; s.canAfford = false;
    UpgradeLayout l = BuildingUpgradeDialog_BuildUpgrade(s);
    CHECK_EQ(l.cost, 150);
    sink.reset();
    bool done = BuildingUpgradeDialog_DispatchUpgrade(l, s, kClickOK);
    CHECK(done);                              // dialog dismissed
    CHECK(sink.kind == UpgSink::kNone);       // but no command (cannot afford)
}

TEST(GuiBldUpgrade, UpgradeCappedShowsMessage) {
    UpgradeState s{};
    s.curUpgradeLevel = 5; s.maxUpgradeLevel = 5;  // cur >= max
    UpgradeLayout l = BuildingUpgradeDialog_BuildUpgrade(s);
    CHECK(l.capped);
    CHECK_EQ(l.promptText, kTextUpgradeCapped);  // 5098
}

TEST(GuiBldUpgrade, UpgradeRequirementMessageByCategory) {
    UpgradeState s{};
    s.curUpgradeLevel = 0; s.maxUpgradeLevel = 2;
    s.meetsRequirements = false;
    s.category = 4;                              // 1/4/8 -> 5097
    UpgradeLayout l = BuildingUpgradeDialog_BuildUpgrade(s);
    CHECK(l.requirementsFailed);
    CHECK_EQ(l.requirementText, kTextUpgradeNeedReq158);  // 5097
    s.category = 7;                              // other -> 5100
    UpgradeLayout l2 = BuildingUpgradeDialog_BuildUpgrade(s);
    CHECK_EQ(l2.requirementText, kTextUpgradeNeedReq);    // 5100
}

TEST(GuiBldUpgrade, UpgradeFreeBuildSkipsRequirement) {
    UpgradeState s{};
    s.curUpgradeLevel = 0; s.maxUpgradeLevel = 2;
    s.meetsRequirements = false; s.freeBuild = true;  // global dword_63C7B8 set
    s.slotsWorth = 100;
    UpgradeLayout l = BuildingUpgradeDialog_BuildUpgrade(s);
    CHECK(!l.requirementsFailed);
    CHECK_EQ(l.promptText, kTextUpgradePrompt);
    CHECK_EQ(l.cost, 30);
}

TEST(GuiBldUpgrade, UpgradePlotMismatch) {
    UpgradeState s{};
    s.curUpgradeLevel = 0; s.maxUpgradeLevel = 2;
    s.plotMatches = false;
    UpgradeLayout l = BuildingUpgradeDialog_BuildUpgrade(s);
    CHECK(l.plotFailed);
    CHECK_EQ(l.promptText, kTextUpgradeNeedPlot);  // 5099
}

// ===========================================================================
// ExpandRoom dialog
// ===========================================================================

TEST(GuiBldExpand, SlotTableStrideAndPrice) {
    // Stride constant must be the recovered 12 bytes / 3 ints.
    CHECK_EQ(kExpandSlotStride, 12);
    CHECK_EQ(kExpandSlotMax, 32);

    RoomSlotInput rooms[6] = {};
    // [0] buildable, [1] not-buildable kind, [2] the excluded back slot (253),
    // [3] buildable + in progress, [4] roomType -1 (skip), [5] buildable.
    rooms[0] = {.roomType = 10, .kind = 2, .marketPrice = 200};
    rooms[1] = {.roomType = 11, .kind = 1, .marketPrice = 200};  // kind != 2 -> skip
    rooms[2] = {.roomType = 253, .kind = 2, .marketPrice = 200}; // back slot -> skip
    rooms[3] = {.roomType = 12, .kind = 2, .marketPrice = 100,
                .inProgress = true, .elapsedMinutes = 30, .totalMinutes = 120};
    rooms[4] = {.roomType = -1, .kind = 2, .marketPrice = 200}; // -1 -> skip
    rooms[5] = {.roomType = 14, .kind = 2, .marketPrice = 400};

    // priceMode 2 -> factor = 2*0.25 + 0.5 = 1.0.
    ExpandRoomLayout l = BuildingUpgradeDialog_BuildExpandRoom(rooms, 6, /*priceMode=*/2);
    CHECK(std::strcmp(l.form, kFormExpandRoomPanel) == 0);
    CHECK_EQ(l.titleText, kTextExpandTitleId);   // 5089
    CHECK_EQ(l.footerText, kTextExpandFooter);   // 5090
    CHECK_EQ(l.loopForm, kLoopFormPanel);        // 415687
    CHECK_EQ(l.slotCount, 3);                    // rooms 0,3,5
    CHECK(!l.hasSlider);                         // only 3 (<=4)

    CHECK_EQ(l.slots[0].roomTypeId, 10);
    CHECK_EQ(l.slots[0].price, 200);             // 200 * 1.0
    CHECK_EQ(l.slots[1].roomTypeId, 12);
    CHECK_EQ(l.slots[1].price, 100);
    CHECK(l.slots[1].inProgress);
    CHECK(l.slots[1].progress > 0.24f && l.slots[1].progress < 0.26f);  // 30/120 = 0.25
    CHECK_EQ(l.slots[2].roomTypeId, 14);
    CHECK_EQ(l.slots[2].price, 400);
    // distinct object ids per slot.
    CHECK(l.slots[0].priceObjId != l.slots[1].priceObjId);
    CHECK(l.slots[0].priceObjId != l.slots[0].labelObjId);
}

TEST(GuiBldExpand, PriceModeScale) {
    RoomSlotInput rooms[1] = {};
    rooms[0] = {.roomType = 5, .kind = 2, .marketPrice = 400};
    // priceMode 0 -> factor = 0.5 -> price 200.
    ExpandRoomLayout l0 = BuildingUpgradeDialog_BuildExpandRoom(rooms, 1, 0);
    CHECK_EQ(l0.slots[0].price, 200);
    // priceMode 4 -> factor = 1.5 -> price 600.
    ExpandRoomLayout l4 = BuildingUpgradeDialog_BuildExpandRoom(rooms, 1, 4);
    CHECK_EQ(l4.slots[0].price, 600);
}

TEST(GuiBldExpand, SliderWhenMoreThanFourSlots) {
    RoomSlotInput rooms[6] = {};
    for (int i = 0; i < 6; ++i)
        rooms[i] = {.roomType = 10 + i, .kind = 2, .marketPrice = 100};
    ExpandRoomLayout l = BuildingUpgradeDialog_BuildExpandRoom(rooms, 6, 2);
    CHECK_EQ(l.slotCount, 6);
    CHECK(l.hasSlider);    // > 4 slots -> Hud_BuildSliderPanel
}

TEST(GuiBldExpand, SlotCapAt32) {
    RoomSlotInput rooms[64] = {};
    for (int i = 0; i < 64; ++i)
        rooms[i] = {.roomType = 10 + i, .kind = 2, .marketPrice = 50};
    ExpandRoomLayout l = BuildingUpgradeDialog_BuildExpandRoom(rooms, 64, 2);
    CHECK_EQ(l.slotCount, kExpandSlotMax);  // capped at 32
}

TEST(GuiBldExpand, DispatchSlotConfirm) {
    UpgSink sink; BuildingDialog_SetCommandSink(&sink);
    RoomSlotInput rooms[2] = {};
    rooms[0] = {.roomType = 10, .kind = 2, .marketPrice = 200};
    rooms[1] = {.roomType = 14, .kind = 2, .marketPrice = 400};
    ExpandRoomLayout l = BuildingUpgradeDialog_BuildExpandRoom(rooms, 2, 2);

    sink.reset();
    bool done = BuildingUpgradeDialog_DispatchExpandRoom(
        l, /*building=*/77, kClickOK, l.slots[1].priceObjId, /*canAfford=*/true);
    CHECK(done);
    CHECK(sink.kind == UpgSink::kExpand);
    CHECK_EQ(sink.building, 77);
    CHECK_EQ(sink.itemId, 14);          // the selected slot's room type
    CHECK_EQ(sink.cost, 400);
    CHECK(std::strcmp(sink.label, "room_upgrade") == 0);
}

TEST(GuiBldExpand, DispatchAffordabilityGate) {
    UpgSink sink; BuildingDialog_SetCommandSink(&sink);
    RoomSlotInput rooms[1] = {};
    rooms[0] = {.roomType = 10, .kind = 2, .marketPrice = 200};
    ExpandRoomLayout l = BuildingUpgradeDialog_BuildExpandRoom(rooms, 1, 2);
    sink.reset();
    bool dispatched = BuildingUpgradeDialog_DispatchExpandRoom(
        l, 1, kClickOK, l.slots[0].priceObjId, /*canAfford=*/false);
    CHECK(!dispatched);                 // can't afford -> ShowMessageBox(0), no command
    CHECK(sink.kind == UpgSink::kNone);
}

// ===========================================================================
// Options-menu hub
// ===========================================================================

TEST(GuiBldMenu, OwnBuildingEntrySet) {
    BuildingMenuState s{};
    s.handle = 5; s.isOwn = true; s.category = 1;
    s.stateByte = 0; s.roomCount = 3;            // >1 -> ExpandRoom present
    BuildingMenuLayout l = BuildingOptionsMenu_Build(s, /*roomWorth=*/1234);
    CHECK(std::strcmp(l.form, kFormOptionsPergament) == 0);
    CHECK_EQ(l.loopForm, kLoopFormOptionsMenu);  // 415687
    CHECK_EQ(l.displayWorth, 1234);

    // Expect: rename, sell-preview, renovate, tear-down, open-upgrade, expand, upgrade.
    auto has = [&](BuildingMenuAction a) {
        for (int i = 0; i < l.entryCount; ++i)
            if (l.entries[i].action == a) return true;
        return false;
    };
    CHECK(has(BuildingMenuAction::kRenameField));
    CHECK(has(BuildingMenuAction::kSellPreview));
    CHECK(has(BuildingMenuAction::kRenovate));
    CHECK(has(BuildingMenuAction::kTearDown));
    CHECK(has(BuildingMenuAction::kOpenUpgradeWindow));
    CHECK(has(BuildingMenuAction::kExpandRoom));
    CHECK(has(BuildingMenuAction::kUpgrade));
}

TEST(GuiBldMenu, ExpandRoomNeedsMultipleRooms) {
    BuildingMenuState s{};
    s.isOwn = true; s.category = 1; s.roomCount = 1;  // not > 1 -> no ExpandRoom
    BuildingMenuLayout l = BuildingOptionsMenu_Build(s, 0);
    for (int i = 0; i < l.entryCount; ++i)
        CHECK(l.entries[i].action != BuildingMenuAction::kExpandRoom);
}

TEST(GuiBldMenu, Category2NoUpgradeButDemolish) {
    BuildingMenuState s{};
    s.isOwn = true; s.category = 2; s.roomCount = 2;
    BuildingMenuLayout l = BuildingOptionsMenu_Build(s, 0);
    auto has = [&](BuildingMenuAction a) {
        for (int i = 0; i < l.entryCount; ++i)
            if (l.entries[i].action == a) return true;
        return false;
    };
    CHECK(has(BuildingMenuAction::kDemolishRoom));  // cat==2 sell row -> demolish
    CHECK(!has(BuildingMenuAction::kUpgrade));      // cat==2 -> no upgrade
    CHECK(!has(BuildingMenuAction::kTearDown));     // cat==2 -> no tear-down
}

TEST(GuiBldMenu, ForeignBuildingBuyGate) {
    BuildingMenuState s{};
    s.isOwn = false; s.isCity = false; s.category = 1;
    s.hasBuyHandler = true;
    BuildingMenuLayout l = BuildingOptionsMenu_Build(s, 0);
    bool buy = false;
    for (int i = 0; i < l.entryCount; ++i)
        if (l.entries[i].action == BuildingMenuAction::kBuyBuilding) buy = true;
    CHECK(buy);
}

TEST(GuiBldMenu, Flag90DisablesSellGroup) {
    BuildingMenuState s{};
    s.isOwn = true; s.category = 1; s.flag90 = 4;  // building[90] & 4
    BuildingMenuLayout l = BuildingOptionsMenu_Build(s, 0);
    for (int i = 0; i < l.entryCount; ++i) {
        const BuildingMenuEntry& e = l.entries[i];
        if (e.action == BuildingMenuAction::kRenovate ||
            e.action == BuildingMenuAction::kSellPreview ||
            e.action == BuildingMenuAction::kTearDown)
            CHECK(!e.enabled);
    }
}

TEST(GuiBldMenu, DispatchClickedEntry) {
    BuildingMenuState s{};
    s.isOwn = true; s.category = 1; s.roomCount = 3;
    BuildingMenuLayout l = BuildingOptionsMenu_Build(s, 0);
    // find the Upgrade entry and click it.
    int upObj = -1;
    for (int i = 0; i < l.entryCount; ++i)
        if (l.entries[i].action == BuildingMenuAction::kUpgrade) upObj = l.entries[i].objId;
    CHECK(upObj != -1);
    CHECK(BuildingOptionsMenu_Dispatch(l, /*clickedId=*/1, upObj) ==
          BuildingMenuAction::kUpgrade);
    // no click -> none.
    CHECK(BuildingOptionsMenu_Dispatch(l, /*clickedId=*/-1, upObj) ==
          BuildingMenuAction::kNone);
}

TEST(GuiBldMenu, AltVariantTrimmedSet) {
    BuildingMenuState s{};
    s.isOwn = true; s.category = 1; s.roomCount = 2; s.stateByte = 0;
    BuildingMenuLayout l = BuildingOptionsMenu_BuildAlt(s, 99);
    CHECK_EQ(l.loopForm, kLoopFormOptionsMenuAlt);  // 423879
    auto has = [&](BuildingMenuAction a) {
        for (int i = 0; i < l.entryCount; ++i)
            if (l.entries[i].action == a) return true;
        return false;
    };
    CHECK(has(BuildingMenuAction::kRenovate));
    CHECK(has(BuildingMenuAction::kOpenUpgradeWindow));
    CHECK(has(BuildingMenuAction::kExpandRoom));
    CHECK(has(BuildingMenuAction::kUpgrade));
    // The Alt hub never builds sell / buy / extinguish / rename entries.
    CHECK(!has(BuildingMenuAction::kSellPreview));
    CHECK(!has(BuildingMenuAction::kBuyBuilding));
    CHECK(!has(BuildingMenuAction::kExtinguishFire));
    CHECK(!has(BuildingMenuAction::kRenameField));
}
