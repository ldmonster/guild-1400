#pragma once
// =====================================================================================
// office_recon2_rules.h — remaining Office (Amt) / Guild / BuildingType / Privilege
// rule-and-table logic, reconstructed 1:1 from gilde.exe (Hex-Rays reference of record).
//
// This module covers the *pure decision logic* of a cluster of office/guild/privilege
// functions whose original forms are heavily fused with the live engine (the 268-WORD
// entity array word_12CE910, the 134-DWORD parallel array dword_12CE919, the HUD frame
// loop VIBE_GameLogic_RunFrameLoop, the lockstep command queue VIBE_Command_*, the rich
// text renderer, and the 3D character/cutscene system). Per the project rules:
//
//   * The genuinely portable logic (eligibility gates, selection/min-pick math, table
//     strides, fallback tables, layout arithmetic, integer wraparound, branch order)
//     is reconstructed here EXACTLY, operating on abstracted inputs.
//   * The coupled leaves (entity arrays, HUD object creation, command emission, 3D
//     actor spawn, network packet status polling) are surfaced as caller-supplied
//     hooks with inert defaults — never faked with an analogue (rule 8). Where a
//     function is *entirely* a GUI frame-loop with no separable rule, it is documented
//     as deferred rather than half-translated.
//
// All symbols live in a unique translation unit (office_recon2_*) and reuse existing
// records via include; nothing here redefines an existing global/record (ODR-safe).
//
// Provenance addresses (gilde.exe, imagebase 0x400000):
//   0x57c1e8  VIBE_Office_ResolveStaffModel            (model-selection decision logic) — RECON
//   0x47e6c4  VIBE_Office_AddEntryDefault              (thin forwarder)                 — RECON
//   0x49da18  VIBE_Office_SpawnSessionActor            (3D actor; model-name fallback)  — RECON(rule)+HOOK
//   0x51fc50  VIBE_Office_ShowCandidacyDialog          (HUD loop; collect+gate+layout)  — RECON(rules)+HOOK
//   0x4a03f4  VIBE_Office_PrepareSuccessorChoiceA      (successor eligibility gate)     — RECON(gate)+HOOK
//   0x4a04f4  VIBE_Office_PrepareSuccessorChoiceB      (successor eligibility gate)     — RECON(gate)+HOOK
//   0x5210e4  VIBE_Guild_ShowLevel3OfficeDialogB       (HUD loop; text-id selection)    — RECON(rule)+HOOK
//   0x521234  VIBE_Guild_ShowLevel3OfficeDialogC       (HUD loop; text-id selection)    — RECON(rule)+HOOK
//   0x52149c  VIBE_Guild_RunLevel2ContactLoop          (HUD frame loop)                 — RECON(struct)+HOOK
//   0x56499c  VIBE_Privilege_BuildOfficeMemberTable    (entity dispatch; wealth pick)   — RECON(rules)+HOOK
//   0x5564ac/0x5566dc/0x556858/0x556a2c  candidate-list builders                        — DEFERRED (pure HUD)
//   0x589a32..0x589a44  VIBE_BuildingType_ReturnCodeNN (3-byte thunks)                  — SKIPPED (size<12)
// =====================================================================================
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// =====================================================================================
// 0x57c1e8 — VIBE_Office_ResolveStaffModel  (__usercall, eax = entityIndex@<eax>)
// -------------------------------------------------------------------------------------
// Picks the staff 3D-model record for an office building, by branching on the building's
// staffing state. In the original, the operands come from the parallel global arrays:
//   word_12CE910[268*idx]                  base entity record (stride 268 WORD = 536 B)
//   byte_12CE912[536*idx]                  buildingType byte         (entity+0x02)
//   byte_12CEA74[536*idx]                  staffFlagA  (entity+0x164)
//   byte_12CEA75[536*idx]                  staffFlagB  (entity+0x165)
//   byte_12CEB00[536*idx]                  staffFlagC  (entity+0x1F0)
//   dword_12CE919[134*idx] (LOBYTE)        genderByte  (entity+0x09)
//   *(u16*)((char*)&dword_12CE919+1)[..]   curStaffCount (entity+0x0A, u16)
//   flt_12CE930[134*idx]                   staffThreshold (float, entity+0x20)
//   dword_12CEA9C[134*idx] (LOBYTE)        roleId       (entity+0x18C / +0x18D)  (==-1 sentinel)
//   (unk_12CEA71>>24)[134*idx]             selectorA   (entity+0x161 high byte)
//   (unk_12CEA72>>24)[134*idx]             selectorB   (entity+0x162 high byte)
// and the candidate model tables are 40-byte-stride record arrays:
//   unk_63E338 / unk_63EF18  ("female"/"male" full staff list, scan cap 76)
//   unk_63DAC8 / unk_63DF00  ("female"/"male" short list,      scan cap 27)
//   unk_6405E8               (buildingType==17 special, 3 random entries)
//   unk_63DAA0 / unk_63DA78  (under-threshold default models)
// Each model record's first DWORD is the role key compared against selectorA/B.
//
// The decision STRUCTURE (branch order, scan caps 76/27, the round-robin name-copy
// branch, the under-threshold fallback, the not-found tail) is the faithful logic and
// is reconstructed verbatim. The model tables and the entity arrays are *data the
// engine owns*; we surface them through StaffModelInputs so the rule is testable and
// reusable without fabricating game state.
// =====================================================================================

