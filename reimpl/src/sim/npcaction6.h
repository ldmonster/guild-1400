#pragma once
// NpcAction6 — the "AI debug-command" leaf family of the NpcAction dispatch table
// (gilde.exe, VIBE_NpcAction_*Cmd @0x575414..0x576618, plus the wander-pair
// notifier @0x5694c0). These are the table entries 0x2F..0x44 reached by
// VIBE_NpcAction_Dispatch (npcaction.h): each receives the He/handler record in
// eax, resolves the record's person id (record[+0]) to a Person record, gates on
// a stat byte, rolls the shared CRT RNG (VIBE_Math_RandomModulo) to derive a
// loyalty/salary/relation delta, then emits a build/queue command plus a localized
// status message.
//
// The deterministic core of each function — the RNG roll sequence, the integer
// clamp (loyalty: min(ceiling, roll+base)), and the float wealth math
// (salary: (int)(wealth * (roll+1) * 0.01)) — is translated 1:1 and golden-tested
// bit-exact against the LCG. Every cross-module leaf (Person lookup, AiNeeds need
// picker, total-wealth, money rate, text render, He message send, command queue,
// season-of-clock) is routed through NpcAction6Hooks so the bodies run in
// isolation; a test installs a recording mock, nullptr installs an inert default.
//
// Translated functions (absolute addresses, imagebase 0x400000):
//   0x575da0 RaiseSalaryCmd        0x5760a8 LowerSalaryCmd
//   0x575eac IncreaseLoyaltyCmd    0x575fac DecreaseLoyaltyCmd
//   0x576258 MoveToObjectCmd       0x576198 ShowPositionCmd
//   0x576334 DialogCmdA            0x5763d0 DialogCmdB     0x57646c DialogCmdC
//   0x576508 MessageCmdA           0x576590 MessageCmdB    0x576618 MessageCmdC
//   0x5694c0 NotifyWanderPair
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Leaf hooks for the NpcAction6 command family.
// ---------------------------------------------------------------------------
// The originals call into the Person/AiNeeds/Text/He/Command/GameTime clusters.
// A Person record is treated as a raw byte buffer by these functions (they read
// recordById[+0] as a u16 person index, recordById[+4] as a dword entity id,
// and recordById[+128+kind] as a per-relation stat byte). The hook returns the
// record base as a byte pointer so tests can supply a small mock buffer.
// ===========================================================================
struct NpcAction6Hooks {
    // VIBE_Person_FindRecordById(id) -> record base (byte pointer), or nullptr.
    u8* (*findRecordById)(i32 id);
    // VIBE_Person_ComputeTotalWealth(personIndexWord, record) -> wealth (>=0).
    i32 (*computeTotalWealth)(u16 personIndex, u8* record);
    // VIBE_Money_MultiplyByRate(amount, currencyByte) -> rate-scaled amount.
    i32 (*moneyMultiplyByRate)(int amount, u8 currency);
    // VIBE_AiNeeds_PickRandomNeedAndClearGroup(record) -> nonzero if a need was
    //   picked (MoveToObjectCmd gate).
    int (*pickNeedAndClearGroup)(u8* record);
    // VIBE_AiNeeds_PickRandomNeedAndClearGroupB(record) -> DialogCmdA gate.
    int (*pickNeedAndClearGroupB)(u8* record);
    // VIBE_AiNeeds_PickRandomFlagFromFourA / _B(record) -> DialogCmdB / DialogCmdC.
    int (*pickFlagFromFourA)(u8* record);
    int (*pickFlagFromFourB)(u8* record);
    // VIBE_Command_RequestBuildOp93(id, kind, id2, amount) — relation/loyalty delta.
    void (*requestBuildOp93)(i32 id, int kind, i32 id2, u8 amount);
    // VIBE_Command_QueueRequest16(idA, idB, amount, currency) — salary/move payment.
    void (*queueRequest16)(i32 idA, i32 idB, i32 amount, u8 currency);
    // VIBE_Command_QueueRequestEntity29(arg, record) — re-arm an He entity request.
    i32 (*queueRequestEntity29)(int arg, HeRecord* h);
    // VIBE_He_SendEntityMessage(idA, idB, big, text, channel, flag) — status line.
    //   We collapse the formatted text to its textId for testability.
    void (*sendEntityMessage)(i32 idA, i32 idB, i32 textId);
    // VIBE_GameTime_GetSeasonFromYear(clock) -> season byte (0..3) for ShowPosition.
    int (*currentSeason)();
    // He scan: VIBE_He_FindFirstHandlerByFilter(1,0,53) / _FindNextMatchingHandler.
    //   Iterates live handler records of action-type 53. begin() resets the cursor
    //   and returns the first match (or nullptr); next() advances. NotifyWanderPair.
    HeRecord* (*wanderScanBegin)();
    HeRecord* (*wanderScanNext)();
    // VIBE_History_NotifyWanderPairEvent(record, peerPersonPtr) — log the pairing.
    void (*notifyWanderPairEvent)(HeRecord* h, void* peerPerson);
    // Resolve a Person array slot pointer for a given 0..767 index (the original
    //   computes &word_12CE910[268*index]); NotifyWanderPair passes it to the
    //   history leaf. Returns nullptr if unavailable.
    void* (*personSlotByIndex)(u16 index);
    // The currency byte the originals read from byte_6477A1 (active player's coin).
    u8 currencyByte;
};

