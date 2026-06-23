// Unit tests for the cutscene active-processing step machine + the DebugCmd
// event-command dispatch family (guild::sim).
#include "tests/framework/test.h"
#include "sim/cutscene.h"
#include "sim/cutscene_process.h"
#include "sim/debugcmd.h"
#include "crt/rand.h"

#include <vector>
#include <cstring>

using namespace guild::sim;

// ---------------------------------------------------------------------------
// Helpers: build a GameTime, build a slot.
// ---------------------------------------------------------------------------
namespace {
GameTime MkTime(int day, int hour, int minute, int second) {
    GameTime t{};
    t.day = day; t.hour = static_cast<guild::u16>(hour);
    t.minute = minute; t.second = second;
    return t;
}

// A recording set of step-fn observations.
struct StepLog {
    std::vector<int> mainCalls;   // slot ids that ran main
    std::vector<int> stepCalls;   // slot ids that ran step
    int  stepReturn = 1;          // what step fns return
};
StepLog* g_log = nullptr;

int RecMainType6(CutsceneSlot* s) { g_log->mainCalls.push_back(s->id); return 7; }
int RecStepReady(CutsceneSlot* s) { g_log->stepCalls.push_back(s->id);
                                    return g_log->stepReturn; }

// personKind callback: a tiny map.
const int* g_kindLookup = nullptr;   // pairs {id,kind,...}, terminated by id<0
guild::u8 KindOf(int id) {
    if (!g_kindLookup) return 0;       // resolves to "present" kind 0
    for (const int* p = g_kindLookup; *p >= 0; p += 2)
        if (p[0] == id) return static_cast<guild::u8>(p[1]);
    return 0xFF;                        // absent
}
}  // namespace

// ===========================================================================
// Slot-table / participant primitives (substrate the driver sits on).
// ===========================================================================
TEST(CutsceneProc, ActorHasParticipant) {
    CutsceneTable tbl;
    CutsceneSlot tmpl{};
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.partCount = 1;                 // alloc needs an alive gate to be findable
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/100);
    s->partCount = 3;
    s->partIds[0] = 11; s->partIds[1] = 22; s->partIds[2] = 33;

    CutsceneContext ctx;
    ctx.table = &tbl;
    ctx.personKind = nullptr;          // resolves always
    CHECK(CutsceneActorHasParticipant(ctx, 22, s));
    CHECK(!CutsceneActorHasParticipant(ctx, 99, s));

    // With a kind map where 33 is absent, 33 fails the resolve.
    static const int kinds[] = {11, 4, 22, 6, -1, 0};
    g_kindLookup = kinds;
    ctx.personKind = KindOf;
    CHECK(CutsceneActorHasParticipant(ctx, 22, s));
    CHECK(!CutsceneActorHasParticipant(ctx, 33, s));   // absent -> 0xFF
    g_kindLookup = nullptr;
}

TEST(CutsceneProc, AddRemoveActorSlot) {
    int list[8];
    for (int& v : list) v = -1;
    CHECK_EQ(CutsceneAddActorToSlot(500, list), 0);    // first free
    CHECK_EQ(list[0], 500);
    CHECK_EQ(CutsceneAddActorToSlot(500, list), 0);    // already present -> idx 0
    CHECK_EQ(CutsceneAddActorToSlot(600, list), 1);    // next free
    CHECK_EQ(list[1], 600);
    CHECK_EQ(CutsceneRemoveActorFromSlot(500, list), 0);
    CHECK_EQ(list[0], -1);
    CHECK_EQ(CutsceneRemoveActorFromSlot(600, list), 1);
    CHECK_EQ(list[1], -1);
}

