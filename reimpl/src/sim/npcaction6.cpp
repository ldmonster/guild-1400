// NpcAction6 — AI debug-command leaf family. See npcaction6.h for the map.
//
// Calling convention (recovered from VIBE_NpcAction_Dispatch @0x5766a0 and each
// function's prologue): the dispatcher calls funcs_5766CB[type](record@eax). The
// command-descriptor record is read as raw bytes:
//   record[+0]  (dword) : person id passed to VIBE_Person_FindRecordById.
//   record[+4]  (word)  : action type (the jump-table index) — also reused as the
//                         localized-message base text id (+3881).
// The resolved Person record is likewise byte-addressed:
//   recordById[+0]   (word)  : person/array index (ComputeTotalWealth arg).
//   recordById[+4]   (dword) : entity id (RequestBuildOp93 / message target).
//   recordById[+44]  (dword) : "has-salary" / employed flag (salary gates).
//   recordById[+128+kind] (byte) : per-relation loyalty stat (loyalty cmds).
//   recordById[+130] (byte)  : salary-raise willingness stat (RaiseSalaryCmd gate).
//
// Float math is performed with x87-equivalent double precision then truncated to
// int (fistp toward zero with the default rounding the originals rely on; the
// magnitudes here never hit the rounding-mode edge). The 0.01 scale is the
// recovered dbl_625584/dbl_62558C (= 0.01). The RNG draws reuse util::RandomModulo
// (the shared CRT LCG), so the roll sequences are bit-exact.

#include "sim/npcaction6.h"

#include "sim/npcaction.h"     // NpcClock()
#include "sim/gametime.h"      // GameTimeAdvance
#include "util/math_random.h"  // RandomModulo

#include <cstdint>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook plumbing (mirrors the NpcAction5 pattern).
// ---------------------------------------------------------------------------
static const NpcAction6Hooks kInertHooks{};
static const NpcAction6Hooks* g_hooks6 = &kInertHooks;
void SetNpcAction6Hooks(const NpcAction6Hooks* hooks) {
    g_hooks6 = hooks ? hooks : &kInertHooks;
}
const NpcAction6Hooks& GetNpcAction6Hooks() { return *g_hooks6; }

// ---------------------------------------------------------------------------
// Command-descriptor record accessors (byte-faithful to the disasm).
// ---------------------------------------------------------------------------
static inline i32 Cmd_PersonId(HeRecord* h) {
    return *reinterpret_cast<i32*>(HeBytes(h) + 0);   // record[+0]
}
static inline u16 Cmd_TextBaseWord(HeRecord* h) {
    return *reinterpret_cast<u16*>(HeBytes(h) + 4);   // record[+4] word
}

// ---------------------------------------------------------------------------
// Resolved Person-record accessors (record base is a raw byte pointer).
// ---------------------------------------------------------------------------
static inline u16 Per_Index(u8* p)        { return *reinterpret_cast<u16*>(p + 0); }
static inline i32 Per_EntityId(u8* p)      { return *reinterpret_cast<i32*>(p + 4); }
static inline i32 Per_SalaryFlag(u8* p)    { return *reinterpret_cast<i32*>(p + 44); }
static inline u8  Per_Relation(u8* p, int kind) { return *(p + 128 + kind); }
static inline u8  Per_RaiseStat(u8* p)     { return *(p + 130); }

// Truncate-toward-zero conversion matching the x87 (int)double the originals use.
static inline i32 ToIntTrunc(double v) { return static_cast<i32>(v); }

// ===========================================================================
// gilde.exe 0x575eac — VIBE_NpcAction_IncreaseLoyaltyCmd.
// ===========================================================================
int NpcAction6_IncreaseLoyaltyCmd(HeRecord* h) {
    const auto& hk = GetNpcAction6Hooks();

    // kind = (u16)RandomModulo(5); resolve actor by record[+0].
    int kind = static_cast<u16>(util::RandomModulo(5));
    u8* rec = hk.findRecordById ? hk.findRecordById(Cmd_PersonId(h)) : nullptr;
    if (!rec) return kNpc6NoActor;

    // room = 0xFC - relationByte; if room <= 0 -> retry.
    int room = 0xFC - Per_Relation(rec, kind);
    if (room <= 0) return kNpc6Retry;

    // roll = RandomModulo(0x11) + 8; delta = min(room, roll).
    // (The original re-draws RandomModulo(0x11) inside the clamp branch; that
    //  second draw is the value actually stored, so it consumes one RNG step.)
    int roll = static_cast<u16>(util::RandomModulo(0x11)) + 8;
    int delta = room;
    if (room >= roll)
        delta = static_cast<u16>(util::RandomModulo(0x11)) + 8;  // re-roll; byte cast at emit

    if (hk.requestBuildOp93)
        hk.requestBuildOp93(Per_EntityId(rec), kind, kind, static_cast<u8>(delta));
    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(rec), Per_EntityId(rec),
                             Cmd_TextBaseWord(h) + 3881);
    return kNpc6Ok;
}

