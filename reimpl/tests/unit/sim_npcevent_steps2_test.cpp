// Unit tests for the second batch of NpcEvent step state machines
// (npcevent_steps2.{h,cpp}): ProtectionMoneyStep, ExtortionStep, PatrolStep,
// GatherGuildMembersStep, AwardTitleStep. Each test drives one step with synthetic
// He state, a seeded CRT RNG, and recording/scripted mock leaves, then checks the
// re-armed record (phase, appointment GameTime, packet slot), the branch taken, and
// the leaf dispatch against the IDA decompilation.
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>

#include "sim/npcevent_steps2.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"
#include "sim/he.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

struct Trace2 {
    int freeCount = 0;
    int entity29Count = 0;
    int lastEntity29Arg = 0;
    int op49Count = 0;
    int op53Count = 0;
    int op25Count = 0;
    int op16Count = 0;
    int op39Count = 0;
    int changeActionCount = 0;
    int msgCount = 0;
    int quickjumpCount = 0;
    int panelCreateCount = 0;
    int panelDestroyCount = 0;
    int renderTextCount = 0;
    int voiceCount = 0;
    int titleDeltaCount = 0;
    // scripted scalars
    int scriptStatus = 0;
    int scriptSeq = 0;
    int scriptCutscene = 0;
    int scriptPerson = 0;     // findPerson result handle
    int scriptObject = 0;     // queryBegin result handle
    int scriptDialog = -1;
    int scriptGate = 1;
    int scriptCompare = 0;
    int scriptCash = 0;
    int scriptBrawl = 0;
    int scriptOwnerWord = 7;  // object +39 owner word (not -1)
    int scriptBusy = 0;       // live-actor +296 busy flag
    int scriptPanel = 0;      // panel handle returned by eventPanelCreate
};
Trace2 g_t;

void ResetTrace() { g_t = Trace2(); }

i32 PersonField(i32 p, int off) {
    if (!p) return 0;
    if (off == 0) return 5;            // method word
    if (off == 1) return 0x3000 + p;   // id
    if (off == 97 * 4) return p ? 0x9000 + p : 0;  // live ch_t ptr (+388)
    if (off == 296) return g_t.scriptBusy; // busy flag on the live actor
    if (off == 0x10) return 1;         // gesture byte (not 6/7)
    if (off == 43 * 4) return 13;      // new title
    return 0;
}
i32 ObjectField(i32 o, int off) {
    if (!o) return 0;
    if (off == 1) return 0x1000 + o;   // object id
    if (off == 39) return g_t.scriptOwnerWord; // owner word
    if (off == 0x10) return 1;         // gesture byte
    if (off == 91) return 0;           // always-pay flag clear
    return 0;
}

NpcLeafHooks MakeLeafHooks() {
    NpcLeafHooks lh{};
    lh.freeHandlerEntry = [](HeRecord* h) -> i32 { ++g_t.freeCount; return reinterpret_cast<intptr_t>(h); };
    lh.queueRequestEntity29 = [](int arg, HeRecord*) -> i32 {
        ++g_t.entity29Count; g_t.lastEntity29Arg = arg; return 0x7777 + arg;
    };
    lh.packetStatus = [](i32) -> i32 { return g_t.scriptStatus; };
    return lh;
}

