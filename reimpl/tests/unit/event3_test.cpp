// Unit tests for src/world/event3.cpp — the further VIBE_Event_* He-action slice.
// Golden vectors for the RNG-driven paths are computed against the documented LCG
// (state = state*1103515245 + 12345; RandNext = (state>>16)&0x7FFF; RandomModulo
// = RandNext()%n) from the MSVC-CRT initial seed 1; the GameTime arithmetic is
// checked against the recovered VIBE_GameTime_Advance semantics.
#include "world/event3.h"
#include "sim/he.h"
#include "sim/npcaction.h"
#include "crt/rand.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using guild::world::Event3Hooks;
using guild::world::SetEvent3Hooks;
using guild::sim::HeRecord;
using guild::sim::HeBytes;
using guild::sim::GameTime;
using guild::sim::SetNpcClock;

namespace {

// --- raw record helpers mirroring the original byte offsets ----------------
i32&      RD(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
u16&      RW(HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
u8&       RB(HeRecord* h, int off) { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
GameTime& RT(HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }

HeRecord MakeHe() { HeRecord h; std::memset(&h, 0, sizeof(h)); return h; }

// --- recording mock hooks --------------------------------------------------
struct Recorder {
    int frees = 0;
    int changeActions = 0;
    int stampReqs = 0;
    std::vector<i32> freedIds;       // person id (+172) of the freed record
    std::vector<u16> actionCharIds;  // charId of each ChangePlayerAction
    // scripted query results:
    void* personRet = nullptr;
    u16   charId = 0;
    i32   scriptHandle = -1;
    void* buildingRet = nullptr;
    i32   guardState = 0;
    i32   guardSlots[6] = {0,0,0,0,0,0};
    i32   guardOwner = 0;
    i32   variantRet = 0;
    i32   enqueueRet = 0;
    i32   guard61Ret = 0;
    int   enqueueCalls = 0;
    int   guard61Calls = 0;
    // handler-pool walk:
    std::vector<HeRecord*> pool;
    size_t cursor = 0;
    i32    poolFilterKind = -1;
    // packet/listener:
    i32   packetStatusRet = 0;
    int   listenerCalls = 0;
    double outputRatio = 0.0;
    int   adjustStockCalls = 0;
    i32   lastStockDelta = 0;
    int   reports = 0;
};
Recorder* g_rec = nullptr;

i32  H_free(HeRecord* h) { g_rec->frees++; g_rec->freedIds.push_back(RD(h,172)); return 7; }
void* H_findPerson(i32) { return g_rec->personRet; }
u16  H_charId(void*) { return g_rec->charId; }
i32  H_scriptHandle(void*) { return g_rec->scriptHandle; }
HeRecord* H_findFirst(i32, i32, i32 kind) {
    g_rec->poolFilterKind = kind; g_rec->cursor = 0;
    return g_rec->pool.empty() ? nullptr : g_rec->pool[g_rec->cursor++];
}
HeRecord* H_findNext() {
    return g_rec->cursor < g_rec->pool.size() ? g_rec->pool[g_rec->cursor++] : nullptr;
}
void H_stampReq(HeRecord*) { g_rec->stampReqs++; }
void* H_findBuilding(i32) { return g_rec->buildingRet; }
void* H_findObject(i32) { return nullptr; }
void H_changeAction(void*, void*, void*, u16 c) { g_rec->changeActions++; g_rec->actionCharIds.push_back(c); }
i32  H_enqueueObj(i32, i32, i32, i32, i32, i32, i32, i32) { g_rec->enqueueCalls++; return g_rec->enqueueRet; }
i32  H_guard61(void*, i32, i32, i32) { g_rec->guard61Calls++; return g_rec->guard61Ret; }
void H_pair33(i32, i32) {}
i32  H_req17(i32, i32, i32, i32, i32, i32) { return 555; }
i32  H_packetStatus(i32) { return g_rec->packetStatusRet; }
i32  H_packetSeq(i32) { return 0x1000; }
i32  H_variant(i32, i32) { return g_rec->variantRet; }
i32  H_guardState(void*) { return g_rec->guardState; }
i32  H_guardSlot(void*, int s) { return (s >= 0 && s < 6) ? g_rec->guardSlots[s] : 0; }
i32  H_guardOwner(void*) { return g_rec->guardOwner; }
i32  H_shuffle(u32 n, i32*) { return static_cast<i32>(n); }
i32  H_listener(const void*, const float*, i32, const float*, i32) { g_rec->listenerCalls++; return 42; }
void H_report(const char*) { g_rec->reports++; }
void H_panelDestroy(HeRecord*, i32) {}
void H_panelCreate(HeRecord*, i32, i32) {}
void H_formSelect(i32, i32) {}
i32  H_richString(const char*, const char*) { return 99; }
i32  H_activeWindow() { return 0; }
i32  H_activeMessage() { return 0; }
void H_resolveEntity(i32* a, i32* b, i32, i32) { if (a) *a = 0; if (b) *b = 0; }
void H_buildModel(i32, i32, u32) {}
double H_outputRatio(void*) { return g_rec->outputRatio; }
void H_adjustStock(u16, i32 d, i32) { g_rec->adjustStockCalls++; g_rec->lastStockDelta = d; }
void H_arrival(HeRecord*, void*, u16) {}

Event3Hooks MakeHooks() {
    Event3Hooks hk{};
    hk.freeHandlerEntry = &H_free;
    hk.findPersonById = &H_findPerson;
    hk.personCharId = &H_charId;
    hk.personScriptHandle = &H_scriptHandle;
    hk.findFirstHandler = &H_findFirst;
    hk.findNextHandler = &H_findNext;
    hk.stampTimeAndRequest = &H_stampReq;
    hk.findBuildingById = &H_findBuilding;
    hk.findObjectById = &H_findObject;
    hk.changePlayerAction = &H_changeAction;
    hk.enqueueObjectInteraction = &H_enqueueObj;
    hk.queueGuardTarget61 = &H_guard61;
    hk.queueRequestPair33 = &H_pair33;
    hk.queueRequest17 = &H_req17;
    hk.packetStatus = &H_packetStatus;
    hk.packetSeq = &H_packetSeq;
    hk.buildingVariantIndex = &H_variant;
    hk.buildingGuardState = &H_guardState;
    hk.buildingGuardSlot = &H_guardSlot;
    hk.buildingGuardOwner = &H_guardOwner;
    hk.initAndShuffleDwordArray = &H_shuffle;
    hk.setListener = &H_listener;
    hk.reportMessage = &H_report;
    hk.eventPanelDestroySlot = &H_panelDestroy;
    hk.eventPanelCreateSlot = &H_panelCreate;
    hk.formSelectWindow = &H_formSelect;
    hk.textRenderRichString = &H_richString;
    hk.activeWindowHandle = &H_activeWindow;
    hk.activeWindowMessage = &H_activeMessage;
    hk.resolveEntityById = &H_resolveEntity;
    hk.objectBuildModelName = &H_buildModel;
    hk.buildingOutputRatio = &H_outputRatio;
    hk.buildingAdjustStock = &H_adjustStock;
    hk.sendArrivalMessage = &H_arrival;
    return hk;
}

void SetClock(int day, int hour, int minute, int second) {
    GameTime t{}; t.day = day; t.hour = static_cast<u16>(hour); t.minute = minute; t.second = second;
    SetNpcClock(t);
}

}  // namespace

// ---------------------------------------------------------------------------
// Predicates (no RNG / no hooks)
// ---------------------------------------------------------------------------
TEST(Event3, MatchPersonState15Cmd272) {
    // cmd layout: byte0=kind, dword+4=personPtr, dword+12=cmd code.
    alignas(8) std::uint8_t cmd[16] = {0};
    cmd[0] = 25;
    void* person = reinterpret_cast<void*>(0x1234);
    std::memcpy(cmd + 4, &person, sizeof(person) >= 4 ? 4 : sizeof(person));
    // ensure pointer slot non-zero on both 32/64-bit
    std::memcpy(cmd + 4, &person, sizeof(void*));
    i32 code = 272; std::memcpy(cmd + 12, &code, 4);

    CHECK(world::MatchPersonState15Cmd272(cmd, 15));        // all conditions met
    CHECK(!world::MatchPersonState15Cmd272(cmd, 14));       // wrong state
    cmd[0] = 24;
    CHECK(!world::MatchPersonState15Cmd272(cmd, 15));       // wrong kind
    cmd[0] = 25;
    code = 271; std::memcpy(cmd + 12, &code, 4);
    CHECK(!world::MatchPersonState15Cmd272(cmd, 15));       // wrong cmd code
}

TEST(Event3, MatchType26OrJump) {
    alignas(8) std::uint8_t cmd[16] = {0};
    cmd[0] = 26;
    i32 nonzero = 0x55; std::memcpy(cmd + 4, &nonzero, 4);
    CHECK(world::MatchType26OrJump(cmd));      // kind 26, person set -> matched
    cmd[0] = 25;
    CHECK(!world::MatchType26OrJump(cmd));     // wrong kind -> fall-through (false)
    cmd[0] = 26;
    i32 zero = 0; std::memcpy(cmd + 4, &zero, 4);
    CHECK(!world::MatchType26OrJump(cmd));     // no person -> false
}

// ---------------------------------------------------------------------------
// SetActorAnimById — season anim + RNG (golden vector)
// ---------------------------------------------------------------------------
TEST(Event3, SetActorAnimByldSeasonAndRng) {
    crt::Srand(1);
    SetClock(/*day*/8, /*hour*/0, 0, 0);   // day 8 -> season 8%4 = 0 -> base 8.0
    HeRecord h = MakeHe();
    i32 ret = world::SetActorAnimById(&h);
    CHECK_EQ(ret, 8);                 // (int)flt_6476FC[0] == 8
    CHECK_EQ((int)RW(&h, 86), 8);     // +86 := 8
    CHECK_EQ(RD(&h, 88), 0);          // +88 := 0
    CHECK_EQ(RD(&h, 172), 3 - 5);     // RandomModulo(5)@seed1 == 3 -> -2

    crt::Srand(1);
    SetClock(/*day*/11, 0, 0, 0);     // 11%4 = 3 -> winter -> base 9.0
    HeRecord h2 = MakeHe();
    CHECK_EQ(world::SetActorAnimById(&h2), 9);
}

// ---------------------------------------------------------------------------
// StartActorAction — clock stamp + advance (golden vector)
// ---------------------------------------------------------------------------
TEST(Event3, StartActorActionStampAndAdvance) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.personRet = reinterpret_cast<void*>(1);
    rec.charId = 0x33;
    SetEvent3Hooks(&hk);

    SetClock(5, 10, 0, 0);
    HeRecord h = MakeHe();
    RD(&h, 172) = 100;     // person id
    RD(&h, 16) = 50;       // building id
    RD(&h, 196) = -1;      // no object

    i32 ret = world::StartActorAction(&h);
    CHECK_EQ(ret, 16);                       // 10:00 + 400min = 16:40 -> hour 16
    CHECK_EQ((int)RT(&h, 180).minute, 40);   // advanced block at +180
    CHECK_EQ(RD(&h, 176), 200);
    CHECK_EQ(rec.changeActions, 1);
    CHECK_EQ((int)rec.actionCharIds[0], 0x33);
    CHECK_EQ((int)RT(&h, 82).day, 5);        // appointment stamped from clock

    SetEvent3Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// RequestGuardInteraction — both branches (RNG golden vector)
// ---------------------------------------------------------------------------
TEST(Event3, RequestGuardInteractionSpawnPath) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.buildingRet = reinterpret_cast<void*>(0xB);
    rec.guardState = 2;                  // -> enqueue object interaction
    rec.enqueueRet = 1234;
    SetEvent3Hooks(&hk);
    crt::Srand(1);                       // RandomModulo(12)=2, RandomModulo(5)=3
    SetClock(2, 8, 0, 0);

    HeRecord h = MakeHe();
    RD(&h, 180) = 9;
    i32 ret = world::RequestGuardInteraction(&h);
    CHECK_EQ(ret, 1234);
    CHECK_EQ(rec.enqueueCalls, 1);
    CHECK_EQ(rec.guard61Calls, 0);
    CHECK_EQ(RD(&h, 172), 1234);
    CHECK_EQ((int)RT(&h, 82).day, 2);    // appointment stamped

    SetEvent3Hooks(nullptr);
}

TEST(Event3, RequestGuardInteractionGuardSlotPath) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.buildingRet = reinterpret_cast<void*>(0xB);
    rec.guardState = 0;                  // -> guard-target path
    rec.guardSlots[2] = 0x77;            // slot 5 empty; first set slot scanning 4..0 is slot 2
    rec.guard61Ret = 888;
    SetEvent3Hooks(&hk);
    HeRecord h = MakeHe();
    i32 ret = world::RequestGuardInteraction(&h);
    CHECK_EQ(ret, 888);
    CHECK_EQ(rec.guard61Calls, 1);
    CHECK_EQ(rec.enqueueCalls, 0);
    SetEvent3Hooks(nullptr);
}

TEST(Event3, RequestGuardInteractionNoBuildingFrees) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.buildingRet = nullptr;           // building missing -> free
    SetEvent3Hooks(&hk);
    HeRecord h = MakeHe();
    i32 ret = world::RequestGuardInteraction(&h);
    CHECK_EQ(ret, 7);                    // mock free returns 7
    CHECK_EQ(rec.frees, 1);
    SetEvent3Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// SinkToGroundStateMachine — phase transitions
// ---------------------------------------------------------------------------
TEST(Event3, SinkToGroundPhases) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.personRet = reinterpret_cast<void*>(1);
    rec.charId = 5;
    SetEvent3Hooks(&hk);
    SetClock(3, 12, 0, 0);

