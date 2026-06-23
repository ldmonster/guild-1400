#pragma once
// Location interaction FSMs, batch 3 — the per-NPC/per-building *dialog bodies*
// the contact loops dispatch TO (gilde.exe VIBE_Location_*). location2.cpp models
// the contact menus (what items are offered and which dialog a click opens); this
// file models the dialog bodies themselves: thief-guild burglary/pickpocket,
// robber-camp standard/raid, the guard arrest/raid/customs/detain panels, the
// spy-building, kidnap, pickpocket-select and training dialogs.
//
// Every one of these dialogs is the same GUI frame-loop shell
//   form = GameTick_Finalize(...); CenterChildWindows(form);
//   yes = GetChildObjectId(...); no = GetChildObjectId(...);
//   dword_75BF38 = -1;
//   do { ... if (clicked == yes) <ASSEMBLE A COMMAND BATCH>; else if (clicked==no) close; }
//   while ( GameLogic_RunFrameLoop(...) );
//   Form_Destroy(form);
// wrapped around a small amount of DETERMINISTIC, RECOVERABLE logic — the part
// that decides whether the action is allowed and what command batch is queued.
// THAT logic is what we reconstruct 1:1 and test with golden vectors; the GUI /
// command / handler-table callees are routed through LocationDialogHooks.
//
// Functions modeled here (address -> what is reconstructed):
//   VIBE_Location_TavernRules                0x516014  (LocationFormShell info dialog)
//   VIBE_Location_ThievesGuildBurglary       0x512110  (action-code 72, player-selection batch)
//   VIBE_Location_ThievesGuildPickpocket     0x512374  (action-code 73, handler-match + sel batch)
//   VIBE_Location_RobberCampStandard         0x5126cc  (action-code 98, occupied-slots batch)
//   VIBE_Location_RobberCampRaid             0x512eb0  (action-code 117,occupied-slots batch)
//   VIBE_Location_ThiefBurglaryDialog        0x524074  (action-code 63, occupied-slots batch)
//   VIBE_Location_ThiefPickpocketDialog      0x524b98  (action-code 73, handler-match + sel batch)
//   VIBE_Location_ThiefSpyBuildingDialog     0x524380  (action-code 64, occupied-slots batch, cap 8)
//   VIBE_Location_ThiefKidnapDialog          0x52554c  (action-code 60, occupied-slots batch)
//   VIBE_Location_ThiefPickpocketSelectDialog0x524e8c  (action-code 97, player-selection batch, cap6)
//   VIBE_Location_GuardArrestDialog          0x526344  (action-code 67, occupied-slots batch, cap 4)
//   VIBE_Location_GuardRaidDialog            0x5265c8  (action-code 68, occupied-slots batch, cap 6)
//   VIBE_Location_GuardCustomsDialog         0x5268a8  (action-code 101,player-selection batch, cap6)
//   VIBE_Location_GuardDetainDialog          0x526b34  (action-code 100,player-selection batch)
//   VIBE_Location_ThiefTrainingDialog        0x5253c0  (drag-grid commit loop)
#include <array>
#include <cstdint>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered byte-constants: the per-dialog "action code" written as the leading
// byte of the command batch struct (v17/v18/... .actionCode). These are the
// command opcodes the server-side request carries; recovered from each dialog's
// `vXX = <code>;` store just before VIBE_Command_QueueRequestSlotReset28.
// ===========================================================================
enum class DialogAction : std::uint8_t {
    ThiefKidnap        = 60,  // 0x3C  VIBE_Location_ThiefKidnapDialog        0x52554c
    ThiefBurglary      = 63,  // 0x3F  VIBE_Location_ThiefBurglaryDialog       0x524074
    ThiefSpyBuilding   = 64,  // 0x40  VIBE_Location_ThiefSpyBuildingDialog    0x524380
    GuardArrest        = 67,  // 0x43  VIBE_Location_GuardArrestDialog         0x526344
    GuardRaid          = 68,  // 0x44  VIBE_Location_GuardRaidDialog           0x5265c8
    ThievesGuildBurg   = 72,  // 0x48  VIBE_Location_ThievesGuildBurglary      0x512110
    Pickpocket         = 73,  // 0x49  VIBE_Location_ThievesGuildPickpocket    0x512374
    PickpocketSelect   = 97,  // 0x61  VIBE_Location_ThiefPickpocketSelect     0x524e8c
    RobberCampStandard = 98,  // 0x62  VIBE_Location_RobberCampStandard        0x5126cc
    GuardDetain        = 100, // 0x64  VIBE_Location_GuardDetainDialog         0x526b34
    GuardCustoms       = 101, // 0x65  VIBE_Location_GuardCustomsDialog        0x5268a8
    RobberCampRaid     = 117, // 0x75  VIBE_Location_RobberCampRaid            0x512eb0
};

