#pragma once
// NpcDaily — the per-turn DAILY-ROUTINE director for the Guild simulation
// (gilde.exe). This translates the last "big" per-NPC director:
//
//   VIBE_NpcAction_DailyRoutineStep (0x4e7e88, 0xcfd bytes) — the per-turn daily
//     director. It sweeps the whole 768-person array via the Person record's
//     PARALLEL COLUMNS (byte_12CEA74/75 @+356/+357, dword_12CEA7C @+364,
//     dword_12CEA80 @+368, dword_12CEA94 @+388, dword_12CEAD8 @+456 turn-bits)
//     and, keyed off the season (qword_13CE852.day % 4) and the time-of-day,
//     decides each person's activity for the round: go to work, go to the
//     wirtshaus (tavern), or go home — emitting the corresponding network
//     commands (RequestBuildOp77, RequestChrMoveToUniverse, QueueRequestString47,
//     QueueRequestNamedObject53, QueueRequestArgs25). It is a 2-state machine on
//     the He record's state @+112 (0 = morning work-dispatch, 1 = social/evening),
//     with the appointment (+82) re-armed via GameTime_Advance/GameTime_Set.
//
// Recovered SEASON TABLES (byte-for-byte via get_bytes; see kWorkStartHour /
// kWorkEndHour below): flt_6476FC = work-start hour by season {8,7,8,9}; the
// season-keyed compare uses flt_61F968 = -1.0 (work-start window slack) and
// flt_61F964 = +2.0 (evening window). flt_64770C = work-end / "go home" hour by
// season {20,21,20,19}.
//
// The 768-person column sweep, the per-person carry/interaction target search
// (VIBE_NpcAction_FindCarryTargetForChar 0x4e786c / FindInteractionTarget
// 0x4e79c0 / PickClosestByWeight 0x4e7c3c — all render/entity/bone-chain coupled),
// the building-type/production probes, the SumCurrencyHeld check, and the command
// emissions are routed through NpcDailyHooks (mirroring the NpcLeafHooks /
// NpcAction4Hooks pattern) so the director's RULES are exercisable in isolation.
// The control flow (state switch, per-person gating, season/time decisions, RNG
// draws, appointment re-arm) is translated 1:1 against the IDA disassembly.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Recovered season tables and window constants (get_bytes).
//   flt_6476FC @0x6476FC : work-START hour by season (season = day % 4).
//   flt_64770C @0x64770C : work-END / go-home hour by season.
//   flt_61F968 @0x61F968 : work-start window slack (-1.0).
//   flt_61F964 @0x61F964 : evening window offset (+2.0).
// ===========================================================================
extern const float kWorkStartHour[4];   // flt_6476FC = {8,7,8,9}
extern const float kWorkEndHour[4];      // flt_64770C = {20,21,20,19}
extern const float kWorkStartSlack;      // flt_61F968 = -1.0
extern const float kEveningOffset;       // flt_61F964 = +2.0
extern const double kPickWeightFactor;   // dbl_61F938 = 0.0125 (PickClosestByWeight)

// ===========================================================================
// Per-person daily-routine snapshot. The director sweeps the 768-person array
// reading the parallel columns; the host/test provides one row per person index
// via the hook PersonColumns(i). A row mirrors exactly the fields the original
// reads off the Person record (base + column offset):
//   activeA  : byte_12CEA74 (+356) — "scheduled A" flag
//   activeB  : byte_12CEA75 (+357) — profession/role flag (either nonzero = live)
//   homeBld  : dword_12CEA7C (+364) — home/work building ptr (0 = absent)
//   workBld  : dword_12CEA80 (+368) — work/production building ptr (0 = absent)
//   destBld  : dword_12CEA94 (+388) — current destination building ptr; its
//              +44 / +48 dwords are the dest universe/object ids the search
//              results are compared against (no-op if already there)
//   turnBits : dword_12CEAD8 (+456) — per-turn bitfield. VERIFIED against the
//              disasm (2026-06-11, IDA back online): every dispatch writeback in
//              0x4e7e88 targets BYTE 1 of the dword (`or byte ptr
//              ds:(dword_12CEAD8+1)[esi], 10h` @0x4e8184/0x4e82a5, `mov byte ptr
//              ds:(dword_12CEAD8+1)[..]` @0x4e8578/0x4e8767/0x4e89ab), i.e. bits
//              8..15: work-dispatch sets bit 0x1000, social sets bit 0x800 —
//              the SAME bits the entry gates test (`& 0x1000` / `& 0x800`). The
//              sweeps are therefore self-limiting: a dispatched person is gated
//              out of the rest of the round. (The pre-verification model used
//              0x100000/0x80000 — a BYTE1-vs-BYTE2 misread; fixed.) The bits are
//              cleared each round by BeginPlayerRound's per-NPC mask
//              dword_12CEAD8[i] &= 0xE0874703 (@0x5331b7: bits 11 and 12 are
//              NOT in the persist mask).
//   personId : dword_12CE914 (+4) — the id used in every command emission.
// ===========================================================================
struct DailyPersonRow {
    u8  activeA;     // byte_12CEA74 (+356)
    u8  activeB;     // byte_12CEA75 (+357)
    i32 homeBld;     // dword_12CEA7C (+364)
    i32 workBld;     // dword_12CEA80 (+368)
    i32 destBld;     // dword_12CEA94 (+388)
    u32 turnBits;    // dword_12CEAD8 (+456)
    i32 personId;    // dword_12CE914 (+4)
    bool valid;      // false => slot does not exist (treated as not-live)
};

