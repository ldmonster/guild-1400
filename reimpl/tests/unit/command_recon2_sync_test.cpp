// Golden-vector unit tests for the three scene-sync command orchestrators
// (command_recon2_sync.{h,cpp}). We wire the SceneSyncDispatchHooks to recording stubs
// and assert the exact opcode / argument / field-offset sequence each
// orchestrator emits, plus the barrier and pump bookkeeping — i.e. the 1:1
// command stream the binary would produce. Self-contained: deterministic RNG.

#include "tests/framework/test.h"
#include "sim/command_recon2_sync.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

using namespace guild;
using namespace guild::sim;

namespace {

// A recorder that captures every codec/barrier/pump call as a tagged string so
// the whole emitted stream can be compared against a golden expectation.
struct Recorder {
    std::vector<std::string> log;
    // scripted RNG: returns values in sequence, then wraps.
    std::vector<u16> rngScript;
    size_t rngPos = 0;
    int ackAfter = 0;          // acked() returns false this many times, then true
    int ackCalls = 0;

    u16 nextRng(u32 m) {
        u16 v = rngScript.empty() ? 0 : rngScript[rngPos % rngScript.size()];
        ++rngPos;
        return static_cast<u16>(m ? (v % m) : 0);
    }
};

Recorder* g = nullptr;

u16  hRand(u32 m) { return g->nextRng(m); }
void hRandNext() { g->log.push_back("randNext"); }
i32  hMoney(i32 a, u8 r) { return a + r; } // distinguishable, deterministic
void hBegin(void*, u32 id) { g->log.push_back("begin id=" + std::to_string((i32)id)); }
void hRaw(u8 w, u8 c, const void* v, u16 o) {
    i32 val = 0; std::memcpy(&val, v, w <= 4 ? w : 4);
    g->log.push_back("raw w=" + std::to_string(w) + " c=" + std::to_string(c) +
                     " off=" + std::to_string(o) + " val=" + std::to_string(val));
}
void hDelta(u8 w, u8 c, const void* v, u16 o) {
    i32 val = 0; std::memcpy(&val, v, w <= 4 ? w : 4);
    g->log.push_back("delta w=" + std::to_string(w) + " c=" + std::to_string(c) +
                     " off=" + std::to_string(o) + " val=" + std::to_string(val));
}
void hState22() { g->log.push_back("state22"); }
u32  hEnqObj(u8 a1, i32 a2, i16 a3, i32 a4, i32 a5, u8 a6, u8 a7, u8 a8) {
    g->log.push_back("obj op=" + std::to_string(a1) + " a2=" + std::to_string(a2) +
                     " a3=" + std::to_string(a3) + " a4=" + std::to_string(a4) +
                     " a5=" + std::to_string(a5) + " a6=" + std::to_string(a6) +
                     " a7=" + std::to_string(a7) + " a8=" + std::to_string(a8));
    return static_cast<u32>(g->log.size());   // unique ring id per call
}
void hCmd15(i32 a1, i32 a2, i32 a3, u8 a4) {
    g->log.push_back("cmd15 a1=" + std::to_string(a1) + " a2=" + std::to_string(a2) +
                     " a3=" + std::to_string(a3) + " a4=" + std::to_string(a4));
}
void hReq17(i32 a1, i32 a2, i32 a3, i16 a4, u8 a5, i32 a6) {
    g->log.push_back("req17 a1=" + std::to_string(a1) + " a3=" + std::to_string(a3) +
                     " a4=" + std::to_string(a4) + " a5=" + std::to_string(a5));
}
i32  hSeq(u32 ringId) { return static_cast<i32>(ringId) + 1000; }
void hPump() { g->log.push_back("pump"); }
void hRefresh() { g->log.push_back("refresh"); }
void hStart() { g->log.push_back("markStart"); }
void hEnd() { g->log.push_back("markEnd"); }
bool hAcked() {
    ++g->ackCalls;
    if (g->ackCalls <= g->ackAfter) return false;
    return true;
}

SceneSyncDispatchHooks makeHooks() {
    SceneSyncDispatchHooks h;
    h.randNext = hRandNext;
    h.randMod = hRand;
    h.moneyRate = hMoney;
    h.currencyByte = 7;
    h.guildBankFlag = 0;
    h.beginDelta = hBegin;
    h.appendRaw = hRaw;
    h.appendDelta = hDelta;
    h.queueState22 = hState22;
    h.enqObjInteraction = hEnqObj;
    h.enqCmd15 = hCmd15;
    h.queueReq17 = hReq17;
    h.getPacketSeqById = hSeq;
    h.pumpOnce = hPump;
    h.refreshGuild = hRefresh;
    h.markStart = hStart;
    h.markEnd = hEnd;
    h.acked = hAcked;
    h.deltaEntityBase = 0;
    return h;
}

int countTag(const std::vector<std::string>& log, const char* prefix) {
    int n = 0;
    for (auto& s : log) if (s.rfind(prefix, 0) == 0) ++n;
    return n;
}

} // namespace