NpcEventHooks2 MakeHooks() {
    NpcEventHooks2 ev{};
    ev.queryBegin = [](i32) -> i32 { return g_t.scriptObject; };
    ev.findPerson = [](i32) -> i32 { return g_t.scriptPerson; };
    ev.personField = PersonField;
    ev.objectField = ObjectField;
    ev.buildingUpgradeLevel = [](i32) -> int { return 2; };
    ev.computeRoomWorth = [](i32, int, i32) -> i32 { return 1000; };
    ev.sumCurrencyHeld = [](i32) -> i32 { return g_t.scriptCash; };
    ev.patrolBrawlEligible = [](HeRecord*) -> int { return g_t.scriptBrawl; };
    ev.changePlayerAction = [](i32, i32, u16) { ++g_t.changeActionCount; };
    ev.queueRequestSingle49 = [](i32) -> i32 { ++g_t.op49Count; return 0x49; };
    ev.queueRequestNamedObject53 = [](i32, i32, int, int, int, const char*) -> i32 { ++g_t.op53Count; return 0x53; };
    ev.queueRequestArgs25 = [](i32, int, int, int, int) -> i32 { ++g_t.op25Count; return 0x25; };
    ev.queueRequest16 = [](i32, i32, int, int) -> i32 { ++g_t.op16Count; return 0x16; };
    ev.queueRequest39 = [](const void*) -> i32 { ++g_t.op39Count; return 0x39; };
    ev.applyTitleDelta = [](i32, i32, i32) { ++g_t.titleDeltaCount; };
    ev.packetStatus = [](i32) -> i32 { return g_t.scriptStatus; };
    ev.packetSeq = [](i32) -> i32 { return g_t.scriptSeq; };
    ev.cutsceneActive = [](i32) -> int { return g_t.scriptCutscene; };
    ev.sendEntityMessage = [](i32, i32, const char*, int, const char*) { ++g_t.msgCount; };
    ev.sendQuickjumpMessage = [](i32, i32, const char*, int, i32, const char*) { ++g_t.quickjumpCount; };
    ev.compareAwardTime = [](i32) -> int { return g_t.scriptCompare; };
    ev.eventPanelCreate = [](HeRecord*) -> int { ++g_t.panelCreateCount; return g_t.scriptPanel; };
    ev.eventPanelDestroy = [](HeRecord*) { ++g_t.panelDestroyCount; };
    ev.renderAwardText = [](HeRecord*, i32, int) { ++g_t.renderTextCount; };
    ev.playAwardVoice = [](HeRecord*, i32, int) { ++g_t.voiceCount; };
    ev.awardDialogResult = []() -> i32 { return g_t.scriptDialog; };
    ev.awardActivePlayerGate = [](HeRecord*) -> int { return g_t.scriptGate; };
    return ev;
}

// A fresh, zeroed He record with a known clock.
struct Fixture {
    alignas(8) std::uint8_t buf[600];
    HeRecord* h;
    NpcLeafHooks lh;
    NpcEventHooks2 ev;
    Fixture() {
        std::memset(buf, 0, sizeof(buf));
        h = reinterpret_cast<HeRecord*>(buf);
        ResetTrace();
        crt::Srand(1);
        GameTime clk{}; clk.day = 5; clk.hour = 11; clk.minute = 0; clk.second = 0;
        SetNpcClock(clk);
        lh = MakeLeafHooks();
        ev = MakeHooks();
        SetNpcLeafHooks(&lh);
        SetNpcEventHooks2(&ev);
    }
    ~Fixture() { SetNpcLeafHooks(nullptr); SetNpcEventHooks2(nullptr); }
    i32& D(int off) { return *reinterpret_cast<i32*>(buf + off); }
    u8& B(int off) { return *reinterpret_cast<u8*>(buf + off); }
};

} // namespace

// ---------------------------------------------------------------------------
// ProtectionMoneyStep
// ---------------------------------------------------------------------------
TEST(NpcEventSteps2, ProtectionMoney_TeardownFreesAndEmitsWhenFlagged) {
    Fixture f;
    g_t.scriptObject = 7;       // a shop object exists
    f.D(112) = -2;              // teardown phase
    f.B(120) = 2;               // flag bit 2 set -> final op25
    NpcEvent_ProtectionMoneyStep(f.h);
    CHECK_EQ(g_t.op25Count, 1);
    CHECK_EQ(g_t.freeCount, 1);
}

TEST(NpcEventSteps2, ProtectionMoney_MissingTargetArmsTeardown) {
    Fixture f;
    g_t.scriptObject = 0;       // no shop -> can't proceed
    g_t.scriptPerson = 0;
    f.D(112) = 1;
    f.D(132) = -1;              // no pending packet -> re-arm allowed
    NpcEvent_ProtectionMoneyStep(f.h);
    CHECK_EQ(f.D(112), -1);     // armed teardown phase
    CHECK_EQ(g_t.lastEntity29Arg, -1);
}