// One staff-model candidate record (the original's 40-byte rows; only the leading role
// key participates in the decision — the rest is name/mesh data the caller carries).
struct StaffModelRecord {
    i32 roleKey;   // +0x00 — compared against selectorA/B (record empty when 0)
    // (original rows continue with a name/mesh blob at +4; opaque to the rule)
};

// A model table = a pointer to the first record + its scan cap (76 or 27 in the original).
struct StaffModelTable {
    const StaffModelRecord* records = nullptr; // null => treated as the original's v2==0
    int                     cap     = 0;       // 76 (full) or 27 (short)
};

// Abstracted per-building inputs (mirrors the 12CExxxx parallel arrays for one index).
struct StaffModelInputs {
    // --- flags & counters read by the gate ---
    u8     staffFlagA   = 0;     // byte_12CEA74
    u8     staffFlagB   = 0;     // byte_12CEA75
    u8     staffFlagC   = 0;     // byte_12CEB00
    u8     buildingType = 0;     // byte_12CE912
    u8     genderByte   = 0;     // LOBYTE(dword_12CE919): 0,1, or other
    u16    curStaffCount = 0;    // (u16) at entity+0x0A
    float  staffThreshold = 0.f; // flt_12CE930
    i32    roleIdField  = -1;    // dword_12CEA9C (LOBYTE used; ==-1 sentinel checked on full dword)
    i32    selectorA    = 0;     // (unk_12CEA71 >> 24) — high byte, sign-extended in original
    i32    selectorB    = 0;     // (unk_12CEA72 >> 24)

    // --- candidate tables (engine data) ---
    StaffModelTable fullFemale; // unk_63E338  (cap 76)  — staffFlagA, gender 0
    StaffModelTable fullMale;   // unk_63EF18  (cap 76)  — staffFlagA, gender 1
    StaffModelTable shortFemale;// unk_63DAC8  (cap 27)  — staffFlagB, gender 0
    StaffModelTable shortMale;  // unk_63DF00  (cap 27)  — staffFlagB, gender 1
    StaffModelTable type17;     // unk_6405E8  (3 rows)  — buildingType==17

    // under-threshold default model rows (single records)
    const StaffModelRecord* underThreshFemale = nullptr; // unk_63DA78 (gender 0)
    const StaffModelRecord* underThreshMale   = nullptr; // unk_63DAA0 (gender 1+)
};

// Result of the decision. The original returns a char* into a model table (or into the
// round-robin scratch buffer unk_1234610). We classify the outcome so callers can route
// to the correct table/scratch slot without us inventing the scratch buffer contents.
struct StaffModelResult {
    enum Kind {
        kNone,            // returned 0 (no table / not selected)            — original: result==0
        kUnderThreshFemale,// unk_63DA78
        kUnderThreshMale,  // unk_63DAA0
        kRoundRobinName,   // unk_1234610 + 40*idx (custom name copied in)   — see roundRobinSlot
        kType17,           // unk_6405E8 + 40*rand(3)                        — see randIndex
        kTableRecord,      // a record inside one of the model tables         — see record
        kFallbackByGender, // tail: gender 0 -> shortFemale base, 1 -> shortMale base, else v2
    };
    Kind                    kind = kNone;
    const StaffModelRecord* record = nullptr; // kTableRecord
    int                     randIndex = 0;     // kType17: index 0..2 (caller draws rand%3)
    int                     roundRobinSlot = 0;// kRoundRobinName: (prev+1)%8
};

// Random source the caller supplies (original: VIBE_Math_RandomModulo(n) for kType17).
// Defaulting to 0 keeps the rule deterministic in tests; the live caller passes the RNG.
using StaffRandFn = int (*)(unsigned modulus, void* user);

