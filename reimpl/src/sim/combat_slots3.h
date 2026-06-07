#pragma once
// gilde.exe — Combat batch-3 LEAVES: object-def slot table lookup, active-target
// validity gates, selection / damage-number / order-slot window-table teardown,
// player-unit gathering, team-order slot issuing, and the two battle frame loops
// (namespace guild::sim). MODULE: combat (prefix VIBE_Combat_*).
//
// SCOPE. The deterministic table-walking / slot-bookkeeping the battle-loop and
// order modules left untranslated, extracted as testable rules over caller-
// provided table views and a hooks struct (inert defaults). The originals address
// static globals and issue cross-module GameObject/Window/Widget/Command/GameLogic
// calls; this module exposes the SAME control flow + arithmetic with those side
// effects routed through CombatSlots3Hooks so the logic stays testable in
// isolation (no OS / render / animation / network coupling).
//
//   * FindObjectDef (0x485a54)        — object-def ring lookup w/ free-slot fallback.
//   * FindActiveTarget (0x485b1c)     — active-target lookup + HP/class validity gate.
//   * ResetObjectHighlights (0x485b94)— walk owned objects, queue a clear request each.
//   * CloseSelectionWindows (0x4870f0)— tear down the 32-entry selection window table.
//   * ResetDamageNumberTable (0x48751c)— zero the 16-entry damage-number table (pure).
//   * DestroyDamageNumbers (0x487548) — destroy + zero the 16-entry dmg-number table.
//   * DestroyOrderSlotWindows (0x488828)— destroy the 6-entry order-slot window table.
//   * GatherPlayerUnits (0x489578)    — collect the player-owned combat units (<=31).
//   * ResetSelectionState (0x48938c)  — clear highlight/selection bookkeeping.
//   * IssueOrdersForTeam (0x48c15c)   — locate a team's squad row, push its slot ids.
//   * RunOrderWaitLoop (0x48c648)     — order-wait frame loop "any unit still moving".
//   * RunBattleFrameLoop (0x48c5e8)   — the inner battle frame loop predicate.
//   * BeginBattleOrCacheState (0x492e6c)— duel-mode dispatch vs. cache battle state.
#include "guild/common/types.h"

#include <vector>

namespace guild::sim {

// ===========================================================================
// Recovered table geometry (the static globals the originals address)
// ===========================================================================
//   dword_630E4A — the object-def ring: 23-dword stride, 10 entries (i<230 step 23).
//     entry[+0] high word == object-instance id (dword_630E4A[i] >> 16);
//     entry+2 (a `char*` view) carries +88 == a class byte. 0-id == free slot.
constexpr int kObjDefStride   = 23;   // dwords between ring entries
constexpr int kObjDefCount    = 10;   // 230 / 23
//   dword_B5A1D0 — the selection-window table: 32 i32 handles, -1 == empty.
constexpr int kSelWindowCount = 32;   // 128 bytes / 4
//   dword_B5F6C0.. — the damage-number table: 16 records of stride 5 dwords
//     (+0 ttl/age, +1 handle@dword_B5F6C4, +2 flag@dword_B5F6C8, +4 widget@dword_B5F6D8).
constexpr int kDmgNumCount    = 16;   // 80 / 5
constexpr int kDmgNumStride   = 5;
//   dword_B5A014/018 — the order-slot window table: 6 records of stride 8 dwords
//     (+0 widget@dword_B5A014, +1 window@dword_B5A018, ...), -1 == empty.
constexpr int kOrderSlotWindowCount  = 6;   // 192 / 32
//   word_B5A350 — the 32-slot combat-unit table (stride 268 words / 536 bytes);
//     +97th dword == unit ptr; player-units copied 1-based into dword_11BB69C (cap 31).
constexpr int kCombatUnitCount = 32;
constexpr int kPlayerUnitCap   = 31;
//   squad roster: dword_631208 holds rosterCount@+48, teamId[i]@+52 (stride 4),
//     two 16-entry id arrays at +128 (generic side) and +192 (player side).
constexpr int kSquadOrderSlots = 16;

// ===========================================================================
// Leaf hooks — the cross-module side effects these bodies make. Tests install a
// recording mock; the inert default (all null) makes every leaf a no-op so the
// table-walk / arithmetic logic can be exercised in isolation.
// ===========================================================================
struct CombatSlots3Hooks {
    // --- GameObject query iterator (FindObjectDef / FindActiveTarget / Reset) ---
    // QueryFind(objSetPtr, kind, sub) -> first matching object* (0 if none).
    // IterNext() -> next object* (0 at end). The original passes obj+0x178 + (1,5).
    const void* (*gameObjectQueryFind)(u32 objSet, int kind, int sub) = nullptr;
    const void* (*gameObjectIterNext)() = nullptr;
    // Read the object-instance id word ( *(i16*)obj ) used as the ring key.
    i16 (*gameObjectId)(const void* obj) = nullptr;
    // Read the object's HP-ish dword ( *((i32*)obj + 8), obj+0x20 ): the
    // FindActiveTarget validity gate rejects class 1/2 targets with hp <= 0.
    i32 (*gameObjectHp)(const void* obj) = nullptr;

