#include "gui/building_upgrade_dialog.h"

namespace guild::gui {

namespace {

// gilde.exe dbl_624350 — the upgrade-cost scale (0.3) applied to the summed
// flagged-slot worth before the display-coordinate conversion.
constexpr double kUpgradeWorthScale = 0.3;

// gilde.exe dbl_624328 / dbl_624330 — the expand-room price scale: the market
// price is multiplied by (priceMode * 0.25 + 0.5).
constexpr double kExpandPriceModeScale = 0.25;
constexpr double kExpandPriceBase      = 0.5;

// The child-object id base mirrors building_dialog.cpp: GetChildObjectId / the
// widget index returned by Object_AddToWindow are stable nonzero handles; only the
// relative offsets matter for the wiring tests, so we synthesize a deterministic
// base for the expand-room slot objects.
constexpr int kExpandObjBase = 2000;

}  // namespace

// ===========================================================================
// Upgrade — DATA/LAYOUT.
// ===========================================================================
//
// gilde.exe 0x54b604 — VIBE_BuildingDialog_Upgrade.
//   if (type[+583] >= type[+584]) { msg 5098; box(0); return; }            // capped
//   cat = MapTypeToCategory(type);
//   if (!freeBuild) {
//     if (cat==1||cat==4||cat==8) { if (!CheckBuildRequirements) msg 5097; }
//     else                        { if (!CheckBuildRequirements) msg 5100; }
//   }
//   ... plot/name match (else msg 5099) ...
//   worth = SumFlaggedSlotsWorth(building+1);
//   cost  = ConvertToDisplayCoord(worth * 0.3);                             // 5096 prompt
//   if (box(17) && affordable) { Start("building_upgrade"); Cmd15; Reset28(30); End(); }
UpgradeLayout BuildingUpgradeDialog_BuildUpgrade(const UpgradeState& s) {
    UpgradeLayout l{};
    l.actionLabel = kActionBuildingUpgrade;
    l.reset28Kind = kReset28KindUpgrade;

    // (1) upgrade-level cap.
    if (s.curUpgradeLevel >= s.maxUpgradeLevel) {
        l.capped = true;
        l.promptText = kTextUpgradeCapped;  // messagebox 5098 -> box(0)
        return l;
    }

    // (2) build requirements (skipped when the global free-build flag is set).
    if (!s.freeBuild && !s.meetsRequirements) {
        l.requirementsFailed = true;
        const bool reqCategory =
            (s.category == 1 || s.category == 4 || s.category == 8);
        l.requirementText = reqCategory ? kTextUpgradeNeedReq158 : kTextUpgradeNeedReq;
        return l;
    }

    // (3) plot/name match.
    if (!s.plotMatches) {
        l.plotFailed = true;
        l.promptText = kTextUpgradeNeedPlot;  // 5099
        return l;
    }

    // (4) cost prompt. worth * 0.3 -> display coordinate (truncate, matching (int)v7).
    l.cost = static_cast<int>(static_cast<double>(s.slotsWorth) * kUpgradeWorthScale);
    l.promptText = kTextUpgradePrompt;  // 5096
    return l;
}

// ===========================================================================
// ExpandRoom — DATA/LAYOUT.
// ===========================================================================
//
// gilde.exe 0x54aeb0 — VIBE_BuildingDialog_ExpandRoom.
//   Scan up to 64 room-slot words at building+35 (2-byte stride). For each word w
//   != -1, mask off the high bit (w & 0x7FFF); if the room-type kind byte
//   (dword_13CE27C + 65*roomType) == 2 and roomType != 253, append a slot to the
//   12-byte (3-int) table:  v54[3*i+1]=roomType, v54[3*i+2]=priceObj, v54[3*i+3]=label.
//   Stop at 32 slots (widget-array index < 96). For each collected slot the
//   per-slot price = ComputeMarketPrice(roomType,100) * (priceMode*0.25 + 0.5);
//   the in-flight handler progress (elapsed/total game-minutes) is written to the
//   price-button widget +480.
ExpandRoomLayout BuildingUpgradeDialog_BuildExpandRoom(const RoomSlotInput* rooms,
                                                       int count, int priceMode) {
    ExpandRoomLayout l{};
    l.form = kFormExpandRoomPanel;
    l.titleText = kTextExpandTitleId;   // 5089 (window slot 1)
    l.footerText = kTextExpandFooter;   // 5090 (window slot 3)
    l.loopForm = kLoopFormPanel;        // RunFrameLoop(415687)

    const int scan = (count < kExpandRoomScanCount) ? count : kExpandRoomScanCount;
    const double priceFactor =
        static_cast<double>(priceMode) * kExpandPriceModeScale + kExpandPriceBase;

    int collected = 0;
    // The original also stops when the widget-array write index v8 reaches 96 (v8 += 3
    // per appended slot); 32 slots == index 96, so the slot cap is the binding limit.
    for (int i = 0; i < scan && collected < kExpandSlotMax; ++i) {
        const RoomSlotInput& r = rooms[i];
        if (r.roomType < 0)
            continue;
        const int roomType = r.roomType & 0x7FFF;  // mask off the high marker bit
        if (r.kind != kRoomSlotKindBuildable)
            continue;
        if (roomType == kRoomSlotBackId)
            continue;

        ExpandRoomSlot& slot = l.slots[collected];
        slot.roomTypeId = roomType;
        // Object_AddToWindow returns the price-button object; AddTextLabel the label.
        slot.priceObjId = kExpandObjBase + 2 * collected;
        slot.labelObjId = kExpandObjBase + 2 * collected + 1;
        slot.price = static_cast<int>(static_cast<double>(r.marketPrice) * priceFactor);
        slot.inProgress = r.inProgress;
        slot.occupied = r.occupied;
        if (r.inProgress && r.totalMinutes != 0) {
            // widget+480 = elapsed / total (bar pixel draw deferred to the renderer).
            slot.progress = static_cast<float>(r.elapsedMinutes) /
                            static_cast<float>(r.totalMinutes);
        } else {
            slot.progress = 0.0f;
        }
        ++collected;
    }

    l.slotCount = collected;
    l.hasSlider = (collected > 4);  // Hud_BuildSliderPanel when v79 > 4
    return l;
}

// ===========================================================================
// Wiring.
// ===========================================================================

// gilde.exe 0x54b604 — confirm path.
//   box(17) confirm + CheckResource* -> Start("building_upgrade"); Reset28(30); End().
bool BuildingUpgradeDialog_DispatchUpgrade(const UpgradeLayout& l, const UpgradeState& s,
                                           int clickedId) {
    // A capped / requirement-failed / plot-failed dialog only showed a messagebox.
    if (l.capped || l.requirementsFailed || l.plotFailed)
        return true;
    if (clickedId == kClickCancel)
        return true;
    if (clickedId != kClickOK)
        return false;
    if (s.canAfford)
        BuildingDialog_CommandSink()->Upgrade(s.handle, l.cost, l.actionLabel);
    return true;
}

// gilde.exe 0x54aeb0 — confirm path.
//   the clicked price-button selects its slot; if affordable enqueue room_upgrade.
bool BuildingUpgradeDialog_DispatchExpandRoom(const ExpandRoomLayout& l, int buildingHandle,
                                              int clickedId, int clickedObj, bool canAfford) {
    if (clickedId == kClickCancel)
        return true;
    for (int i = 0; i < l.slotCount; ++i) {
        if (l.slots[i].priceObjId != clickedObj)
            continue;
        // Affordable -> Start("room_upgrade"); Cmd15; Reset28(31); End().
        if (canAfford) {
            BuildingDialog_CommandSink()->ExpandRoom(buildingHandle, l.slots[i].roomTypeId,
                                                     l.slots[i].price, kActionRoomUpgradeLbl);
        }
        // else: ShowMessageBox(0); either way the click is consumed (loop continues).
        return canAfford;
    }
    return false;
}

}  // namespace guild::gui
