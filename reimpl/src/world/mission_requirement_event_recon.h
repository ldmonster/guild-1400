#pragma once
// ---------------------------------------------------------------------------
// Mission requirement / objective-goal evaluators — second leaf cluster.
//
// Faithful 1:1 port of nine VIBE_MissionReq_* "is this goal met?" checkers from
// gilde.exe that were NOT covered by world/mission_requirement.{h,cpp}. These are
// the pure trigger/requirement *decision* routines: given an objective record and
// a requirement-table row, they answer true/false (some gated through the shared
// hold-timer).  See world/mission_requirement.h for the already-translated
// ObjectiveRecord / ReqTableRow / MissionReqAccumulateTimer that this module
// reuses (never redefines).
//
// Translated functions (gilde.exe addresses):
//   VIBE_MissionReq_CheckBloodLevel      0x539138 — owner's coin wealth >= goal.
//   VIBE_MissionReq_CheckObjectCount     0x5391d8 — owns a matching object held
//                                                   long enough.
//   VIBE_MissionReq_CheckBuildingEquip   0x539230 — >=3 owned buildings of a type
//                                                   are fully equipped.
//   VIBE_MissionReq_CheckSkillAbove      0x5392f0 — family skill sum > goal.
//   VIBE_MissionReq_CheckGuildMemberCount0x5394d4 — clan size gate (timer) AND
//                                                   >=3 guild members of state 7.
//   VIBE_MissionReq_CheckZeroValue       0x539708 — weighted law score == 0.
//   VIBE_MissionReq_CheckTimeElapsed     0x539728 — economy index >= goal, held.
//   VIBE_MissionReq_CheckMinThresholds   0x539778 — economy demand floor gate.
//   VIBE_MissionReq_CheckNoActiveCombat  0x5397bc — no owned person is in combat.
//
// COUPLED LEAVES → injectable inert-default hooks.  The originals call into the
// Person/family iterator, the money/coordinate converter, the economy snapshot
// loader, the building-type mapper and the GameObject query — subsystems that
// live in other clusters and are not faithfully reachable from this leaf in a
// headless build.  Rather than fake them (rule 8), the coupled inputs are exposed
// as function-pointer hooks with inert defaults; the *pure decision arithmetic /
// control flow* (the part that IS the requirement spec) is translated exactly.
// Golden-vector tests install hooks to drive each branch.
//
// Provenance: every routine carries its gilde.exe address.  Constants recovered
// with get_bytes (flt_*, dbl_*, the 0.3f/0.1 economy floors).
// ---------------------------------------------------------------------------
#include "guild/common/types.h"
#include "world/mission_requirement.h"  // ObjectiveRecord, ReqTableRow, MemberCount,
                                        // MissionReqAccumulateTimer (reused)

namespace guild::world {

// ===========================================================================
// Injectable coupled-leaf hooks (inert defaults).  Each mirrors the exact
// signature/semantics the original leaf is invoked with.
// ===========================================================================
struct MissionReqEventHooks {
    // --- money / wealth -----------------------------------------------------
    // VIBE_Person_GetCurrencyAmount(person, currencyIdx) — coins of a kind.
    int (*personGetCurrencyAmount)(const void* person, u8 currencyIdx) = nullptr;
    // VIBE_Money_ConvertToDisplayCoord(amount, currencyIdx) — scaled display value.
    int (*moneyConvertToDisplayCoord)(int amount, u8 currencyIdx) = nullptr;
    // VIBE_Person_GetFamilyRecord(person) — pointer to the 14+-dword family rec,
    // or null.  Caller reads dwords [11] and [13] (byte +44 / +52).
    const i32* (*personGetFamilyRecord)(const void* person) = nullptr;

    // --- economy ------------------------------------------------------------
    // VIBE_Economy_ComputeWeightedLawScore(a1, a2) — weighted gesetz sum.
    double (*economyComputeWeightedLawScore)(int a1, int a2) = nullptr;
    // VIBE_Economy_LoadDemandSnapshot(out[10]) — fills 10 floats, returns out[9].
    // (Original copies 0x28 bytes then overwrites out[6] with flt_641DA8.)
    double (*economyLoadDemandSnapshot)(float* out) = nullptr;

    // --- person iteration (object-ownership / state queries) ----------------
    // Begin/Next over the live person store; the originals read raw byte fields
    // off each returned record.  Modeled as an opaque cursor.
    //   personQueryObjects   — VIBE_Person_QueryBegin(owner,2,4,id,5,kind)
    //   personQueryState     — VIBE_Person_QueryBegin(...,0,state)/(state,1,6)
    //   personIterNext       — VIBE_Person_IterNext()
    // Each returns an opaque record pointer (or null at end).
    const u8* (*personQueryOwnedObjects)(int owner) = nullptr;
    const u8* (*personQueryMemberState)(u8 state) = nullptr;
    const u8* (*personQueryAll)() = nullptr;
    const u8* (*personIterNext)() = nullptr;

