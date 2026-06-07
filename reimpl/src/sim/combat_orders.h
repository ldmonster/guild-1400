#pragma once
// gilde.exe — Combat ORDER-TICK non-attack state machine + the small per-unit
// action/target/selection RULES (namespace guild::sim). MODULE: combat (prefix
// VIBE_Combat_*). The earlier combat agents translated state 2 (attack) of
// VIBE_Combat_UpdateUnitOrders @0x491688 (see combat_battle.cpp EvaluateAttack /
// TickOrderSlot) and the role/order glue (BuildOrderForUnit). This file
// translates the REMAINING order-tick state cases — the non-attack states —
// plus the small rule helpers the deferred lists left out:
//
//   * VIBE_Combat_UpdateUnitOrders @0x491688 — the non-attack switch cases of the
//     per-unit order-tick state machine (OrderSlot::state):
//        1 move, 3 march, 4 capture, 5 stand, 6 ware-collect, 7 escape, 8 standup.
//     Each case gates on the in-flight packet, evaluates the unit's position vs
//     its target tile (heightmap tile->world + within-tolerance), and emits the
//     next path/animation/sync command. The DECISION (which command, and the slot
//     bookkeeping) is the translated RULE; the path lookup, tile->world transform,
//     and command emission are routed through forward-declared context predicates
//     + the command sink (mock) so the state machine is testable in isolation.
//   * VIBE_Combat_StandUpUnitAction      @0x490f18 — stand-up action emitter.
//   * VIBE_Combat_PickUpFromGroundAction @0x490fa8 — pick-up-from-ground emitter.
//   * VIBE_Combat_PlayCelebrateGesture   @0x490f40 — celebrate-gesture emitter.
//   * VIBE_Combat_CaptureUnitAction      @0x48cf30 — the capture-flag RULE: set
//     the captured object's owner to the attacker side (dword_6311E8) + link it.
//   * VIBE_Combat_WareObjectCallback     @0x48b488 — ware-pickup collector filter.
//   * VIBE_Combat_ConquerObjectCallback  @0x48b5a4 — conquer-flag collector filter.
//   * VIBE_Combat_ClassifyTileType       @0x48d4d8 — person-type -> tile-type id.
//   * VIBE_Combat_GetSelectionFlag       @0x486460 — selection-highlight flag rule.
//   * VIBE_Combat_CountActiveSlots       @0x4897e0 — populated roster-slot count.
//   * VIBE_Combat_IsTargetUnderfull      @0x57e4c8 — person under fill-target gate.
//   * VIBE_Combat_SelectBeatingTarget    @0x57829c — street-brawl target picker.
//   * VIBE_Combat_AssignGuardTarget      @0x57e714 — guard target (re)assignment.
//   * VIBE_Object_SpawnBomb              @0x486648 — dropped-bomb table allocator.
//
// Determinism: SelectBeatingTarget uses the CRT LCG (Math_RandomModulo) exactly.
#include "guild/common/types.h"
#include "sim/combat.h"
#include "sim/combat_battle.h"
#include "sim/combat_types.h"

#include <functional>
#include <vector>

