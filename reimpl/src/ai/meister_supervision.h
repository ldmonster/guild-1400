#pragma once
// MeisterAi master-supervision pass — P6 sim coverage (gilde.exe).
//
// The master-AI (VIBE_MeisterAi_*) runs a once-per-day supervision sweep over a
// building's person roster, testing per-person conditions and emitting command
// packets (slot-reset / build-op / delta-state) when an action is warranted.
// This module reconstructs that decision layer.
//
// Like the sibling ai/meisterai3.cpp module, every routine here is a pure,
// deterministic transform over scalar inputs or fixed-stride record views; the
// cross-module *leaves* the originals reach through (the person query iterator,
// the GameObject_QueryFind resolver, the He handler-table scanner, the RNG, and
// the command-builder emitters) live in subsystems wired elsewhere, so they are
// injected through a SupervisionHooks struct with inert defaults defined in this
// module's .cpp. Tests install their own leaves.
//
// Functions translated (original address — symbol):
//   0x4c73b4  VIBE_MeisterAi_RequestCmd134           — queue a type-134 reset if
//                                                       no pending 134 handler.
//   0x4c7308  VIBE_MeisterAi_RequestBuildingCmd43    — per-employee type-43 queue
//                                                       guarded by an existing
//                                                       handler that already
//                                                       targets that person id.
//   0x4c7590  VIBE_MeisterAi_ClearDarkCorner         — for each tavern employee,
//                                                       if the dark-corner room's
//                                                       last-use day is >1 day old
//                                                       emit build-op83 ("clear
//                                                       dunkle ecke").
//   0x4c74c8  VIBE_MeisterAi_SuperviseStammtisch     — for each tavern employee,
//                                                       for every stammtisch seat
//                                                       whose occupant is gone /
//                                                       inactive / state 15, emit
//                                                       build-op84.
//   0x45df7c  VIBE_MeisterAi_FlagIdleStaff           — roll-gated pass that flags
//                                                       idle (no-action) staff of
//                                                       a building with bit 0x10.
//   0x45d3ec  VIBE_MeisterAi_CountStaffByType        — recursive walk of the staff
//                                                       tree splitting the head +
//                                                       descendants into "masters"
//                                                       (type 23/37) vs "others".
//   0x4c9104  VIBE_MeisterAi_UpdateBuildingHealthState — per-production employee,
//                                                       send a low/critical health
//                                                       quickjump message and a
//                                                       delta-state packet.
//
// Recovered double constants (gilde.exe, decoded byte-for-byte; VAs):
//   dbl_619970 = 168.0  — FlagIdleStaff per-staff "is idle" gauge threshold.
//   dbl_619968 = 252.0  — FlagIdleStaff "very fresh staff" reprieve threshold.
//   dbl_61E884 = 0.5    — UpdateBuildingHealthState critical health ratio gate.
#include "guild/common/types.h"

#include "ai/building_needs.h"  // BuildingTask enum + RunBuildingTasksOrder (0x4c930c)