// The 1:1 decision. `roundRobinPrev` is the original's dword_641FE8 (mutated in place,
// (prev+1)%8) when the round-robin branch is taken.
StaffModelResult OfficeResolveStaffModel(const StaffModelInputs& in,
                                         int* roundRobinPrev = nullptr,
                                         StaffRandFn rng = nullptr,
                                         void* rngUser = nullptr);

// =====================================================================================
// 0x47e6c4 — VIBE_Office_AddEntryDefault  (__usercall, eax = AddTableEntry(al,0,3,0,255))
// -------------------------------------------------------------------------------------
// Thin forwarder: install a vacancy entry (officeType 3, null primary, flag 255) for the
// holder key. The original tail-calls VIBE_Office_AddTableEntry (0x47e750), already
// reconstructed as OfficeAddTableEntry in office_assign.cpp. We forward through a hook so
// this TU stays self-contained and ODR-safe (no redefinition of OfficeAddTableEntry).
// =====================================================================================
struct OfficeAddTableEntryHook {
    // mirrors OfficeAddTableEntry(u8 holderKey, primary, officeType, succ, flag) -> id
    int (*fn)(u8 holderKey, int primaryId, int officeType, int succId, int flag, void* user) = nullptr;
    void* user = nullptr;
};
int OfficeAddEntryDefault(u8 holderKey, const OfficeAddTableEntryHook& hook);

// =====================================================================================
// 0x49da18 — VIBE_Office_SpawnSessionActor  (model-name fallback rule)
// -------------------------------------------------------------------------------------
// Spawns the 3D session actor for an office scene. The 3D object lookup, bone-chain
// transform, character creation and slot-name copy are all engine/3D leaves (deferred to
// hooks). The portable RULE is the model-name selection:
//   model = ResolveStaffModel(entity) ? that_model_name
//         : switch (rand%3) { 1 -> "buerger_MANN"; 2 -> "buerger2_MANN"; else -> "handwerker3_MANN" }
// We expose just that selection.
// =====================================================================================
// Returns the fallback model name when no staff model resolves (rand in {0,1,2}).
const char* OfficeSpawnActorFallbackModel(int rand3);

// =====================================================================================
// 0x51fc50 — VIBE_Office_ShowCandidacyDialog  (candidate collection + gate + layout)
// -------------------------------------------------------------------------------------
// A HUD frame-loop dialog. The portable RULES extracted:
//
// (1) Candidate collection scan: over entity array (idx 0..767), include records whose
//     office-type byte (+360) == officeKey, stop after 16 candidates (v7<16, 4 bytes
//     each). Returns count. (Original: word_12CE910 loop at 0x51fcbf.)
//
// (2) "Apply" button enable gate (0x51ff25/0x51ffc2):
//        enabled = !(count >= 4 || holderHasOffice)   [holderHasOffice == person+360 != 0]
//
// (3) Three-column person-card X layout (0x51fe39):
//        usable = (formW>>16) - 2*(margin>>16)
//        colW   = usable / 3 ; extra = (usable % 3) / 2
//        x[i]   = extra + colW*(i%2 + 1) + 10*(i%2 - 1) + (margin>>16)*(i%2)
//        y[i]   = 130*(i/2) + 40
//     (i%2, i/2 integer arithmetic preserved.)
//
// The frame loop, command queue (VIBE_Office_ApplyForCandidacy / packet status poll),
// form construction and rich-text are deferred to the caller.
// =====================================================================================
int  OfficeCandidacyCountCandidates(const u8* officeTypeByOf, // [768] entity+360 bytes
                                    int entityCount,           // 768 in original
                                    u8 officeKey);
bool OfficeCandidacyApplyEnabled(int candidateCount, bool holderHasOffice);
void OfficeCandidacyCardLayout(int index, int formWidthRaw, int marginRaw,
                               int* outX, int* outY);