    // Phase 2 (counter 0): play action + advance counter.
    HeRecord h = MakeHe();
    RD(&h, 112) = 0;
    world::SinkToGroundStateMachine(&h);
    CHECK_EQ(RD(&h, 112), 1);
    CHECK_EQ(rec.changeActions, 1);

    // Phase 3 (counter 1): script clear -> advance.
    rec.scriptHandle = -1;
    world::SinkToGroundStateMachine(&h);
    CHECK_EQ(RD(&h, 112), 2);

    // Phase 4 (counter 2): action + advance(1,0,0) + counter. The first Advance
    // arg is added to the hour-of-day result (see gametime.cpp: result =
    // addDays + hour), so clock 12:00 -> 13:00, day unchanged.
    world::SinkToGroundStateMachine(&h);
    CHECK_EQ(RD(&h, 112), 3);
    CHECK_EQ((int)RT(&h, 82).day, 3);     // day unchanged
    CHECK_EQ((int)RT(&h, 82).hour, 13);   // 12 + 1

    // Phase 5 (counter 3): flag bit2 set -> pair33 then free.
    RB(&h, 120) = 2;
    world::SinkToGroundStateMachine(&h);
    CHECK_EQ(rec.frees, 1);

    // Teardown (counter -2): free.
    HeRecord h2 = MakeHe();
    RD(&h2, 112) = -2;
    world::SinkToGroundStateMachine(&h2);
    CHECK_EQ(rec.frees, 2);