TEST(NpcEventSteps2, ProtectionMoney_Phase1StartsActionAndArmsPhase2) {
    Fixture f;
    g_t.scriptObject = 7;
    g_t.scriptPerson = 3;
    g_t.scriptOwnerWord = 7;    // valid owner
    f.D(112) = 1;
    f.D(132) = -1;
    NpcEvent_ProtectionMoneyStep(f.h);
    CHECK_EQ(f.D(112), 2);
    CHECK_EQ(g_t.op49Count, 1);
    CHECK_EQ(g_t.op53Count, 1);
    CHECK_EQ(g_t.lastEntity29Arg, 2);
    // appointment advanced 10 minutes from the 11:00 clock.
    GameTime* appt = reinterpret_cast<GameTime*>(f.buf + 82);
    CHECK_EQ((int)appt->hour, 11);
    CHECK_EQ((int)appt->minute, 10);
}

TEST(NpcEventSteps2, ProtectionMoney_Phase2BusyActorReholds) {
    Fixture f;
    g_t.scriptObject = 7;
    g_t.scriptPerson = 3;
    g_t.scriptBusy = 1;         // live actor busy -> stay phase 2, +4 min
    f.D(112) = 2;
    f.D(132) = -1;
    NpcEvent_ProtectionMoneyStep(f.h);
    CHECK_EQ(f.D(112), 2);
    CHECK_EQ(g_t.lastEntity29Arg, 2);
    GameTime* appt = reinterpret_cast<GameTime*>(f.buf + 82);
    CHECK_EQ((int)appt->minute, 4);
}

// ---------------------------------------------------------------------------
// ExtortionStep
// ---------------------------------------------------------------------------
TEST(NpcEventSteps2, Extortion_MissingTargetFrees) {
    Fixture f;
    g_t.scriptObject = 0;
    f.D(112) = 0;
    NpcEvent_ExtortionStep(f.h);
    CHECK_EQ(g_t.freeCount, 1);
}

TEST(NpcEventSteps2, Extortion_Phase0StartsActionArmsPhase1) {
    Fixture f;
    g_t.scriptObject = 7;
    g_t.scriptPerson = 3;
    g_t.scriptOwnerWord = 7;
    f.D(112) = 0;
    NpcEvent_ExtortionStep(f.h);
    CHECK_EQ(f.D(112), 1);
    CHECK_EQ(g_t.op49Count, 1);
    CHECK_EQ(g_t.op53Count, 1);
    GameTime* appt = reinterpret_cast<GameTime*>(f.buf + 82);
    CHECK_EQ((int)appt->minute, 10);
}

TEST(NpcEventSteps2, Extortion_Phase2EndsActionAndFrees) {
    Fixture f;
    g_t.scriptObject = 7;
    g_t.scriptPerson = 3;
    g_t.scriptOwnerWord = 7;
    f.D(112) = 2;
    NpcEvent_ExtortionStep(f.h);
    CHECK_EQ(g_t.op49Count, 1);
    CHECK_EQ(g_t.op53Count, 1);
    CHECK_EQ(g_t.freeCount, 1);
}

// ---------------------------------------------------------------------------
// PatrolStep
// ---------------------------------------------------------------------------
TEST(NpcEventSteps2, Patrol_Phase0StartsAllMembersArmsPhase1) {
    Fixture f;
    g_t.scriptPerson = 3;       // each member resolves
    for (int i = 0; i < 6; ++i) f.D(140 + 4 * i) = 100 + i;
    f.D(112) = 0;
    NpcEvent_PatrolStep(f.h);
    CHECK_EQ(g_t.changeActionCount, 6);  // 6 members started
    CHECK_EQ(g_t.op49Count, 6);
    CHECK_EQ(g_t.lastEntity29Arg, 1);    // armed phase 1
    GameTime* appt = reinterpret_cast<GameTime*>(f.buf + 82);
    CHECK_EQ((int)appt->minute, 5);
}

TEST(NpcEventSteps2, Patrol_Minus2ReroutesToPhase5ThenFrees) {
    Fixture f;
    g_t.scriptPerson = 0;       // no members -> no per-member work
    f.D(112) = -2;
    f.B(120) = 0;               // flag 0x04 clear -> reroute
    NpcEvent_PatrolStep(f.h);
    CHECK_EQ(g_t.freeCount, 1); // finalize path frees at end of phase 5
}

