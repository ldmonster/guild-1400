#include "test.h"

#include "sim/command_apply11.h"
#include "sim/command_codec.h"

#include <cstring>

// Unit tests for command_apply11 — the 0x4FB000-region debug-command builders.
// We verify (A) the opcode-28 scratch field-packing against a byte-exact oracle
// and the StagePendingBlock side-channel, and (B) the string-parse / clamp logic
// of the (B) emitters with deterministic hooks.

using namespace guild;
using namespace guild::sim;

namespace {

// Byte accessors over the 248-byte scratch (mirrors the source).
u8  sB (const SlotResetScratch& s, int o) { return reinterpret_cast<const u8*>(s.words)[o]; }
i32 sI (const SlotResetScratch& s, int o) { i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(s.words)+o, 4); return v; }
u16 sW (const SlotResetScratch& s, int o) { u16 v; std::memcpy(&v, reinterpret_cast<const u8*>(s.words)+o, 2); return v; }

DebugCmdCtx MakeCtx() {
    DebugCmdCtx c;
    c.playerId = 0x1234;
    for (int i = 0; i < 14; ++i) c.gameTime[i] = static_cast<u8>(i + 1); // 1..14
    return c;
}

// Hook spies.
i32 g_lastParse = 0;
i32 SpyParse(const char*) { return g_lastParse; }
int g_applyCalls = 0; u8 g_applyLaw = 0; i32 g_applyVal = 0;
void SpyApply(int, u8 law, i32 v) { ++g_applyCalls; g_applyLaw = law; g_applyVal = v; }

DebugCmdHooks::GesetzRecord g_rec;
int g_getRet = 1;
int SpyGet(u8, DebugCmdHooks::GesetzRecord* out) { if (out) *out = g_rec; return g_getRet; }

} // namespace

// --- (A) scratch packing oracle --------------------------------------------

TEST(CmdApply11Pack, GiveGoldFields) {
    DebugCmdCtx c = MakeCtx();
    SlotResetScratch s{};
    PackSlotResetScratch(s, c, /*op*/78, /*w2Hi*/7, /*w8*/c.playerId, /*w12*/-1,
                         /*b36*/1, /*w58*/0, /*w9C*/1000, true, true);
    CHECK_EQ(sB(s, 4), 78);
    CHECK_EQ(sI(s, 8), 0x1234);
    CHECK_EQ(sI(s, 12), -1);
    // gameTime copied at +0x28; the WORD2 store overwrites bytes +0x2C/+0x2D.
    CHECK_EQ(sB(s, 0x28), 1);
    CHECK_EQ(sB(s, 0x2B), 4);
    CHECK_EQ(sW(s, 0x2C), 7);
    CHECK_EQ(sB(s, 0x2E), 7);   // byte that followed (gameTime[6])
    CHECK_EQ(sB(s, 0x36), 1);
    CHECK_EQ(sI(s, 0x58), 0);
    CHECK_EQ(sI(s, 0x9C), 1000);
}

TEST(CmdApply11Pack, AdjustReputationFields) {
    DebugCmdCtx c = MakeCtx();
    SlotResetScratch s{};
    PackSlotResetScratch(s, c, 80, 16, c.playerId, -1, 2, -1, 1000, true, true);
    CHECK_EQ(sB(s, 4), 80);
    CHECK_EQ(sW(s, 0x2C), 16);
    CHECK_EQ(sB(s, 0x36), 2);
    CHECK_EQ(sI(s, 0x58), -1);
    CHECK_EQ(sI(s, 0x9C), 1000);
}

// --- (A) emit drives QueueRequestSlotReset28 -> opcode 28 packet + staged block

TEST(CmdApply11Emit, GiveGoldQueuesOpcode28) {
    CommandQueue q; q.Init();
    PendingState pending;
    DebugCmdCtx c = MakeCtx();

    i32 slot = QueueGiveGold(q, pending, c);  // returns 1
    CHECK_EQ(slot, 1);
    CHECK_EQ(q.send_count(), 1u);
    CommandPacket& p = q.ring_slot(1);
    CHECK_EQ(p.opcode(), 28);
    // The 248-byte body was staged through StagePendingBlock (len = 0xF8).
    CHECK_EQ(pending.staged, 0xF8u + 2u);
    CHECK_EQ(pending.block_len(), 0xF8u);
    // SlotReset28 forces scratch.words[14] (byte offset 56 == 0x38) to -1 if 0.
    i32 w14; std::memcpy(&w14, pending.block + 2 + 14 * 4, 4);
    CHECK_EQ(w14, -1);
}