    SetEvent3Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// AllocKillPlayer — handler-pool scan + schedule reset
// ---------------------------------------------------------------------------
TEST(Event3, AllocKillPlayerFindsAndResets) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.personRet = reinterpret_cast<void*>(1);  // person found
    SetEvent3Hooks(&hk);

    HeRecord self = MakeHe();
    RD(&self, 172) = 42;
    HeRecord other = MakeHe();
    RD(&other, 172) = 42;          // matches self's person id
    RT(&other, 68).day = 9;        // saved time -> copied to appointment
    rec.pool = { &self, &other };  // self first (skipped), then match

    HeRecord* found = world::AllocKillPlayer(&self);
    CHECK(found == &other);
    CHECK_EQ(rec.poolFilterKind, 114);
    CHECK_EQ(RD(&other, 180), -1);
    CHECK_EQ(RD(&other, 176), -1);
    CHECK_EQ(RD(&other, 132), -1);
    CHECK_EQ((int)RT(&other, 82).day, 9);   // appointment <- saved time
    CHECK_EQ(rec.frees, 0);

    SetEvent3Hooks(nullptr);
}

TEST(Event3, AllocKillPlayerMissingPersonReportsAndFrees) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.personRet = nullptr;       // person missing
    SetEvent3Hooks(&hk);
    HeRecord self = MakeHe();
    world::AllocKillPlayer(&self);
    CHECK_EQ(rec.reports, 1);
    CHECK_EQ(rec.frees, 1);
    SetEvent3Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// CancelMatchingActors — re-stamp matching armed handlers
