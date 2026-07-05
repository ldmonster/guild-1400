#pragma once
// ---------------------------------------------------------------------------
// VIBE_MissionReq_Evaluate (gilde.exe 0x5398c4) — the central objective/goal
// requirement DISPATCHER for the Guild's mission system.
//
// This is the recon3 cluster's contribution: the one remaining piece of the
// VIBE_Mission_* / VIBE_MissionReq_* family that is *pure decision logic* and was
// not yet translated.  All of its individual leaf checkers (the stat counters,
// the timer accumulator, the object-count / building-equip / economy gates) are
// already translated elsewhere:
//   * world/mission_requirement.{h,cpp}             — the stat-based checkers
//   * world/mission_requirement_event_recon.{h,cpp} — the event/economy checkers
// and VIBE_Mission_FinishByOwner (0x539e48) is already world/mission_reward.cpp
// (guild::world::MissionFinishByOwner).  This module reconstructs ONLY the
// dispatcher, which is the part that decides — given an objective record — which
// requirement-table row applies and which leaf check to run.
//
// FAITHFULNESS / COUPLING (rules 1, 3, 8, 13):
//   The dispatcher fans out to ~20 distinct leaves spanning the Person/family,
//   Building-type, Money/currency and Economy clusters, several reached through
//   register-arg (__usercall) calling conventions.  Wiring it directly to every
//   owning cluster from a single headless function would drag in the whole game
//   state.  Following the cluster rule, every coupled leaf is exposed as a
//   function-pointer hook with an INERT default (returns 0 / null), while the
//   *dispatch control flow itself* — the requirement-row scan, the master gate,
//   the special short-circuit, the 49-case selector and the exact comparison
//   arithmetic per case — is translated 1:1 from the disassembly.  Production
//   wiring installs the real leaves (see MissionReq3InstallProductionHooks notes
//   at the call site); golden tests install stub leaves to drive each branch.
//
// Provenance / constants recovered via IDA get_bytes:
//   flt_623D68 = 0x3C23D70A = 0.01f   (case 47 threshold scale, 1/100)
//   byte_6477A1                        display-currency index (g_displayCurrency)
//   byte_63CC40                        master "requirements enabled" gate
//   dword_63CD44                       one-shot "special objective met" flag
//   byte_63C8F4 == g_missionSlotMode   slot mode (signed compare 0..5 here)
// ---------------------------------------------------------------------------
#include "guild/common/types.h"
#include "world/mission_requirement.h"  // ObjectiveRecord, ReqTableRow, MemberCount

namespace guild::world {

// ---------------------------------------------------------------------------
// Requirement-table globals (gilde.exe byte_63CD4C / dword_5383F0).  The original
// reuses the same stride-24 table the event picker walks (modelled as
// g_eventTable in world/mission.cpp).  For a self-contained, golden-testable
// dispatcher this module keeps its own view: a pointer + count installed by the
// host.  In production this points at the live requirement table.
// ---------------------------------------------------------------------------
void MissionReq3SetTable(const ReqTableRow* rows, int count);  // byte_63CD4C / dword_5383F0

// ---------------------------------------------------------------------------
// Dispatcher gate globals (recovered).  Exposed so tests / host can set them.
// ---------------------------------------------------------------------------
extern u8  g_missionReqEnabled;       // byte_63CC40 — master enable gate
extern i32 g_missionReqSpecialFlag;   // dword_63CD44 — one-shot "special met" flag
extern u8  g_missionReqDisplayCcy;    // byte_6477A1 — display-currency index
// NOTE: byte_63C8F4 is g_missionSlotMode (declared in world/mission.h) and is
// reused, not redefined.  The dispatcher compares it as a SIGNED char (0..5).

// ---------------------------------------------------------------------------
// Coupled-leaf hooks (inert defaults).  Each mirrors the exact signature /
// semantics of the original __usercall leaf as invoked by the dispatcher.
// ---------------------------------------------------------------------------
struct MissionReq3Hooks {
    // --- person / family resolution ---------------------------------------
    // VIBE_Person_FindRecordById(id) -> opaque person record (or null).
    const u8* (*personFindRecordById)(i32 id) = nullptr;
    // VIBE_Person_GetFamilyRecord(person) -> family record dwords (or null).
    // Caller reads dwords at +0x2C/+0x30/+0x34/+0x38/+0x3C ([11][12][13][14][15]).
    const i32* (*personGetFamilyRecord)(const u8* person) = nullptr;
    // person[+0x161] top signed byte (building-type code) for cases 2/7.
    i32 (*personBuildingTypeCode)(const u8* person) = nullptr;
    // person[+0x166] state byte for cases 15-18/34/35.
    u8 (*personStateByte)(const u8* person) = nullptr;
    // *(u16*)person for cases 5/13/21/30 (the wealth marker word).
    u16 (*personMarkerWord)(const u8* person) = nullptr;