TEST(Command2ReconSync, EntryExit_EmitsTwoInteractionsAndTwoDeltas) {
    Recorder rec;
    rec.ackAfter = 0;
    g = &rec;
    auto h = makeHooks();
    i32 seqA = 0, seqB = 0;
    h.objSeqA = &seqA;
    h.objSeqB = &seqB;

    int r = SyncSceneEntryExit(h, 42);
    CHECK_EQ(r, 1);

    // Exactly two object interactions (opcode 12, kind 18). Per the binary
    // (gilde.exe 0x500f30..0x500f3d: edx == -1 from 0x500f10, NOT `this`), BOTH
    // calls are EnqueueObjectInteraction(12, -1, 18, -1, -1, 0,0,0) — entryEntityId
    // is stored only into the dead ack-tracker slot v7[0] and never used.
    CHECK_EQ(countTag(rec.log, "obj op=12"), 2);
    CHECK(rec.log[1] == "obj op=12 a2=-1 a3=18 a4=-1 a5=-1 a6=0 a7=0 a8=0");
    // (rec.log[0] is markStart)
    CHECK(rec.log[0] == "markStart");

    // Two delta packets: state byte 0 then 1, both width 1 count 1.
    CHECK_EQ(countTag(rec.log, "delta"), 2);
    int d0 = -1, d1 = -1;
    for (size_t i = 0; i < rec.log.size(); ++i) {
        if (rec.log[i].rfind("delta", 0) == 0) { if (d0 < 0) d0 = (int)i; else d1 = (int)i; }
    }
    CHECK(rec.log[d0].find("w=1 c=1") != std::string::npos);
    CHECK(rec.log[d0].find("val=0") != std::string::npos);
    CHECK(rec.log[d1].find("val=1") != std::string::npos);

    // The two seqs are recorded via GetPacketSeqById (ringId + 1000).
    CHECK(seqA != 0);
    CHECK(seqB != 0);
    CHECK_EQ(countTag(rec.log, "state22"), 2);
    // Two barriers (one per phase).
    CHECK_EQ(countTag(rec.log, "markStart"), 2);
    CHECK_EQ(countTag(rec.log, "markEnd"), 2);
}

TEST(Command2ReconSync, EntryExit_SpinsUntilAcked) {
    Recorder rec;
    rec.ackAfter = 2;        // first 2 acked() calls return false -> pump twice
    g = &rec;
    auto h = makeHooks();
    int r = SyncSceneEntryExit(h, 7);
    CHECK_EQ(r, 1);
    // Each unacked spin runs the pump body once.
    CHECK(countTag(rec.log, "pump") >= 1);
}

