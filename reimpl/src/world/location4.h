#pragma once
// Location interaction FSMs, batch 4 — the per-NPC/per-building *dialog bodies*
// that location2.cpp's contact loops dispatch TO, for the dialogs NOT already
// reconstructed in location3.cpp / location_thief.cpp / location_church.cpp.
//
// Like location3, every one of these is the same GUI frame-loop shell
//   form = GameTick_Finalize(...); CenterChildWindows(form);
//   yes = GetChildObjectId(...); no = GetChildObjectId(...);
//   dword_75BF38 = -1;
//   do { ... if (clicked==yes) <gates + ASSEMBLE A COMMAND BATCH>;
//             else if (clicked==no) close; } while ( GameLogic_RunFrameLoop(...) );
//   Form_Destroy(form);
// wrapped around a small amount of DETERMINISTIC, RECOVERABLE logic. We reconstruct
// THAT logic 1:1 and route the GUI / command / handler-table / law / build-op
// callees through hooks (the location3 LocationDialogHooks for the shared GUI +
// command + selection/occupied collectors, plus LocationDialog4Hooks below for the
// extra per-dialog side-effects these four dialogs make).
//
// REUSE from location3.h (the live binary shares these exact primitives across
// every dialog body): SlotTableView, SelectionTable, DialogOutcome, FirstOccupiedSlot,
// SlotTableFull, CollectOccupiedIds, CollectSelectionIds, the GUI form shell.
//
// Functions modeled here (address -> what is reconstructed):
//   VIBE_Location_ThiefKidnapDialog      0x52554c  (action 60, occupied batch + law + buildop)
//   VIBE_Location_GuardCustomsDialog     0x5268a8  (action 101, selection batch cap 6)
//   VIBE_Location_GuardDetainDialog      0x526b34  (action 100, single + "more than 1" warn)
//   VIBE_Location_ThiefSpyBuildingDialog 0x524380  (action 64, occupied batch cap 8 + handler-dedup)
//   VIBE_Location_DungeonBribeDialog     0x523d1c  (action 57, single enqueue cmd15)
//   VIBE_Location_ThiefInformationDialog 0x52481c  (radio-select -> quad43 request)
//   VIBE_Location_ThiefTrainingDialog    0x5253c0  (drag-grid commit loop)
//   VIBE_Location_RobberCampStandard.head probe shared scanners reused as-is.
#include <array>
#include <cstdint>
#include <vector>

#include "guild/common/types.h"
#include "world/location3.h"   // SlotTableView, SelectionTable, DialogOutcome, scanners

namespace guild::world {

// ===========================================================================
// Recovered action-code bytes for THIS batch (the leading byte written into the
// command struct just before VIBE_Command_QueueRequestSlotReset28; recovered from
// each dialog's `vXX = <code>;` store).
// ===========================================================================
enum class DialogAction4 : std::uint8_t {
    DungeonBribe    = 57,   // 0x39  VIBE_Location_DungeonBribeDialog  0x523d1c (v22=57)
    ThiefKidnap     = 60,   // 0x3C  VIBE_Location_ThiefKidnapDialog   0x52554c (v19=60)
    ThiefSpyBuilding= 64,   // 0x40  VIBE_Location_ThiefSpyBuildingDialog 0x524380
    GuardDetain     = 100,  // 0x64  VIBE_Location_GuardDetainDialog   0x526b34 (v17=100)
    GuardCustoms    = 101,  // 0x65  VIBE_Location_GuardCustomsDialog  0x5268a8 (v24=101)
};

// Recovered text ids (the dword_8C* message-box arguments and the literal text
// codes passed to VIBE_Text_RenderFormattedMessage / VIBE_Dialog_ShowMessageBox).
constexpr int kMsgKidnapBlocked   = 5577;  // ThiefKidnap: target has flag +433 set
constexpr int kMsgDungeonNoHolder = 0;     // DungeonBribe: dword_8C8DC0 (no jailer record)
constexpr int kMsgDungeonExists   = 5573;  // DungeonBribe: a bribe handler already pending
constexpr int kMsgDetainMultiple  = 0;     // GuardDetain: dword_8C90B4 ">1 selected" (mode 4)

// ===========================================================================
// LocationDialog4Hooks — the side-effect callees that location3's
// LocationDialogHooks does NOT cover, used only by this batch. Inert defaults
// (defined in location4.cpp) make every dialog runnable headless. NEVER let a
// test define these symbols — they are referenced from src/.
// ===========================================================================
struct LocationDialog4Hooks {
    // ThiefKidnap 0x52554c — VIBE_Dialog_CheckSkillRequirement(guildState,3): the
    // dialog aborts (returns 0) when the player lacks the level-3 thief skill.
    bool (*checkSkillRequirement)(int level) = nullptr;     // default: true (allowed)