TEST(CutsceneProc, ParticipantStateGates) {
    CutsceneTable tbl;
    CutsceneSlot tmpl{}; tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, 7);
    s->partCount = 2; s->partIds[0] = 1; s->partIds[1] = 2;

    CutsceneParticipants pt;
    pt.Init(s);
    CHECK_EQ(pt[0].personId, 1);
    CHECK_EQ(pt[1].personId, 2);
    CHECK_EQ(pt[0].enabled, 1);
    CHECK_EQ(pt[2].personId, -1);     // beyond count -> empty

    // None done yet -> AllDone false; not ready yet -> AllReady false.
    CHECK(!pt.AllDone(2));
    CHECK(!pt.AllReady(2, nullptr));
    pt[0].done = 1; pt[1].done = 1;
    CHECK(pt.AllDone(2));
    pt[0].ready = 1; pt[1].ready = 1;
    CHECK(pt.AllReady(2, nullptr));
}

// ===========================================================================
// The recovered per-type table layout.
// ===========================================================================
TEST(CutsceneProc, TypeTableLayout) {
    CutsceneTypeTable tt;
    CutsceneTypeFns fns{};
    // Distinct sentinels for a few slots to verify the mapping.
    auto A = reinterpret_cast<CutsceneTypeFn>(0x10);
    auto B = reinterpret_cast<CutsceneTypeFn>(0x20);
    auto C = reinterpret_cast<CutsceneTypeFn>(0x30);
    fns.duel = A; fns.duelCheckParticipants = B;
    fns.auction = C; fns.broadcastMessage = A;
    InitCutsceneTypeTable(tt, fns);
    // type 4: main=duel, step=duelCheckParticipants
    CHECK_EQ(tt[4].main, A);
    CHECK_EQ(tt[4].step, B);
    // type 10: main=auction, step=broadcastMessage, flag byte == 1
    CHECK_EQ(tt[10].main, C);
    CHECK_EQ(tt[10].step, A);
    CHECK_EQ((int)tt[10].flag, 1);
    // type 3 flag byte == 1 (combat)
    CHECK_EQ((int)tt[3].flag, 1);
    CHECK_EQ((int)tt[0].flag, 0);
}

// ===========================================================================
// ProcessActive: a synthetic cutscene stepped through its phases.
// ===========================================================================
TEST(CutsceneProc, ProcessActiveTimeoutFires) {
    // A slot of a type with NO step fn (e.g. type 9 bankruptcy) whose timeout
    // window is in the past -> ProcessActive marks it finished, ExecMainFunc's
    // it, and tears it down.
    StepLog log; g_log = &log;
    CutsceneTable tbl;
    CutsceneTypeTable tt;
    CutsceneTypeFns fns{}; fns.bankruptcy = RecMainType6;   // type 9 main fn
    InitCutsceneTypeTable(tt, fns);
    CutsceneRng rng;

    CutsceneSlot tmpl{}; tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/42);
    s->type = 9;                                  // bankruptcy: no step fn
    s->partCount = 1; s->partIds[0] = 1000;
    s->master = 7;
    s->readyTime = MkTime(1, 0, 0, 0);            // timeout at day1 00:00
    *reinterpret_cast<guild::i32*>(reinterpret_cast<guild::u8*>(s) + 120) = 12345; // seed

    CutsceneContext ctx;
    ctx.table = &tbl; ctx.types = &tt; ctx.rng = &rng;
    ctx.localMaster = 7; ctx.localSlotId = 42;
    ctx.clock = MkTime(2, 0, 0, 0);               // clock past the timeout
    ctx.personKind = nullptr;

    CutsceneProcessActive(ctx);
    CHECK_EQ(ctx.execCount, 1);
    CHECK_EQ((int)log.mainCalls.size(), 1);
    CHECK_EQ(log.mainCalls[0], 42);
    // slot torn down
    CHECK(tbl.FindById(42) == nullptr);
    // exec seeded the RNG from the slot seed
    CHECK_EQ(rng.GetSeed(), 12345);
}