// =====================================================================================
// 0x4a03f4 / 0x4a04f4 — VIBE_Office_PrepareSuccessorChoice A / B  (eligibility gate)
// -------------------------------------------------------------------------------------
// Both share the identical gate; they differ only in which dialog/AI builder fires
// (A: BuildSuccessorDialogA/B ; B: AiPlayer_EvaluateApproachDirection / PickBestTarget).
// The portable gate (0x4a03fc..0x4a0464):
//   if (entity+156 != 0)              -> abort (already resolved)
//   holder = FindRecordById(entity+16); if !holder -> abort
//   if !(holder+358 set) || (holder+433 set) -> abort   [must hold office, not blocked]
//   dispatch on slot-type byte (slot+0):
//        ==1 : two-person path  (resolve slot+4 and slot+8; both must be valid)
//        ==2 : category path    (IsNextRankInCategory(holder, slot+2) must be true)
//        else: no successor dialog
// We expose the gate decision (which branch fires, or none).
// =====================================================================================
enum class SuccessorChoice {
    kAbort,        // any precondition failed
    kTwoPerson,    // slot-type 1, both persons valid  -> A: DialogA / B: ApproachDirection
    kCategory,     // slot-type 2, next-rank-in-category true -> A: DialogB / B: PickBestTarget
};
struct SuccessorGateInputs {
    bool entitySlot156Zero;   // entity+156 == 0  (not yet resolved)
    bool holderValid;         // FindRecordById(entity+16) != 0
    bool holderHoldsOffice;   // holder+358 != 0
    bool holderBlocked;       // holder+433 != 0
    u8   slotType;            // slot+0
    bool twoPersonsValid;     // slot-type 1: persons at slot+4 and slot+8 both resolve
    bool nextRankInCategory;  // slot-type 2: IsNextRankInCategory(holder, slot+2)
};
SuccessorChoice OfficePrepareSuccessorChoice(const SuccessorGateInputs& in);

// =====================================================================================
// 0x5210e4 / 0x521234 — VIBE_Guild_ShowLevel3OfficeDialog B / C  (text-id selection)
// -------------------------------------------------------------------------------------
// HUD frame-loop dialogs (form build / rich-text / action dispatch deferred). The
// portable RULE is the body text-id (0x52116a / 0x5212ba):
//   base = (genderByteLow ? 560 : 525) + officeNameByte
// (officeNameByte == byte_12CEA79[536*guildEntity]; genderByteLow == LOBYTE(dword_12CE919)).
// Dialogs B and C differ only in their static rich-string title/body resource ids
// (B: 0x127D/0x1280 ; C: 0x1287/0x128A), which are GUI data, not rule.
// =====================================================================================
int GuildLevel3DialogBodyTextId(bool genderByteLow, u8 officeNameByte);

// =====================================================================================
// 0x56499c — VIBE_Privilege_BuildOfficeMemberTable  (wealth selection rule)
// -------------------------------------------------------------------------------------
// A privilege/"miracle" panel action dispatched on a sub-action code 0..5. The body is
// almost entirely engine side-effect (entity search, command queue, text broadcast),
// deferred to the caller. Two portable pieces are reconstructed:
//
// (1) Case-0 / case-5 amount formula. The original computes a donation/payout amount as:
//        amount = (int)( (double)mult * ((double)wealth * 0.01) )      [VIBE_Coord_ConvertX
//                                                                       truncates to int]
//     case 0: mult = rand%3 + 2     (factor dbl_624D64 == 0.01)
//     case 5: mult = rand%5 + 3     (factor flt_624D6C == 0.01f)
//
// (2) Case-5 richest-of-candidates pick: among up to 8 collected member records
//     (those with type byte in {5,6,7}, capped at 8 = 32 bytes/4), pick the one whose
//     VIBE_Person_ComputeTotalWealth is MINIMUM (the original keeps v64=min, v24=ptr).
//     [Yes — it selects the LEAST wealthy; reproduced exactly.]
// =====================================================================================

// Case-0 amount: mult = rand3 + 2 ; amount = trunc(mult * wealth * 0.01)
i32 PrivilegeMiracleAmountCase0(int rand3, i32 wealth); // rand3 = VIBE_Math_RandomModulo(3)
// Case-5 amount: mult = rand5 + 3 ; amount = trunc(mult * wealth * 0.01)
i32 PrivilegeMiracleAmountCase5(int rand5, i32 wealth); // rand5 = VIBE_Math_RandomModulo(5)

// Case-5 candidate selection: returns index of the MINIMUM-wealth candidate (or -1 if
// count==0). `wealth[count]` are the precomputed ComputeTotalWealth values in scan order.
int PrivilegeMiraclePickLeastWealthy(const i32* wealth, int count);

// =====================================================================================
// 0x589a32..0x589a44 — VIBE_BuildingType_ReturnCodeNN
//   SKIPPED: 3-byte epilogue thunks (size < 12). Per buildingtype_recon.h / building.cpp
//   these are already documented as the building.cpp dispatch epilogue thunks.
// =====================================================================================

} // namespace guild::world