// ===========================================================================
// gilde.exe 0x575fac — VIBE_NpcAction_DecreaseLoyaltyCmd.
// ===========================================================================
int NpcAction6_DecreaseLoyaltyCmd(HeRecord* h) {
    const auto& hk = GetNpcAction6Hooks();

    int kind = static_cast<u16>(util::RandomModulo(5));
    u8* rec = hk.findRecordById ? hk.findRecordById(Cmd_PersonId(h)) : nullptr;
    if (!rec) return kNpc6NoActor;

    int rel = Per_Relation(rec, kind);
    if (rel == 0) return kNpc6Retry;            // nothing to subtract

    int roll = static_cast<u16>(util::RandomModulo(0xC)) + 6;
    int delta = rel;
    if (rel >= roll)
        delta = static_cast<u16>(util::RandomModulo(0xC)) + 6;   // re-roll; byte cast at emit

    if (hk.requestBuildOp93)
        hk.requestBuildOp93(Per_EntityId(rec), kind, kind,
                            static_cast<u8>(-static_cast<u8>(delta)));  // negated
    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(rec), Per_EntityId(rec),
                             Cmd_TextBaseWord(h) + 3881);
    return kNpc6Ok;
}

// ===========================================================================
// gilde.exe 0x575da0 — VIBE_NpcAction_RaiseSalaryCmd.
// ===========================================================================
int NpcAction6_RaiseSalaryCmd(HeRecord* h) {
    const auto& hk = GetNpcAction6Hooks();

    u8* rec = hk.findRecordById ? hk.findRecordById(Cmd_PersonId(h)) : nullptr;
    if (!rec) return kNpc6NoActor;

    // Gate: if RandomModulo(0x100) > recordById[+130] -> retry.
    int roll = static_cast<u16>(util::RandomModulo(0x100));
    if (roll > Per_RaiseStat(rec)) return kNpc6Retry;

    // mult = RandomModulo(3) + 1.0; amount = (int)(wealth * mult * 0.01).
    double mult = static_cast<double>(static_cast<u16>(util::RandomModulo(3))) + 1.0;
    i32 wealth = hk.computeTotalWealth ? hk.computeTotalWealth(Per_Index(rec), rec) : 0;
    i32 amount = ToIntTrunc(static_cast<double>(wealth) * mult * 0.01);

    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(rec), Per_EntityId(rec),
                             Cmd_TextBaseWord(h) + 3881);
    if (hk.queueRequest16)
        hk.queueRequest16(Cmd_PersonId(h), -1, amount, hk.currencyByte);
    return kNpc6Ok;
}

// ===========================================================================
// gilde.exe 0x5760a8 — VIBE_NpcAction_LowerSalaryCmd.
// ===========================================================================
int NpcAction6_LowerSalaryCmd(HeRecord* h) {
    const auto& hk = GetNpcAction6Hooks();

    u8* rec = hk.findRecordById ? hk.findRecordById(Cmd_PersonId(h)) : nullptr;
    if (!rec) return kNpc6NoActor;

    // Gate: must be employed (recordById[+44] != 0).
    if (Per_SalaryFlag(rec) == 0) return kNpc6Retry;

    double mult = static_cast<double>(static_cast<u16>(util::RandomModulo(4))) + 1.0;
    i32 wealth = hk.computeTotalWealth ? hk.computeTotalWealth(Per_Index(rec), rec) : 0;
    i32 amount = ToIntTrunc(static_cast<double>(wealth) * mult * 0.01);

    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(rec), Per_EntityId(rec),
                             Cmd_TextBaseWord(h) + 3881);
    if (hk.queueRequest16)
        hk.queueRequest16(-1, Cmd_PersonId(h), amount, hk.currencyByte);   // ids swapped
    return kNpc6Ok;
}

// ===========================================================================
// gilde.exe 0x576258 — VIBE_NpcAction_MoveToObjectCmd.
// ===========================================================================
int NpcAction6_MoveToObjectCmd(HeRecord* h) {
    const auto& hk = GetNpcAction6Hooks();

    // amount computed BEFORE the actor resolve (original draws the RNG first).
    int roll = static_cast<u16>(util::RandomModulo(5));
    i32 amount = hk.moneyMultiplyByRate
                     ? hk.moneyMultiplyByRate(roll + 1, hk.currencyByte) : 0;

    u8* rec = hk.findRecordById ? hk.findRecordById(Cmd_PersonId(h)) : nullptr;
    if (!rec) return kNpc6NoActor;
    if (!(hk.pickNeedAndClearGroup && hk.pickNeedAndClearGroup(rec)))
        return kNpc6Retry;

    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(rec), Per_EntityId(rec),
                             Cmd_TextBaseWord(h) + 3881);
    if (hk.queueRequest16)
        hk.queueRequest16(-1, Cmd_PersonId(h), amount, hk.currencyByte);
    return kNpc6Ok;
}