    // --- money / building / wealth / economy ------------------------------
    // VIBE_Money_ConvertToDisplayCoord(amount, currencyIdx).
    i32 (*moneyConvertToDisplayCoord)(i32 amount, u8 currencyIdx) = nullptr;
    // VIBE_BuildingType_ComputeRankWithinGroup(code).
    i32 (*buildingTypeComputeRank)(u8 code) = nullptr;
    // VIBE_Person_ComputeTotalWealth(markerWord, objectiveRec).
    i32 (*personComputeTotalWealth)(u16 marker, const u8* objectiveRec) = nullptr;
    // VIBE_Person_GetCurrencyAmount(person, currencyIdx).
    i32 (*personGetCurrencyAmount)(const u8* person, u8 currencyIdx) = nullptr;
    // VIBE_Economy_ComputeWeightedLawScore(person, lawIdx) -> double.
    double (*economyComputeWeightedLawScore)(const u8* person, u8 lawIdx) = nullptr;

    // --- already-translated MissionReq leaf checkers ----------------------
    // Routed through hooks so this dispatcher stays self-contained for golden
    // tests; production installs the real world/mission_requirement* impls.
    bool (*checkStatThreshold)(const u8* person, const ReqTableRow* row) = nullptr;       // 0x539160
    bool (*checkObjectCount)(const u8* person, const ReqTableRow* row,
                             const ObjectiveRecord* obj) = nullptr;                        // 0x5391d8
    bool (*checkBuildingEquip)(const u8* buildingTypeRec) = nullptr;                       // 0x539230
    bool (*checkGuildMemberCount)(const u8* person, const ObjectiveRecord* obj,
                                  const ReqTableRow* row) = nullptr;                       // 0x5394d4
    bool (*checkOwnPersonRatio)(const u8* person, const ReqTableRow* row) = nullptr;       // 0x539380
    bool (*checkMultiStat)(const ObjectiveRecord* obj) = nullptr;                          // 0x5393ec
    bool (*checkStatCombo)(const ObjectiveRecord* obj) = nullptr;                          // 0x53945c
    bool (*checkCumulativeStats)(const u8* person, const ReqTableRow* row) = nullptr;      // 0x539534
    // 0x53963c takes edx=objective record (AccumulateTimer target), ebx=row
    // (dispatcher call site 0x539bb9: mov edx,esi(objective); ebx=row).
    bool (*checkMemberStats)(ObjectiveRecord* obj, const ReqTableRow* row) = nullptr;      // 0x53963c
    bool (*checkMinThresholds)(const u8* person) = nullptr;                                // 0x539778
    // 0x539728 takes edx=objective record (0x53972c mov ecx,edx feeds the inner
    // AccumulateTimer eax), ebx=row (call site 0x539bd9: mov edx,esi(objective)).
    bool (*checkTimeElapsed)(ObjectiveRecord* obj, const ReqTableRow* row) = nullptr;      // 0x539728
    bool (*checkNoActiveCombat)(const ObjectiveRecord* obj) = nullptr;                     // 0x5397bc
    // VIBE_MissionReq_AccumulateTimer(obj, conditionMet, requiredMinutes).
    bool (*accumulateTimer)(ObjectiveRecord* obj, bool met, i32 requiredMinutes) = nullptr;// 0x539c44
    // VIBE_MissionReq_CountGuildMembers(state, out) — fills out, used by case 47.
    void (*countGuildMembers)(u8 state, MemberCount* out) = nullptr;                       // 0x539da0
};

// Process-wide hook table (all-null / inert by default).  Tests overwrite fields.
MissionReq3Hooks& MissionReq3GetHooks();

// gilde.exe 0x5398c4 — VIBE_MissionReq_Evaluate (__usercall: eax = objective).
// Returns true when the objective's requirement is satisfied (the original
// returns __int16* used as a 0/1 boolean; some leaf cases return a small pointer
// that the callers test for truth).  Faithful translation:
//   1. type = objective->type.
//   2. Scan the requirement table (stride 24) for the row whose [+0] == type.
//   3. If !g_missionReqEnabled -> false.
//   4. If no row -> false.
//   5. If g_missionReqSpecialFlag==1 AND (signed)g_missionSlotMode in [0,5]:
//        clear the flag, return true (one-shot special objective).
//   6. person = personFindRecordById(objective->personId); if null -> false.
//   7. switch(type): run the matching leaf / comparison, return its result.
//      Unknown type -> false.
bool MissionReqEvaluate(ObjectiveRecord* objective);

}  // namespace guild::world
