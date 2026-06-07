// E2E: drive a synthetic cutscene through ProcessActive tick-by-tick to
// completion, and execute a sequence of DebugCmd event-commands, verifying the
// cutscene state machine, the per-type dispatch, the participant gate, and the
// emitted commands against a reference.
#include "tests/framework/test.h"
#include "sim/cutscene.h"
#include "sim/cutscene_process.h"
#include "sim/debugcmd.h"
#include "crt/rand.h"

#include <vector>
#include <cstring>

using namespace guild::sim;

namespace {
GameTime MkTime(int day, int hour, int minute, int second) {
    GameTime t{};
    t.day = day; t.hour = static_cast<guild::u16>(hour);
    t.minute = minute; t.second = second;
    return t;
}

// Step/main observation log shared by the e2e cutscene.
struct Trace {
    std::vector<int> mains;
    std::vector<int> steps;
    int stepReturn = 1;
};
Trace* g_tr = nullptr;
int E2EMain(CutsceneSlot* s) { g_tr->mains.push_back(s->id); return 0; }
int E2EStep(CutsceneSlot* s) { g_tr->steps.push_back(s->id); return g_tr->stepReturn; }
// A step fn that, on a "ready" verdict, also latches the slot's ready bit
// (stateFlags bit 0x02) — the mechanism the real per-type prepare/step fns use
// to tell ProcessActive's cleanup to FINISH (and thus exec) the slot.
int E2EStepReadyBit(CutsceneSlot* s) {
    g_tr->steps.push_back(s->id);
    if (g_tr->stepReturn) s->stateFlags |= 0x02;   // mark ready
    return g_tr->stepReturn;
}
}  // namespace

// ===========================================================================
// A wedding-style (type 6) cutscene driven tick by tick:
//   phase 1: clock before the ready window -> nothing happens.
//   phase 2: clock past the -60s ready window -> PrepareReady passes, started
//            set, step fn runs (returns ready).
//   phase 3: clock past the timeout window -> the slot is finished, ExecMainFunc
//            runs, the slot is torn down.
// ===========================================================================
TEST(CutsceneE2E, WeddingToCompletion) {
    Trace tr; g_tr = &tr; tr.stepReturn = 1;

    CutsceneTable tbl;
    CutsceneTypeTable tt;
    CutsceneTypeFns fns{};
    fns.wedding = E2EMain;            // type 6 main
    fns.checkMarriage = E2EStepReadyBit;  // type 6 step (latches the ready bit)
    InitCutsceneTypeTable(tt, fns);
    CutsceneRng rng;

    CutsceneSlot tmpl{}; tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/77);
    s->type = 6;
    s->master = 3;
    s->partCount = 2; s->partIds[0] = 3; s->partIds[1] = 4;  // master participates
    s->started = 0;
    s->readyTime = MkTime(5, 12, 0, 0);          // timeout/ready window: day5 12:00
    *reinterpret_cast<guild::i32*>(reinterpret_cast<guild::u8*>(s) + 120) = 2024; // seed

    CutsceneContext ctx;
    ctx.table = &tbl; ctx.types = &tt; ctx.rng = &rng;
    ctx.localMaster = 3; ctx.localSlotId = 77;
    ctx.personKind = nullptr;        // all participants resolve

    // ---- phase 1: clock well before the ready window (-60s -> 11:59) ----
    ctx.clock = MkTime(5, 11, 0, 0);
    CutsceneProcessActive(ctx);
    CHECK_EQ(s->started, 0);
    CHECK_EQ((int)tr.steps.size(), 0);
    CHECK_EQ(ctx.execCount, 0);
    CHECK(tbl.FindById(77) != nullptr);

    // ---- phase 2: clock in the ready window (11:59 < clock < 12:00) ----
    ctx.clock = MkTime(5, 11, 59, 30);            // past -60s window, before timeout
    CutsceneProcessActive(ctx);
    CHECK_EQ(s->started, 1);                       // started latched
    CHECK_EQ((int)tr.steps.size(), 1);             // step fn fired once
    CHECK_EQ(tr.steps[0], 77);
    CHECK_EQ(ctx.execCount, 0);                     // not yet at timeout
    CHECK(tbl.FindById(77) != nullptr);

    // ---- phase 3: clock past the timeout window -> finish + exec + teardown ----
    ctx.clock = MkTime(5, 12, 0, 1);
    CutsceneProcessActive(ctx);
    CHECK_EQ(ctx.execCount, 1);
    CHECK_EQ((int)tr.mains.size(), 1);
    CHECK_EQ(tr.mains[0], 77);
    CHECK_EQ(rng.GetSeed(), 2024);                 // exec seeded the cutscene RNG
    CHECK(tbl.FindById(77) == nullptr);            // torn down
}