namespace guild::ai {

// Inputs the RunBuildingTasks dispatcher needs to drive each sub-task pass. The
// originals pull these out of the live building/roster records; we keep them as
// plain views so the wiring is testable without the live loop.
struct SupervisionContext {
    int masterBuildId = 0;
    int currentDay = 0;
    const int* employeeIds = nullptr; int employeeCount = 0;       // for cmd43
    const int* darkCornerLastUse = nullptr; int darkCornerCount = 0;
    const struct StammtischSeat* stammtischSeats = nullptr; int stammtischCount = 0;
    struct StaffSlot* staffSlots = nullptr; int staffSlotCount = 0;
    struct BuildingFlag* buildingFlag = nullptr;
    const struct HealthEmp* healthEmps = nullptr; int healthEmpCount = 0;
};

// 0x4c930c — RunBuildingTasks: invoke every reconstructed sub-task pass in the
// canonical dispatch order (see building_needs.h RunBuildingTasksOrder). The
// AssignWorkersToBuilding case is the one still-deferred leaf (inert no-op here).
// Returns the total number of command packets emitted across all passes.
int RunBuildingTasks(const SupervisionContext& ctx);

// ---------------------------------------------------------------------------
// Leaf-injection struct (defaults are inert, defined in meister_supervision.cpp).
// ---------------------------------------------------------------------------
struct SupervisionHooks {
    // VIBE_He_FindFirstHandlerByFilter @0x4c63f8 — does a pending command of the
    // given type already exist for this owner? (Collapsed to a boolean probe.)
    bool (*handler_exists)(int filterKind, int type, int ownerId) = nullptr;
    // VIBE_Command_QueueRequestSlotReset28 @0x4948c8 — emit a slot-reset packet.
    // The original packs (type, masterBuildId, field=-1, kind=2, game-time tail).
    void (*queue_slot_reset28)(int slotId, u8 type, int field7, int field8) = nullptr;
    // VIBE_Command_RequestBuildOp83 @0x495954 — "clear dunkle ecke" build op.
    void (*request_build_op83)(int buildId) = nullptr;
    // VIBE_Command_RequestBuildOp84 @0x495980 — "stammtisch supervisor" build op.
    void (*request_build_op84)(int buildId, int seatPersonId) = nullptr;
    // VIBE_Math_RandomModulo @0x58b89c — uniform [0,n).
    u16 (*rng_mod)(u16 n) = nullptr;
    // VIBE_Character_ChangePlayerAction @0x4b09c8 — cancel/replace a worker action.
    void (*change_player_action)(int buildId, int personSlotWord) = nullptr;
    // VIBE_He_SendQuickjumpMessage @0x4c5d98 — collapsed to (msgId, ownerId).
    void (*send_quickjump)(int msgId, int ownerId) = nullptr;
    // VIBE_Command_QueueRequestState22 @0x494750 — emit the delta-state packet.
    void (*queue_state22)(int personId) = nullptr;
};

SupervisionHooks SetSupervisionHooks(const SupervisionHooks& hooks);
SupervisionHooks GetSupervisionHooks();

// ---------------------------------------------------------------------------
// 0x4c73b4 — VIBE_MeisterAi_RequestCmd134.
// If no pending type-134 handler exists for this master, queue a type-134
// slot-reset tagged with `masterBuildId`. Returns true iff a packet was emitted.
// ---------------------------------------------------------------------------
bool RequestCmd134(int masterBuildId);

// ---------------------------------------------------------------------------
// 0x4c7308 — VIBE_MeisterAi_RequestBuildingCmd43.
// Walk the building's type-7 employees. For each, if no pending type-43 handler
// already targets that employee's id, queue a type-43 slot-reset. Returns the
// number of packets emitted.
//   employeeIds  : the person ids returned by Person_QueryBegin(building,1,5,7).
// ---------------------------------------------------------------------------
int RequestBuildingCmd43(int masterBuildId, const int* employeeIds, int employeeCount);

// ---------------------------------------------------------------------------
// 0x4c7590 — VIBE_MeisterAi_ClearDarkCorner.
// For each employee, the original resolves the building's "dark corner" room and
// reads its last-use day (room+40 dword). If currentDay - lastUseDay > 1, emit a
// build-op83 for the building. Returns the number of packets emitted.
//   lastUseDays  : per-employee resolved dark-corner last-use day; INT_MIN means
//                  the room could not be resolved (no emission).
// ---------------------------------------------------------------------------
int ClearDarkCorner(int currentDay, const int* lastUseDays, int employeeCount);

// ---------------------------------------------------------------------------
// 0x4c74c8 — VIBE_MeisterAi_SuperviseStammtisch.
// For each employee's stammtisch (8 seats), a seat needs a supervisor packet if
// its occupant id != -1 AND the occupant record is gone, inactive (record+8==0)
// or in state 15 (record+2==15). Emits one build-op84 per such seat. Returns the
// number of packets emitted.
// ---------------------------------------------------------------------------
struct StammtischSeat {
    int  occupantId = -1;       // record+14 (word*7); -1 = empty seat (skipped)
    bool recordFound = true;    // Person_FindRecordById(occupantId) != null
    bool active = true;         // record+8 != 0
    u8   state = 0;             // record+2
};
int SuperviseStammtisch(const StammtischSeat* seats, int seatCount);

// ---------------------------------------------------------------------------
// 0x45df7c — VIBE_MeisterAi_FlagIdleStaff.
// Roll-gated idle-staff flagger over a building's staff slots. A slot is an
// "idle candidate" when it is live, owned by this building, employed, has no
// current action object, is not already flagged (bit 4), AND either of its two
// fatigue gauges is below 168.0. Reprieve rolls (RandomModulo(128)) then either
// abort (a candidate exists but the first roll < 32) or skip (no candidate but
// the slot at the scanned-count index has gauges below 252.0 and the second roll
// > 96). Otherwise every such
// candidate slot gets bit 0x10 set in its flags byte. Returns 1 if it flagged,
// else 0; -1 if the building was already processed (its bit 4 was set).
// NOTE: the no-candidate reprieve reads slots[scannedCount]; the original relies
// on its 768-entry staff array, so callers must size `slots` to slotCount+1.
// ---------------------------------------------------------------------------
struct StaffSlot {
    bool live = false;          // word_12CE910 != -1
    int  ownerBuildId = 0;      // dword_12CEA7C
    bool employed = false;      // byte_12CEA75
    bool hasActionObj = false;  // dword_12CEA8C != 0
    u8   flags = 0;             // dword_12CEAC4 (bit4 candidate set) — out
    u8   busyFlags = 0;         // dword_12CEAD8 (bit4 == already supervised)
    u8   gaugeA = 0;            // byte_12CE993
    u8   gaugeB = 0;            // byte_12CE992
};
struct BuildingFlag { u8 supervisedBit = 0; };  // byte at building+456
int FlagIdleStaff(BuildingFlag* building, int buildId, StaffSlot* slots, int slotCount,
                  const SupervisionHooks& hooks);

// ---------------------------------------------------------------------------
// 0x45d3ec — VIBE_MeisterAi_CountStaffByType.
// Recursively walks the master's staff tree. The head record and every
// descendant is classified by its trade byte (table[65*trade]): trade 23 or 37
// counts as a "master", everything else as an "other". Returns {masters, others}.
// ---------------------------------------------------------------------------
struct StaffNode {
    u8  trade = 0;          // table byte for *record (craftsman class)
    bool counted = false;   // record+84 bit1 already counted (head pre-check)
    int childTree = -1;     // index of a sub-tree to recurse into, or -1
};
struct StaffCount { int masters = 0; int others = 0; };
StaffCount CountStaffByType(const StaffNode* head, int headChildCount,
                            const StaffNode* nodes, int nodeCount,
                            const int* treeRoots, int treeRootCount);

// ---------------------------------------------------------------------------
// 0x4c9104 — VIBE_MeisterAi_UpdateBuildingHealthState.
// For each production-class (trade 6 or 7) employee with a positive max-health
// (record+105), if cur/max >= 0.5 send the "low health" message (5297/1418) else
// the "critical" message (5296/1418), then queue a delta-state packet. Skips
// employees with no building (record+39 == 0xFFFF) or the no-update bit
// (record+90 bit1). Returns the number of delta packets queued.
// ---------------------------------------------------------------------------
struct HealthEmp {
    int  personId = 0;
    bool inBuilding = true;   // record+39 != 0xFFFF
    bool noUpdate = false;    // record+90 bit1
    u8   trade = 0;           // byte_12CE912 (production class)
    int  curHealth = 0;       // record+107 (int16)
    int  maxHealth = 0;       // record+105 (int16)
    int  ownerId = 0;         // for the quickjump owner
};
int UpdateBuildingHealthState(const HealthEmp* emps, int empCount,
                              const SupervisionHooks& hooks);

} // namespace guild::ai