// ===========================================================================
// World tables these dialogs read. We model the two that matter for the
// recovered logic as injectable views so the pure builders stay testable:
//   byte_12CEA98 — the building/occupancy table, stride 536 bytes (= 134 dwords).
//     byte_12CEA98[i*4] (i.e. the first byte of dword #134*slot) is the "slot
//     occupied" flag the dialogs scan.  Capacity is 768 slots (>=768 => "full").
//   dword_12CE914 — the parallel id table, dword_12CE914[134*slot] is the object
//     id stored for that slot (what the batch collects).
// ===========================================================================
constexpr int kSlotStrideDwords = 134;     // 536 bytes / 4
constexpr int kMaxSlots         = 768;     // capacity gate (>=768 => full)
constexpr int kSlotScanDwords   = 102912;  // 768 * 134, the loop bound

// A view over the occupancy/id tables for one player/city. `occupied(i)` is the
// byte_12CEA98[134*i] flag; `id(i)` is dword_12CE914[134*i].
struct SlotTableView {
    std::vector<std::uint8_t> occupied;  // per-slot occupied flag
    std::vector<std::int32_t> id;        // per-slot object id
};

// gilde.exe (RobberCampStandard 0x5126cc head, GuardArrest 0x526344 head, ...):
//   v3=0; v4=0; if(!byte_12CEA98[0]) do { v4+=536; ++v3; } while(v4<411648 && !byte_12CEA98[v4]);
// Returns the index of the FIRST occupied slot (or kMaxSlots if none in range).
// This is the leading "do we have any building / are we at capacity" probe; the
// dialogs treat result >= kMaxSlots as "full" and abort with a message box.
int FirstOccupiedSlot(const SlotTableView& t);

// `true` when FirstOccupiedSlot(t) >= kMaxSlots — the capacity-full gate.
bool SlotTableFull(const SlotTableView& t);

// gilde.exe occupied-slots batch collector (e.g. RobberCampRaid 0x512eb0,
// ThiefBurglary 0x524074, GuardArrest 0x526344, GuardRaid 0x5265c8, Spy 0x524380,
// Kidnap 0x52554c):
//   v15=0; for(j=0;j!=102912;j+=134) if(byte_12CEA98[j*4]){ v15+=4; collect dword_12CE914[j]; }
// gathers the object id of every occupied slot, in slot order, into `out`,
// stopping after `maxCount` ids (GuardArrest caps 4, GuardRaid/Spy cap 6/8...).
// Returns the number collected (== v13/v27, the count passed to the command).
int CollectOccupiedIds(const SlotTableView& t, int maxCount,
                       std::vector<std::int32_t>& out);

// gilde.exe GuardRaid 0x526770 — the SEPARATE uncapped re-count used only for the
// over-capacity warning (for(j=0;j!=411648;j+=536) if(byte_12CEA98[j]) ++v18).
int CountOccupiedSlots(const SlotTableView& t);

// ===========================================================================
// The player-selection table dword_11BB6A0[0..32): up to 32 currently-selected
// characters. An entry is a character record pointer (0 == empty); a character
// "participates" when its +392 byte flag is set; the value collected is its id
// (the dword at +4, a.k.a. *((_DWORD*)rec+1)).  We model it as plain structs so
// the collectors are pure and testable.
// ===========================================================================
constexpr int kSelectionSlots = 32;   // dword_11BB6A0 has 32 entries