// ===========================================================================
// Participant gate: a slot whose local master is NOT a participant and whose
// timeout has passed is torn down WITHOUT executing (orphan cleanup).
// ===========================================================================
TEST(CutsceneE2E, OrphanSlotTornDownWithoutExec) {
    Trace tr; g_tr = &tr;
    CutsceneTable tbl;
    CutsceneTypeTable tt;
    // Type 6 (wedding) HAS a step fn, so `finished` is not auto-set when the
    // clock passes the timeout — it stays 0 unless the slot started/readied.
    CutsceneTypeFns fns{}; fns.wedding = E2EMain; fns.checkMarriage = E2EStep;
    InitCutsceneTypeTable(tt, fns);

    CutsceneSlot tmpl{}; tmpl.partCount = 1;
    CutsceneSlot* s = tbl.AllocSlot(tmpl, /*id*/8);
    s->type = 6;
    s->master = 50;                  // NOT the local master -> never starts here
    s->partCount = 1; s->partIds[0] = 50;
    s->stateFlags = 0;               // high bit (0x80) clear, ready bit (0x02) clear
    s->readyTime = MkTime(1, 0, 0, 0);

    CutsceneContext ctx;
    ctx.table = &tbl; ctx.types = &tt;
    ctx.localMaster = 7;             // local master 7 is NOT a participant
    ctx.localSlotId = -1;
    ctx.clock = MkTime(2, 0, 0, 0);  // past timeout
    ctx.personKind = nullptr;

    // step fn present -> finished stays 0; clock past timeout, not finished ->
    // orphan-cleanup branch: local master not a participant, master != local
    // master, ready bit clear -> the slot is removed without executing.
    CutsceneProcessActive(ctx);
    CHECK_EQ(ctx.execCount, 0);                    // never executed
    CHECK_EQ((int)tr.mains.size(), 0);
    CHECK(tbl.FindById(8) == nullptr);             // removed as orphan
}

// ===========================================================================
// DebugCmd console sequence: execute a batch of event-commands and verify the
// emitted network commands + entity messages against a reference.
// ===========================================================================
namespace {
struct CmdBus {
    std::vector<int> cmd16;
    std::vector<int> op90;
    std::vector<int> msgs;
    DebugCmdPerson person;
    bool present = true;
};
CmdBus* g_bus = nullptr;
bool BusFind(guild::i32 id, DebugCmdPerson* out) {
    if (!g_bus->present) return false;
    *out = g_bus->person; out->id = id; return true;
}
void BusMsg(guild::i32, guild::i32, guild::i32 t) { g_bus->msgs.push_back(t); }
void BusCmd16(guild::i32, guild::i32, guild::i32 a, guild::u8) { g_bus->cmd16.push_back(a); }
void BusOp90(guild::i32 a, guild::i32) { g_bus->op90.push_back(a); }
}  // namespace

TEST(CutsceneE2E, DebugCmdConsoleSequence) {
    CmdBus bus; g_bus = &bus;
    DebugCmdHooks h{};
    h.findPerson = BusFind;
    h.sendEntityMessage = BusMsg;
    h.queueRequest16 = BusCmd16;
    h.requestBuildOp90 = BusOp90;
    h.market = 0;
    SetDebugCmdHooks(&h);

    bus.person.kind = 4;             // real person
    bus.person.wealth = 20000;
    bus.person.buildType = 3;        // not type 7
    bus.person.charges = 10;

    // Reseed once and replay the exact same sequence into a reference oracle.
    auto runSequence = [&]() {
        // cmd A: SpawnEntityScaledA (roll%3, gold = wealth*(roll+1)*0.01)
        DebugCmdSpawnEntityScaledA(101);
        // cmd B: SpawnEntityIfNotType7 (roll%4, gold = wealth*(roll+1)*0.01)
        DebugCmdSpawnEntityIfNotType7(102);
        // cmd C: SendEntityWithFlagA (amount = roll%3 + 2; op90(-amount))
        DebugCmdSendEntityWithFlagA(103);
    };

    // ---- reference: compute the expected rolls/golds independently ----
    guild::crt::Srand(2024);
    int rollA = guild::crt::RandNext() % 3;
    int goldA = DebugCmdScaledGold(20000, rollA, 1.0, 0.01);
    int rollB = guild::crt::RandNext() % 4;
    int goldB = DebugCmdScaledGold(20000, rollB, 1.0, 0.01);
    int amtC  = (guild::crt::RandNext() % 3) + 2;

    // ---- actual: run the real handlers off the same seed ----
    guild::crt::Srand(2024);
    runSequence();

    CHECK_EQ((int)bus.cmd16.size(), 2);
    CHECK_EQ(bus.cmd16[0], goldA);
    CHECK_EQ(bus.cmd16[1], goldB);
    CHECK_EQ((int)bus.op90.size(), 1);
    CHECK_EQ(bus.op90[0], -amtC);
    CHECK_EQ((int)bus.msgs.size(), 3);   // each command sent one entity message

    // ---- dispatcher path: kind>=10 short-circuits to 1, no emits ----
    bus.person.kind = 20;
    int before = (int)bus.cmd16.size();
    CHECK_EQ((int)DebugCmdDispatchByType(0, 200), 1);
    CHECK_EQ((int)bus.cmd16.size(), before);   // unchanged

    SetDebugCmdHooks(nullptr);
}