void SetNpcAction6Hooks(const NpcAction6Hooks* hooks);
const NpcAction6Hooks& GetNpcAction6Hooks();

// Result codes (the dispatcher sentinels these functions return verbatim).
//   0    : success (command emitted)
//   1    : person record not found
//   1024 : precondition failed (gate/no target) — retry later
constexpr int kNpc6Ok      = 0;
constexpr int kNpc6NoActor = 1;
constexpr int kNpc6Retry   = 1024;

// gilde.exe 0x575eac — VIBE_NpcAction_IncreaseLoyaltyCmd(record@eax).
//   kind = RandomModulo(5);  rel = record[+128+kind];  room = 252 - rel;
//   if (room <= 0) -> 1024;  roll = RandomModulo(0x11)+8;  delta = min(room, roll);
//   RequestBuildOp93(record[+4], kind, kind, +delta); send message (textId base
//   + kind + 4810). Returns 0, or 1 (no actor) / 1024 (capped).
int NpcAction6_IncreaseLoyaltyCmd(HeRecord* h);

// gilde.exe 0x575fac — VIBE_NpcAction_DecreaseLoyaltyCmd(record@eax).
//   kind = RandomModulo(5);  rel = record[+128+kind];  if (rel == 0) -> 1024;
//   roll = RandomModulo(0xC)+6;  delta = min(rel, roll);
//   RequestBuildOp93(record[+4], kind, kind, -delta); send message. Returns 0/1/1024.
int NpcAction6_DecreaseLoyaltyCmd(HeRecord* h);

// gilde.exe 0x575da0 — VIBE_NpcAction_RaiseSalaryCmd(record@eax).
//   if (RandomModulo(0x100) > record[+130]) -> 1024;  mult = RandomModulo(3)+1;
//   amount = (int)(ComputeTotalWealth(record[+0]) * mult * 0.01);
//   send message; QueueRequest16(record[+0]id, -1, amount, currency). Returns 0/1/1024.
int NpcAction6_RaiseSalaryCmd(HeRecord* h);

// gilde.exe 0x5760a8 — VIBE_NpcAction_LowerSalaryCmd(record@eax).
//   if (record[+44] == 0) -> 1024;  mult = RandomModulo(4)+1;
//   amount = (int)(ComputeTotalWealth(record[+0]) * mult * 0.01);
//   send message; QueueRequest16(-1, record[+0]id, amount, currency). Returns 0/1/1024.
int NpcAction6_LowerSalaryCmd(HeRecord* h);

// gilde.exe 0x576258 — VIBE_NpcAction_MoveToObjectCmd(record@eax).
//   amount = MoneyMultiplyByRate(RandomModulo(5)+1, currency);  resolve actor;
//   if (!PickRandomNeedAndClearGroup(actor)) -> 1024;  send message;
//   QueueRequest16(-1, record[+0]id, amount, currency). Returns 0/1/1024.
int NpcAction6_MoveToObjectCmd(HeRecord* h);

// gilde.exe 0x576198 — VIBE_NpcAction_ShowPositionCmd(record@eax).
//   resolve actor; if (record[+44] == 0) -> 1024;  season = season-of-clock;
//   send message (base textId, season + 76). Returns 0/1/1024.
int NpcAction6_ShowPositionCmd(HeRecord* h);

// gilde.exe 0x576334 / 0x5763d0 / 0x57646c — VIBE_NpcAction_DialogCmd{A,B,C}.
//   resolve actor; if (!gate(actor)) -> 1024; send message. Gates:
//     A: PickRandomNeedAndClearGroupB   B: PickRandomFlagFromFourA
//     C: PickRandomFlagFromFourB.  Returns 0/1/1024.
int NpcAction6_DialogCmdA(HeRecord* h);
int NpcAction6_DialogCmdB(HeRecord* h);
int NpcAction6_DialogCmdC(HeRecord* h);

// gilde.exe 0x576508 / 0x576590 / 0x576618 — VIBE_NpcAction_MessageCmd{A,B,C}.
//   resolve actor; send message (no gate). Three byte-identical twins. Returns 0/1.
int NpcAction6_MessageCmdA(HeRecord* h);
int NpcAction6_MessageCmdB(HeRecord* h);
int NpcAction6_MessageCmdC(HeRecord* h);

// gilde.exe 0x5694c0 — VIBE_NpcAction_NotifyWanderPair(record@eax).
//   Scan type-53 handlers; for each whose +188 dword equals record[+4], log a
//   wander-pair event, stamp the clock into the peer's +82, and re-arm a cmd29
//   entity request. Always returns 1.
int NpcAction6_NotifyWanderPair(HeRecord* h);

} // namespace guild::sim