struct SelectionEntry {
    bool        present = false;   // record pointer non-null
    bool        active  = false;   // record[+392] != 0
    std::int32_t id     = 0;       // record[+4] (the collected id)
};
using SelectionTable = std::array<SelectionEntry, kSelectionSlots>;

// gilde.exe player-selection batch collector (ThievesGuildBurglary 0x512110,
// Pickpocket 0x512374/0x524b98, Customs 0x5268a8, PickpocketSelect 0x524e8c):
//   for(v11=0; v11<32 && v12<cap*4; ++v11) if(rec=dword_11BB6A0[v11]; rec && rec[392]){
//       v12+=4; collect rec[+4]; ++count; }
// Collects the ids of active selected characters, in slot order, up to `maxCount`
// ids (the original caps via `v12 < N` where N is 24 => 6 ids, or 32 => 8 ids).
// Returns the count (== v7/v10/v18; the value the dialog tests before queuing).
int CollectSelectionIds(const SelectionTable& sel, int maxCount,
                        std::vector<std::int32_t>& out);

// `true` if any selection entry is present && active (the cheap "is anyone
// selected" predicate the dialogs use, e.g. RobberCampStandard's break-on-first).
bool AnySelectionActive(const SelectionTable& sel);

// gilde.exe over-capacity warning gate (Pickpocket 0x512374 / GuardRaid 0x5265c8):
// after queuing, the dialog re-counts and, if more than 6 are active/occupied,
// shows the "too many" message (text 5630). Returns true when count > 6.
bool TooManyParticipants(int count);

// ===========================================================================
// LocationDialogHooks — the unreconstructed GUI / command / handler-table
// callees these dialogs invoke. Inert defaults (defined in location3.cpp) make
// every dialog runnable headless in a test (no GUI, no real command queue);
// a test installs its own hooks to observe what the dialog does. NEVER let a
// test define these symbols — they are referenced from src/.
// ===========================================================================
struct LocationDialogHooks {
    // --- GUI form shell (VIBE_Form_* / VIBE_GameTick_Finalize) ---------------
    // openForm(resourceKey) -> opaque form handle (0 default). The do/while
    // frame loop is driven by frameStep(): it returns 0 to end the loop. A test
    // scripts a sequence of (clickedObjectId) frames via frameStep.
    std::int32_t (*openForm)(const char* resourceKey) = nullptr;
    void         (*destroyForm)(std::int32_t form)    = nullptr;
    // frameStep(form) -> the object id clicked this frame, or -1 for "none", or
    // INT32_MIN to end the loop. Default: returns INT32_MIN immediately (the loop
    // body never runs; the dialog opens and closes). This stands in for
    // VIBE_GameLogic_RunFrameLoop + the dword_62D22C clicked-object read.
    std::int32_t (*frameStep)(std::int32_t form) = nullptr;

    // --- handler table (VIBE_He_FindFirstHandlerByFilter / *NextMatching) -----
    // findExistingRequest(actionCode, targetId) -> true if a matching pending
    // handler already exists (Pickpocket/ThievesGuildBurglary branch on this:
    // when found, they re-issue ChangePlayerAction instead of queuing a batch).
    bool (*findExistingRequest)(int actionCode, std::int32_t targetId) = nullptr;

    // --- command queue (VIBE_Command_QueueRequestSlotReset28 + friends) -------
    // queueBatch(actionCode, count, ids, idCount) — submit the assembled batch.
    void (*queueBatch)(int actionCode, int count,
                       const std::int32_t* ids, int idCount) = nullptr;
    // changePlayerAction(targetId, charId) — VIBE_Character_ChangePlayerAction.
    void (*changePlayerAction)(std::int32_t targetId, std::int32_t charId) = nullptr;

    // --- feedback (VIBE_Dialog_ShowMessageBox / VIBE_Voice_PlayCraftFavorComment)
    void (*showMessage)(int textId) = nullptr;  // the "full"/"too many"/error popups
    void (*playFavorVoice)()        = nullptr;  // ack voice line after a batch

