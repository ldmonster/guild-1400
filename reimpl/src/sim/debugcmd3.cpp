#include "sim/debugcmd3.h"
#include <cstring>

namespace guild::sim {

// ===========================================================================
// Leaf-hook plumbing (the HE-list iteration + searches). Inert defaults yield
// nothing / resolve invalid, so a handler with no installed hooks reports "no
// eligible target" (1024) after a successful person lookup — faithful to the
// engine when the handler list is empty.
// ===========================================================================
namespace {
const DebugCmd3Hooks* g_hooks3 = nullptr;

bool InertFindFirst(i32, DebugCmd3Handler*) { return false; }
bool InertFindNext(DebugCmd3Handler*) { return false; }
DebugCmd3Entity InertResolve(i32) { return DebugCmd3Entity{}; }
bool InertNearest(const DebugCmdPerson*, i32*) { return false; }
int  InertQueryList(const DebugCmdPerson*, i32*, int) { return 0; }

DebugCmd3Hooks MakeInert3() {
    DebugCmd3Hooks h{};
    h.heFindFirst       = InertFindFirst;
    h.heFindNext        = InertFindNext;
    h.resolveEntity     = InertResolve;
    h.findNearestEntity = InertNearest;
    h.queryEntityList   = InertQueryList;
    return h;
}
DebugCmd3Hooks g_inert3 = MakeInert3();
}  // namespace

void SetDebugCmd3Hooks(const DebugCmd3Hooks* hooks) { g_hooks3 = hooks; }
const DebugCmd3Hooks& GetDebugCmd3Hooks() {
    return g_hooks3 ? *g_hooks3 : g_inert3;
}

// ===========================================================================
// Per-handler scale/base constants for this wave (recovered from .rdata):
//   FromHandlerList 0x571a74: base dbl_62546C=2.0    scale dbl_625474=0.01
//   NearNearest     0x572400: base 1.0(imm)          scale dbl_6254DC=0.01
//   QueueScaled     0x5740ec: base 1.0(imm)          scale flt_625538=0.01f
//   QueueState      0x573930: scale flt_625534=0.01f (applied to a count, not
//                             wealth; routed to the state-request leaf).
// ===========================================================================
namespace {
constexpr double kBase1 = 1.0;
constexpr double kBase2 = 2.0;
constexpr double kScale01 = 0.01;
constexpr float  kScale01f = 0.009999999776482582f;   // flt_625538 / flt_625534

// ---------------------------------------------------------------------------
// The shared broadcast/per-handler collection loop. Walks the HE handler list
// via the hooks, applies the per-variant production + building-code gate, and
// collects up to `cap` matching (handler, owner-kindWord) entries. Returns the
// match count (0..cap). `gate` receives the two resolved buildings (A=+44,
// B=+45) and decides whether the handler is collected.
//
// Faithful to the original loop:
//   h = He_FindFirstHandlerByFilter(1, kindWord, 15);
//   while (h) {
//     owner = Resolve(h.ownerEntityId);
//     if (owner.valid && person.kindWord == owner.kindWord) {
//       bB = Resolve(h.buildingIdB);  bA = Resolve(h.buildingIdA);   // B then A
//       if ((bB.valid && bB.production) || (bA.valid && bA.production))
//         if (gate(bA, bB)) collect();
//     }
//     h = He_FindNextMatchingHandler();
//     if (count*step >= 32) break;     // loop bound v5 < 32
//   }
// ---------------------------------------------------------------------------
using GateFn = bool (*)(const DebugCmd3Entity& bA, const DebugCmd3Entity& bB);

int CollectHandlers(const DebugCmdPerson& p, GateFn gate, int cap) {
    const auto& h = GetDebugCmd3Hooks();
    int count = 0;
    DebugCmd3Handler hnd;
    bool more = h.heFindFirst ? h.heFindFirst(p.kindWord, &hnd) : false;
    while (more) {
        DebugCmd3Entity owner = h.resolveEntity ? h.resolveEntity(hnd.ownerEntityId)
                                                : DebugCmd3Entity{};
        if (owner.valid && p.kindWord == owner.kindWord) {
            // Original resolves +45 (B) then +44 (A).
            DebugCmd3Entity bB = h.resolveEntity ? h.resolveEntity(hnd.buildingIdB)
                                                 : DebugCmd3Entity{};
            DebugCmd3Entity bA = h.resolveEntity ? h.resolveEntity(hnd.buildingIdA)
                                                 : DebugCmd3Entity{};
            bool prod = (bB.valid && bB.production) || (bA.valid && bA.production);
            if (prod && gate(bA, bB)) {
                ++count;
                if (count >= cap) break;          // v5 (==count*4) < 32 -> cap 8
            }
        }
        more = h.heFindNext ? h.heFindNext(&hnd) : false;
    }
    return count;
}

// Per-variant collection gates on the two building codes (A = handler[44],
// B = handler[45]). Each reproduces the exact && / || / 71-comparison of its
// handler (the only thing that distinguishes A..E / the PerHandler pair).
bool GateA(const DebugCmd3Entity& a, const DebugCmd3Entity& b) {   // 0x5740a0
    return a.buildingCode == 71 || b.buildingCode == 71;
}
bool GateB(const DebugCmd3Entity& a, const DebugCmd3Entity& b) {   // 0x574573
    return (a.valid && b.valid && a.buildingCode != 71) || b.buildingCode != 71;
}
bool GateC(const DebugCmd3Entity& a, const DebugCmd3Entity& b) {   // 0x574762
    return a.buildingCode != 71 || b.buildingCode != 71;
}
bool GateD(const DebugCmd3Entity& a, const DebugCmd3Entity& b) {   // 0x5749a8
    return (b.valid && a.valid && a.buildingCode == 71) || b.buildingCode == 71;
}
bool GateE(const DebugCmd3Entity& a, const DebugCmd3Entity& b) {   // 0x574bef
    return a.buildingCode == 71 || b.buildingCode == 71;
}
bool GateQ(const DebugCmd3Entity& a, const DebugCmd3Entity& b) {   // 0x573b8b/0x57432a
    return (a.valid && b.valid && a.buildingCode == 71) || b.buildingCode == 71;
}

// The shared message tail: He_SendEntityMessage(rec.id, rec.id, ...). The text
// render is a pure GUI leaf with no observable side effect we model.
void SendTail(const DebugCmdPerson& p) {
    const auto& h = GetDebugCmdHooks();
    if (h.sendEntityMessage)
        h.sendEntityMessage(p.id, p.id, 3881);
}

}  // namespace

// ===========================================================================
// BroadcastMsgToHandlers A..E. Lookup person -> collect handlers with the
// variant gate -> if none 1024 -> RandomModulo(count) pick -> message.
// (B/C/E also roll an extra %3+2 fed to the GUI text render; we consume it to
// keep the RNG bit-faithful.)
// ===========================================================================

// gilde.exe 0x573ebc — VIBE_DebugCmd_BroadcastMsgToHandlersA (index 36).
i32 DebugCmdBroadcastMsgToHandlersA(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    int count = CollectHandlers(p, GateA, 8);
    if (count == 0) return kDbgIneligible;
    (void)DebugCmdRandomModulo(static_cast<u16>(count));   // pick
    SendTail(p);
    return kDbgHandled;
}

// gilde.exe 0x574388 — VIBE_DebugCmd_BroadcastMsgToHandlersB (index 38).
i32 DebugCmdBroadcastMsgToHandlersB(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    int count = CollectHandlers(p, GateB, 8);
    if (count == 0) return kDbgIneligible;
    (void)(DebugCmdRandomModulo(3) + 2);                   // extra (text arg)
    (void)DebugCmdRandomModulo(static_cast<u16>(count));   // pick
    SendTail(p);
    return kDbgHandled;
}

// gilde.exe 0x57458c — VIBE_DebugCmd_BroadcastMsgToHandlersC (index 39).
i32 DebugCmdBroadcastMsgToHandlersC(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    int count = CollectHandlers(p, GateC, 8);
    if (count == 0) return kDbgIneligible;
    (void)(DebugCmdRandomModulo(3) + 2);                   // extra (text arg)
    (void)DebugCmdRandomModulo(static_cast<u16>(count));   // pick
    SendTail(p);
    return kDbgHandled;
}

// gilde.exe 0x574784 — VIBE_DebugCmd_BroadcastMsgToHandlersD (index 41).
//   (reads a global counter qword_13CE852 into the text args — no RNG advance.)
i32 DebugCmdBroadcastMsgToHandlersD(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    int count = CollectHandlers(p, GateD, 8);
    if (count == 0) return kDbgIneligible;
    (void)DebugCmdRandomModulo(static_cast<u16>(count));   // pick
    SendTail(p);
    return kDbgHandled;
}

// gilde.exe 0x5749f8 — VIBE_DebugCmd_BroadcastMsgToHandlersE (index 42).
i32 DebugCmdBroadcastMsgToHandlersE(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    int count = CollectHandlers(p, GateE, 8);
    if (count == 0) return kDbgIneligible;
    (void)(DebugCmdRandomModulo(3) + 2);                   // extra (text arg)
    (void)DebugCmdRandomModulo(static_cast<u16>(count));   // pick
    SendTail(p);
    return kDbgHandled;
}

// ===========================================================================
// gilde.exe 0x573930 — VIBE_DebugCmd_QueueStateRequestPerHandler (index 34).
//   Same collection (GateQ); after the pick rolls Math_RandomModulo(0x23)+60,
//   scales it by flt_625534 and a building byte, builds a delta packet and a
//   state request (Command_QueueRequestState22). The packet build is a command
//   leaf; we faithfully reproduce the RNG advance + the message tail.
// ===========================================================================
i32 DebugCmdQueueStateRequestPerHandler(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    int count = CollectHandlers(p, GateQ, 8);
    if (count == 0) return kDbgIneligible;
    (void)DebugCmdRandomModulo(static_cast<u16>(count));   // pick
    (void)(DebugCmdRandomModulo(0x23) + 60);               // amount roll
    SendTail(p);
    return kDbgHandled;
}

// ===========================================================================
// gilde.exe 0x5740ec — VIBE_DebugCmd_QueueScaledRequestPerHandler (index 37).
//   Same collection (GateQ); after passing the collect, rolls a wealth-scaled
//   gold (roll%2, base 1.0, scale ~0.01) and QueueRequest16, THEN picks an entry
//   at random for the message. (RNG order: gold roll, then pick.)
// ===========================================================================
i32 DebugCmdQueueScaledRequestPerHandler(i32 personId) {
    const auto& h = GetDebugCmdHooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;
    int count = CollectHandlers(p, GateQ, 8);
    if (count == 0) return kDbgIneligible;

    int roll = DebugCmdRandomModulo(2);
    i32 gold = DebugCmdScaledGold(p.wealth, roll, kBase1,
                                  static_cast<double>(kScale01f));
    if (h.queueRequest16)
        h.queueRequest16(-1, -1, gold, h.market);          // QR16(?,?,gold,mkt)
    (void)DebugCmdRandomModulo(static_cast<u16>(count));   // pick
    SendTail(p);
    return kDbgHandled;
}

// ===========================================================================
// gilde.exe 0x571a74 — VIBE_DebugCmd_SpawnEntityFromHandlerList (index 7).
//   Person_QueryBegin/IterNext collects up to 12 entity ids; if none 1024;
//   RandomModulo(count) pick; a second QueryBegin on the picked id; roll%5,
//   base 2.0, scale 0.01; QueueRequest16(personId, ...) + message.
// ===========================================================================
i32 DebugCmdSpawnEntityFromHandlerList(i32 personId) {
    const auto& h  = GetDebugCmdHooks();
    const auto& h3 = GetDebugCmd3Hooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;

    i32 ids[12];
    int count = h3.queryEntityList ? h3.queryEntityList(&p, ids, 12) : 0;
    if (count == 0) return kDbgIneligible;                 // v8 == 0 -> 1024

    (void)DebugCmdRandomModulo(static_cast<u16>(count));   // pick from list
    int roll = DebugCmdRandomModulo(5);                    // RandomModulo(5u)
    i32 gold = DebugCmdScaledGold(p.wealth, roll, kBase2, kScale01);

    if (h.queueRequest16)
        h.queueRequest16(personId, -1, gold, h.market);    // QR16(personId,...)
    SendTail(p);
    return kDbgHandled;
}

// ===========================================================================
// gilde.exe 0x572400 — VIBE_DebugCmd_SpawnEntityNearNearest (index 17).
//   ObjectSearch_FindNearestEntity(rec, 6, 0.0, 100.0) -> 1024 if none;
//   ResolveEntityById(found) -> 1024 if invalid; roll%3, base 1.0, scale 0.01;
//   QueueRequest16(-1, personId, ...) + message.
// ===========================================================================
i32 DebugCmdSpawnEntityNearNearest(i32 personId) {
    const auto& h  = GetDebugCmdHooks();
    const auto& h3 = GetDebugCmd3Hooks();
    DebugCmdPerson p;
    if (!h.findPerson || !h.findPerson(personId, &p)) return kDbgNoPerson;

    i32 target = 0;
    if (!h3.findNearestEntity || !h3.findNearestEntity(&p, &target))
        return kDbgIneligible;                             // nothing near -> 1024
    DebugCmd3Entity ent = h3.resolveEntity ? h3.resolveEntity(target)
                                           : DebugCmd3Entity{};
    if (!ent.valid) return kDbgIneligible;                 // resolve fail -> 1024

    int roll = DebugCmdRandomModulo(3);                    // RandomModulo(3u)
    i32 gold = DebugCmdScaledGold(p.wealth, roll, kBase1, kScale01);

    if (h.queueRequest16)
        h.queueRequest16(-1, personId, gold, h.market);    // QR16(-1,personId,..)
    SendTail(p);
    return kDbgHandled;
}

// ===========================================================================
// funcs_5766CB (NpcAction dispatch table @0x63d964) index -> this wave's handler.
// debugcmd.cpp owns 0/4/32/33; debugcmd2.cpp owns 1/3/5/6/8..16/18/19/20/40.
// This wave adds the indices below; nullptr otherwise.
// ===========================================================================
i32 (*DebugCmd3NpcTableEntry(int npcActionIndex))(i32 personId) {
    switch (npcActionIndex) {
        case 7:  return DebugCmdSpawnEntityFromHandlerList;    // 0x571a74
        case 17: return DebugCmdSpawnEntityNearNearest;        // 0x572400
        case 34: return DebugCmdQueueStateRequestPerHandler;   // 0x573930
        case 36: return DebugCmdBroadcastMsgToHandlersA;       // 0x573ebc
        case 37: return DebugCmdQueueScaledRequestPerHandler;  // 0x5740ec
        case 38: return DebugCmdBroadcastMsgToHandlersB;       // 0x574388
        case 39: return DebugCmdBroadcastMsgToHandlersC;       // 0x57458c
        case 41: return DebugCmdBroadcastMsgToHandlersD;       // 0x574784
        case 42: return DebugCmdBroadcastMsgToHandlersE;       // 0x5749f8
        default: return nullptr;
    }
}

} // namespace guild::sim
