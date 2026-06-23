#include "sim/debugcmd.h"
#include <cstring>

namespace guild::sim {

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
namespace {
const DebugCmdHooks* g_hooks = nullptr;

i32 InertMultiplyByRate(i32 amount, u8) { return amount; }

DebugCmdHooks MakeInert() {
    DebugCmdHooks h{};
    h.moneyMultiplyByRate = InertMultiplyByRate;
    return h;
}
DebugCmdHooks g_inert = MakeInert();
}

void SetDebugCmdHooks(const DebugCmdHooks* hooks) { g_hooks = hooks; }
const DebugCmdHooks& GetDebugCmdHooks() {
    return g_hooks ? *g_hooks : g_inert;
}

// ===========================================================================
// Shared deterministic helpers.
// ===========================================================================
// gilde.exe 0x58b89c — VIBE_Math_RandomModulo(a1@ax):
//   result = 0; if (a1) return (int)VIBE_Util_RandNext() % a1; return result;
// VIBE_Util_RandNext is the CRT LCG (crt::RandNext) returning bits 16..30.
int DebugCmdRandomModulo(u16 n) {
    if (n == 0) return 0;
    return crt::RandNext() % n;
}

// (int)(wealth * (roll + base) * scale) — the original computes the double in
// the x87 stack then truncates to int (the VIBE_Coord_ConvertX rounding mode is
// chop). Reproduce with a C double + truncating cast.
i32 DebugCmdScaledGold(i32 wealth, int roll, double base, double scale) {
    double f = (static_cast<double>(roll) + base) * scale;
    return static_cast<i32>(static_cast<double>(wealth) * f);
}

// ===========================================================================
// Per-handler scale constants (extracted from the data section, gilde.exe).
//   dbl_62541C = dbl_62544C = dbl_625474 = 0.01   (0x3F847AE147AE147B)
//   dbl_62546C = dbl_6254FC = 2.0                  (0x4000000000000000)
// SpawnEntityScaledA / IfNotType7 use base 1.0 and scale 0.01.
// ===========================================================================
namespace {
constexpr double kScale01 = 0.01;   // 1% of wealth
}

// ===========================================================================
// gilde.exe 0x5712a0 — VIBE_DebugCmd_SpawnEntityScaledA (table index 0).
//   roll = Math_RandomModulo(3); gold = wealth * (roll+1) * 0.01.
//   QueueRequest16(personId, -1, gold, market); He_SendEntityMessage(id,id,...).
// ===========================================================================
i32 DebugCmdSpawnEntityScaledA(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p))
        return kDbgNoPerson;                                // !RecordById -> 1

    int roll = DebugCmdRandomModulo(3);                     // RandomModulo(3u)
    i32 gold = DebugCmdScaledGold(p.wealth, roll, 1.0, kScale01);

    // Binary emit ORDER (0x571323..0x57138d): RenderFormattedMessage ->
    // QueueRequest16 -> He_SendEntityMessage. So the cmd16 is queued BEFORE the
    // entity message is sent.
    if (h.queueRequest16)
        h.queueRequest16(personId, -1, gold, h.market);     // cmd16 (0x57133a)
    if (h.sendEntityMessage)
        h.sendEntityMessage(p.id, p.id, /*textId base*/ 3881);  // 0x57138d
    return kDbgHandled;
}

// ===========================================================================
// gilde.exe 0x571794 — VIBE_DebugCmd_SpawnEntityIfNotType7 (table index 6).
//   if (BuildingType_GroupFromCode(rec->buildType) == 7) return 1024;
//   roll = Math_RandomModulo(4); gold = wealth * (roll+1) * 0.01.
//   The group-from-code mapping is a small building-type leaf; the gate's only
//   observable effect is the early-out, so we test buildType against 7 directly
//   (the host installs the real GroupFromCode via the person adapter's buildType
//   already mapped to a group code).
// ===========================================================================
i32 DebugCmdSpawnEntityIfNotType7(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p))
        return kDbgNoPerson;

    if (p.buildType == 7)                                   // group == 7 gate
        return kDbgIneligible;                              // 1024

    int roll = DebugCmdRandomModulo(4);                     // RandomModulo(4u)
    i32 gold = DebugCmdScaledGold(p.wealth, roll, 1.0, kScale01);

    if (h.queueRequest16)
        h.queueRequest16(personId, -1, gold, h.market);
    if (h.sendEntityMessage)
        h.sendEntityMessage(p.id, p.id, 3881);
    return kDbgHandled;
}

// ===========================================================================
// gilde.exe 0x5737b0 — VIBE_DebugCmd_SendEntityWithFlagA (table index 32).
//   amount = Math_RandomModulo(3) + 2;     (NOTE: rolled BEFORE the person fetch)
//   if (!RecordById) return 1;
//   if (amount > rec->charges) return 1024;
//   He_SendEntityMessage(id,id,...); RequestBuildOp90(-amount, id);
// ===========================================================================
i32 DebugCmdSendEntityWithFlagA(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    int amount = DebugCmdRandomModulo(3) + 2;               // roll first

    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p))
        return kDbgNoPerson;

    if (amount > p.charges)                                 // v3 > rec[101]
        return kDbgIneligible;                              // 1024

    if (h.sendEntityMessage)
        h.sendEntityMessage(p.id, p.id, 3881);
    if (h.requestBuildOp90)
        h.requestBuildOp90(-amount, p.id);                  // op90(-amount)
    return kDbgHandled;
}

