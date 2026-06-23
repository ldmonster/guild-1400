// Golden-vector unit tests for the recovered CharAction / NpcAction step routines
// in src/sim/charaction_npcaction_recon.{h,cpp}. Each test drives a function with
// an instrumented CharActionReconHooks table and asserts the exact control-flow
// observable (state transitions, clock advances, emit ordering/arguments, return
// values) against the gilde.exe decompilation. Self-contained; uses only the
// shared test framework and the public recon API.
#include "tests/framework/test.h"

#include "sim/charaction_npcaction_recon.h"
#include "sim/he.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock

#include <cstring>
#include <vector>
#include <string>

using namespace guild;
using namespace guild::sim;

namespace {

// A heap-backed He record (>= 512 bytes covers every offset these routines touch).
struct FakeHe {
    u8 buf[640];
    FakeHe() { std::memset(buf, 0, sizeof(buf)); }
    HeRecord* he() { return reinterpret_cast<HeRecord*>(buf); }
};

// Shared recording harness for the hook bridge.
struct Recorder {
    std::vector<std::string> log;
    // resolve/query programmability
    HeRecord* resolveResult = nullptr;
    HeRecord* objectResult  = nullptr;
    HeRecord* personResult  = nullptr;
    HeRecord* queryResult   = nullptr;
    i32 entity29Handle = 0x29;
    i32 freeResult     = 0x4f;
    i32 packetStat     = 1;
    i32 quad60Handle   = 0x60;
    i32 violationHandle = 0x1313;
    int rngValue = 0;
    HeRecord* filterFirst = nullptr;   // single-pass: returns this once then null
    bool filterServed = false;
};
Recorder* g = nullptr;

void   rResolve(HeRecord** out, i32 id) { if (out) *out = g->resolveResult; g->log.push_back("resolve:" + std::to_string(id)); }
HeRecord* rObject(i32 s, int, int, i32 key) { g->log.push_back("object:" + std::to_string(s) + ":" + std::to_string(key)); return g->objectResult; }
HeRecord* rPerson(i32 id) { g->log.push_back("findperson:" + std::to_string(id)); return g->personResult; }
HeRecord* rQuery(i32, int, int, i32 key) { g->log.push_back("query:" + std::to_string(key)); return g->queryResult; }
HeRecord* rFirst(int, int, i32 f) { g->log.push_back("first:" + std::to_string(f)); if (!g->filterServed && g->filterFirst) { g->filterServed = true; return g->filterFirst; } return nullptr; }
HeRecord* rNext() { return nullptr; }
i32  rStatus(i32 hnd) { g->log.push_back("status:" + std::to_string(hnd)); return g->packetStat; }
i32  rEnt29(int arg, HeRecord*) { g->log.push_back("ent29:" + std::to_string(arg)); return g->entity29Handle; }
i32  rFree(HeRecord*) { g->log.push_back("free"); return g->freeResult; }
void rMixed45(i32 id, i32 lo, i32 hi) { g->log.push_back("mixed45:" + std::to_string(id) + ":" + std::to_string(lo) + ":" + std::to_string(hi)); }
i32  rQuad60(i32 a, i32 b, int c, i32 d) { g->log.push_back("quad60:" + std::to_string(a) + ":" + std::to_string(b) + ":" + std::to_string(c) + ":" + std::to_string(d)); return g->quad60Handle; }
void rArgs25(i32 id, int off, int val, int sz, int extra) { g->log.push_back("args25:" + std::to_string(id) + ":" + std::to_string(off) + ":" + std::to_string(val) + ":" + std::to_string(sz) + ":" + std::to_string(extra)); }
void rCoord27(i32 f, i32 t, int d) { g->log.push_back("coord27:" + std::to_string(f) + ":" + std::to_string(t) + ":" + std::to_string(d)); }
void rEntMsg(i32 to, int txt) { g->log.push_back("msg:" + std::to_string(to) + ":" + std::to_string(txt)); }
void rQuickMsg(i32 to, int txt) { g->log.push_back("qjmsg:" + std::to_string(to) + ":" + std::to_string(txt)); }
i32  rViolation(int kind, int sev, i32 v, i32 r, i32 o) { g->log.push_back("viol:" + std::to_string(kind) + ":" + std::to_string(sev) + ":" + std::to_string(v) + ":" + std::to_string(r) + ":" + std::to_string(o)); return g->violationHandle; }
void rStamp(HeRecord*) { g->log.push_back("stampreq"); }
void rBeginDelta(i32, i32) { g->log.push_back("begindelta"); }
void rAppend(i32, i32, const u8* b, int v) { g->log.push_back("append:" + std::to_string(b ? *b : 0) + ":" + std::to_string(v)); }
void rState22() { g->log.push_back("state22"); }
void rOp72(i32, int sub) { g->log.push_back("op72:" + std::to_string(sub)); }
u8   rGroup(int) { return 3; }
void rRankPair(int, u8* a, u8* b) { if (a) *a = 2; if (b) *b = 5; }
i32  rApEvent(u16 p, i32 amt, int) { g->log.push_back("apevent:" + std::to_string(p) + ":" + std::to_string(amt)); return 0x4242; }
i32  rSeqById(i32) { return 0; }
void rConfirm(i32, u8) {}
u8   rVacant(u8) { return 0; }
i32  rReqText(u8) { return -1; }
int  rRng(int) { return g->rngValue; }

CharActionReconHooks MakeHooks() {
    return CharActionReconHooks{
        rResolve, rObject, rPerson, rQuery, rFirst, rNext, rStatus,
        rEnt29, rFree, rMixed45, rQuad60, rArgs25, rCoord27, rEntMsg,
        rQuickMsg, rViolation, rStamp, rBeginDelta, rAppend, rState22,
        rOp72, rGroup, rRankPair, rApEvent, rSeqById, rConfirm, rVacant,
        rReqText, rRng,
    };
}

void SetClock(int day, int hour, int minute, int second) {
    GameTime t; t.day = day; t.hour = static_cast<u16>(hour); t.minute = minute; t.second = second;
    SetNpcClock(t);
}

bool LogHas(const std::string& s) {
    for (const auto& e : g->log) if (e == s) return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// BeginActionState7 (0x4ca938)
// ---------------------------------------------------------------------------
TEST(NpcActionReconState7, SpawnFlagShortCircuits) {
    Recorder rec; g = &rec; auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe f; HeRecord* h = f.he();
    He_Flags(h) = 4;          // spawn flag set
    He_State(h) = 99;
    CHECK_EQ(NpcReconBeginActionState7(h), 99);
    CHECK(rec.log.empty());
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconState7, AbsentEntityArmsMinusOne) {
    Recorder rec; g = &rec; rec.resolveResult = nullptr; rec.entity29Handle = 0x77;
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(2, 8, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    He_CityId(h) = 123;
    CHECK_EQ(NpcReconBeginActionState7(h), 0x77);
    CHECK_EQ((int)HeR_TypeWord(h), 7);
    CHECK(LogHas("resolve:123"));
    CHECK(LogHas("ent29:-1"));
    // +82 stamped + advanced 24h -> day 3 hour 8.
    CHECK_EQ(He_ApptTime(h).day, 3);
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconState7, PresentEntityNo437EmitsUnbindAndArmsZero) {
    Recorder rec; g = &rec;
    FakeHe ent; *reinterpret_cast<i32*>(ent.buf + 93) = 55; *reinterpret_cast<i32*>(ent.buf + 4) = 808;
    rec.resolveResult = ent.he();
    rec.objectResult = nullptr;     // no 437 object co-located
    rec.entity29Handle = 5;
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(0, 0, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    He_CityId(h) = 10;
    CHECK_EQ(NpcReconBeginActionState7(h), 5);
    CHECK(LogHas("object:55:437"));
    CHECK(LogHas("mixed45:808:-1:-1"));
    CHECK_EQ(He_ReqHandle(h), -1);
    CHECK(LogHas("ent29:0"));
    SetCharActionReconHooks(nullptr);
}

// ---------------------------------------------------------------------------
// BeginActionState20 (0x4cbc20)
// ---------------------------------------------------------------------------
TEST(NpcActionReconState20, StrideNotOneSkipsLoop) {
    Recorder rec; g = &rec; rec.entity29Handle = 9; auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(1, 0, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    HeR_StrideWord(h) = 0;         // != 1
    CHECK_EQ(NpcReconBeginActionState20(h), 9);
    CHECK_EQ((int)HeR_TypeWord(h), 20);
    CHECK_EQ(He_State(h), 0);
    // no findperson/coord emitted, just the cmd29.
    CHECK(!LogHas("findperson:1"));
    CHECK(LogHas("ent29:0"));
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconState20, StrideOnePriorityByCategory) {
    Recorder rec; g = &rec;
    FakeHe member; *reinterpret_cast<u8*>(member.buf + 2) = 6; *reinterpret_cast<i32*>(member.buf + 4) = 700;
    rec.personResult = member.he();
    rec.entity29Handle = 9;
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(1, 0, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    He_Id(h) = 1;
    HeR_StrideWord(h) = 1;
    HeR_Target(h) = 11; HeR_Target2(h) = 12; HeR_Target3(h) = 13;
    NpcReconBeginActionState20(h);
    // three member ids read (+172,+176,+180); each resolvable -> priority 9 coord.
    CHECK(LogHas("findperson:11"));
    CHECK(LogHas("findperson:12"));
    CHECK(LogHas("findperson:13"));
    CHECK(LogHas("coord27:700:1:9"));   // category 6 => priority 9
    SetCharActionReconHooks(nullptr);
}

// ---------------------------------------------------------------------------
// ArrestStep (0x4cf798)
// ---------------------------------------------------------------------------
TEST(NpcActionReconArrest, StateMinus2ReleasesAndFrees) {
    Recorder rec; g = &rec;
    FakeHe a; *reinterpret_cast<i32*>(a.buf + 4) = 200;
    rec.personResult = a.he();    // both lookups return this
    rec.freeResult = 0xF;
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe f; HeRecord* h = f.he();
    He_State(h) = -2;
    He_Flags(h) = 2;              // report flag set -> release path runs
    HeR_Target(h) = 1; HeR_Target2(h) = 2;
    CHECK_EQ(CharReconArrestStep(h), 0xF);
    CHECK(LogHas("args25:200:456:0:4:256"));
    CHECK(LogHas("args25:200:456:0:4:512"));
    CHECK(LogHas("free"));
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconArrest, StateMinus3PassesThrough) {
    Recorder rec; g = &rec; auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe f; HeRecord* h = f.he();
    He_State(h) = -3;
    CHECK_EQ(CharReconArrestStep(h), -3);
    CHECK(rec.log.empty());
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconArrest, State2DragsAndArmsMinusOne) {
    Recorder rec; g = &rec;
    FakeHe a; *reinterpret_cast<i32*>(a.buf + 4) = 300; *reinterpret_cast<u8*>(a.buf + 2) = 7;
    rec.personResult = a.he();   // a and b resolve to same record (cat 7)
    rec.entity29Handle = 0x29;
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(4, 10, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    He_State(h) = 2;
    He_Flags(h) = 2;
    HeR_Target(h) = 1; HeR_Target2(h) = 2;
    CHECK_EQ(CharReconArrestStep(h), 0x29);
    CHECK(LogHas("coord27:300:300:-104"));
    CHECK(LogHas("msg:300:6493"));    // category 7 messages
    CHECK(LogHas("msg:300:6492"));
    CHECK(LogHas("ent29:-1"));
    CHECK_EQ(He_ReqHandle(h), 0x29);
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconArrest, State3PassesThrough) {
    Recorder rec; g = &rec; auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe f; HeRecord* h = f.he();
    He_State(h) = 3; He_Flags(h) = 2;
    CHECK_EQ(CharReconArrestStep(h), 3);
    CHECK(rec.log.empty());
    SetCharActionReconHooks(nullptr);
}

// ---------------------------------------------------------------------------
// GuildJoinStep (0x4d1d40)
// ---------------------------------------------------------------------------
TEST(NpcActionReconGuildJoin, TerminalStatesFree) {
    Recorder rec; g = &rec; rec.freeResult = 0x55; auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe f; HeRecord* h = f.he();
    He_State(h) = -2;
    CHECK_EQ(CharReconGuildJoinStep(h), 0x55);
    He_State(h) = -1;
    rec.log.clear();
    CHECK_EQ(CharReconGuildJoinStep(h), 0x55);
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconGuildJoin, State0IntroBumpsRetryAndRegistersAp) {
    Recorder rec; g = &rec;
    FakeHe p; *reinterpret_cast<u16*>(p.buf) = 42; *reinterpret_cast<i32*>(p.buf + 4) = 900;
    rec.personResult = p.he();
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe f; HeRecord* h = f.he();
    He_State(h) = 0;
    He_ReqHandle(h) = -1;          // no packet gate
    HeR_TypeWord(h) = 100;
    HeR_Target(h) = 7;
    HeR_Target3(h) = 50;           // AP amount = -50
    CHECK_EQ(CharReconGuildJoinStep(h), 0x4242);
    CHECK_EQ((int)HeR_RetryByte(h), 1);
    CHECK_EQ((int)HeR_TypeWord(h), 124);    // 100 + 24
    CHECK(LogHas("msg:900:5800"));
    CHECK(LogHas("ent29:0"));               // retry 1 < 3 -> arg 0
    CHECK(LogHas("apevent:42:-50"));
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconGuildJoin, State1RegistersMembershipAndArmsMinusOne) {
    Recorder rec; g = &rec;
    FakeHe p; *reinterpret_cast<u8*>(p.buf + 9) = 1; *reinterpret_cast<i32*>(p.buf + 4) = 901;
    rec.personResult = p.he();
    rec.rngValue = 0;
    rec.entity29Handle = 0xE;
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(0, 0, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    He_State(h) = 1;
    He_ReqHandle(h) = -1;
    HeR_Packed173(h) = (5 << 24);
    HeR_Target(h) = 7;
    CHECK_EQ(CharReconGuildJoinStep(h), 0xE);
    CHECK(LogHas("op72:5"));
    CHECK(LogHas("state22"));
    CHECK(LogHas("append:16:130"));   // rngValue 0 -> field 16, rankA 2 +128 = 130
    CHECK(LogHas("append:16:133"));   // rankB 5 +128 = 133
    CHECK(LogHas("msg:901:5801"));
    CHECK(LogHas("ent29:-1"));
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconGuildJoin, PacketPendingGateReturnsZero) {
    Recorder rec; g = &rec; rec.packetStat = 0; auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe f; HeRecord* h = f.he();
    He_State(h) = 1;
    He_ReqHandle(h) = 77;          // armed, pending
    CHECK_EQ(CharReconGuildJoinStep(h), 0);
    CHECK(LogHas("status:77"));
    CHECK(!LogHas("state22"));
    SetCharActionReconHooks(nullptr);
}

// ---------------------------------------------------------------------------
// CancelEntityActions (0x4dc074)
// ---------------------------------------------------------------------------
TEST(NpcActionReconCancel, NullOrSentinelIsNoop) {
    Recorder rec; g = &rec; auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    CHECK(CharReconCancelEntityActions(nullptr) == nullptr);
    FakeHe f; *reinterpret_cast<u16*>(f.buf) = 0xFFFF;
    CHECK(CharReconCancelEntityActions(f.he()) == f.he());
    CHECK(rec.log.empty());
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconCancel, MatchingActorHandlerIsRestamped) {
    Recorder rec; g = &rec;
    // a pool handler whose actor id (+43*4 = +172) matches our self id, active flag.
    FakeHe handler;
    *reinterpret_cast<u8*>(handler.buf + 120) = 2;          // active
    *reinterpret_cast<i32*>(handler.buf + 4 * 43) = 500;    // actor id
    *reinterpret_cast<i32*>(handler.buf + 4 * 44) = 999;    // peer id
    rec.filterFirst = handler.he();
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe self; *reinterpret_cast<u16*>(self.buf) = 1; He_Id(self.he()) = 500;
    CharReconCancelEntityActions(self.he());
    // first scan (filter 65) serves the handler -> resolves the peer (999) + re-stamp.
    CHECK(LogHas("first:65"));
    CHECK(LogHas("findperson:999"));
    CHECK(LogHas("stampreq"));
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconCancel, InactiveHandlerSkipped) {
    Recorder rec; g = &rec;
    FakeHe handler;
    *reinterpret_cast<u8*>(handler.buf + 120) = 0;          // inactive (no flag 1/2)
    *reinterpret_cast<i32*>(handler.buf + 4 * 43) = 500;
    rec.filterFirst = handler.he();
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    FakeHe self; *reinterpret_cast<u16*>(self.buf) = 1; He_Id(self.he()) = 500;
    CharReconCancelEntityActions(self.he());
    CHECK(!LogHas("stampreq"));
    SetCharActionReconHooks(nullptr);
}

// ---------------------------------------------------------------------------
// BeginCarryGoods (0x4e55bc)
// ---------------------------------------------------------------------------
TEST(NpcActionReconCarry, AdvancesSecondClockAndMirrors) {
    Recorder rec; g = &rec; rec.queryResult = nullptr;
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(5, 0, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    HeR_SeqId(h) = 2;              // +184 = 2 days -> +48h
    HeR_Target(h) = 33;
    HeRecord* r = NpcReconBeginCarryGoods(h);
    CHECK(r == nullptr);           // no carrier resolved
    CHECK_EQ((int)HeR_Clock2Word(h), 8);
    CHECK_EQ(HeR_CarryArg(h), 2);  // +188 mirrors +184
    CHECK_EQ(HeR_Clock2(h).day, 7);  // day 5 + 48h
    CHECK(LogHas("query:33"));
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconCarry, CarrierCategory6EvaluatesViolation) {
    Recorder rec; g = &rec;
    FakeHe carrier; *reinterpret_cast<u8*>(carrier.buf + 2) = 6; *reinterpret_cast<i32*>(carrier.buf + 4) = 444;
    rec.queryResult = carrier.he();
    rec.personResult = nullptr;
    rec.violationHandle = 0xABC;
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(0, 0, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    He_Flags(h) = 2;
    HeR_SeqId(h) = 0;
    HeR_Target(h) = 1; HeR_Target2(h) = 88; HeR_StrideWord(h) = 1;
    HeRecord* r = NpcReconBeginCarryGoods(h);
    CHECK_EQ(reinterpret_cast<intptr_t>(r), 0xABC);
    CHECK(LogHas("qjmsg:444:5350"));
    CHECK(LogHas("viol:13:1:88:444:444"));
    SetCharActionReconHooks(nullptr);
}

TEST(NpcActionReconCarry, CarrierCategoryOtherSkipsReport) {
    Recorder rec; g = &rec;
    FakeHe carrier; *reinterpret_cast<u8*>(carrier.buf + 2) = 4; *reinterpret_cast<i32*>(carrier.buf + 4) = 5;
    rec.queryResult = carrier.he();
    auto hk = MakeHooks(); SetCharActionReconHooks(&hk);
    SetClock(0, 0, 0, 0);
    FakeHe f; HeRecord* h = f.he();
    He_Flags(h) = 2;
    HeR_SeqId(h) = 0;
    HeRecord* r = NpcReconBeginCarryGoods(h);
    CHECK(r == carrier.he());      // returns the carrier; no report
    CHECK(!LogHas("qjmsg:5:5350"));
    SetCharActionReconHooks(nullptr);
}
