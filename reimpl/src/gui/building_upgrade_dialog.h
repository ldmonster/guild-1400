#pragma once
// guild::gui — the building UPGRADE and EXPAND-ROOM dialogs.
//
// Two of the deferred "big BuildingDialog builders":
//
//   VIBE_BuildingDialog_Upgrade    @gilde.exe 0x54b604 (0x64c bytes)
//     A confirm dialog that upgrades a building one level. It gates on:
//       1. the upgrade-level cap — the building-type-table bytes at type+583
//          (current level) vs type+584 (max level): if cur >= max it shows
//          messagebox 5098 and returns;
//       2. (when the global "free build" flag dword_63C7B8 is clear) the build
//          requirements (VIBE_Building_CheckBuildRequirements) — failing shows
//          5097 (category 1/4/8) or 5100 (other categories);
//       3. a name/plot match (5099) — the groundplan bauplatz must match the
//          building type name;
//       4. affordability — the upgrade cost is the building's summed flagged-slot
//          worth scaled by 0.3 (dbl_624350), converted to a display coordinate;
//          the resource/money check (CheckResourceAmount / CheckResourceByItem)
//          gates the actual command.
//     On confirm it brackets the mutation with the command channel:
//       EnqueueBuildingActionStart("building_upgrade"); EnqueueCmd15(...);
//       QueueRequestSlotReset28(kind=30); EnqueueBuildingActionEnd().
//
//   VIBE_BuildingDialog_ExpandRoom @gilde.exe 0x54aeb0 (0x751 bytes)
//     A panel listing every empty buildable room slot of a building as a clickable
//     price button + a text label + a handler-progress bar:
//       * form = "Panel\Gebaeude_Raum_Erweitern";
//       * it scans the building's room-slot word array (building+35, up to 64
//         entries, 2-byte stride) for slots whose room-type byte
//         (dword_13CE27C + 65*roomType) == 2 and != 253 (the courtyard/back slot),
//         collecting up to 32 of them into a 12-byte-stride slot table
//         (v54[3*i+0]=roomTypeId word, [3*i+1]=price-button object id,
//          [3*i+2]=text-label object id);
//       * per slot the price = ComputeMarketPrice(roomType, 100) *
//         (priceMode*0.25 + 0.5)   (dbl_624328=0.25, dbl_624330=0.5), shown via
//         text 5094, button-widget fields {+72=1,+444=3,+464=1221,+468=-2,+472=-2};
//       * a handler-progress fraction per in-flight room-upgrade handler is the
//         elapsed/total game-minutes ratio, written to the price-button widget +480
//         (the bar PIXEL draw is the renderer's job — we compute the fraction);
//       * a slider panel (Hud_BuildSliderPanel) is added when > 4 slots;
//       * on a slot click + affordability it brackets the mutation with
//         EnqueueBuildingActionStart("room_upgrade"); EnqueueCmd15(...);
//         QueueRequestSlotReset28(kind=31); EnqueueBuildingActionEnd().
//
// As with building_dialog.{h,cpp}, the modal frame loop (RunFrameLoop 415687), the
// text/glyph engine, coordinate transforms and command codec live in other clusters;
// they are forward-declared and routed through a mockable command sink, so the DATA /
// LAYOUT / WIRING is testable in isolation. The well-known click ids are the GUI-wide
// constants 1210 (OK) and 1155 (Cancel / right button) — see building_dialog.h.

#include "gui/building_dialog.h"  // BuildingState, kClickOK/Cancel, kLoopFormPanel

