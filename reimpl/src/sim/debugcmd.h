#pragma once
// ===========================================================================
// debugcmd.{h,cpp} — the "DebugCmd" console / event-command dispatch family
// (gilde.exe, VIBE_DebugCmd_* family, 49 functions; namespace guild::sim).
// ===========================================================================
//
// Despite the auto-generated "DebugCmd" prefix, this family is the engine's
// EVENT-COMMAND dispatch layer driven from the AI eval functions
// (VIBE_AiMethod_*, VIBE_AiAction_*, VIBE_Interaction_*, dialog forms): each AI
// decision routes a small command code through
//
//   VIBE_DebugCmd_DispatchByType  (0x5711ec)  — the parse/dispatch entry:
//       char Dispatch(char type, Person* rec, void* ctx);
//       if (type >= 46 || rec == nullptr) return 64;   // 0x40 = "bad command"
//       if (rec->kind /* +2 */ < 10)                   // a "real person"?
//           return funcs_57120D[type](rec, ctx);        // 46-entry jump table
//       return 1;                                       // not a person -> noop
//
// The 46-entry jump table funcs_57120D @0x63d8ac is recovered byte-for-byte in
// the .cpp (DebugCmdId enum + the address-ordered handler list). Each handler is
// a small, deterministic routine of the shape:
//
//   rec = Person_FindRecordById(cmd->personId);  if (!rec) return 1;
//   (optional guard -> return 1024 if the person fails an eligibility test)
//   roll  = Math_RandomModulo(N);                          // crt::RandNext % N
//   gold  = (int)(Person_ComputeTotalWealth(rec) * (roll+base) * scale);
//   Text_RenderFormattedMessage(buf, textId, gold, ...);   // GUI leaf
//   He_SendEntityMessage(rec.id, rec.id, long, buf, 1418, 0);   // GUI/voice leaf
//   QueueRequest16 / RequestBuildOp90 / QueueRequestCoord27(...);// command leaf
//   return 0;                                              // 0 = handled
//
// Return codes (observed): 0 = handled, 1 = no such person / not-a-person,
// 1024 (0x400) = person ineligible for this command.
//
// We translate the dispatcher, the table recovery, and a representative,
// self-contained set of handlers. The deterministic decision math
// (RNG roll + wealth-scaled gold) is reproduced exactly (constants extracted
// from the data section); the GUI/voice text render + entity message + command
// emit are routed through DebugCmdHooks so the handlers are testable in
// isolation. The person record is reached through a DebugCmdPerson adapter so
// the module needs no Person array.
#include "guild/common/types.h"
#include "crt/rand.h"          // RandNext (the LCG behind Math_RandomModulo)
#include <cstdint>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Command codes (the `type` byte). Names follow the recovered jump-table order
// (index == code). The handler at each index is listed in debugcmd.cpp.
// ---------------------------------------------------------------------------
constexpr int kDebugCmdCount = 46;

// Result codes returned by handlers / the dispatcher.
constexpr int kDbgHandled    = 0;     // command applied
constexpr int kDbgNoPerson   = 1;     // person not found / not a person
constexpr int kDbgIneligible = 1024;  // 0x400 — person fails the command's gate

// ---------------------------------------------------------------------------
// Person adapter — the handful of Person fields the handlers read. The original
// reaches these by raw offset into the 536-byte record (FindRecordById result).
//   +0   (word)  kind/marker — also the wealth-fn key (rec[0])
//   +2   (byte)  kind byte (< 10 == real person; dispatcher gate)
//   +4   (dword) entity id (rec[1])
//   +9   (byte)  gender/married flag
//   +353 hi-byte building-type code (>>24) — SpawnEntityIfNotType7 gate
//   +358 (byte)  office rank — SpawnEntityByOfficeCategory gate
//   +404 (dword) "remaining charges" (rec[101]) — SendEntityWithFlagA gate
// ---------------------------------------------------------------------------
struct DebugCmdPerson {
    i32  id        = -1;     // +4
    i16  kindWord  = 0;      // +0  (wealth-fn key)
    u8   kind      = 0;      // +2
    u8   gender    = 0;      // +9
    u8   buildType = 0;      // hi-byte of +353
    u8   officeRank = 0;     // +358
    u8   officeAlt  = 0;     // +361 (ScaledJ secondary eligibility byte)
    i32  charges   = 0;      // +404 (rec[101])
    i32  wealth    = 0;      // Person_ComputeTotalWealth(rec) result
};