// turnBits bit masks (disasm-verified: the BYTE1(..)|= writebacks land on the
// same bits the gates test — see the column table above).
enum DailyTurnBit : u32 {
    kDailySkip       = 0x1000,  // & 0x1000 gate — "already work-dispatched"
    kDailyDispWork   = 0x1000,  // BYTE1 |= 0x10 @0x4e8132/0x4e8184/0x4e82a5
    kDailyDispSocial = 0x800,   // BYTE1 |= 8    @0x4e8578/0x4e8767/0x4e89ab
    kDailySocialReq  = 0x800,   // & 0x800 gate — "already social-dispatched"
};

// The activity the director assigns to a person this round (the RULE outcome).
enum class DailyActivity {
    kNone,        // not dispatched (gated out / already at destination)
    kGoToWork,    // morning: dispatched to the work/production building
    kGoToTavern,  // evening: "Go to wirtshaus" (PickClosestByWeight tavern)
    kGoHome,      // evening: "Go home"
};

// ===========================================================================
// Leaf hooks. Tests install a synthetic provider; nullptr installs an inert
// default (no persons, every effect a no-op). These mirror the originals'
// cross-cluster leaf calls (entity columns, target searches, command emits).
// ===========================================================================
struct NpcDailyHooks {
    // --- person-array columns (the 768-person parallel-column sweep) ---
    // personCount(): number of live person slots to sweep (<= 768).
    int  (*personCount)();
    // personRow(i): the parallel-column snapshot for person index i (0..count-1).
    DailyPersonRow (*personRow)(int i);
    // setTurnBits(i, bits): write back the per-turn bitfield (+456) for person i.
    void (*setTurnBits)(int i, u32 bits);

    // --- per-person target searches (render/entity/bone-chain coupled) ---
    // findCarryTarget(i, &outU, &outObj): VIBE_NpcAction_FindCarryTargetForChar.
    //   Returns nonzero on found; writes the destination universe id + object id.
    int  (*findCarryTarget)(int i, i32* outUniverse, i32* outObj);
    // findInteractionTarget(i, &outU, &outObj): VIBE_NpcAction_FindInteractionTarget.
    int  (*findInteractionTarget)(int i, i32* outUniverse, i32* outObj);
    // destDoorIds(i, &out44, &out48): the dest building's +44/+48 dwords (the
    //   "already there" comparison targets). Returns false if no dest building.
    bool (*destDoorIds)(int i, i32* out44, i32* out48);

    // --- building / production probes ---
    // homeIsProduction(i): VIBE_Building_IsProductionType(homeBld).
    bool (*homeIsProduction)(int i);
    // homeHasMesh(i): homeBld->+97 (mesh/bone-root) nonzero.
    bool (*homeHasMesh)(int i);
    // ownerKind(i): byte_12CE912[536 * homeBld->+39] — the owner person's kind
    //   byte (6/7 == human-player-owned -> different dispatch branch).
    u8   (*ownerKind)(int i);
    // aiPlayerClass(i): *(byte*)(dword_13CE294 + 589 * *homeBld) — the faction/
    //   type-def class byte (4/16/19 == skip social dispatch).
    u8   (*aiPlayerClass)(int i);
    // workDistanceOk(i): the bone-chain distance probe between homeBld's scene
    //   node (+97, PointThroughBoneChain @0x5c8b38 over node +76) and the node of
    //   Building_FindById(charPtr(+388)->+44) — the char's CURRENT building. When
    //   either node is missing the distance stays 0.0 (passes). DISASM-VERIFIED
    //   gate @0x4e80b2: `cmp [esp+..var_40], 45DAC000h; jge skip` — i.e.
    //   distance < 7000.0f (0x45DAC000), int-compare of the positive float bits.
    //   (The pre-verification "~960 / 1171963904" note misdecoded the constant;
    //   1171963904 == 0x45DAC000 == 7000.0f.) Returns true if within range.
    bool (*workDistanceOk)(int i);
    // characterBudget(): VIBE_Character_CountByOwner(0,0) < 32 (the live-actor cap).
    bool (*characterBudgetOk)();