namespace guild::sim {

// ===========================================================================
// Order-tick non-attack state machine.
// ===========================================================================
//
// Each non-attack case in UpdateUnitOrders has the same skeleton:
//   1. the slot only progresses when the in-flight command finished:
//        if (slot.packetId == 1 || GetPacketStatus(slot.packetId)) { ... }
//   2. it then evaluates the acting unit's world position against its target
//      tile via VIBE_Heightmap_TileToWorld + VIBE_Math_VectorWithinTolerance
//      (a per-state tolerance), and/or VIBE_Path_FindNearestFreeTile, and emits
//      the next command (RequestBuildOp78 path / Op85 anim / Op80 sync / Op81).
//
// We model the two world-space predicates as context callbacks so the state
// machine is exercisable without the heightmap/scene. The command emissions are
// surfaced as an OrderCommand enum (and reported via the sink), the slot is
// mutated 1:1, and the function returns whether work was done this tick.

// The next command an order-tick non-attack case resolves to (the
// RequestBuildOp* / packet emission). Surfaced for testing + sink reporting.
enum class OrderCommand {
    None,        // gated on in-flight packet, or nothing to do this tick
    PathToTile,  // RequestBuildOp78DualStr — walk to a (free) tile
    AnimMode,    // RequestBuildOp85Unit    — set unit animation/formation mode
    SyncDone,    // RequestBuildOp80        — signal the order phase complete
    WareSave,    // RequestBuildOp81        — ware-save sync (state 6)
    Captured,    // the unit reached/finished its objective (LABEL_202 path)
};

// The per-state world predicates the order tick needs (heightmap/path leaves).
// In the original these are VIBE_Heightmap_TileToWorld + VectorWithinTolerance
// (does the unit stand on its target tile, within `tol`) and
// VIBE_Path_FindNearestFreeTile (is there a reachable free tile near the goal).
struct OrderWorldContext {
    // True when the acting unit is within `tol` world units of the slot's target
    // tile (TileToWorld(tileX,tileZ) vs the unit's mesh origin). Per-state tol:
    // move/escape 30.0, march/stand 50.0, capture 49.5.
    std::function<bool(const OrderSlot& slot, float tol)> unitOnTargetTile;
    // True when a free tile near (tileX,tileZ) was found (Path_FindNearestFreeTile).
    // Writes the resolved tile back into outX/outZ. Defaults to "found, unchanged".
    std::function<bool(i32 inX, i32 inZ, i32& outX, i32& outZ)> findFreeTile;
    // True when the acting unit currently has a script action queued
    // (*(actor+296) != 0): a walk/anim is still playing -> don't re-issue.
    std::function<bool()> unitBusy;
};

// gilde.exe 0x491688 — one tick of a NON-ATTACK order slot (states 1,3,4,5,6,7,8).
// Mirrors the corresponding switch(slot.state) case: gates on the in-flight
// packet, evaluates position/path via `ctx`, mutates the slot (packetId, phase,
// moved, warePhase, tile fields) and returns the command it would emit (also
// reported through the optional sink). Returns OrderCommand::None when the slot
// is gated (packet still pending) or the state is idle/attack (handled elsewhere).
OrderCommand TickNonAttackOrder(OrderSlot& slot, const OrderWorldContext& ctx);

// Per-state tolerances (the VectorWithinTolerance thresholds in the cases).
constexpr float kOrderTolMove    = 30.0f;  // states 1 (move) / 7 (escape)
constexpr float kOrderTolMarch   = 50.0f;  // state 3 (march) / state 6 stand phase
constexpr float kOrderTolCapture = 49.5f;  // state 4 (capture)

// ===========================================================================
// Small per-unit action emitters (the order-completion gestures).
// ===========================================================================
//
// All three share the slot gate: act only when slot.state != 0 and
// slot.unitId != -1, after resolving the unit by id. They queue a character
// action (StandUp / pick-up / celebrate). We surface the gate result + which
// action would be queued; the anim-queue insert is the forward-declared leaf.
enum class UnitGesture { None, StandUp, PickUpFromGround, Celebrate };

// gilde.exe 0x490f18 — VIBE_Combat_StandUpUnitAction.
//   if (slot.state && slot.unitId != -1 && (u=FindUnitById(slot.unitId)))
//       Character_StandUp(u->actorPtr);
UnitGesture StandUpUnitAction(const OrderSlot& slot, bool unitExists);

// gilde.exe 0x490fa8 — VIBE_Combat_PickUpFromGroundAction.
//   if (slot.state && slot.unitId != -1) { u=FindUnitById; Character_StandUp(u);
//       o=Object_FindByHandle(u->+91); if (o) queue capture/pick-up action; }
UnitGesture PickUpFromGroundAction(const OrderSlot& slot, bool unitExists,
                                   bool groundObjectFound);

// gilde.exe 0x490f40 — VIBE_Combat_PlayCelebrateGesture.
//   if (slot.state && slot.unitId != -1) queue "gestik/jubeln" gesture action.
UnitGesture PlayCelebrateGesture(const OrderSlot& slot, bool unitExists);

// ===========================================================================
// Capture-flag RULE (the conquer-objective transfer).
// ===========================================================================

// gilde.exe 0x48cf30 — VIBE_Combat_CaptureUnitAction (the RULE, extracted).
// When the pick-up/capture action lands, the captured scene object's owner word
// is set to the ATTACKER side's owner id (dword_6311E8) and the captured object
// is linked onto the acting unit (+428). The mesh-state restore is a leaf.
//   captured.ownerId = attackerSideOwnerId;   // *(obj+10) = *(word*)dword_6311E8
//   actingUnit.capturedObject = captured;      // *(unit+428) = obj
// Returns the new owner id written (the attacker side). `attackerSideOwnerId` is
// dword_6311E8 (side-A owner). `hasParentObject` mirrors v3[128] != 0 (the object
// actually has the parent node the original dereferences).
struct CaptureResult { bool applied = false; i32 newOwnerId = -1; };
CaptureResult CaptureUnitAction(i32 attackerSideOwnerId, bool hasParentObject);

// ===========================================================================
// Objective-collector callbacks (ware / conquer-flag enumeration filters).
// ===========================================================================
//
// Both are scene-object enumeration callbacks that test a name prefix and the
// owner, and append the matching object's handle to the conquer/ware list
// (dword_B59F0C[++dword_631268]). We model the FILTER decision: does this object
// qualify for collection?

// gilde.exe 0x48b488 — VIBE_Combat_WareObjectCallback.
//   collect iff name starts with "WARE_"  AND  unitFaction != objectOwner.
//   (returns true to qualify; the original always returns 1 to keep iterating but
//    only appends when both conditions hold — `appended` mirrors that append.)
bool WareObjectQualifies(bool namePrefixWare, i32 unitFaction, i32 objectOwner);

// gilde.exe 0x48b5a4 — VIBE_Combat_ConquerObjectCallback.
//   collect iff name starts with "sp_CONQUER".
bool ConquerObjectQualifies(bool namePrefixConquer);

// ===========================================================================
// Tile / selection rule helpers.
// ===========================================================================

// gilde.exe 0x48d4d8 — VIBE_Combat_ClassifyTileType.
// Maps a person/owner TYPE byte (dword_13CE294 + 589*type) to a tile-type id:
//   19 -> 0,  16 -> 1,  4 -> 2,  anything else -> 0.
u8 ClassifyTileType(u8 personType);

// gilde.exe 0x486460 — VIBE_Combat_GetSelectionFlag.
// Returns the 0x800 highlight flag when the queried person at index `personIndex`
// IS the unit's faction owner, OR when the global force-highlight (dword_63C7B8)
// is set; else 0.
//   v3 = (&persons[268*personIndex] == unit.factionOwnerPtr) ? 0x800 : 0;
//   if (globalHighlight) return 0x800;  return v3;
constexpr i32 kSelectionHighlightFlag = 0x800; // 2048
i32 GetSelectionFlag(bool factionMatches, bool globalHighlight);

// gilde.exe 0x4897e0 — VIBE_Combat_CountActiveSlots.
// Counts the leading populated entries of a 16-wide roster (-1 == empty).
// Returns the count; returns -1 if all 16 are populated (the "full" sentinel).
int CountActiveSlots(const i32* roster16);

// gilde.exe 0x57e4c8 — VIBE_Combat_IsTargetUnderfull.
// A person is "underfull" (a valid attack/supply target) when its current fill
// (a u16 at the person's parallel column) is below its float target capacity.
//   return (double)currentFill < targetCapacity;
bool IsTargetUnderfull(u16 currentFill, float targetCapacity);

// gilde.exe 0x57e714 — VIBE_Combat_AssignGuardTarget (the RULE, extracted).
// Re-points a guard at a new target person IF that target is not already at its
// guard capacity. Capacity is word_641DB0[type] for the target's type byte; the
// target's current guard count is its +101 byte. On a switch it decrements the
// old target's guard count and increments the new one.
//   if (targetGuardCount < capacityForType) {
//       if (old) old.guardCount = max(0, old.guardCount-1);
//       guard.target = target;  ++target.guardCount;  return true; }
//   return false;
struct GuardAssign {
    bool   assigned = false;
    u8     newTargetCount = 0;   // target.+101 after assignment
    u8     oldTargetCount = 0;   // previous target.+101 after decrement
};
GuardAssign AssignGuardTarget(u16 capacityForType, u8 targetGuardCount,
                              bool hasOldTarget, u8 oldGuardCount);

// ===========================================================================
// Street-brawl target picker (the "beating" mob selector).
// ===========================================================================

// gilde.exe 0x57829c — VIBE_Combat_SelectBeatingTarget (RNG-driven RULE).
// Builds a candidate list of up to 16 person ids and returns one at random:
//   * if the brawler has a primary target (+364) with a live actor, it seeds the
//     list with that target's id; likewise a secondary target (+368).
//   * then, up to 32 query attempts, it picks a random faction filter
//     (filterTable[5*RandomModulo(4) + RandomModulo(5)]) and iterates persons of
//     kind 5 with that filter, appending up to 3 of them per query (each gated by
//     RandomModulo(4) < 3), until the list reaches 16 or attempts run out.
//   * returns list[RandomModulo(count)].
// We model the candidate-supply as a callback (the scene query is the leaf) but
// reproduce the RNG-driven selection exactly. `seedIds` are the pre-seeded
// primary/secondary target ids (0..2 entries). `queryFn(attempt, filterByte)`
// returns the ids a single faction query yielded (the iterator results); the rule
// appends up to 3 of them, each gated by RandomModulo(4) < 3, in order. Returns
// the chosen id, or -1 if no brawler (seedIds empty and no query results).
int SelectBeatingTarget(const std::vector<i32>& seedIds,
                        const std::vector<u8>& filterTable25,
                        const std::function<std::vector<i32>(int attempt, u8 filterByte)>& queryFn);

// ===========================================================================
// Dropped-bomb table allocator (the bomb-spawn RULE).
// ===========================================================================

// gilde.exe 0x486648 — VIBE_Object_SpawnBomb (the table-slot allocation RULE).
// Finds the first free dropped-bomb slot (dword_B5F810 handle column, 8-byte
// stride == 2 dwords, 32 slots) and records the spawn tick (dword_B5F814 = now).
// The detonation fuse is spawnTick + 350 (see projectile.h kDroppedBombFuse).
// Returns the slot index it claimed, or -1 when the 32-slot table is full.
//   (The mesh attach VIBE_Object_AttachToUniverseNode is the forward-declared
//    leaf; here we model the table bookkeeping + fuse arming.)
int SpawnDroppedBomb(std::vector<Bomb>& bombs, u32 now);

} // namespace guild::sim