// ---------------------------------------------------------------------------
// Leaf hooks — GUI/voice/command side effects. Tests install a recording mock;
// nullptr installs an inert default (every effect a no-op, queries inert).
// ---------------------------------------------------------------------------
struct DebugCmdHooks {
    // VIBE_Person_FindRecordById(id) — resolve a person; return false if absent.
    bool (*findPerson)(i32 id, DebugCmdPerson* out);
    // VIBE_He_SendEntityMessage(fromId, toId, longText, textId, ...) — UI/voice.
    void (*sendEntityMessage)(i32 fromId, i32 toId, i32 textId);
    // VIBE_Command_QueueRequest16(a1, a2, amount, market) — opcode 16 money cmd.
    void (*queueRequest16)(i32 a1, i32 a2, i32 amount, u8 market);
    // VIBE_Command_RequestBuildOp90_Thunk(amount, entityId) — opcode 90.
    void (*requestBuildOp90)(i32 amount, i32 entityId);
    // VIBE_Command_QueueRequestCoord27(entityId, targetId, code) — opcode 27.
    void (*queueRequestCoord27)(i32 entityId, i32 targetId, i32 code);
    // VIBE_Money_MultiplyByRate(amount, market) — scale by the market index. The
    // inert default returns `amount` unchanged.
    i32 (*moneyMultiplyByRate)(i32 amount, u8 market);
    // word_63CC5C-style "current market" byte (byte_6477A1) the emits carry.
    u8 market;
};

void SetDebugCmdHooks(const DebugCmdHooks* hooks);
const DebugCmdHooks& GetDebugCmdHooks();

// ---------------------------------------------------------------------------
// Dispatcher.
// gilde.exe 0x5711ec — VIBE_DebugCmd_DispatchByType(type, personId, ctx).
//   The original takes (type@al, Person*@edx, ctx@ebx). We take the personId and
//   resolve the record via the hook (the gate reads rec->kind @+2). Returns the
//   handler's result, 64 for a bad command (type out of range / unresolved id),
//   or 1 when the resolved entity is not a real person (kind >= 10).
// ---------------------------------------------------------------------------
i8 DebugCmdDispatchByType(i8 type, i32 personId);

// The translated VIBE_DebugCmd_* handlers actually live in the NpcAction dispatch
// table (funcs_5766CB @0x63d964, see npcaction.{h,cpp}). This maps a funcs_5766CB
// index to the translated handler (or nullptr if that entry is deferred):
//   0 -> SpawnEntityScaledA, 4 -> SpawnEntityIfNotType7,
//   32 -> SendEntityWithFlagA, 33 -> SendEntityWithFlagB.
i32 (*DebugCmdNpcTableEntry(int npcActionIndex))(i32 personId);

// ---------------------------------------------------------------------------
// Representative handlers (the deterministic core of each, faithfully). Each
// takes the command's personId; the per-handler gates/rolls/emits are as in the
// original. They are individually exposed for testing and are also reachable via
// the dispatcher table.
// ---------------------------------------------------------------------------

// gilde.exe 0x5712a0 — VIBE_DebugCmd_SpawnEntityScaledA (table index 0).
//   roll in [0,3); gold = wealth * (roll+1) * 0.01; emit cmd16 + entity message.
i32 DebugCmdSpawnEntityScaledA(i32 personId);

// gilde.exe 0x571794 — VIBE_DebugCmd_SpawnEntityIfNotType7 (table index 6).
//   gate: building-type group of rec->buildType != 7 (else 1024);
//   roll in [0,4); gold = wealth * (roll+1) * 0.01; emit cmd16 + entity message.
i32 DebugCmdSpawnEntityIfNotType7(i32 personId);

// gilde.exe 0x5737b0 — VIBE_DebugCmd_SendEntityWithFlagA (table index 32).
//   amount = roll[0,3)+2; gate: amount <= rec->charges (else 1024);
//   entity message + RequestBuildOp90(-amount, id).
i32 DebugCmdSendEntityWithFlagA(i32 personId);

// gilde.exe 0x573870 — VIBE_DebugCmd_SendEntityWithFlagB (table index 33).
//   amount = roll[0,3)+3; gold = MultiplyByRate(1, market); entity message +
//   RequestBuildOp90(amount, id).
i32 DebugCmdSendEntityWithFlagB(i32 personId);

// ---------------------------------------------------------------------------
// Shared deterministic helpers (exposed for tests).
// ---------------------------------------------------------------------------
// VIBE_Math_RandomModulo(n) @0x58b89c: (n==0)?0 : (RandNext() % n).
int DebugCmdRandomModulo(u16 n);

// The wealth-scaled gold formula: (int)(wealth * (roll + base) * scale).
// `scale` is the per-handler double constant (see debugcmd.cpp constants).
i32 DebugCmdScaledGold(i32 wealth, int roll, double base, double scale);

} // namespace guild::sim
