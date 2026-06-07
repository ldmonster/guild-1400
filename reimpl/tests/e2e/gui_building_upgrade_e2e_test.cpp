// End-to-end flow for the building options-menu hub and its upgrade/expand-room
// sub-dialogs. Mirrors the original modal flow:
//   1. open the options menu for a synthetic (own) building;
//   2. verify the hub entry tree;
//   3. click the Upgrade entry -> dispatch into the Upgrade sub-dialog -> confirm ->
//      verify the mock command (building_upgrade) fires with the recovered cost;
//   4. click the ExpandRoom entry -> dispatch into the ExpandRoom sub-dialog -> build
//      its 12-byte-stride slot table -> confirm a slot -> verify room_upgrade command.
#include "gui/building_options_menu.h"
#include "gui/building_upgrade_dialog.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

namespace {

struct FlowSink : BuildingCommandSink {
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

int FindEntryObj(const BuildingMenuLayout& l, BuildingMenuAction a) {
    for (int i = 0; i < l.entryCount; ++i)
        if (l.entries[i].action == a) return l.entries[i].objId;
    return -1;
}

}  // namespace

TEST(GuiBldUpgE2E, OpenHubNavigateUpgradeAndExpand) {
    FlowSink sink; BuildingDialog_SetCommandSink(&sink);

    // --- A synthetic own building with 3 rooms. -----------------------------
    BuildingMenuState ms{};
    ms.handle = 1001; ms.isOwn = true; ms.category = 1;
    ms.stateByte = 0; ms.roomCount = 3;

    // 1. open the hub.
    BuildingMenuLayout hub = BuildingOptionsMenu_Build(ms, /*roomWorth=*/2000);
    CHECK(std::strcmp(hub.form, kFormOptionsPergament) == 0);
    CHECK_EQ(hub.loopForm, kLoopFormOptionsMenu);

    // 2. the hub tree contains the navigable entries.
    int upgObj = FindEntryObj(hub, BuildingMenuAction::kUpgrade);
    int expObj = FindEntryObj(hub, BuildingMenuAction::kExpandRoom);
    CHECK(upgObj != -1);
    CHECK(expObj != -1);

    // 3. navigate to Upgrade: clicking the upgrade entry dispatches the action.
    CHECK(BuildingOptionsMenu_Dispatch(hub, /*clickedId=*/1, upgObj) ==
          BuildingMenuAction::kUpgrade);

    //    open the Upgrade sub-dialog for the same building.
    UpgradeState us{};
    us.handle = ms.handle;
    us.curUpgradeLevel = 1; us.maxUpgradeLevel = 4;
    us.slotsWorth = 1500;          // cost = round(1500 * 0.3) = 450
    us.canAfford = true;
    UpgradeLayout ul = BuildingUpgradeDialog_BuildUpgrade(us);
    CHECK(!ul.capped && !ul.requirementsFailed && !ul.plotFailed);
    CHECK_EQ(ul.cost, 450);
    CHECK(std::strcmp(ul.actionLabel, "building_upgrade") == 0);

    //    confirm -> the mock building_upgrade command fires.
    sink.reset();
    CHECK(BuildingUpgradeDialog_DispatchUpgrade(ul, us, kClickOK));
    CHECK(sink.kind == FlowSink::kUpgrade);
    CHECK_EQ(sink.building, 1001);
    CHECK_EQ(sink.cost, 450);
    CHECK(std::strcmp(sink.label, "building_upgrade") == 0);

    // 4. back to the hub; navigate to ExpandRoom.
    CHECK(BuildingOptionsMenu_Dispatch(hub, /*clickedId=*/1, expObj) ==
          BuildingMenuAction::kExpandRoom);

    //    open the ExpandRoom sub-dialog with a synthetic room-slot array.
    RoomSlotInput rooms[4] = {};
    rooms[0] = {.roomType = 20, .kind = 2, .marketPrice = 300};
    rooms[1] = {.roomType = 253, .kind = 2, .marketPrice = 300};  // back slot -> skip
    rooms[2] = {.roomType = 22, .kind = 1, .marketPrice = 300};   // wrong kind -> skip
    rooms[3] = {.roomType = 24, .kind = 2, .marketPrice = 500};
    ExpandRoomLayout el =
        BuildingUpgradeDialog_BuildExpandRoom(rooms, 4, /*priceMode=*/2);  // factor 1.0
    CHECK(std::strcmp(el.form, kFormExpandRoomPanel) == 0);
    CHECK_EQ(el.slotCount, 2);                 // rooms 0 and 3
    CHECK_EQ(el.slots[0].roomTypeId, 20);
    CHECK_EQ(el.slots[0].price, 300);
    CHECK_EQ(el.slots[1].roomTypeId, 24);
    CHECK_EQ(el.slots[1].price, 500);

    //    confirm slot 1 -> the mock room_upgrade command fires.
    sink.reset();
    CHECK(BuildingUpgradeDialog_DispatchExpandRoom(
        el, ms.handle, kClickOK, el.slots[1].priceObjId, /*canAfford=*/true));
    CHECK(sink.kind == FlowSink::kExpand);
    CHECK_EQ(sink.building, 1001);
    CHECK_EQ(sink.itemId, 24);
    CHECK_EQ(sink.cost, 500);
    CHECK(std::strcmp(sink.label, "room_upgrade") == 0);
}

TEST(GuiBldUpgE2E, AltHubReachesUpgradeAndExpand) {
    FlowSink sink; BuildingDialog_SetCommandSink(&sink);
    BuildingMenuState ms{};
    ms.handle = 2002; ms.isOwn = true; ms.category = 1; ms.roomCount = 2; ms.stateByte = 0;

    BuildingMenuLayout hub = BuildingOptionsMenu_BuildAlt(ms, 0);
    CHECK_EQ(hub.loopForm, kLoopFormOptionsMenuAlt);

    int upgObj = FindEntryObj(hub, BuildingMenuAction::kUpgrade);
    int expObj = FindEntryObj(hub, BuildingMenuAction::kExpandRoom);
    CHECK(upgObj != -1 && expObj != -1);
    CHECK(BuildingOptionsMenu_Dispatch(hub, 1, upgObj) == BuildingMenuAction::kUpgrade);
    CHECK(BuildingOptionsMenu_Dispatch(hub, 1, expObj) == BuildingMenuAction::kExpandRoom);

    // affordability gate on the upgrade confirm.
    UpgradeState us{};
    us.handle = ms.handle; us.curUpgradeLevel = 0; us.maxUpgradeLevel = 2;
    us.slotsWorth = 100; us.canAfford = false;
    UpgradeLayout ul = BuildingUpgradeDialog_BuildUpgrade(us);
    sink.reset();
    CHECK(BuildingUpgradeDialog_DispatchUpgrade(ul, us, kClickOK));  // dismissed
    CHECK(sink.kind == FlowSink::kNone);                            // no command
}