// ===========================================================================
// gilde.exe 0x573870 — VIBE_DebugCmd_SendEntityWithFlagB (table index 33).
//   if (!RecordById) return 1;
//   v3 = Math_RandomModulo(3) + 3;
//   v4 = Money_MultiplyByRate(1, market);
//   He_SendEntityMessage(id,id,...); RequestBuildOp90(v3, id);
// ===========================================================================
i32 DebugCmdSendEntityWithFlagB(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p))
        return kDbgNoPerson;

    int v3 = DebugCmdRandomModulo(3) + 3;
    i32 v4 = h.moneyMultiplyByRate ? h.moneyMultiplyByRate(1, h.market) : 1;
    (void)v4;   // feeds Text_RenderFormattedMessage (GUI leaf) only

    if (h.sendEntityMessage)
        h.sendEntityMessage(p.id, p.id, 3881);
    if (h.requestBuildOp90)
        h.requestBuildOp90(v3, p.id);                       // op90(+v3)
    return kDbgHandled;
}

// ===========================================================================
// Two distinct dispatch tables touch this family:
//
//  (1) funcs_57120D @0x63d8ac (46 entries) — the table DispatchByType (0x5711ec)
//      indexes. Its handlers live in the 0x56Exxx range (VIBE_ContextAction_*),
//      NOT the VIBE_DebugCmd_* set translated here; those bodies are owned by the
//      context-action module. DispatchByType is reproduced faithfully below; the
//      handler bodies are deferred (host installs them / inert noop).
//
//  (2) funcs_5766CB @0x63d964 (69 entries, indices 0..0x44) — the NpcAction
//      dispatch table (VIBE_NpcAction_Dispatch @0x5766a0, already in
//      npcaction.cpp). THIS is where the VIBE_DebugCmd_* handler set actually
//      lives: index 0 == SpawnEntityScaledA (0x5712a0), index 4 ==
//      SpawnEntityIfNotType7 (0x571794), index 32 == SendEntityWithFlagA
//      (0x5737b0), index 33 == SendEntityWithFlagB (0x573870). We expose
//      DebugCmdNpcTableEntry so a host/test can dispatch through the real index.
// ===========================================================================
namespace {
using HandlerFn = i32 (*)(i32 personId);
}  // namespace

// The translated VIBE_DebugCmd_* handlers, keyed by their funcs_5766CB index
// (the byte-for-byte NpcAction table from npcaction.cpp). Returns nullptr for
// the deferred entries.
HandlerFn DebugCmdNpcTableEntry(int npcActionIndex) {
    switch (npcActionIndex) {
        case 0:  return DebugCmdSpawnEntityScaledA;    // 0x5712a0
        case 4:  return DebugCmdSpawnEntityIfNotType7; // 0x571794
        case 32: return DebugCmdSendEntityWithFlagA;   // 0x5737b0
        case 33: return DebugCmdSendEntityWithFlagB;   // 0x573870
        default: return nullptr;
    }
}

// ===========================================================================
// gilde.exe 0x5711ec — VIBE_DebugCmd_DispatchByType.
//   char Dispatch(type@al, Person*@edx, ctx@ebx):
//     if ((signed char)type >= 46) return 64;          (cmp cl,0x2E; jge -> 0x40)
//     if (rec == 0) return 64;                          (test eax,eax; jz -> 0x40)
//     if ((signed char)rec[+2] >= 10) return 1;         (cmp [eax+2],0xA; jl call)
//     movsx ecx, cl; return funcs_57120D[ecx](rec,ctx); (signed index!)
//   funcs_57120D is the 46-entry ContextAction table; its handlers are deferred
//   here (host-side).
//
// FIDELITY (disasm 0x5711f3/0x57120a): the type gate is the SIGNED comparison
// `type >= 46`; a NEGATIVE type is NOT rejected — it falls through and the
// `movsx ecx, cl; call funcs_57120D[ecx*4]` indexes the table with a sign-
// extended (negative) index, i.e. an out-of-bounds call into whatever precedes
// the table. That OOB call is a genuine BOUNDARY (out-of-tree data); we do not
// fake it. We reproduce the exact gate decision (negatives are NOT 64) and route
// negatives/in-range types to the deferred-handler stub.
// ===========================================================================
i8 DebugCmdDispatchByType(i8 type, i32 personId) {
    if (type >= kDebugCmdCount)                 // SIGNED: type >= 46 -> bad (0x40)
        return 64;
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p))
        return 64;                              // rec == 0 -> 64

    if (p.kind >= 10)                           // *(signed char*)(rec+2) >= 10
        return 1;                               // not a real person -> 1
    // kind < 10: the binary calls funcs_57120D[type] (a deferred ContextAction
    // handler; host-installed). For type < 0 this is the documented OOB BOUNDARY.
    return static_cast<i8>(kDbgHandled);
}

} // namespace guild::sim