// ===========================================================================
// gilde.exe 0x576198 — VIBE_NpcAction_ShowPositionCmd.
// ===========================================================================
int NpcAction6_ShowPositionCmd(HeRecord* h) {
    const auto& hk = GetNpcAction6Hooks();

    u8* rec = hk.findRecordById ? hk.findRecordById(Cmd_PersonId(h)) : nullptr;
    if (!rec) return kNpc6NoActor;
    if (Per_SalaryFlag(rec) == 0) return kNpc6Retry;   // employed gate

    int season = hk.currentSeason ? hk.currentSeason() : 0;   // season-of-clock
    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(rec), Per_EntityId(rec),
                             Cmd_TextBaseWord(h) + 3881 /*+ (season+76) arg*/);
    (void)season;   // the season byte feeds the formatter's substitution arg
    return kNpc6Ok;
}

// ===========================================================================
// Dialog / Message leaf cmds (gate + status message).
// ===========================================================================
static int DialogCmd(HeRecord* h, int (*gate)(u8*)) {
    const auto& hk = GetNpcAction6Hooks();
    u8* rec = hk.findRecordById ? hk.findRecordById(Cmd_PersonId(h)) : nullptr;
    if (!rec) return kNpc6NoActor;
    if (!(gate && gate(rec))) return kNpc6Retry;
    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(rec), Per_EntityId(rec),
                             Cmd_TextBaseWord(h) + 3881);
    return kNpc6Ok;
}

// gilde.exe 0x576334 — DialogCmdA (gate: PickRandomNeedAndClearGroupB).
int NpcAction6_DialogCmdA(HeRecord* h) {
    return DialogCmd(h, GetNpcAction6Hooks().pickNeedAndClearGroupB);
}
// gilde.exe 0x5763d0 — DialogCmdB (gate: PickRandomFlagFromFourA).
int NpcAction6_DialogCmdB(HeRecord* h) {
    return DialogCmd(h, GetNpcAction6Hooks().pickFlagFromFourA);
}
// gilde.exe 0x57646c — DialogCmdC (gate: PickRandomFlagFromFourB).
int NpcAction6_DialogCmdC(HeRecord* h) {
    return DialogCmd(h, GetNpcAction6Hooks().pickFlagFromFourB);
}

static int MessageCmd(HeRecord* h) {
    const auto& hk = GetNpcAction6Hooks();
    u8* rec = hk.findRecordById ? hk.findRecordById(Cmd_PersonId(h)) : nullptr;
    if (!rec) return kNpc6NoActor;
    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(rec), Per_EntityId(rec),
                             Cmd_TextBaseWord(h) + 3881);
    return kNpc6Ok;
}

// gilde.exe 0x576508 / 0x576590 / 0x576618 — MessageCmd{A,B,C} (byte-identical).
int NpcAction6_MessageCmdA(HeRecord* h) { return MessageCmd(h); }
int NpcAction6_MessageCmdB(HeRecord* h) { return MessageCmd(h); }
int NpcAction6_MessageCmdC(HeRecord* h) { return MessageCmd(h); }

// ===========================================================================
// gilde.exe 0x5694c0 — VIBE_NpcAction_NotifyWanderPair.
//   for (peer = scanBegin(); peer; peer = scanNext())
//     if (peer[+188] == record[+4]) {
//        idx = ((peer[+188]^record[+4]) << 16) | peer[+8]  (== peer[+8]; the XOR'd
//              high word is 0 because the two ids are equal -> idx = peer[+8] word);
//        NotifyWanderPairEvent(record, &person[idx]);
//        clock -> peer[+82]; QueueRequestEntity29(-1, peer);
//     }
//   return 1.
// ===========================================================================
int NpcAction6_NotifyWanderPair(HeRecord* h) {
    const auto& hk = GetNpcAction6Hooks();

    i32 myId = He_Id(h);   // record[+4] (dword id) — the value matched against peers.
    if (!hk.wanderScanBegin) return 1;

    for (HeRecord* peer = hk.wanderScanBegin(); peer; peer = hk.wanderScanNext()) {
        i32 peerId = He_PacketId(peer, 0);   // peer[+188]
        if (peerId != myId) continue;

        // idx = LOWORD = peer[+8] word; HIWORD = HIWORD(peerId)^HIWORD(myId) == 0.
        u16 idx = He_CityIndex(peer);        // peer[+8]
        void* peerPerson = hk.personSlotByIndex ? hk.personSlotByIndex(idx) : nullptr;
        if (hk.notifyWanderPairEvent)
            hk.notifyWanderPairEvent(h, peerPerson);

        He_ApptTime(peer) = NpcClock();      // clock -> peer[+82]
        if (hk.queueRequestEntity29)
            hk.queueRequestEntity29(-1, peer);
    }
    return 1;
}

} // namespace guild::sim