    // --- misc gates ----------------------------------------------------------
    // activeCharFlag() -> VIBE_Dialog_CheckActiveCharFlag(); when true the dialog
    // (PickpocketSelect/Training) is suppressed entirely.
    bool (*activeCharFlag)() = nullptr;
    // targetBusy(target) -> VIBE_CharAction_IsAnimalTargetBusy; when false the
    // dialog aborts with the "target is busy elsewhere" message (text 5791).
    bool (*targetBusy)(std::int32_t target) = nullptr;
};

void                       SetLocationDialogHooks(const LocationDialogHooks* hooks);
const LocationDialogHooks& GetLocationDialogHooks();

// ===========================================================================
// Dialog runners. Each is the recovered control flow of the named function with
// the GUI/command callees routed through the hooks. The return value mirrors the
// original (the "an action was committed" flag, e.g. v30/v33/v35/v39).
// ===========================================================================

// Common result of a "collect occupied slots, queue if any" dialog.
struct DialogOutcome {
    bool opened    = false;  // did the form open (gates passed)?
    bool committed = false;  // was a command batch actually queued?
    int  count     = 0;      // ids collected for the batch
    int  action    = 0;      // DialogAction byte used
};

// gilde.exe 0x5126cc — VIBE_Location_RobberCampStandard. Gate order matches the
// binary: if(!a1) return; existing-handler(code 98) -> msg 5790, return;
// FirstOccupiedSlot>=768 -> msg(dword_8C90C8), return; else IsAnimalTargetBusy
// -> open form (else msg 5782, return). On confirm: break on first active
// selection, queue a single-id action 98.
// `requestExists` == VIBE_He_FindFirstHandlerByFilter(1,0,98) match (0x5126ff).
// `targetBusy`    == VIBE_CharAction_IsAnimalTargetBusy(target) (0x512748).
DialogOutcome RobberCampStandard(std::int32_t target, bool hasTarget,
                                 bool requestExists, bool targetBusy,
                                 const SlotTableView& table,
                                 const SelectionTable& sel);

// gilde.exe 0x512eb0 — VIBE_Location_RobberCampRaid. Gate: a2 && NPC city != -1
// && IsAnimalTargetBusy (else msg 5791); then collects ALL occupied slot ids and
// queues action 117 when any exist; else shows the "no camp" message.
// `hasTarget`  == (a2 && *(WORD*)(a2+39) != 0xFFFF) (0x512ec3).
// `targetBusy` == VIBE_CharAction_IsAnimalTargetBusy(a2) (0x512ed7).
DialogOutcome RobberCampRaid(bool hasTarget, bool targetBusy,
                             const SlotTableView& table);

// gilde.exe 0x524074 — VIBE_Location_ThiefBurglaryDialog. Occupied-slots batch,
// action 63; gated by target-busy and a building security threshold.
DialogOutcome ThiefBurglaryDialog(bool targetBusy, bool securityOk,
                                  const SlotTableView& table);

// gilde.exe 0x526344 — VIBE_Location_GuardArrestDialog. Occupied-slots batch
// capped at 4, action 67; aborts when the table is full.
DialogOutcome GuardArrestDialog(const SlotTableView& table);

// gilde.exe 0x5265c8 — VIBE_Location_GuardRaidDialog. Occupied-slots batch capped
// at 6, action 68; after queuing, warns when more than 6 buildings exist.
DialogOutcome GuardRaidDialog(const SlotTableView& table);

// gilde.exe 0x512110 / 0x512374 / 0x524b98 — the selection-batch thief actions.
// When a matching request already exists the dialog re-issues per-character
// actions (committed=false, the `i` branch); otherwise it collects the active
// selection and queues `action`. `maxIds` is the per-dialog cap (8 for burglary,
// 6 for pickpocket). Returns committed=true when a batch was queued.
DialogOutcome SelectionBatchDialog(DialogAction action, int maxIds,
                                   bool requestExists,
                                   const SelectionTable& sel);

} // namespace guild::world
