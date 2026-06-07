#pragma once
// =============================================================================
// guild::play — REAL NPC DAILY-SCHEDULE / ACTION-DISPATCH BRIDGE (PLAYABLE_PLAN P6).
//
// The "inert hook" gap on the LIVE PER-TURN path's NPC daily-routine director.
//
// The per-day cascade (WAVE28 grounding pointers): VIBE_GameTick_BeginPlayerRound
// @0x533188 steps each He-record handler; the last "big" per-NPC director is
//   gilde.exe 0x4e7e88 — VIBE_NpcAction_DailyRoutineStep
// reconstructed in src/sim/npc_daily.cpp as sim::NpcDaily_DailyRoutineStep. That
// director sweeps the live 768-person array (word_12CE910 @0x12CE910) by its
// PARALLEL COLUMNS — activeA/activeB (+356/+357), homeBld (+364), workBld (+368),
// destBld (+388), turnBits (+456), id (+4) — keyed off the season (day%4) and the
// clock hour, and decides each person's activity for the round (go to work / go to
// the wirtshaus / go home), emitting the corresponding network commands AND writing
// back the per-turn dispatch bits into the person's +456 column.
//
// THE INERT HOOK
// ---------------------------------------------------------------------------
// All of the director's cross-cluster leaves go through sim::NpcDailyHooks
// (sim/npc_daily.h). NO real_hooks installer wires it — by default g_hd points at
// a zeroed kInert table, so on the live per-turn path the director sees:
//   personCount() == 0  (no persons swept)  -> the whole sweep is a no-op,
//   every command emitter / column writeback is a null function pointer -> nothing
//   is enqueued and NO person record is mutated. That is the engine's "subsystem
//   not present" no-op: the real NPC daily schedule never runs.
//
// THE BRIDGE (additive, public-setter only)
// ---------------------------------------------------------------------------
// InstallRealNpcActions() routes the inert NpcDailyHooks at the REAL reconstructed
// leaves:
//   * personCount / personRow / setTurnBits  -> the REAL live g_persons array
//     (sim/entity.h). personRow reads the exact parallel columns the engine reads
//     by raw byte offset; setTurnBits writes the +456 turn-bits column back (the
//     director's in-line per-person state mutation — folded by HashFullWorld).
//   * homeIsProduction  -> the REAL sim::Building_IsProductionKind (building_type.h)
//     resolved over the home building's type byte via sim::BuildingFindById.
//   * command emitters (requestBuildOp77 / requestChrMoveToUniverse /
//     queueRequestString47 / queueRequestNamedObject53 / queueRequestArgs25)
//     -> the REAL sim::Command_* packet builders (command_apply7/9, command_codec,
//     command_builders) onto a bridge-owned live CommandQueue (NpcActionsQueue()),
//     so each scheduled action BUILDS its real network packet.
//
// Nothing here edits wiring.cpp / frameloop.cpp / turn_driver.cpp / the hook-table
// .cpp. The bridge wires ONLY through the public sim::SetNpcDailyHooks setter from
// this self-owned installer, mirroring wire_scene_bridge / wire_render_bridge.
//
// FINDINGS (documented, not worked around): the director's render/entity/bone-chain
// -coupled target searches have NO standalone reconstructed leaf to point at —
//   findCarryTarget  (VIBE_NpcAction_FindCarryTargetForChar 0x4e786c)
//   findInteractionTarget (0x4e79c0)  pickTavern (PickClosestByWeight 0x4e7c3c)
//   destDoorIds / homeHasMesh / ownerKind / aiPlayerClass / workDistanceOk /
//   characterBudgetOk / currencyHeld / candidateCount
// are all bone-chain / scene-graph / type-descriptor-table reads whose real
// reconstruction is owned by other clusters (character-mesh / scene-walk) or not
// yet translated. The bridge supplies a DETERMINISTIC, settable provider for those
// (NpcActionsTargetProvider) so the director's dispatch path is exercisable end to
// end; that provider is NOT a 1:1 real leaf and is flagged as such. See the report.
// =============================================================================
#include "guild/common/types.h"
#include "sim/command.h"   // sim::CommandQueue

namespace guild::play {

// ---------------------------------------------------------------------------
// Deterministic stand-in for the render/entity-coupled per-person searches the
// director calls (the leaves with NO standalone reconstructed sibling — see the
// FINDINGS above). Tests/e2e install one so the dispatch path can run; values are
// pure functions of the person index so the run is reproducible. With NO provider
// installed these all report "nothing found" (the safe no-op), so the bridge alone
// still de-inerts the column sweep + writeback + production probe + command builds.
// ---------------------------------------------------------------------------
struct NpcActionsTargetProvider {
    // findCarryTarget / findInteractionTarget: write a destination (universe,obj)
    // for person i; return nonzero if a target was found.
    int (*findTarget)(int i, i32* outUniverse, i32* outObj) = nullptr;
    // destDoorIds: the dest building's +44/+48 "already-there" comparison ids.
    bool (*destDoorIds)(int i, i32* out44, i32* out48) = nullptr;
    bool (*homeHasMesh)(int i) = nullptr;       // home +97 mesh root nonzero
    u8   (*ownerKind)(int i) = nullptr;          // owner person kind byte (6/7 player)
    u8   (*aiPlayerClass)(int i) = nullptr;      // type-def class byte (4/16/19 skip)
    bool (*workDistanceOk)(int i) = nullptr;     // bone-chain distance gate
    bool (*characterBudgetOk)() = nullptr;       // live-actor cap (<32)
    i32  (*currencyHeld)(int i) = nullptr;       // SumCurrencyHeld (>3200 tavern)
    int  (*pickTavern)(int i, i32* outU, i32* outObj) = nullptr;
    int  (*candidateCount)() = nullptr;          // nearby owned persons (>0 enables roll)
};

// Install the REAL NPC daily-schedule / action dispatch: route sim::NpcDailyHooks
// at the real reconstructed leaves (live g_persons columns, Building_IsProductionKind,
// the real Command_* packet builders on NpcActionsQueue()). Idempotent;
// process-static. After this, sim::NpcDaily_DailyRoutineStep sweeps the live person
// array and writes back real per-turn dispatch bits + builds real command packets.
void InstallRealNpcActions();

// Restore the inert default (sim::SetNpcDailyHooks(nullptr)): no persons swept,
// every effect a no-op. For tests that observe the inert-vs-real difference.
void UninstallRealNpcActions();

// True while the real NPC daily-schedule dispatch is installed.
bool RealNpcActionsInstalled();

// Install / clear the deterministic target provider (the render-coupled searches).
// Pass nullptr to clear (then the searches report "nothing found").
void SetNpcActionsTargetProvider(const NpcActionsTargetProvider* provider);

// The bridge-owned live CommandQueue the real command emitters build into. Tests
// read its size to prove real packets were enqueued by the scheduled actions.
sim::CommandQueue& NpcActionsQueue();

// Reset the bridge command queue to empty (test helper; not in the original).
void ResetNpcActionsQueue();

// Tallies of what the last director sweep did through the real leaves (read back by
// tests/report). Reset by ResetNpcActionsTallies().
struct NpcActionsTallies {
    int rowsRead       = 0;   // personRow() calls served from g_persons
    int turnBitsWrites = 0;   // setTurnBits() writebacks into the +456 column
    int prodProbes     = 0;   // Building_IsProductionKind() calls
    int commandsBuilt  = 0;   // real Command_* packets enqueued
};
const NpcActionsTallies& GetNpcActionsTallies();
void ResetNpcActionsTallies();

} // namespace guild::play