namespace guild::gui {

// ---------------------------------------------------------------------------
// Recovered .form resource names (the VIBE_GameTick_Finalize string argument).
// ---------------------------------------------------------------------------
inline constexpr const char* kFormExpandRoomPanel = "Panel\\Gebaeude_Raum_Erweitern";

// Recovered command-action label strings (EnqueueBuildingActionStart arg).
inline constexpr const char* kActionBuildingUpgrade = "building_upgrade"; // Upgrade
inline constexpr const char* kActionRoomUpgradeLbl  = "room_upgrade";     // ExpandRoom

// Recovered Reset28 "kind" bytes (the v25/v34/v43/v57 byte fed to QueueRequestSlotReset28).
inline constexpr int kReset28KindUpgrade   = 30; // Upgrade
inline constexpr int kReset28KindRoomExpand = 31; // ExpandRoom

// Recovered RenderRichString / RenderFormattedMessage text ids.
inline constexpr int kTextUpgradeCapped     = 5098; // Upgrade: level==max -> messagebox 0
inline constexpr int kTextUpgradeNeedReq158 = 5097; // Upgrade: category 1/4/8 req fail
inline constexpr int kTextUpgradeNeedReq    = 5100; // Upgrade: other-category req fail
inline constexpr int kTextUpgradeNeedPlot   = 5099; // Upgrade: plot/name mismatch
inline constexpr int kTextUpgradePrompt     = 5096; // Upgrade: cost/confirm prompt
inline constexpr int kTextUpgradeDone       = 5101; // Upgrade: post-confirm notice
inline constexpr int kTextExpandTitleId     = 5089; // ExpandRoom title (window slot 1)
inline constexpr int kTextExpandSlotLabel   = 5094; // ExpandRoom per-slot price label
inline constexpr int kTextExpandFooter      = 5090; // ExpandRoom footer (window slot 3)

// Recovered ShowMessageBox kinds used by the Upgrade dialog.
inline constexpr int kMsgBoxUpgradeConfirm  = 17; // confirm-with-cost messagebox

// ExpandRoom per-slot table — recovered 12-byte (3-int) stride and capacity.
inline constexpr int kExpandSlotStride = 12;  // bytes per slot (3 ints): v54[3*i..]
inline constexpr int kExpandSlotMax    = 32;  // <32 collected; widget array indices < 96
inline constexpr int kExpandRoomScanCount = 64; // building+35 words scanned (stride 2)

// ExpandRoom price-button widget field writes (offsets into the 740-byte widget record).
inline constexpr int kExpandBtnField72   = 1;    // widget+72  = 1   (clickable button)
inline constexpr int kExpandBtnField444  = 3;    // widget+444 = 3
inline constexpr int kExpandBtnField464  = 1221; // widget+464 = 1221 (sprite/style id)
inline constexpr int kExpandBtnField468  = -2;   // widget+468 = -2
inline constexpr int kExpandBtnField472  = -2;   // widget+472 = -2

// The room-type "back/courtyard" slot id that is excluded from the expand grid.
inline constexpr int kRoomSlotBackId = 253;

// The room-slot kind byte value (dword_13CE27C + 65*roomType) that marks a buildable
// expandable room slot.
inline constexpr int kRoomSlotKindBuildable = 2;
inline constexpr int kRoomTypeTableStride   = 65;

// ---------------------------------------------------------------------------
// Synthetic building state for the upgrade/expand dialogs.
// ---------------------------------------------------------------------------

// Upgrade gate inputs (recovered from VIBE_BuildingDialog_Upgrade).
struct UpgradeState {
    int handle = 0;             // building+1 entity id
    int curUpgradeLevel = 0;    // type table +583 (current level)
    int maxUpgradeLevel = 1;    // type table +584 (max level)  -> capped when cur>=max
    u8  category = 0;           // VIBE_Building_MapTypeToCategory(type)
    bool freeBuild = false;     // global dword_63C7B8 (skip requirement checks)
    bool meetsRequirements = true; // VIBE_Building_CheckBuildRequirements == 1
    bool plotMatches = true;    // groundplan bauplatz name match (else messagebox 5099)
    int  slotsWorth = 0;        // VIBE_Building_SumFlaggedSlotsWorth(building+1)
    int  priceMode = 2;         // global dword_63C744 (display price mode)
    bool canAfford = true;      // CheckResourceAmount / CheckResourceByItem result
};

// One expand-room slot — the recovered 12-byte slot-table row, plus the derived data.
struct ExpandRoomSlot {
    int roomTypeId = -1;    // v54[3*i+1] (word)  the room/object type id
    int priceObjId = -1;    // v54[3*i+2]         the clickable price-button object id
    int labelObjId = -1;    // v54[3*i+3]         the text-label object id
    int price = 0;          // ComputeMarketPrice(roomTypeId,100)*(mode*0.25+0.5)
    float progress = 0.0f;  // handler progress fraction -> widget+480 (bar pixel deferred)
    bool inProgress = false;// an in-flight room-upgrade handler targets this slot
    bool occupied = false;  // GameObject_QueryFind found an object already in this slot
};

// A room input slot as seen in the building's +35 word array.
struct RoomSlotInput {
    int roomType = -1;     // the +35 word (the low 15 bits index the room-type table)
    int kind = 0;          // dword_13CE27C + 65*roomType (2 => buildable)
    int marketPrice = 0;   // VIBE_Building_ComputeMarketPrice(roomType, 100) base
    bool inProgress = false;// a room-upgrade handler currently targets this room type
    bool occupied = false; // an object already sits in this slot
    int  elapsedMinutes = 0; // GameTime_DiffMinutes(handler.start, now)
    int  totalMinutes = 0;   // GameTime_DiffMinutes(handler.start, handler.end)
};

struct ExpandRoomLayout {
    const char* form = nullptr;
    int titleText = 0;       // 5089 (slot 1)
    int footerText = 0;      // 5090 (slot 3)
    int slotCount = 0;       // number of slots collected (<= kExpandSlotMax)
    bool hasSlider = false;  // slot count > 4 => Hud_BuildSliderPanel
    int loopForm = 0;        // RunFrameLoop selector
    ExpandRoomSlot slots[kExpandSlotMax];
};

struct UpgradeLayout {
    bool capped = false;          // cur>=max -> messagebox 5098, dialog not shown
    bool requirementsFailed = false; // CheckBuildRequirements failed (5097/5100)
    int  requirementText = 0;     // 5097 or 5100 (which message)
    bool plotFailed = false;      // plot/name mismatch -> 5099
    int  cost = 0;                // displayed upgrade cost
    int  promptText = 0;          // 5096
    const char* actionLabel = nullptr; // "building_upgrade"
    int  reset28Kind = 0;         // 30
};

// ---------------------------------------------------------------------------
// Layout builders (DATA/LAYOUT).
// ---------------------------------------------------------------------------

// gilde.exe 0x54b604 — VIBE_BuildingDialog_Upgrade.
// Computes the gate decisions + cost for a synthetic state, without entering the loop.
//   cost = round(slotsWorth * 0.3) via ConvertToDisplayCoord  (modeled as integer here).
UpgradeLayout BuildingUpgradeDialog_BuildUpgrade(const UpgradeState& s);

// gilde.exe 0x54aeb0 — VIBE_BuildingDialog_ExpandRoom.
// Scans `rooms[count]` (the building's +35 word array view) for buildable empty slots
// and builds the 12-byte-stride slot table with per-slot price + handler-progress data.
ExpandRoomLayout BuildingUpgradeDialog_BuildExpandRoom(const RoomSlotInput* rooms,
                                                       int count, int priceMode);

// ---------------------------------------------------------------------------
// Wiring (WIRING) — clicked id/object -> action through the BuildingCommandSink.
// `clickedId` is the live dword_75BF38 (1210/1155); `clickedObj` is dword_62D22C.
// Returns true when the click confirmed/dispatched (the loop should end).
// ---------------------------------------------------------------------------

// Upgrade: confirm (1210) enqueues the building_upgrade bracket iff affordable; the
// returned bool also reflects "dialog dismissed".
bool BuildingUpgradeDialog_DispatchUpgrade(const UpgradeLayout& l, const UpgradeState& s,
                                           int clickedId);

// ExpandRoom: a price-button click picks the slot whose priceObjId == clickedObj and,
// if affordable, enqueues the room_upgrade bracket for that slot.
bool BuildingUpgradeDialog_DispatchExpandRoom(const ExpandRoomLayout& l, int buildingHandle,
                                              int clickedId, int clickedObj, bool canAfford);

}  // namespace guild::gui