    // ThiefKidnap — VIBE_Command_RequestBuildOp90(targetId,-3) then
    // VIBE_Gesetz_EvaluateViolation(25,1,perp,targetId,-1): the law/build-op side
    // effects fired after the kidnap batch commits.
    void (*requestBuildOp)(std::int32_t targetId, int op)   = nullptr;
    void (*evaluateViolation)(int kind, int sev,
                              std::int32_t perp,
                              std::int32_t targetId,
                              int extra)                    = nullptr;

    // ThiefSpyBuilding 0x524380 — handler-dedup: countExistingHandlers() is the
    // VIBE_He_FindFirstHandlerByFilter/FindNextMatchingHandler scan that counts
    // already-pending spy requests; the dialog only opens when
    // existing + currentOccupied < 2*buildingCapacity (else msg 5641).
    int (*countExistingHandlers)() = nullptr;               // default: 0

    // DungeonBribe 0x523d1c — VIBE_Office_GetEntryByHolder(17,&out): true when a
    // jailer office holder exists; and a "bribe already pending" handler probe.
    bool (*dungeonHasJailer)()        = nullptr;            // default: false
    bool (*dungeonBribePending)()     = nullptr;            // default: false
    // VIBE_Command_EnqueueCmd15(...) -> the actual bribe command (return ignored).
    void (*enqueueBribe)(std::int32_t jailerId,
                         std::int32_t cityId, int amount)   = nullptr;

    // ThiefInformation 0x52481c — VIBE_Command_QueueRequestQuad43(...): submit the
    // selected building's info request. selectedBuilding() returns the radio-group
    // selection (or -1 when nothing selected; the confirm is then ignored).
    std::int32_t (*selectedBuilding)()                     = nullptr; // default: -1
    void (*queueInfoRequest)(std::int32_t perp,
                             std::int32_t buildingId)       = nullptr;