TEST(CutsceneProc, ProcessActiveReadyWindowAndStep) {
    // A slot of a type WITH a step fn (e.g. type 4 duel). The slot is owned by
    // the local master, not started, clock past the -60s ready window. The
    // built-in PrepareReady passes (master is a participant) -> started set, step
    // fn runs. The step returning nonzero means the slot stays armed (not op88).
    StepLog log; g_log = &log; log.stepReturn = 1;
    CutsceneTable tbl;
    CutsceneTypeTable tt;
    CutsceneTypeFns fns{};
    fns.duel = RecMainType6; fns.duelCheckParticipants = RecStepReady;
    InitCutsceneTypeTable(tt, fns);

    CutsceneSlot tmpl{}; tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/5);
    s->type = 4;
    s->partCount = 1; s->partIds[0] = 7;          // master IS a participant
    s->master = 7;
    s->started = 0;
    s->readyTime = MkTime(1, 0, 2, 0);            // window: 00:02 ; -60s -> 00:01

    CutsceneContext ctx;
    ctx.table = &tbl; ctx.types = &tt;
    ctx.localMaster = 7; ctx.localSlotId = -1;
    ctx.clock = MkTime(1, 0, 1, 30);              // 00:01:30 > 00:01 window
    static const int kinds[] = {7, 6, -1, 0};
    g_kindLookup = kinds; ctx.personKind = KindOf;

    CutsceneProcessActive(ctx);
    CHECK_EQ(s->started, 1);                       // started flag set
    CHECK_EQ((int)log.stepCalls.size(), 1);        // step fn ran
    CHECK_EQ(log.stepCalls[0], 5);
    // step returned ready: clock 00:01:30 is NOT past the timeout window 00:02,
    // so the slot is neither finished nor exec'd.
    CHECK(tbl.FindById(5) != nullptr);
    CHECK_EQ(ctx.execCount, 0);
    g_kindLookup = nullptr;
}

TEST(CutsceneProc, ProcessActiveStepFailRequestsOp88) {
    // step fn returns 0 -> ProcessActive calls RequestBuildOp88(slotId) and
    // moves on (no exec, slot stays).
    StepLog log; g_log = &log; log.stepReturn = 0;
    static std::vector<int> op88Ids;
    op88Ids.clear();
    CutsceneProcHooks hooks{};
    hooks.requestBuildOp88 = [](guild::i32 id) { op88Ids.push_back(id); };
    SetCutsceneProcHooks(&hooks);

    CutsceneTable tbl;
    CutsceneTypeTable tt;
    CutsceneTypeFns fns{};
    fns.duel = RecMainType6; fns.duelCheckParticipants = RecStepReady;
    InitCutsceneTypeTable(tt, fns);

    CutsceneSlot tmpl{}; tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/9);
    s->type = 4; s->partCount = 1; s->partIds[0] = 7; s->master = 7;
    s->readyTime = MkTime(1, 0, 2, 0);

    CutsceneContext ctx;
    ctx.table = &tbl; ctx.types = &tt;
    ctx.localMaster = 7; ctx.clock = MkTime(1, 0, 1, 30);
    ctx.personKind = nullptr;

    CutsceneProcessActive(ctx);
    CHECK_EQ((int)op88Ids.size(), 1);
    CHECK_EQ(op88Ids[0], 9);
    CHECK_EQ(ctx.execCount, 0);
    CHECK(tbl.FindById(9) != nullptr);
    SetCutsceneProcHooks(nullptr);
}

TEST(CutsceneProc, RunForMaster) {
    StepLog log; g_log = &log;
    CutsceneTable tbl;
    CutsceneTypeTable tt;
    CutsceneTypeFns fns{}; fns.bankruptcy = RecMainType6;
    InitCutsceneTypeTable(tt, fns);

    // Two active slots; one lists master 7 as participant, one does not.
    CutsceneSlot tmpl{}; tmpl.partCount = 1;
    CutsceneSlot* a = tbl.AllocSlot(tmpl, /*id*/1);
    a->type = 9; a->partCount = 1; a->partIds[0] = 7;
    a->stateFlags |= kCsFlagActive;               // FindLowestPriority needs bit0
    CutsceneSlot* b = tbl.AllocSlot(tmpl, /*id*/2);
    b->type = 9; b->partCount = 1; b->partIds[0] = 99;
    b->stateFlags |= kCsFlagActive;

    CutsceneContext ctx;
    ctx.table = &tbl; ctx.types = &tt;
    ctx.personKind = nullptr;

    int ran = CutsceneRunForMaster(ctx, /*master*/7);
    CHECK_EQ(ran, 1);
    CHECK_EQ((int)log.mainCalls.size(), 1);       // only slot 1 ran
    CHECK_EQ(log.mainCalls[0], 1);
    // both slots torn down (RunForMaster removes every picked slot)
    CHECK(tbl.FindById(1) == nullptr);
    CHECK(tbl.FindById(2) == nullptr);
}