// ---------------------------------------------------------------------------
TEST(Event3, CancelMatchingActorsRestampsMatches) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    SetEvent3Hooks(&hk);

    HeRecord self = MakeHe();
    RD(&self, 4) = 77;             // person key

    HeRecord a = MakeHe();         // armed (flag bit1), matching +176
    RB(&a, 120) = 1; RD(&a, 176) = 77;
    HeRecord b = MakeHe();         // armed (flag bit2), non-matching +176
    RB(&b, 120) = 2; RD(&b, 176) = 99;
    rec.pool = { &a, &b };

    world::CancelMatchingActors(&self);
    CHECK_EQ(rec.poolFilterKind, 35);
    CHECK_EQ(rec.stampReqs, 1);    // only `a` matches and gets re-stamped
    SetEvent3Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// UpdateListenerFromActor — transform read + countdown decrement
// ---------------------------------------------------------------------------
TEST(Event3, UpdateListenerFromActor) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    SetEvent3Hooks(&hk);

    float actor[40] = {0};
    actor[33] = 10.0f; actor[34] = 2.0f; actor[35] = 3.0f;
    HeRecord h = MakeHe();
    RB(&h, 208) = 5;
    i32 r = world::UpdateListenerFromActor(actor, &h);
    CHECK_EQ(r, 42);
    CHECK_EQ(rec.listenerCalls, 1);
    CHECK_EQ((int)RB(&h, 208), 4);   // decremented
    SetEvent3Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// MoveTowardTargetRun — phase 0 transport math, phase 1 arrival