    // --- building / object --------------------------------------------------
    // VIBE_BuildingType_MapToActionCode(buildingTypeRec) — action code, 0 = none.
    u8 (*buildingTypeMapToActionCode)(const void* buildingTypeRec) = nullptr;
    // VIBE_GameObject_QueryFind(objId,2,6,0,equipWord) — true if equip present.
    bool (*gameObjectQueryFind)(i32 objId, u16 equipWord) = nullptr;
    // byte_12CE912[536*idx] — the state byte of object record `idx` (combat==5).
    u8 (*objectStateByIndex)(u16 idx) = nullptr;
    // dword_13CE294 + 589*record[0] (+0x23 word) building-equip table lookups are
    // performed inside CheckBuildingEquip via the cursor's per-building accessor:
    //   buildingEquipWord(buildingRec, slot) -> equipment word (0 = end of list),
    //   buildingObjId(buildingRec)           -> object id used by gameObjectQueryFind.
    u16 (*buildingEquipWord)(const u8* buildingRec, int slot) = nullptr;
    i32 (*buildingObjId)(const u8* buildingRec) = nullptr;
};

// Process-wide hook table (all-null inert by default). Tests overwrite fields.
MissionReqEventHooks& MissionReqEventGetHooks();

// ===========================================================================
// gilde.exe 0x539138 — VIBE_MissionReq_CheckBloodLevel
//   (__usercall: edx=row, eax=objectiveOwner).
// Owner's coin wealth (currency byte_6477A1==0), converted to display value, must
// reach the row threshold (row+0x10).
//   return MoneyConvertToDisplayCoord(GetCurrencyAmount(owner,0),0) >= row->threshold
// ===========================================================================
bool MissionReqCheckBloodLevel(const ReqTableRow* row, const void* objectiveOwner);

// gilde.exe 0x5391d8 — VIBE_MissionReq_CheckObjectCount
//   (__usercall: eax=queryArgPtr, edx=row, esi=owner).
// Iterates the owner's objects of a queried kind; the goal is met when ANY held
// object is "alive" (record[0] >= 5), matches the target id (record+101 == arg+4),
// AND has been owned long enough (DiffMinutes(record+105, now) > row+0x14).
// (Object age/iteration is provided by the cursor hook; the comparison arithmetic
//  is translated exactly.)
bool MissionReqCheckObjectCount(const u16* queryArg, const ReqTableRow* row,
                                int owner);

// gilde.exe 0x539230 — VIBE_MissionReq_CheckBuildingEquip
//   (__usercall: eax=buildingTypeRec).
// Maps the building type to an action code (0 => fail), then iterates the owner's
// buildings of that type; a building is "fully equipped" (v6=1) unless any of its
// up-to-64 required equipment words (building+35, +2 each, high-bit masked) is
// missing from the object store.  Goal met when >=3 buildings are fully equipped.
bool MissionReqCheckBuildingEquip(const void* buildingTypeRec);

// gilde.exe 0x5392f0 — VIBE_MissionReq_CheckSkillAbove
//   (__usercall: edx=row, eax=person).
// rec = GetFamilyRecord(person); if null => false. Otherwise
//   MoneyConvertToDisplayCoord(rec[11]+rec[13], 0) > row->threshold.
bool MissionReqCheckSkillAbove(const ReqTableRow* row, const void* person);

// gilde.exe 0x5394d4 — VIBE_MissionReq_CheckGuildMemberCount
//   (__usercall: eax=person, edx=record(objective), ebx=row).
// Two-stage:
//   1. clanSize (PERSON+13, unsigned byte) >= row->threshold, gated through
//      MissionReqAccumulateTimer(objective, met, row->timerMin) — note the
//      clan-size byte and the hold timer live in DIFFERENT records;
//   2. if the timer says met, count guild members of state 7 and require >=3.
bool MissionReqCheckGuildMemberCount(const u8* person, ObjectiveRecord* objective,
                                     const ReqTableRow* row);

// gilde.exe 0x539708 — VIBE_MissionReq_CheckZeroValue (fastcall a1,a2).
// True iff the weighted law score is exactly 0.0.
bool MissionReqCheckZeroValue(int a1, int a2);

// gilde.exe 0x539728 — VIBE_MissionReq_CheckTimeElapsed
//   (__usercall: edx=record, ebx=row).
// If the current day (low dword of g_sysGameTime) < 10 => false. Otherwise load
// the economy demand snapshot; met = row->threshold >= snapshot[6]; gate through
// MissionReqAccumulateTimer(record, met, row->timerMin).
bool MissionReqCheckTimeElapsed(ObjectiveRecord* record, const ReqTableRow* row,
                                i32 currentDay);

// gilde.exe 0x539778 — VIBE_MissionReq_CheckMinThresholds (thiscall).
// Economy demand floor: snapshot[9] <= 0.1 AND snapshot[3], snapshot[1],
// snapshot[2] are each >= 0.3f (compared as signed int bit patterns).
bool MissionReqCheckMinThresholds();

// gilde.exe 0x5397bc — VIBE_MissionReq_CheckNoActiveCombat (esi=owner).
// Iterate the owner's persons; if any person's linked object (member+39 word !=
// 0xFFFF) is in combat state (g_objects[idx].state == 5) => false; else true.
bool MissionReqCheckNoActiveCombat(int owner);

}  // namespace guild::world