// ===========================================================================
// DebugCmd dispatch + handlers.
// ===========================================================================
namespace {
struct DbgRec {
    std::vector<int> cmd16Amounts;
    std::vector<int> op90Amounts;
    std::vector<int> messages;
    DebugCmdPerson person;
    bool present = true;
};
DbgRec* g_dbg = nullptr;

bool FindP(guild::i32 id, DebugCmdPerson* out) {
    if (!g_dbg->present) return false;
    *out = g_dbg->person; out->id = id; return true;
}
void SendMsg(guild::i32 from, guild::i32, guild::i32 textId) {
    (void)from; g_dbg->messages.push_back(textId);
}
void QReq16(guild::i32, guild::i32, guild::i32 amt, guild::u8) {
    g_dbg->cmd16Amounts.push_back(amt);
}
void Op90(guild::i32 amt, guild::i32) { g_dbg->op90Amounts.push_back(amt); }

DebugCmdHooks MakeHooks() {
    DebugCmdHooks h{};
    h.findPerson = FindP;
    h.sendEntityMessage = SendMsg;
    h.queueRequest16 = QReq16;
    h.requestBuildOp90 = Op90;
    h.market = 0;
    return h;
}
}  // namespace

TEST(DebugCmd, DispatchGates) {
    DbgRec rec; g_dbg = &rec;
    DebugCmdHooks h = MakeHooks();
    SetDebugCmdHooks(&h);

    rec.person.kind = 6;                  // real person
    rec.present = true;
    // bad command type -> 64. The gate is SIGNED `type >= 46` (disasm 0x5711f3
    // cmp cl,0x2E; jge). Only types >= 46 are rejected here.
    CHECK_EQ((int)DebugCmdDispatchByType(46, 1), 64);
    CHECK_EQ((int)DebugCmdDispatchByType(127, 1), 64);
    // NEGATIVE type is NOT rejected by the binary: it falls through to the
    // (signed-index) funcs_57120D call. With a real person (kind<10) it reaches
    // the deferred ContextAction handler stub (0), NOT 64.
    CHECK_EQ((int)DebugCmdDispatchByType(-1, 1), 0);
    // unresolved person -> 64
    rec.present = false;
    CHECK_EQ((int)DebugCmdDispatchByType(0, 1), 64);
    rec.present = true;
    // real person (kind<10) -> handled (0)
    CHECK_EQ((int)DebugCmdDispatchByType(0, 1), 0);
    // not a real person (kind>=10) -> 1
    rec.person.kind = 15;
    CHECK_EQ((int)DebugCmdDispatchByType(0, 1), 1);
    SetDebugCmdHooks(nullptr);
}

TEST(DebugCmd, RandomModuloMatchesRandNext) {
    guild::crt::Srand(99);
    // Math_RandomModulo(n) == RandNext() % n  (RandNext advances the LCG).
    guild::crt::Srand(99);
    int r0 = guild::crt::RandNext() % 7;
    guild::crt::Srand(99);
    CHECK_EQ(DebugCmdRandomModulo(7), r0);
    // n == 0 -> 0 (no advance).
    guild::crt::Srand(123);
    CHECK_EQ(DebugCmdRandomModulo(0), 0);
}

TEST(DebugCmd, ScaledGoldFormula) {
    // gold = (int)(wealth * (roll + base) * scale)
    CHECK_EQ(DebugCmdScaledGold(10000, /*roll*/2, /*base*/1.0, /*scale*/0.01),
             (guild::i32)(10000.0 * (2.0 + 1.0) * 0.01));   // 300
    CHECK_EQ(DebugCmdScaledGold(0, 5, 1.0, 0.01), 0);
}