TEST(NpcEventSteps2, Patrol_Phase3PacketAppliedAdvancesToPhase4) {
    Fixture f;
    f.D(112) = 3;
    f.D(216) = 0x500;
    g_t.scriptStatus = 1;       // packet applied
    g_t.scriptSeq = 0x42;       // valid sequence
    NpcEvent_PatrolStep(f.h);
    CHECK_EQ(f.D(212), 0x42);
    CHECK_EQ(g_t.lastEntity29Arg, 4);
}

// ---------------------------------------------------------------------------
// GatherGuildMembersStep
// ---------------------------------------------------------------------------
TEST(NpcEventSteps2, Gather_Minus1Frees) {
    Fixture f;
    f.D(112) = -1;
    NpcEvent_GatherGuildMembersStep(f.h);
    CHECK_EQ(g_t.freeCount, 1);
}

TEST(NpcEventSteps2, Gather_Phase0BroadcastsArmsPhase1) {
    Fixture f;
    g_t.scriptObject = 7;       // begin object resolves
    f.D(112) = 0;
    NpcEvent_GatherGuildMembersStep(f.h);
    CHECK_EQ(g_t.msgCount, 1);
    CHECK_EQ(g_t.lastEntity29Arg, 1);
    // method id stamped at +172 (word).
    CHECK_EQ((int)*reinterpret_cast<u16*>(f.buf + 172), 13);
}

TEST(NpcEventSteps2, Gather_Phase1EmitsPacketWhenWealthy) {
    Fixture f;
    f.D(112) = 1;
    f.D(176) = 500;             // threshold
    g_t.scriptCash = 1000;      // wealthy -> gathered
    NpcEvent_GatherGuildMembersStep(f.h);
    CHECK_EQ(g_t.op39Count, 1);
    CHECK_EQ(g_t.lastEntity29Arg, -1);
    GameTime* appt = reinterpret_cast<GameTime*>(f.buf + 82);
    CHECK_EQ((int)appt->day, 6);  // +24h from day 5
}

// ---------------------------------------------------------------------------
// AwardTitleStep
// ---------------------------------------------------------------------------
TEST(NpcEventSteps2, Award_GateClosedReturnsZero) {
    Fixture f;
    g_t.scriptGate = 0;
    f.D(112) = 0;
    CHECK_EQ(NpcEvent_AwardTitleStep(f.h), 0);
    CHECK_EQ(g_t.panelCreateCount, 0);
}

TEST(NpcEventSteps2, Award_Phase0BuildsPanelAndRenders) {
    Fixture f;
    g_t.scriptGate = 1;
    g_t.scriptCompare = 0;
    g_t.scriptPerson = 3;       // target + self slot resolve
    g_t.scriptPanel = 0x1234;
    f.D(112) = 0;
    NpcEvent_AwardTitleStep(f.h);
    CHECK_EQ(g_t.panelCreateCount, 1);
    CHECK_EQ(f.D(116), 0x1234);
    CHECK_EQ(f.D(112), 1);
    CHECK(g_t.renderTextCount >= 1);
}

TEST(NpcEventSteps2, Award_Phase1ConfirmDestroysAndFrees) {
    Fixture f;
    g_t.scriptGate = 1;
    g_t.scriptCompare = 0;
    g_t.scriptPerson = 3;
    f.D(112) = 1;
    f.D(116) = 0x1234;          // panel open
    g_t.scriptDialog = 1210;    // confirm
    NpcEvent_AwardTitleStep(f.h);
    CHECK_EQ(g_t.panelDestroyCount, 1);
    CHECK_EQ(g_t.freeCount, 1);
}

TEST(NpcEventSteps2, Registration_CountsAndLookup) {
    CHECK_EQ(RegisterNpcEvents2(), 5);
    CHECK(NpcEvent2_TableEntry(0x4d3c50) == &NpcEvent_ProtectionMoneyStep);
    CHECK(NpcEvent2_TableEntry(0x4d4460) == &NpcEvent_ExtortionStep);
    CHECK(NpcEvent2_TableEntry(0x4d49b8) == &NpcEvent_PatrolStep);
    CHECK(NpcEvent2_TableEntry(0x4d5308) == &NpcEvent_GatherGuildMembersStep);
    CHECK(NpcEvent2_TableEntry(0x4d57a0) == &NpcEvent_AwardTitleStep);
    CHECK(NpcEvent2_TableEntry(0x12345) == nullptr);
}