TEST(CmdApply11Emit, SpawnSelectedGateRejectsNonNeg) {
    CommandQueue q; q.Init();
    PendingState pending;
    DebugCmdCtx c = MakeCtx();
    DebugCmdHooks h{}; g_lastParse = 0; h.parseInt = SpyParse; SetDebugCmdHooks(h);

    // parseInt returns 0 -> no packet, but returns 1.
    i32 r = QueueSpawnSelected(q, pending, c, "-0");
    CHECK_EQ(r, 1);
    CHECK_EQ(q.send_count(), 0u);

    g_lastParse = 5;
    r = QueueSpawnSelected(q, pending, c, "-5");
    CHECK_EQ(r, 1);
    CHECK_EQ(q.send_count(), 1u);
    CHECK_EQ(q.ring_slot(1).opcode(), 28);
    SetDebugCmdHooks(DebugCmdHooks{});  // reset to inert
}

TEST(CmdApply11Emit, SpawnSelectedRejectsNonDash) {
    CommandQueue q; q.Init();
    PendingState pending;
    DebugCmdCtx c = MakeCtx();
    CHECK_EQ(QueueSpawnSelected(q, pending, c, "x5"), 0);
}

// --- (B) string-parse + scan emitters ---------------------------------------

TEST(CmdApply11B, AdjustSelectedStatSignSwap) {
    CommandQueue q; q.Init();
    DebugCmdCtx c;
    DebugCmdHooks h{}; g_lastParse = 42; h.parseInt = SpyParse; SetDebugCmdHooks(h);

    // PLUS slot: from = person (selBase), to = -1.
    i32 r = QueueAdjustSelectedStat(q, c, /*selBase*/0x77, /*a2*/0, "-PLUS_42");
    CHECK_EQ(r, 1);
    CHECK_EQ(q.send_count(), 1u);
    CommandPacket& p = q.ring_slot(1);
    CHECK_EQ(p.opcode(), 16);
    CHECK_EQ(p.get32(0x10), 0x77u);             // a1 = from = person
    CHECK_EQ(p.get32(0x14), 0xFFFFFFFFu);       // a2 = to = -1
    CHECK_EQ(p.get32(0x1D), 42u);               // a3 = amount

    // MINUS slot: from = -1, to = person.
    r = QueueAdjustSelectedStat(q, c, 0x77, 0, "-MINUS_42");
    CHECK_EQ(r, 1);
    CommandPacket& p2 = q.ring_slot(2);
    CHECK_EQ(p2.get32(0x10), 0xFFFFFFFFu);      // from = -1
    CHECK_EQ(p2.get32(0x14), 0x77u);            // to = person
    SetDebugCmdHooks(DebugCmdHooks{});
}

TEST(CmdApply11B, AdjustSelectedStatGate) {
    CommandQueue q; q.Init();
    DebugCmdCtx c;
    CHECK_EQ(QueueAdjustSelectedStat(q, c, 0, 0, "-PLUS_1"), 0);       // !selBase
    CHECK_EQ(QueueAdjustSelectedStat(q, c, 0x77, 8, "-PLUS_1"), 0);    // a2 >= 8
    CHECK_EQ(QueueAdjustSelectedStat(q, c, 0x77, 0, "PLUS_1"), 0);     // no dash
    CHECK_EQ(QueueAdjustSelectedStat(q, c, 0x77, 0, "-FOO_1"), 0);     // no sign match
    CHECK_EQ(QueueAdjustSelectedStat(q, c, 0x77, 0, "-PLUS1"), 0);     // no '_' sep
    CHECK_EQ(q.send_count(), 0u);
}

TEST(CmdApply11B, BuildingStatNonPositiveNoEmit) {
    CommandQueue q; q.Init();
    DebugCmdCtx c;
    DebugCmdHooks h{}; g_lastParse = 0; h.parseInt = SpyParse; SetDebugCmdHooks(h);
    // parsed value <= 0 -> returns 1 with no packet.
    CHECK_EQ(QueueAdjustBuildingStat(q, c, "-PLUS_0"), 1);
    CHECK_EQ(q.send_count(), 0u);
    g_lastParse = 9;
    CHECK_EQ(QueueAdjustBuildingStat(q, c, "-PLUS_9"), 1);
    CHECK_EQ(q.send_count(), 1u);
    CHECK_EQ(q.ring_slot(1).opcode(), 16);
    SetDebugCmdHooks(DebugCmdHooks{});
}