// ---------------------------------------------------------------------------
TEST(Event3, MoveTowardTargetRunTransport) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.personRet = reinterpret_cast<void*>(1);
    rec.charId = 4;
    rec.outputRatio = 0.0;          // ratioScaled = 0 -> within cap
    SetEvent3Hooks(&hk);

    SetClock(5, 1, 0, 0);           // clock at 01:00
    HeRecord h = MakeHe();
    RD(&h, 112) = 0;
    RD(&h, 176) = 50;               // remaining stock
    // +96 scratch set to 00:00 so diffMinutes(clock - scratch) = 60.
    RT(&h, 96).day = 5; RT(&h, 96).hour = 0; RT(&h, 96).minute = 0;
    // moved = 60 * 0.1 * 5 = 30; remaining 50 -> 20.
    world::MoveTowardTargetRun(&h);
    CHECK_EQ(rec.adjustStockCalls, 1);
    CHECK_EQ(rec.lastStockDelta, 30);
    CHECK_EQ(RD(&h, 176), 20);
    CHECK_EQ((int)RT(&h, 96).hour, 1);  // scratch refreshed to clock

    SetEvent3Hooks(nullptr);
}

TEST(Event3, MoveTowardTargetRunArrivalFrees) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    rec.personRet = reinterpret_cast<void*>(1);
    rec.buildingRet = reinterpret_cast<void*>(2);
    SetEvent3Hooks(&hk);
    HeRecord h = MakeHe();
    RD(&h, 112) = 1;            // arrival phase
    world::MoveTowardTargetRun(&h);
    CHECK_EQ(rec.frees, 1);
    CHECK_EQ(rec.changeActions, 1);
    SetEvent3Hooks(nullptr);
}