    // ThiefTraining 0x5253c0 — VIBE_DragSlot_CountUsed() (anything dropped) and
    // VIBE_Object_SetValueOrText for each placed item; we model the commit as a
    // count of items committed via commitTrainingItem().
    int  (*dragSlotCount)()                  = nullptr;     // default: 0
    void (*commitTrainingItem)(int slotIndex)= nullptr;
};

void                        SetLocationDialog4Hooks(const LocationDialog4Hooks* hooks);
const LocationDialog4Hooks& GetLocationDialog4Hooks();

// ===========================================================================
// Dialog runners. Each is the recovered control flow of the named function with
// the GUI/command/law callees routed through the hooks. The return value mirrors
// the original (the "committed" flag, e.g. v30/v35).
// ===========================================================================

// gilde.exe 0x52554c — VIBE_Location_ThiefKidnapDialog.
//   if(!target) return 0;
//   if(!CheckSkillRequirement(3)) return 0;                       (skill gate)
//   if(targetCaptive) return 0;                                   (NPC+91 already held by us)
//   if(target[+433]) -> msg 5577, return 0;                       (blocked flag)
//   form loop; on confirm: collect ALL occupied ids (action 60); if any ->
//     QueueRequestSlotReset28 + RequestBuildOp90(firstId,-3)
//     + EvaluateViolation(25,1,perp,firstId,-1) + favor voice + ClearAll; v30=1;
//   else msg(dword_8C8E78). returns v30.
// `targetCaptive` is the recovered `*((_DWORD*)v2+91) && NPC+39==target` predicate;
// `blockedFlag` is `*((_BYTE*)v2+433)`. firstOccupiedId is dword_12CE914[city slot].
DialogOutcome ThiefKidnapDialog(bool hasTarget, bool targetCaptive, bool blockedFlag,
                                std::int32_t perpId, std::int32_t cityFirstId,
                                const SlotTableView& table);

// gilde.exe 0x5268a8 — VIBE_Location_GuardCustomsDialog.
//   if(!a1) return 0;
//   if FirstOccupiedSlot>=768 -> msg(dword_8C905C), return 0;     (capacity gate)
//   form loop; on confirm: collect active selection cap 6 (action 101); if any ->
//     QueueRequestSlotReset28 + favor voice. returns v35 (always 0; no commit flag).
DialogOutcome GuardCustomsDialog(bool hasTarget, const SlotTableView& table,
                                 const SelectionTable& sel);

// gilde.exe 0x526b34 — VIBE_Location_GuardDetainDialog.
//   if(!a1) return 0;
//   if FirstOccupiedSlot>=768 -> msg(dword_8C905C), return 0;
//   if(a1[+91] & 8) -> msg(dword_8CA974), return 0;               (already-detained flag)
//   form loop; on confirm: take FIRST active selection (action 100); if found ->
//     QueueRequestSlotReset28 + favor voice; then re-count ALL active and if >1 ->
//     msg(dword_8C90B4, mode 4). returns v30 (0).
DialogOutcome GuardDetainDialog(bool hasTarget, bool alreadyDetainedFlag,
                                const SlotTableView& table,
                                const SelectionTable& sel);

// gilde.exe 0x524380 — VIBE_Location_ThiefSpyBuildingDialog.
//   if(!target || target+39==0xFFFF) return 0;
//   if(!IsAnimalTargetBusy) -> msg 5791, return 0;                (busy gate)
//   existing = countExistingHandlers(); occupied = #occupied-for-target;
//   if(existing + occupied >= 2*buildingCapacity) -> msg 5641, return 0;
//   form loop; on confirm: collect ALL occupied ids cap 8 (action 64); if any ->
//     QueueRequestSlotReset28 + favor voice; v39=count. returns v39.
DialogOutcome ThiefSpyBuildingDialog(bool hasTarget, bool targetBusy,
                                     int occupiedForTarget, int buildingCapacity,
                                     const SlotTableView& table);

// gilde.exe 0x523d1c — VIBE_Location_DungeonBribeDialog.
//   if(!Office_GetEntryByHolder(17)) return;                      (no jail office)
//   if(!Person_FindRecordById(jailer)) -> msg(dword_8C8DC0), return;
//   if(bribe handler already pending) -> msg 5573, return;
//   form loop; on confirm (yes button): EnqueueCmd15(jailer,city,amount) +
//     QueueRequestSlotReset28. committed=true once.
// `bribeAmount` is the recovered Money_MultiplyByRate(GetDataPtr,rate) value.
DialogOutcome DungeonBribeDialog(bool hasJailer, bool jailerRecordExists,
                                 bool bribePending,
                                 std::int32_t jailerId, std::int32_t cityId,
                                 int bribeAmount);

// gilde.exe 0x52481c — VIBE_Location_ThiefInformationDialog.
//   builds a radio list of nearby buildings; on confirm (yes && a selection):
//     QueueRequestQuad43(perp,-1,group,selectedBuildingId); spins until packet
//     status set. We model the single confirmed submit: returns committed=true
//     iff a building was selected when confirmed.
DialogOutcome ThiefInformationDialog(std::int32_t perpId);

// gilde.exe 0x5253c0 — VIBE_Location_ThiefTrainingDialog.
//   if(CheckActiveCharFlag()) return;                             (suppressed)
//   drag-grid loop; on each "commit" (dword_672230 with items, or close id 258)
//   commits every used grid slot via Object_SetValueOrText + resets the table.
// We model it as: opened iff !activeCharFlag; committedCount = items committed
// across the single confirm. Returns committed=true when any item was placed.
DialogOutcome ThiefTrainingDialog(bool activeCharFlag);

} // namespace guild::world