// --- (B) Justice severity clamp logic (the load-bearing reconstructed part) --

TEST(CmdApply11Justice, SetClampsIntoRange) {
    DebugCmdCtx c;
    DebugCmdHooks h{};
    h.parseInt = SpyParse; h.gesetzGetRecord = SpyGet; h.gesetzRequestApply = SpyApply;
    SetDebugCmdHooks(h);
    g_rec = {0, 10, 20}; g_getRet = 1;             // applyFlag 0 (no out-of-range reject)

    g_lastParse = 25; g_applyCalls = 0;
    CHECK_EQ(QueueSetJusticeSeverity(c, /*lawSel*/3, "-RECHTSPRECHUNG_HAERTE_25"), 1);
    CHECK_EQ(g_applyCalls, 1);
    CHECK_EQ(g_applyVal, 20);                       // clamped to hi
    CHECK_EQ(g_applyLaw, 3);

    g_lastParse = 5; g_applyCalls = 0;
    QueueSetJusticeSeverity(c, 3, "-RECHTSPRECHUNG_HAERTE_5");
    CHECK_EQ(g_applyVal, 10);                        // clamped to lo

    g_lastParse = 15; g_applyCalls = 0;
    QueueSetJusticeSeverity(c, 3, "-RECHTSPRECHUNG_HAERTE_15");
    CHECK_EQ(g_applyVal, 15);                        // in range, unchanged
    SetDebugCmdHooks(DebugCmdHooks{});
}

TEST(CmdApply11Justice, SetRejectsOutOfRangeWithFlag) {
    DebugCmdCtx c;
    DebugCmdHooks h{};
    h.parseInt = SpyParse; h.gesetzGetRecord = SpyGet; h.gesetzRequestApply = SpyApply;
    SetDebugCmdHooks(h);
    g_rec = {1, 10, 20}; g_getRet = 1;              // applyFlag set -> reject OOR
    g_lastParse = 25; g_applyCalls = 0;
    CHECK_EQ(QueueSetJusticeSeverity(c, 3, "-RECHTSPRECHUNG_HAERTE_25"), 0);
    CHECK_EQ(g_applyCalls, 0);
    SetDebugCmdHooks(DebugCmdHooks{});
}

TEST(CmdApply11Justice, SetRejectsWhenNoRecord) {
    DebugCmdCtx c;
    DebugCmdHooks h{};
    h.parseInt = SpyParse; h.gesetzGetRecord = SpyGet; h.gesetzRequestApply = SpyApply;
    SetDebugCmdHooks(h);
    g_getRet = 0; g_lastParse = 15; g_applyCalls = 0;
    CHECK_EQ(QueueSetJusticeSeverity(c, 3, "-RECHTSPRECHUNG_HAERTE_15"), 0);
    CHECK_EQ(g_applyCalls, 0);
    SetDebugCmdHooks(DebugCmdHooks{});
}

TEST(CmdApply11Justice, AdjustSignedDeltaClamped) {
    DebugCmdCtx c;
    DebugCmdHooks h{};
    h.parseInt = SpyParse; h.gesetzGetRecord = SpyGet; h.gesetzRequestApply = SpyApply;
    SetDebugCmdHooks(h);
    g_rec = {0, 0, 100}; g_getRet = 1;
    // PLUS: value = 0(curr) + (+1)*30 = 30, in [0,100].
    g_lastParse = 30; g_applyCalls = 0;
    CHECK_EQ(QueueAdjustJusticeSeverity(c, 7, "-PLUS_RECHTSPRECHUNG_HAERTE_30"), 1);
    CHECK_EQ(g_applyVal, 30);
    // MINUS: value = 0 + (-1)*30 = -30, clamps to lo 0.
    g_lastParse = 30; g_applyCalls = 0;
    QueueAdjustJusticeSeverity(c, 7, "-MINUS_RECHTSPRECHUNG_HAERTE_30");
    CHECK_EQ(g_applyVal, 0);
    SetDebugCmdHooks(DebugCmdHooks{});
}