TEST(DebugCmd, SpawnEntityScaledAEmitsCmd16) {
    DbgRec rec; g_dbg = &rec;
    DebugCmdHooks h = MakeHooks();
    SetDebugCmdHooks(&h);
    rec.person.kind = 4;
    rec.person.wealth = 50000;

    guild::crt::Srand(7);
    int roll = guild::crt::RandNext() % 3;       // predict the roll
    guild::crt::Srand(7);
    guild::i32 got = DebugCmdSpawnEntityScaledA(/*personId*/123);
    CHECK_EQ((int)got, 0);
    CHECK_EQ((int)rec.cmd16Amounts.size(), 1);
    CHECK_EQ(rec.cmd16Amounts[0],
             DebugCmdScaledGold(50000, roll, 1.0, 0.01));
    CHECK_EQ((int)rec.messages.size(), 1);
    SetDebugCmdHooks(nullptr);
}

TEST(DebugCmd, IfNotType7GateAndOp90) {
    DbgRec rec; g_dbg = &rec;
    DebugCmdHooks h = MakeHooks();
    SetDebugCmdHooks(&h);

    // gate: buildType group == 7 -> 1024
    rec.person.kind = 4; rec.person.buildType = 7; rec.person.wealth = 1000;
    CHECK_EQ((int)DebugCmdSpawnEntityIfNotType7(5), 1024);
    CHECK_EQ((int)rec.cmd16Amounts.size(), 0);

    // not type 7 -> emit cmd16
    rec.person.buildType = 3;
    guild::crt::Srand(1);
    CHECK_EQ((int)DebugCmdSpawnEntityIfNotType7(5), 0);
    CHECK_EQ((int)rec.cmd16Amounts.size(), 1);

    // SendEntityWithFlagA: amount = roll[0,3)+2; gate amount <= charges.
    rec.person.charges = 0;                       // every amount > 0 fails
    CHECK_EQ((int)DebugCmdSendEntityWithFlagA(5), 1024);
    rec.person.charges = 100;
    rec.op90Amounts.clear();
    CHECK_EQ((int)DebugCmdSendEntityWithFlagA(5), 0);
    CHECK_EQ((int)rec.op90Amounts.size(), 1);
    CHECK(rec.op90Amounts[0] < 0);                // op90(-amount)

    SetDebugCmdHooks(nullptr);
}

TEST(DebugCmd, NpcTableEntryMapping) {
    // The translated handlers are keyed by their funcs_5766CB index.
    CHECK(DebugCmdNpcTableEntry(0)  == &DebugCmdSpawnEntityScaledA);
    CHECK(DebugCmdNpcTableEntry(4)  == &DebugCmdSpawnEntityIfNotType7);
    CHECK(DebugCmdNpcTableEntry(32) == &DebugCmdSendEntityWithFlagA);
    CHECK(DebugCmdNpcTableEntry(33) == &DebugCmdSendEntityWithFlagB);
    CHECK(DebugCmdNpcTableEntry(1)  == nullptr);     // deferred
}

// ===========================================================================
// Wave-12 hardening: malformed slot / out-of-range type / oversized counts.
// These pin the bounds added to the per-type table dispatch and the participant
// scans. ASAN+UBSAN must stay clean; goldens are unchanged.
// ===========================================================================

// A slot type byte >= kCutsceneTypeCount (12) must not index past the 12-entry
// per-type table. Drive ExecMainFunc with a garbage type and a real type table.
TEST(CutsceneHarden, ExecMainFuncOutOfRangeType) {
    static int g_calls = 0;
    g_calls = 0;
    struct M { static int Fn(CutsceneSlot*) { ++g_calls; return 5; } };

    CutsceneTypeTable types;
    for (int t = 0; t < kCutsceneTypeCount; ++t)
        types[t] = CutsceneTypeEntry{ &M::Fn, nullptr, nullptr, 0, &M::Fn };

    CutsceneTable tbl;
    CutsceneContext ctx;
    ctx.table = &tbl; ctx.types = &types;

    CutsceneSlot s{};
    s.id = 1; s.partCount = 1;
    s.type = 200;                              // OUT OF RANGE (>= 12)
    int r = CutsceneExecMainFunc(ctx, &s);     // must not OOB-dispatch
    CHECK_EQ(r, 0);                            // no fn for an invalid type
    CHECK_EQ(g_calls, 0);

    // a valid type still dispatches (byte-identical behaviour).
    s.type = 4;
    r = CutsceneExecMainFunc(ctx, &s);
    CHECK_EQ(r, 5);
    CHECK_EQ(g_calls, 1);
}