    // --- the clear-highlight request (ResetObjectHighlights) --------------------
    // Command_QueueClearRequest(team, objField7, objId) — the per-object op.
    void (*queueClearRequest)(i32 team, i32 objField7, i16 objId) = nullptr;

    // --- window / widget table teardown ----------------------------------------
    void (*windowRemoveIfActive)(i32 window) = nullptr;   // Window_RemoveIfActive
    void (*widgetDestroyByType)(i32 widget) = nullptr;    // Widget_DestroyByType
    void (*meshClearHighlight)(const void* unit) = nullptr; // Mesh_SetVertexColors(.,0,0,0)
    void (*dragSlotResetTable)() = nullptr;               // DragSlot_ResetTable
    void (*formDestroy)(i32 form) = nullptr;              // Form_Destroy

    // --- the battle frame loop driver -------------------------------------------
    // RunFrameLoop(a1,a2) -> nonzero == keep running (a frame elapsed). The inert
    // default returns 0 so the loops terminate immediately in tests.
    int  (*gameLogicRunFrameLoop)(int a1, int a2) = nullptr;
    void (*cameraEdgeScroll)() = nullptr;
    // The per-frame combat updates; each returns "did something move/animate".
    int  (*combatUpdateBombExplosions)() = nullptr;
    int  (*combatUpdateThrownBombs)() = nullptr;
    void (*combatUpdateProjectiles)() = nullptr;
    void (*combatPickObjectUnderCursor)() = nullptr;
};

void SetCombatSlots3Hooks(const CombatSlots3Hooks* hooks);
const CombatSlots3Hooks& GetCombatSlots3Hooks();

// ===========================================================================
// Object-def ring  (gilde.exe 0x485a54 / 0x485b1c / 0x485b94)
// ===========================================================================
// The ring is `kObjDefCount` records of `kObjDefStride` dwords. The originals read
// each record's high-word id and a +88 class byte through a `char*` to record+2.
// We model one record with the touched fields named; `idHigh==0` marks a free slot.
struct ObjDefEntry {
    i32 idHigh = 0;     // dword_630E4A[i] >> 16  (object-instance id; 0 == free)
    i8  classByte = 0;  // (record+2)[88] — 1/2 == a "conquerable" class
};

// gilde.exe 0x485a54 — VIBE_Combat_FindObjectDef.
// Walks the GameObject iterator for the owning set; for each iterated object whose
// id matches a ring entry it returns that entry. If the matching entry's class byte
// is 1 or 2 AND the object's hp (obj+0x20) <= 0, OR if no object/entry matched, it
// returns the FIRST FREE ring entry (idHigh==0), or nullptr if the ring is full.
// Returns the matched/free entry index, or -1 for "no free slot".
//   `ring` is the ring view; objSet is the value passed to QueryFind.
int FindObjectDef(std::vector<ObjDefEntry>& ring, u32 objSet);

// gilde.exe 0x485b1c — VIBE_Combat_FindActiveTarget.
// Walks the iterator; returns the iterated object* whose id is in the ring and is a
// VALID target: a non-(1/2) class entry is always valid; a class-1/2 entry is valid
// only while the object's hp (obj+0x20) > 0 (else returns nullptr immediately). An
// object with no ring entry is skipped. Returns nullptr if none.
const void* FindActiveTarget(const std::vector<ObjDefEntry>& ring, u32 objSet);

// gilde.exe 0x485b94 — VIBE_Combat_ResetObjectHighlights.
// Walks the iterator; for each object found in the ring, queues a clear-highlight
// request (queueClearRequest). Returns the count of requests queued.
//   `team` and `objField7` are the constant a1+4 / per-object +0x1C arguments.
int ResetObjectHighlights(const std::vector<ObjDefEntry>& ring, u32 objSet,
                          i32 team, i32 objField7);

// ===========================================================================
// Window / widget table teardown  (0x4870f0 / 0x48751c / 0x487548 / 0x488828)
// ===========================================================================

// gilde.exe 0x4870f0 — VIBE_Combat_CloseSelectionWindows.
// For each non-(-1) handle in the 32-entry selection table: windowRemoveIfActive,
// then set the slot to -1. Returns the number of windows closed.
int CloseSelectionWindows(std::vector<i32>& selWindows);

// One damage-number record (the 5-dword stride record). The reset/destroy leaves
// only touch handle / flag / widget; `age` is the +0 dword they leave alone on
// reset but the table semantics keep it for completeness.
struct DamageNumberRecord {
    i32 age    = 0;   // +0 dword_B5F6C0 (reset writes 0 here? -> see notes)
    i32 handle = -1;  // +1 dword_B5F6C4
    i32 flag   = 0;   // +2 dword_B5F6C8
    i32 widget = -1;  // +4 dword_B5F6D8
};

// gilde.exe 0x48751c — VIBE_Combat_ResetDamageNumberTable.
// Pure table reset over the 16 records: flag(+2)=0, handle(+1)=-1, age(+0)=0.
// (Does NOT free widgets — see DestroyDamageNumbers for the freeing variant.)
void ResetDamageNumberTable(std::vector<DamageNumberRecord>& table);

// gilde.exe 0x487548 — VIBE_Combat_DestroyDamageNumbers.
// Like ResetDamageNumberTable but first destroys each record's live widget
// (widget != -1 -> widgetDestroyByType). Returns the number of widgets destroyed.
int DestroyDamageNumbers(std::vector<DamageNumberRecord>& table);

// One order-slot window record (8-dword stride). The teardown touches widget(+0)
// and window(+1).
struct OrderSlotWindow {
    i32 widget = -1;  // +0 dword_B5A014
    i32 window = -1;  // +1 dword_B5A018  (-1 == empty)
};

// gilde.exe 0x488828 — VIBE_Combat_DestroyOrderSlotWindows.
// For each record whose window(+1) != -1: windowRemoveIfActive(window), set
// window=-1, then widgetDestroyByType(widget). Returns the number torn down.
int DestroyOrderSlotWindows(std::vector<OrderSlotWindow>& table);

// ===========================================================================
// Player-unit gathering / selection reset  (0x489578 / 0x48938c)
// ===========================================================================

// One combat-unit slot view (word_B5A350 stride). GatherPlayerUnits only needs the
// unit pointer (+97 dword) and its owner field (+136 of the unit). We collapse to a
// "this unit belongs to the local player" predicate the caller pre-evaluates, plus
// the opaque handle returned in the gathered list.
struct CombatUnitSlot {
    bool        hasUnit = false;    // (+97 dword) != 0
    bool        ownedByPlayer = false; // unit+136 == off_649D64 (the player object)
    const void* handle = nullptr;   // &word_B5A350[slot] — returned in the list
};

// gilde.exe 0x489578 — VIBE_Combat_GatherPlayerUnits.
// Scans up to 32 combat-unit slots; appends each player-owned unit's handle to the
// result (1-based in the original; here a dense vector), stopping at 31 gathered.
// Returns the gathered handles.
std::vector<const void*> GatherPlayerUnits(const std::vector<CombatUnitSlot>& slots);

// A selection-state record the reset leaf touches:
//   highlightByte (byte_B5A4D8) — cleared to 0 if set;
//   unit          (dword_B5A4D4) — if present and its mesh flag (+530 & 2) is set,
//                  the mesh highlight is cleared (meshClearHighlight).
struct SelectionRecord {
    bool        highlightSet = false;  // byte_B5A4D8 != 0
    const void* unit = nullptr;        // dword_B5A4D4 (0 == none)
    bool        meshHighlighted = false; // (*(unit+52)+530) & 2
};

// Aggregate effect counters returned by ResetSelectionState so tests can assert the
// teardown without inspecting the hooks mock.
struct SelectionResetResult {
    int highlightsCleared = 0;   // byte_B5A4D8 entries reset
    int meshesCleared = 0;       // meshClearHighlight calls
    int windowsClosed = 0;       // selection-window handles removed
    bool formDestroyed = false;  // dword_63125C form torn down
};

// gilde.exe 0x48938c — VIBE_Combat_ResetSelectionState.
// Clears the 32 highlight/selection records (clearing set highlight bytes and mesh
// highlights), calls dragSlotResetTable, removes the 32 selection windows, and
// destroys the active form (if formHandle != -1). `selWindows` is reset to -1.
SelectionResetResult ResetSelectionState(std::vector<SelectionRecord>& records,
                                         std::vector<i32>& selWindows,
                                         i32& formHandle);

// ===========================================================================
// Team order issuing  (gilde.exe 0x48c15c)
// ===========================================================================
// The original locates the squad row whose teamId matches the issuing team, then
// walks one of two 16-entry id sub-rows (player side vs. generic side), resolving
// each id to a unit and building an order. We split the deterministic part — the
// row lookup + which order-slot ids should be issued — from the command coupling.

// A squad row: its team id and the two 16-entry order-slot id arrays.
struct SquadRow {
    i32 teamId = 0;                                  // roster teamId[i]
    i32 playerSideIds[kSquadOrderSlots];             // +192 sub-row
    i32 genericSideIds[kSquadOrderSlots];            // +128 sub-row
    SquadRow() {
        for (int i = 0; i < kSquadOrderSlots; ++i) { playerSideIds[i] = -1; genericSideIds[i] = -1; }
    }
};

// gilde.exe 0x48c15c — VIBE_Combat_IssueOrdersForTeam (row resolution).
// Returns the index of the squad row whose teamId matches `team`, scanning the first
// `rosterCount` rows; -1 if none matches. (The original then walks that row.)
int FindSquadRowForTeam(const std::vector<SquadRow>& rows, int rosterCount, i32 team);

// gilde.exe 0x48c15c — VIBE_Combat_IssueOrdersForTeam (slot selection).
// Given the resolved row and whether the issuing team IS the player side
// (a1 == dword_6311E8), returns the list of non-(-1) order-slot ids that should
// have an order built, in order. (Picks playerSideIds if isPlayerSide else generic.)
std::vector<i32> CollectOrderSlotIds(const SquadRow& row, bool isPlayerSide);

// ===========================================================================
// Battle frame loops  (gilde.exe 0x48c648 / 0x48c5e8 / 0x492e6c)
// ===========================================================================

// gilde.exe 0x48c5e8 — VIBE_Combat_RunBattleFrameLoop.
// Drives RunFrameLoop until it returns 0; per surviving frame: EdgeScroll,
// PickObjectUnderCursor, UpdateBombExplosions, UpdateThrownBombs, then
// UpdateProjectiles (both branches call it). Returns the final RunFrameLoop result
// (0). `frameArg`/`stateArg` map to the a1/a2 the original threads through.
//   `slowMoGate`/`tickCounter`/`tickLimit` correspond to the dword_672230 /
//   v4 / dword_6315A8 branch (slow-motion projectile gate); the only observable
//   effect is whether dword_631614 ("force-step projectiles") is set — returned in
//   `forcedStep`.
int RunBattleFrameLoop(int frameArg, int stateArg, bool slowMoGate,
                       int tickCounter, int tickLimit, bool& forcedStep);

// gilde.exe 0x48c648 — VIBE_Combat_RunOrderWaitLoop.
// The order-wait loop: while not aborted (abortGate==0) and RunFrameLoop keeps
// returning nonzero, on each frame it scans the 16 order-slot rows and decides
// whether ANY commanded unit is still moving (unit+97 present && +296 != 0). It then
// runs the bomb/projectile updates; if nothing is busy it sets dword_631614 (force
// step). Loop exits when no unit is busy AND the bomb updates report nothing moving.
// We model the per-iteration predicate as a pure function so the loop is testable
// without the global frame driver.
struct OrderWaitSlot {
    bool active = false;   // record+4 byte != 0
    bool hasId  = false;   // record+0 dword != -1
    bool unitMoving = false; // resolved unit present && unit+296 != 0
};

// gilde.exe 0x48c648 (inner) — returns true if ANY slot reports a unit still moving
// (scans up to 16 slots, short-circuits on the first moving unit, matching the
// original's `break`).
bool AnyOrderUnitMoving(const std::vector<OrderWaitSlot>& slots);

// gilde.exe 0x492e6c — VIBE_Combat_BeginBattleOrCacheState.
// If duelMode (dword_6315A4) != 0, the original tail-calls RunBattleLoop (we report
// that via the result enum + leave caching untouched). Otherwise, if pendingFlag
// (dword_6311F4) is set it caches (duelMode, dword_6311F8) into rec+852/+856; else it
// writes 1 into rec+852. Models the two cached fields.
struct BattleCacheRecord {
    i32 field852 = 0;   // rec+852
    i32 field856 = 0;   // rec+856
};
enum class BattleBeginAction { kRunBattleLoop, kCachedPending, kCachedDefault };

// `duelMode`==dword_6315A4, `pendingFlag`==dword_6311F4, `pendingValue`==dword_6311F8.
BattleBeginAction BeginBattleOrCacheState(BattleCacheRecord& rec, i32 duelMode,
                                          i32 pendingFlag, i32 pendingValue);

} // namespace guild::sim