// ---------------------------------------------------------------------------
// GatherTargetsInit — slot reset + seed pick + follower fill
// ---------------------------------------------------------------------------
TEST(Event3, GatherTargetsInitFillsFollowers) {
    crt::Srand(1);
    SetClock(2, 9, 0, 0);
    HeRecord h = MakeHe();
    RD(&h, 172) = -1;          // unset seed -> random pick

    // 4 candidates; seed pick = RandomModulo(4)@seed1.
    crt::Srand(1);
    // compute expected pick
    // (mirror RandNext)
    std::uint32_t st = 1; auto rn = [&]{ st = st*1103515245u + 12345u; return (int)((st>>16)&0x7FFF); };
    int expectPick = rn() % 4;
    crt::Srand(1);

    i32 cand[4] = {100, 200, 300, 400};
    bool tol[4]  = {true, true, true, true};
    world::GatherTargetsInit(&h, cand, 4, tol);

    // seed slot set to the picked candidate
    CHECK_EQ(RD(&h, 172), cand[expectPick]);
    // follower slots (44..) hold the non-seed in-tolerance candidates.
    int followers = 0;
    for (int idx = 44; idx <= 58; ++idx) if (RD(&h, 4*idx) != -1) followers++;
    CHECK_EQ(followers, 3);                 // 4 candidates minus the seed
    CHECK_EQ(RD(&h, 256), 0);
    CHECK_EQ(RD(&h, 260), 0);
    CHECK_EQ((int)RT(&h, 82).day, 2);       // appointment stamped from clock
}

// ---------------------------------------------------------------------------
// NewDepositMessageBoxRun / RequestSlotResultRun phase machines
// ---------------------------------------------------------------------------
TEST(Event3, NewDepositMessageBoxAdvances) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    SetEvent3Hooks(&hk);
    HeRecord h = MakeHe();
    RD(&h, 112) = 0;          // phase 2 (counter 0 -> +2)
    RD(&h, 172) = 4;          // deposit text id
    i32 r = world::NewDepositMessageBoxRun(&h);
    CHECK_EQ(r, 99);          // rich-string mock return
    CHECK_EQ(RD(&h, 112), 1); // counter advanced
    // teardown phase (counter -2)
    RD(&h, 112) = -2;
    world::NewDepositMessageBoxRun(&h);
    CHECK_EQ(rec.frees, 1);
    SetEvent3Hooks(nullptr);
}

TEST(Event3, RequestSlotResultRunQueuesWhenDue) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    SetEvent3Hooks(&hk);
    SetClock(5, 12, 0, 0);
    HeRecord h = MakeHe();
    RD(&h, 112) = 0;                       // phase 2
    // appointment in the past so the compare is <= 0.
    RT(&h, 82).day = 1; RT(&h, 82).hour = 0;
    RD(&h, 178) = (i32)(7u << 16);         // object id 7 in high word
    i32 r = world::RequestSlotResultRun(&h);
    CHECK_EQ(r, 555);                      // queueRequest17 mock return
    CHECK_EQ(RD(&h, 200), 555);            // handle stored
    CHECK_EQ(RD(&h, 112), 1);              // advanced
    SetEvent3Hooks(nullptr);
}

TEST(Event3, RequestSlotResultRunTeardownFrees) {
    Recorder rec; g_rec = &rec;
    Event3Hooks hk = MakeHooks();
    SetEvent3Hooks(&hk);
    HeRecord h = MakeHe();
    RD(&h, 112) = -2;
    i32 r = world::RequestSlotResultRun(&h);
    CHECK_EQ(r, 7);
    CHECK_EQ(rec.frees, 1);
    SetEvent3Hooks(nullptr);
}