    // --- social-target helpers ---
    // currencyHeld(i): VIBE_Person_SumCurrencyHeld(person i) (> 3200 -> tavern eligible).
    i32  (*currencyHeld)(int i);
    // pickTavern(i, &outU, &outObj): VIBE_NpcAction_PickClosestByWeight over the
    //   candidate list. Returns nonzero if a tavern was chosen.
    int  (*pickTavern)(int i, i32* outUniverse, i32* outObj);
    // candidateCount(): the number of nearby owned persons (v21) gathered by the
    //   Person_QueryBegin(.,1,5,22) pass at state-1 entry; >0 enables the tavern roll.
    int  (*candidateCount)();

    // --- command emissions ---
    void (*requestBuildOp77)(i32 personId);
    void (*requestChrMoveToUniverse)(i32 personId, i32 universe, i32 obj, const char* tag);
    void (*queueRequestString47)(i32 personId, i32 universe, i32 obj);
    void (*queueRequestNamedObject53)(i32 personId, i32 universe, int a, i32 obj,
                                      int flag, const char* name);
    void (*queueRequestArgs25)(i32 personId, i32 fieldOffset, int a, int b, int value);

    // --- global pause gate ---
    // pauseFlag80(): (word_63C740 & 0x80) != 0 — checked at state-1 entry
    // (decompile 0x4e7e88: `if ((word_63C740 & 0x80u) != 0) goto LABEL_2`),
    // BEFORE the saved-day compare; nonzero frees the handler entry.
    bool (*pauseFlag80)();

    // --- free / completion ---
    i32  (*freeHandlerEntry)(HeRecord* h);
};

void SetNpcDailyHooks(const NpcDailyHooks* hooks);
const NpcDailyHooks& GetNpcDailyHooks();

// ===========================================================================
// gilde.exe 0x4e7e88 — VIBE_NpcAction_DailyRoutineStep(h@eax, queryCtx@esi).
//   season = clock.day % 4 (VIBE_GameTime_GetSeasonFromDay 0x58339c).
//   state @+112: -2/-1 -> free; 0 -> morning work-dispatch sweep; 1 -> social /
//   evening sweep; default -> noop. Re-arms the appointment (+82) and advances
//   state. Returns the He record (or the free result).
HeRecord* NpcDaily_DailyRoutineStep(HeRecord* h);

// ---------------------------------------------------------------------------
// The pure RULE (no commands / no record mutation) used by tests and by the
// director above: given season + hour + person row, what activity is selected?
// This isolates the social/work/sleep schedule decision so it can be golden-
// tested directly. `state` is the He state (0 morning, 1 evening), `hour` is the
// clock hour (WORD2(qword_13CE852)).
// ---------------------------------------------------------------------------
DailyActivity SelectDailyActivity(int state, int season, int hour,
                                  const DailyPersonRow& row, bool tavernEligible);

// VIBE_GameTime_GetSeasonFromDay @0x58339c — 1:1.
//   disasm: idiv ecx(=4) ; mov al, dl  -> returns the SIGNED remainder day % 4,
//   truncated to a char (al). For day>=0 this is 0..3; for a negative day it is a
//   negative season (-3..-1) — and the caller (DailyRoutineStep @0x4e80c3:
//   `mov eax,[var]; sar eax,18h; fld flt_6476FC[eax*4]`) SIGN-EXTENDS that char and
//   indexes the 4-entry season tables with it, so a negative day genuinely indexes
//   the table negatively in the original. We reproduce that exactly: char-truncate
//   here, and index the season tables with the raw signed value (no `& 3` mask).
inline int SeasonFromDay(i32 day) {
    return static_cast<int>(static_cast<signed char>(day % 4));
}

} // namespace guild::sim
