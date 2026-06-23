#pragma once
// charaction_npcaction_recon2 — CONTINUATION of charaction_npcaction_recon.
//
// PURPOSE
//   The original recon module (charaction_npcaction_recon.{h,cpp}) translated the
//   subset of the CharAction/NpcAction per-action *step* cluster whose Hex-Rays
//   pseudocode is an unambiguous integer state machine, and *explicitly OMITTED*
//   (rule 8 — no cheap analogues) the rest of the cluster:
//       DuelInit 0x4cfed8, PickpocketStep 0x4d3188, SellObjectStep 0x4d13b4,
//       GoToTavernStep 0x4d2314, DrinkInit 0x4d21ec, RunOfficeGuardAssign 0x4db2e4,
//       ProtectionMoneyStep 0x4d3918, PatrolInit 0x4cdd38, ArrestInit 0x4cf544,
//       FindNearbyPeerState 0x4cae78, ApproachTargetState 0x4cb5dc,
//       GreetTargetState 0x4cb74c.
//   citing dropped/uninitialized __usercall registers feeding control flow, lost
//   x87/FPU call arguments (VIBE_Coord_ConvertX() with an empty arg list whose
//   *result* then steers a branch), or UI/global-state coupling.
//
//   This file re-audits that OMIT list one routine at a time and lands ONLY the
//   ones that are, on independent inspection, faithfully recoverable with NO branch
//   that depends on a dropped register and NO branch that depends on an unknowable
//   x87 conversion result.
//
// LANDED HERE
//   - VIBE_CharAction_DrinkInit (0x4d21ec).  Clean integer/threshold state init.
//     Its single VIBE_Coord_ConvertX() truncates a value (v6) that IS fully known
//     from context — `(int)v6`, identical to the recoverable ConvertX already
//     modeled in npcaction7.cpp's AdjustStatCmd — and it does NOT feed any branch
//     (it only scales a stored duration byte). No register feeding control flow is
//     dropped. => translated 1:1.
//
// STILL OMITTED (rule 8), unchanged from the parent module — re-confirmed:
//   - PatrolInit 0x4cdd38       : `v5` (the iterating person record) is a dropped
//                                 register; the proximity gate `sqrt(...) <= flt`
//                                 reads `*(v5+97)` so the loop's accept/reject
//                                 control flow depends on it. OMIT.
//   - ArrestInit 0x4cf544       : the suspect-validity gate `!v5 || (*(v5+457)&1)`
//                                 reads a dropped register; the branch (release vs.
//                                 proceed) depends on it. OMIT.
//   - DuelInit 0x4cfed8         : gate `(!v4 || !v3)` on dropped registers AND a
//                                 ConvertX result (v32/v13) feeding the win/loss
//                                 branch. OMIT.
//   - SellObjectStep 0x4d13b4   : the candidate-found branch `if (v3)` keys on a
//                                 register (v3) the decompiler initialized to 0 and
//                                 never updates (the loop bumps `i`, not v3); the
//                                 RandomModulo(v3) draw also reads it. OMIT.
//   - GoToTavernStep 0x4d2314   : `if (v15 <= (int)v14)` branches on a lost-x87
//                                 ConvertX result (v15). OMIT.
//   - PickpocketStep 0x4d3188   : the persisted state `+112 = (v7 > 480)` keys on a
//                                 dropped register (v7, the DiffMinutes result).
//                                 OMIT.
//   - ProtectionMoneyStep 0x4d3918 : EventPanel/Form/dword_75BFxx UI-global coupling
//                                 plus a dropped `v10` in the args25 emit. OMIT.
//   - RunOfficeGuardAssign 0x4db2e4 : the assignment loop `v3 < v13` / `v3 >= v13`
//                                 branches on a lost-x87 ConvertX result (v13).
//                                 OMIT.
//   - FindNearbyPeerState 0x4cae78, ApproachTargetState 0x4cb5dc,
//     GreetTargetState 0x4cb74c : dropped __usercall registers (a second
//                                 FindRecordById landing in an undefined edx/ecx)
//                                 feeding the conversation gates. OMIT.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Cross-cluster leaf bridge for this continuation module. A null member installs
// the inert default (every query reports absent / no-op), so the control flow is
// exercised headlessly. Field offsets are byte-faithful (he.h accessors).
//
// Extra byte-faithful offsets used by DrinkInit:
//   record+8  (u16)  : person/city row index used to address the stat table.
//   record+171..174  : packed dword; `>> 24` is the sub-method byte (the per-NPC
//                      column into the 536-stride stat table).
//   record+112 (i32) : action state (He_State).
//   record+172 (u8)  : action-type byte the *Init stamps (DrinkInit -> 21).
//   record+173 (u8)  : computed duration byte (DrinkInit -> 2*(int)scaled + 4).
//   record+82..95    : appointment clock (He_ApptTime).
//   record+68..81    : saved clock (He_SavedTime).
// ---------------------------------------------------------------------------
struct CharActionRecon2Hooks {
    // VIBE_He_FindFirstHandlerByFilter(2, 2, rowIndex, 0, 96) — first matching
    //   handler in the pool with the given action-type filter, or null.
    HeRecord* (*findFirstByFilter)(int a, int b, i32 rowIndex);
    // VIBE_He_FindNextMatchingHandler() — advance the same filter cursor.
    HeRecord* (*findNextMatching)();
    // VIBE_He_FreeHandlerEntry(record) — release the handler entry; result code.
    i32       (*freeHandlerEntry)(HeRecord* h);

    // byte_12CE990[536*rowIndex + subMethod] — the per-NPC saturation stat used as
    //   both the early-out gate input and the duration scale input. Inert => 0.
    u8        (*statTableByte)(i32 rowIndex, int subMethod);

    // dword_63C7B8 — the "is the clock advancing in real time" global. When set the
    //   advance is (hours=0, mins=1); when clear it is (hours=5, mins=0). Inert => 0.
    i32       (*realTimeModeFlag)();
};
void SetCharActionRecon2Hooks(const CharActionRecon2Hooks* hooks);
const CharActionRecon2Hooks& GetCharActionRecon2Hooks();

// ===========================================================================
// Translated routine.
// ===========================================================================

// gilde.exe 0x4d21ec — VIBE_CharAction_DrinkInit(h@eax, a2@edi, a3@esi).
//   The "go drink" action initializer.
//   1. rowIndex = *(u16*)(record+8); sub = *(int*)(record+171) >> 24.
//   2. If the per-NPC stat byte statTable[rowIndex,sub] is already >= 252.0, the NPC
//      is "full"; free the handler entry and bail (returns freeHandlerEntry code).
//   3. Otherwise scan the handler pool for another type-96 handler on the same
//      rowIndex (filter 2,2,rowIndex,0,96). Skipping the handler that IS `record`,
//      if any *other* such handler exists, free this entry and bail (one drinker at
//      a time per row). If the only match is `record` itself (or none), proceed.
//   4. duration = 2*(int)(statByte*0.0238095... + 1.0) + 4  -> stored at record+173.
//      action-type byte record+172 = 21; state record+112 = 0.
//   5. Stamp the appointment clock (record+82) and advance it: real-time mode
//      (h=0,m=1) else (h=5,m=0). Mirror the clock to the saved-clock slot (record+68).
//   Returns the GameTimeAdvance result (matches the original's eax). When the early
//   gates fire, returns the freeHandlerEntry result instead.
i32 CharRecon2DrinkInit(HeRecord* h);

} // namespace guild::sim