TEST(Command2ReconSync, SceneObjectStates_PerObjectRawField428) {
    Recorder rec;
    rec.rngScript = {5, 0, 0}; // RandomModulo(0x122)=5 etc.
    g = &rec;
    auto h = makeHooks();

    // Two entity records, 536 bytes each. Record 0 active (word != 0xffff,
    // byte+2 = 1 < 5); record 1 dead (word == 0xffff).
    std::vector<u8> mem(536 * 2, 0);
    // rec0: first word = 0x0001, id at +4 = 99, state byte +2 = 1.
    mem[0] = 0x01; mem[1] = 0x00; mem[2] = 1;
    u32 id0 = 99; std::memcpy(&mem[4], &id0, 4);
    // rec1: first word = 0xffff -> skipped.
    mem[536 + 0] = 0xFF; mem[536 + 1] = 0xFF;
    h.entityArrayBase = mem.data();
    h.entityArrayCount = 2;
    i32 sA = 0, sB = 0; h.objSeqA = &sA; h.objSeqB = &sB;

    int r = SyncSceneObjectStates(h);
    CHECK_EQ(r, 1);

    // RandNext is called once up front.
    CHECK_EQ(countTag(rec.log, "randNext"), 1);
    // Exactly one per-object raw field at offset 428, value = MoneyRate(5+10,7)
    // = (15 + 7) = 22 (per the test money hook).
    CHECK_EQ(countTag(rec.log, "raw"), 1);
    bool foundRaw = false;
    for (auto& s : rec.log)
        if (s.rfind("raw", 0) == 0) {
            CHECK(s.find("off=428") != std::string::npos);
            CHECK(s.find("w=4 c=1") != std::string::npos);
            CHECK(s.find("val=22") != std::string::npos);
            foundRaw = true;
        }
    CHECK(foundRaw);
    // begin id=99 for the active record.
    bool foundBegin = false;
    for (auto& s : rec.log) if (s == "begin id=99") foundBegin = true;
    CHECK(foundBegin);

    // 4 sync phases -> 4 markStart / 4 markEnd.
    CHECK_EQ(countTag(rec.log, "markStart"), 4);
    CHECK_EQ(countTag(rec.log, "markEnd"), 4);
    // Pass 3: 8 spawns (op 0). Pass 4: 8 removals (op 11) + 8 req17.
    CHECK_EQ(countTag(rec.log, "obj op=0 "), 8);
    CHECK_EQ(countTag(rec.log, "obj op=11 "), 8);
    CHECK_EQ(countTag(rec.log, "req17"), 8);
    // Pass 1: two op-12 interactions.
    CHECK_EQ(countTag(rec.log, "obj op=12"), 2);
}

TEST(Command2ReconSync, CharSlotAssignments_ClearsSlotAndEmitsMoney) {
    Recorder rec;
    rec.rngScript = {1, 1}; // start index 1, stride selector 1 -> stride 1
    g = &rec;
    auto h = makeHooks();
    h.guildBankFlag = 0;

    // 4 slots; only slot index 1 non-zero (value 0xABCD -> low byte 0xCD).
    std::vector<i32> slots = {0, 0xABCD, 0, 0};
    int r = SyncCharSlotAssignments(h, /*count*/1, /*slotCount*/4, slots.data());
    CHECK_EQ(r, 1);

    // Slot 1 cleared.
    CHECK_EQ(slots[1], 0);
    // One object interaction op=5 kind=16 with arg6 = low byte 0xCD = 205, a8=2.
    bool foundObj = false;
    for (auto& s : rec.log)
        if (s.rfind("obj op=5", 0) == 0) {
            CHECK(s.find("a3=16") != std::string::npos);
            CHECK(s.find("a6=205") != std::string::npos);
            CHECK(s.find("a8=2") != std::string::npos);
            foundObj = true;
        }
    CHECK(foundObj);
    // One money cmd15: MoneyRate(750,7) = 757.
    bool foundMoney = false;
    for (auto& s : rec.log)
        if (s.rfind("cmd15", 0) == 0) {
            CHECK(s.find("a3=757") != std::string::npos);
            CHECK(s.find("a1=-2") != std::string::npos);
            foundMoney = true;
        }
    CHECK(foundMoney);
    // guildBankFlag=0 -> exactly one cmd15.
    CHECK_EQ(countTag(rec.log, "cmd15"), 1);
}

TEST(Command2ReconSync, CharSlotAssignments_GuildBankBonus) {
    Recorder rec;
    rec.rngScript = {0 /*start*/, 1 /*stride sel*/, 100 /*bonus rand*/};
    g = &rec;
    auto h = makeHooks();
    h.guildBankFlag = 1;     // enable the bonus second cmd15

    std::vector<i32> slots = {5, 0, 0, 0};
    int r = SyncCharSlotAssignments(h, 1, 4, slots.data());
    CHECK_EQ(r, 1);

    // Two cmd15: base 757 then bonus MoneyRate(100+5000,7)=5107.
    CHECK_EQ(countTag(rec.log, "cmd15"), 2);
    bool foundBonus = false;
    for (auto& s : rec.log)
        if (s.rfind("cmd15", 0) == 0 && s.find("a3=5107") != std::string::npos)
            foundBonus = true;
    CHECK(foundBonus);
}
