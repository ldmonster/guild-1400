#pragma once
// =============================================================================
// guild::play — SESSION DAY-START NPC DAILY-ROUTINE DESTINATION PASS (wave 4,
// agent W4-B: the living city).
//
// In the original, persons get somewhere to go each round WITHOUT player orders
// through the per-turn daily director:
//
//   DISPATCH CHAIN (verified against the binary, 2026-06-11):
//   VIBE_NpcAction_DailyRoutineStep @0x4e7e88 is the He-record TYPE-0x3A run
//   handler (registered by VIBE_Event_RegisterHandlerTable @0x4f225e into
//   funcs_4C6EE9; init = VIBE_NpcAction_InitWalkState @0x4e7810). It is stepped
//   by VIBE_He_RunAllHandlers @0x4c6e38, which the opcode-30 WORLD-CLOCK commit
//   (VIBE_Command_ExAdvanceGameTick @0x498954, call @0x498a36) runs once per
//   committed tick over every live He record whose appointment time +82 is
//   older than qword_13CE852 (GameTime_Compare @0x4c6e74) and whose type is
//   < 0x88 with (flags+120 & 0x18) == 0. BeginPlayerRound @0x533188 does NOT
//   step it — its He pass (MeisterAi_TickRegisteredEvents @0x4c6f0c) only runs
//   records flagged +120 & 0x10. (The wave-4 header previously credited
//   BeginPlayerRound; refuted by the 0x4c6e38/0x4c6f0c disasm.)
//
//   The director itself (reconstructed 1:1 in src/sim/npc_daily.cpp as
//   sim::NpcDaily_DailyRoutineStep): state 0 is the MORNING work-dispatch
//   sweep over the 768-person parallel columns (+356/+357 active, +364 homeBld
//   dword_12CEA7C, +368 workBld dword_12CEA80, +388 live-char ptr
//   dword_12CEA94, +456 turnBits dword_12CEAD8), keyed off the season (day % 4
//   -> flt_6476FC work-start hours {8,7,8,9}) and the clock hour
//   (qword_13CE852). A dispatched person gets a
//   RequestChrMoveToUniverse(person, universe, obj, "dummy_EINGANG") command
//   and its +456 work-dispatch bit 0x1000 set (BYTE1 |= 0x10 @0x4e8184 — the
//   same bit the entry gate tests, so the sweep is self-limiting).
//
// THIS PASS (what the session runs at each day start — 06:00, inside every
// season's morning window since flt_6476FC[s] + flt_61F968(-1) >= 6):
//   1. sync the director's clock image (sim::NpcClock) from the WORLD clock
//      sim::g_tickClock (qword_13CE852 — the same record SessionTick commits),
//   2. run the REAL director step (sim::NpcDaily_DailyRoutineStep, He state 0)
//      through the REAL leaf bridge (play::InstallRealNpcActions — live
//      g_persons columns, Building_IsProductionKind, real Command_* builders)
//      with THIS module's target provider (below),
//   3. for every person the director DISPATCHED (its +456 turn-bit was set by
//      the real sweep), bind the movement destination into the wire_npc_movement
//      bridge: destination = the target building's REAL city placement
//      (CityView3D::boundObjects — the owner-id scene-node match @0x5a8140)
//      mapped to a tile via render::WorldToTileWithHeight @0x5c6644 over the
//      session's REAL terrain heightmap; start = the person's current bound
//      seat (the entrance dummy of its anchor building), else its home
//      building's placement. play::SetEntityDestination then drives the real
//      A* path-follow (PathBuildWaypointList @0x43bd70 / WalkStep @0x4093b0).
//
// NAMED GAPS / DOCUMENTED STAND-INS (rule 8 — never silently faked):
//  * the director's render/entity/bone-chain-coupled searches (findCarryTarget
//    @0x4e786c, findInteractionTarget @0x4e79c0, destDoorIds, homeHasMesh,
//    ownerKind, aiPlayerClass, workDistanceOk, characterBudgetOk @
//    Character_CountByOwner, currencyHeld, pickTavern @0x4e7c3c) have NO
//    standalone reconstructed leaf (wire_npc_actions.h FINDINGS). This module's
//    provider derives them from the REAL live records instead:
//      - findCarryTarget/findInteractionTarget -> the person's own workBld
//        column (+368) when that building has a real bound city placement,
//      - destDoorIds -> the destBld column (+388) as the door-id pair
//        (the +44/+48 dwords ride the unreconstructed building runtime),
//      - homeHasMesh -> the home building has an owner-matched scene node
//        (the +97 mesh-root probe's observable),
//      - ownerKind -> g_persons[homeBld record word +39].kind (the
//        byte_12CE912[536 * homeBld->+39] read),
//      - aiPlayerClass -> g_buildingTypes[type] byte 0 (dword_13CE294 + 589*t),
//      - workDistanceOk -> 3D world distance home->target < 7000.0f (the
//        disasm-verified 0x4e80b2 gate constant 0x45DAC000; missing nodes pass
//        — v69 stays 0.0; the original measures home vs the char's CURRENT
//        building charPtr->+44, this provider home vs the work column),
//      - characterBudgetOk -> bound-person count < 32 (the CountByOwner cap),
//      - tavern leaves -> "none found" (state-1 evening only; the social
//        candidate gather @0x4e8383 is the Person_QueryBegin cluster).
//    The provider is the deterministic seam wire_npc_actions.h already flags
//    as NOT a 1:1 leaf; the director's RULES, sweep, turn-bit writebacks and
//    command builds are the real reconstructed code.
//  * destination POINT: the original walks to the destination building's
//    "dummy_EINGANG" node (the ChrMove tag); the composed dummy position is
//    CityView3D-internal, so this pass uses the building NODE's placement
//    (the VIBE_Object_IsNearDoorAlt @0x4b0ee8 no-dummy fallback point). Tile
//    granularity makes the difference at most one tile.
// =============================================================================
#include "guild/common/types.h"

namespace guild::render { struct Heightmap; }

namespace guild::play {

class CityView3D;

struct SessionNpcDailyResult {
    int dispatched = 0;  // persons the REAL director marked dispatched (+456 bit)
    int assigned   = 0;  // movement destinations bound (SetEntityDestination)
};

// Run the day-start daily-routine pass described above. `view` supplies the
// bound building placements + the bound-person seats; `hm` is the session's
// REAL terrain heightmap (tile<->world mapping). Safe no-op (zero result) when
// `hm` is null or nothing is dispatched. The movement driver itself
// (InstallNpcMovement + grid bind) is the caller's responsibility.
SessionNpcDailyResult SessionNpcDailyAssign(CityView3D& view,
                                            const render::Heightmap* hm);

} // namespace guild::play