// partCount (+48) larger than the 16-entry partIds[] must not over-read.
TEST(CutsceneHarden, ActorScanOversizedPartCount) {
    CutsceneTable tbl;
    CutsceneSlot tmpl{};
    tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/7);
    s->partCount = 255;                        // MALFORMED: far past 16
    for (int i = 0; i < kMaxParticipants; ++i) s->partIds[i] = 1000 + i;

    CutsceneContext ctx;
    ctx.table = &tbl;
    // a known participant within the clamped window resolves.
    CHECK(CutsceneActorHasParticipant(ctx, 1005, s));
    // an id only reachable past partIds[16] would be a stale read; with the clamp
    // it is simply not found (no OOB).
    CHECK(!CutsceneActorHasParticipant(ctx, 999999, s));

    // Remove-participant with the oversized count must also stay bounded.
    CHECK_EQ(tbl.RemoveParticipant(7, 1005), 1);
    CHECK(!CutsceneActorHasParticipant(ctx, 1005, s));  // now cleared
}

// AddParticipant on a slot whose count is already past capacity must reject
// (full) without scanning past partIds[].
TEST(CutsceneHarden, AddParticipantOversizedCountRejects) {
    CutsceneTable tbl;
    CutsceneSlot tmpl{};
    tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/9);
    s->partCount = 200;                        // already "full" and then some
    for (int i = 0; i < kMaxParticipants; ++i) s->partIds[i] = -1;
    CHECK_EQ(tbl.AddParticipant(9, 42), 0);    // full -> 0, no OOB dedup scan
}

// 0-participant slot: scans are empty, ProcessActive skips it (alive gate).
TEST(CutsceneHarden, ZeroParticipantSlotSkipped) {
    CutsceneTable tbl;
    CutsceneSlot tmpl{};
    tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/3);
    s->partCount = 0;                          // alive gate off
    CutsceneContext ctx;
    ctx.table = &tbl;
    CHECK(!CutsceneActorHasParticipant(ctx, 1, s));

    // Full 16-participant slot: every slot is scannable, none past the array.
    s->partCount = kMaxParticipants;
    for (int i = 0; i < kMaxParticipants; ++i) s->partIds[i] = 500 + i;
    CHECK(CutsceneActorHasParticipant(ctx, 515, s));
    CHECK_EQ(tbl.AddParticipant(3, 600), 0);   // full -> rejects
}

// AddActorToSlot / RemoveActorFromSlot on the 8-entry actor list: full list and
// not-present id must stay within 8 entries.
TEST(CutsceneHarden, ActorSlotListBounds) {
    guild::i32 list[8];
    for (int i = 0; i < 8; ++i) list[i] = 100 + i;   // FULL, none == target
    int idx = CutsceneAddActorToSlot(/*id*/999, list);
    CHECK_EQ(idx, 8);                                  // list full -> 8 (no write)
    for (int i = 0; i < 8; ++i) CHECK_EQ(list[i], 100 + i);

    // remove an absent id -> returns 8, no OOB.
    CHECK_EQ(CutsceneRemoveActorFromSlot(999, list), 8);
    // remove a present id at the end.
    CHECK_EQ(CutsceneRemoveActorFromSlot(107, list), 7);
    CHECK_EQ(list[7], -1);
    // now a free slot exists -> add settles there.
    CHECK_EQ(CutsceneAddActorToSlot(999, list), 7);
    CHECK_EQ(list[7], 999);
}
