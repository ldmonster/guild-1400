// Unit tests for cutscene_misc2 — the script-runner family + fade/sky/duel-window
// /birth-check/teardown/restore bodies (gilde.exe VIBE_Cutscene_*).
#include "sim/cutscene_misc2.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Recording mock: drives the frame pump a fixed number of frames, then reports
// "stop" (mirrors RunFrameLoop returning 0 when the loop should end).
// ---------------------------------------------------------------------------
namespace {

struct Rec {
    int   pumpCalls   = 0;
    int   pumpBudget  = 0;     // frames to keep returning nonzero
    i32   lastFlags   = 0;
    i32   lastA       = 0;
    void* lastPayload = nullptr;
    bool  scriptAlive = true;  // FindByHandle result
    int   findCalls   = 0;
    int   damageCalls = 0;
    int   dialogCalls = 0;
    i32   lastDialog  = -99;
    // tick advance applied each pump frame (drives the timed/delayed deadline)
    u32   tickPerFrame = 0;
    int   skipCbCalls = 0;
};
Rec g_r;

int MockPump(i32 flags, i32 a, void* payload) {
    g_r.pumpCalls++;
    g_r.lastFlags = flags;
    g_r.lastA = a;
    g_r.lastPayload = payload;
    Cutscene2().gameTick += g_r.tickPerFrame;
    if (g_r.pumpCalls >= g_r.pumpBudget) return 0;
    return 1;
}
void* MockFind(i32) { g_r.findCalls++; return g_r.scriptAlive ? reinterpret_cast<void*>(1) : nullptr; }
void MockDamage() { g_r.damageCalls++; }
void MockDialog(i32 s) { g_r.dialogCalls++; g_r.lastDialog = s; }
int MockSkipCb() { g_r.skipCbCalls++; return 77; }

void ResetAll() {
    g_r = Rec{};
    Cutscene2() = Cutscene2State{};
    CutsceneSkyState() = CutsceneSky{};   // module-global; clear between tests
    SetCutscene2Hooks(nullptr);
}

Cutscene2Hooks MakeHooks() {
    Cutscene2Hooks h{};
    h.pumpFrame = &MockPump;
    h.scriptFindByHandle = &MockFind;
    h.combatUpdateDamageNumbers = &MockDamage;
    h.showParticipantDialog = &MockDialog;
    return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// Frame-flag bit-set: dword_631598 with byte1 |= 0x80.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc2, FrameFlagsHiBit) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    Cutscene2().frameFlags = 0x0102;   // arbitrary
    g_r.pumpBudget = 1;                // one pump then stop
    g_r.scriptAlive = false;
    CutsceneRunScriptLoop(nullptr, 5);
    // byte1 (bits 8..15) OR 0x80 -> 0x0102 | 0x8000 == 0x8102
    CHECK_EQ(g_r.lastFlags, 0x8102);
}

// ---------------------------------------------------------------------------
// RunScriptLoop / RunScriptWait gating + alive-loop.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc2, RunScriptLoopGated) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    Cutscene2().replayGate = 1;        // dword_6315BC -> short-circuit
    CHECK_EQ(CutsceneRunScriptLoop(nullptr, 1), 0);
    CHECK_EQ(g_r.pumpCalls, 0);
}

TEST(CutsceneMisc2, RunScriptLoopRunsWhileAlive) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.pumpBudget = 1000;             // pump never stops on its own
    g_r.scriptAlive = false;           // FindByHandle says script ended
    i32 r = CutsceneRunScriptLoop(nullptr, 9);
    // one pump (nonzero), then FindByHandle false -> loop ends, returns 0.
    CHECK_EQ(g_r.pumpCalls, 1);
    CHECK_EQ(g_r.findCalls, 1);
    CHECK_EQ(r, 0);
}

TEST(CutsceneMisc2, RunScriptWaitFixedFlags) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.pumpBudget = 1;                // stop after first pump
    CutsceneRunScriptWait(nullptr, 3);
    CHECK_EQ(g_r.lastFlags, 497414);   // literal 0x79706
    CHECK_EQ(g_r.pumpCalls, 1);
}

// ---------------------------------------------------------------------------
// RunScriptUntilSkip: skip-gate returns the callback's result (or 1).
// ---------------------------------------------------------------------------
TEST(CutsceneMisc2, RunScriptUntilSkipGated) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    Cutscene2().replayGate = 1;
    CHECK_EQ(CutsceneRunScriptUntilSkip(&MockSkipCb, 0, 1), 0);
    CHECK_EQ(g_r.pumpCalls, 0);
}

TEST(CutsceneMisc2, RunScriptUntilSkipCallback) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.pumpBudget = 1000;
    g_r.scriptAlive = true;
    Cutscene2().skipGate = 1;          // dword_672230 set on first iteration
    i32 r = CutsceneRunScriptUntilSkip(&MockSkipCb, 4, 1);
    CHECK_EQ(r, 77);                   // skipCb() result
    CHECK_EQ(g_r.skipCbCalls, 1);
    CHECK_EQ(g_r.lastA, 4);            // a2 forwarded to pump
}

TEST(CutsceneMisc2, RunScriptUntilSkipNullCbReturns1) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.pumpBudget = 1000;
    Cutscene2().skipGate = 1;
    CHECK_EQ(CutsceneRunScriptUntilSkip(nullptr, 0, 1), 1);
}

// ---------------------------------------------------------------------------
// RunTimedScript: deadline math + skip path. golden deadline (python):
//   frames=14, start=0, scale=1/14 -> deadline_ms ~= 1.0
// ---------------------------------------------------------------------------
TEST(CutsceneMisc2, RunTimedScriptDeadline) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.tickPerFrame = 2;              // each frame advances the ms clock by 2
    Cutscene2().gameTick = 0;
    // frames=14 -> deadline ~1.0 ms (1/14*14, IEEE-754 slightly above 1.0). The
    // body pumps FIRST: frame1 returns nonzero (budget 2) and advances tick to 2;
    // 2 >= 1.0 -> forceQuit set + damage/dialog run. frame2 pump returns 0 -> ends.
    g_r.pumpBudget = 2;
    i32 r = CutsceneRunTimedScript(14, 7, nullptr);
    CHECK_EQ(r, 0);                    // timed-out path returns v5 (0)
    CHECK_EQ(g_r.lastA, 7);            // a2 forwarded
    // forceQuit latched because gameTick(1) >= 1.0
    CHECK_EQ(Cutscene2().forceQuit, 1);
    CHECK(g_r.damageCalls >= 1);
}

TEST(CutsceneMisc2, RunTimedScriptSkip) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.pumpBudget = 1;                // stop after first frame
    Cutscene2().skipGate = 1;
    i32 r = CutsceneRunTimedScript(140, 0, &MockSkipCb);
    CHECK_EQ(r, 0);                    // pump stopped -> returns v5
    // but skip set forceQuit + ran the cb before pump signalled stop?
    // The body checks pump FIRST: pump returns 0 immediately -> returns v5=0,
    // skip branch not reached.
    CHECK_EQ(g_r.skipCbCalls, 0);
}

TEST(CutsceneMisc2, RunTimedScriptGated) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    Cutscene2().replayGate = 1;
    CHECK_EQ(CutsceneRunTimedScript(100, 0, &MockSkipCb), 0);
    CHECK_EQ(g_r.pumpCalls, 0);
}

// ---------------------------------------------------------------------------
// RunDelayedScript: forceQuit latched past deadline; dialog slot forwarded.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc2, RunDelayedScriptDeadline) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.pumpBudget = 2;                // one full iteration then stop
    g_r.tickPerFrame = 1000;           // jump well past any deadline
    Cutscene2().gameTick = 0;
    CutsceneRunDelayedScript(14, 42);  // deadline ~1ms; tick jumps to 1000
    CHECK_EQ(Cutscene2().forceQuit, 1);
    CHECK_EQ(g_r.lastDialog, 42);      // dialog slot forwarded
    CHECK(g_r.damageCalls >= 1);
}

// ---------------------------------------------------------------------------
// RunCombatScript: tick-counter deadline.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc2, RunCombatScriptDeadline) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.pumpBudget = 2;
    Cutscene2().tickCounter = 50;      // dword_6315A8
    CutsceneRunCombatScript(50, nullptr, 8);  // deadline(50) <= counter(50)
    CHECK_EQ(Cutscene2().forceQuit, 1);
    CHECK_EQ(g_r.lastDialog, 8);
}

TEST(CutsceneMisc2, RunCombatScriptNoDeadline) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    g_r.pumpBudget = 2;
    Cutscene2().tickCounter = 10;
    CutsceneRunCombatScript(100, nullptr, 0);  // deadline > counter -> not latched
    CHECK_EQ(Cutscene2().forceQuit, 0);
}

// ---------------------------------------------------------------------------
// FadeIn: spins until the fade done bit (bit2) is set; inert default = 0 frames.
// ---------------------------------------------------------------------------
namespace {
int g_fadeFrames = 0;
void* MockFadeReg() { return reinterpret_cast<void*>(1); }
u8 MockFadeDone(void*) {
    // done after g_fadeFrames pumps
    return (g_r.pumpCalls >= g_fadeFrames) ? 0x04 : 0x00;
}
}

TEST(CutsceneMisc2, FadeInSpinsUntilDone) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    h.fadeRegister = &MockFadeReg;
    h.fadeDoneByte = &MockFadeDone;
    SetCutscene2Hooks(&h);
    g_fadeFrames = 3;
    g_r.pumpBudget = 1000;
    CutsceneFadeIn(2, nullptr);
    CHECK_EQ(g_r.pumpCalls, 3);        // 3 frames until done bit set
}

TEST(CutsceneMisc2, FadeInGated) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();
    SetCutscene2Hooks(&h);
    Cutscene2().replayGate = 1;
    CutsceneFadeIn(0, nullptr);
    CHECK_EQ(g_r.pumpCalls, 0);
}

TEST(CutsceneMisc2, FadeInInertDoneImmediately) {
    ResetAll();
    Cutscene2Hooks h = MakeHooks();    // no fade hooks -> done byte defaults 0x04
    SetCutscene2Hooks(&h);
    CutsceneFadeIn(0, nullptr);
    CHECK_EQ(g_r.pumpCalls, 0);        // inert fade reports done -> no frames
}

// ---------------------------------------------------------------------------
// SetupSky / DestroySky: handle pair lifecycle.
// ---------------------------------------------------------------------------
namespace {
void* g_skyObj = reinterpret_cast<void*>(0x5C);
void* g_layerObj = reinterpret_cast<void*>(0x1A);
int g_configCalls = 0, g_removeCalls = 0, g_destroyCalls = 0;
void* MockSkyCreate() { return g_skyObj; }
void* MockSkyLayer(void*) { return g_layerObj; }
void MockSkyConfig(void*, void*) { g_configCalls++; }
void MockSkyRemove(void*, void*) { g_removeCalls++; }
void MockSkyDestroy(void*) { g_destroyCalls++; }
}

TEST(CutsceneMisc2, SetupSkyBuildsPair) {
    ResetAll();
    g_configCalls = 0;
    Cutscene2Hooks h{};
    h.skyCreate = &MockSkyCreate;
    h.skyCreateLayer = &MockSkyLayer;
    h.skyConfigLayer = &MockSkyConfig;
    SetCutscene2Hooks(&h);
    CutsceneSetupSky(nullptr);
    CHECK_EQ(CutsceneSkyState().sky, g_skyObj);
    CHECK_EQ(CutsceneSkyState().layer, g_layerObj);
    CHECK_EQ(CutsceneSkyState().mirror, g_skyObj);   // dword_64A7C8 = dword_6315F0
    CHECK_EQ(g_configCalls, 1);
}

TEST(CutsceneMisc2, SetupSkyGated) {
    ResetAll();
    g_configCalls = 0;
    Cutscene2Hooks h{};
    h.skyCreate = &MockSkyCreate;
    SetCutscene2Hooks(&h);
    Cutscene2().replayGate = 1;
    CutsceneSetupSky(nullptr);
    CHECK_EQ(CutsceneSkyState().sky, (void*)nullptr);
}

TEST(CutsceneMisc2, DestroySkyRemovesLayerThenDestroys) {
    ResetAll();
    g_removeCalls = g_destroyCalls = 0;
    Cutscene2Hooks h{};
    h.skyRemoveLayer = &MockSkyRemove;
    h.skyDestroy = &MockSkyDestroy;
    SetCutscene2Hooks(&h);
    CutsceneSkyState().sky = g_skyObj;
    CutsceneSkyState().layer = g_layerObj;
    CutsceneSkyState().mirror = g_skyObj;          // SetupSky would have set this
    CutsceneDestroySky();
    CHECK_EQ(g_removeCalls, 1);
    CHECK_EQ(g_destroyCalls, 1);
    // gilde.exe 0x4aa740: DestroySky zeroes ONLY the mirror (dword_64A7C8); it does
    // NOT clear dword_6315F0 (sky) or dword_6315EC (layer). Verified vs disasm.
    CHECK_EQ(CutsceneSkyState().mirror, (void*)nullptr);
    CHECK_EQ(CutsceneSkyState().sky, g_skyObj);
    CHECK_EQ(CutsceneSkyState().layer, g_layerObj);
}

TEST(CutsceneMisc2, DestroySkyNoLayerSkipsRemove) {
    ResetAll();
    g_removeCalls = g_destroyCalls = 0;
    Cutscene2Hooks h{};
    h.skyRemoveLayer = &MockSkyRemove;
    h.skyDestroy = &MockSkyDestroy;
    SetCutscene2Hooks(&h);
    CutsceneSkyState().sky = g_skyObj;
    CutsceneSkyState().layer = nullptr;   // no layer -> no remove
    CutsceneDestroySky();
    CHECK_EQ(g_removeCalls, 0);
    CHECK_EQ(g_destroyCalls, 1);
}

// ---------------------------------------------------------------------------
// ShowDuelWindow dispatch on dword_6315A4.
// ---------------------------------------------------------------------------
namespace {
int g_outcomeArgs[3] = {0, 0, 0};
int g_choiceArgs[2]  = {0, 0};
int MockOutcomeWin(i32 a, i32 b, i32 c) { g_outcomeArgs[0]=a; g_outcomeArgs[1]=b; g_outcomeArgs[2]=c; return 111; }
int MockChoiceWin(i32 a, i32 c) { g_choiceArgs[0]=a; g_choiceArgs[1]=c; return 222; }
}

TEST(CutsceneMisc2, ShowDuelWindowOutcomeMode) {
    ResetAll();
    Cutscene2Hooks h{};
    h.duelOutcomeWindow = &MockOutcomeWin;
    h.duelChoiceWindow = &MockChoiceWin;
    SetCutscene2Hooks(&h);
    Cutscene2().duelMode = 1;          // dword_6315A4 != 0
    i32 r = CutsceneShowDuelWindow(10, 20, 30);
    CHECK_EQ(r, 111);
    CHECK_EQ(g_outcomeArgs[0], 10);
    CHECK_EQ(g_outcomeArgs[1], 20);
    CHECK_EQ(g_outcomeArgs[2], 30);
}

TEST(CutsceneMisc2, ShowDuelWindowChoiceMode) {
    ResetAll();
    Cutscene2Hooks h{};
    h.duelOutcomeWindow = &MockOutcomeWin;
    h.duelChoiceWindow = &MockChoiceWin;
    SetCutscene2Hooks(&h);
    Cutscene2().duelMode = 0;
    i32 r = CutsceneShowDuelWindow(10, 20, 30);
    CHECK_EQ(r, 222);
    CHECK_EQ(g_choiceArgs[0], 10);     // a1
    CHECK_EQ(g_choiceArgs[1], 30);     // a3 (a2 dropped, matching the original)
}

// ---------------------------------------------------------------------------
// CheckBirthParticipants: the four branches.
// ---------------------------------------------------------------------------
namespace {
// Tiny fake person table keyed by id; record holds kind + parentId.
struct FakePerson { i32 id; u8 kind; i32 parentId; };
std::vector<FakePerson> g_people;
int g_birthFailures = 0;
i32 g_lastBirthFailId = -99;
int g_speechCalls = 0;

FakePerson* Lookup(i32 id) {
    for (auto& p : g_people) if (p.id == id) return &p;
    return nullptr;
}
void* MockResolve(i32 id) { return Lookup(id); }
u8 MockKind(void* p) { return static_cast<FakePerson*>(p)->kind; }
i32 MockParent(void* p) { return static_cast<FakePerson*>(p)->parentId; }
void MockBirthFail(i32 id) { g_birthFailures++; g_lastBirthFailId = id; }
void MockSpeech(void*, const CutsceneSlot*) { g_speechCalls++; }

Cutscene2Hooks BirthHooks() {
    Cutscene2Hooks h{};
    h.resolvePerson = &MockResolve;
    h.personKind = &MockKind;
    h.personParentId = &MockParent;
    h.queueBirthFailure = &MockBirthFail;
    h.buildSpeechPacket = &MockSpeech;
    return h;
}
void ResetBirth() {
    ResetAll();
    g_people.clear();
    g_birthFailures = 0; g_lastBirthFailId = -99; g_speechCalls = 0;
}
}

TEST(CutsceneMisc2, BirthMissingCombatantReturns0) {
    ResetBirth();
    Cutscene2Hooks h = BirthHooks();
    SetCutscene2Hooks(&h);
    CutsceneSlot slot{};
    slot.partIds[0] = 1;   // exists
    slot.partIds[1] = 2;   // missing
    g_people.push_back({1, 6, 100});
    CHECK_EQ(CutsceneCheckBirthParticipants(&slot), 0);
}

TEST(CutsceneMisc2, BirthDeadParentQueuesFailure) {
    ResetBirth();
    Cutscene2Hooks h = BirthHooks();
    SetCutscene2Hooks(&h);
    CutsceneSlot slot{};
    slot.partIds[0] = 1;
    slot.partIds[1] = 2;
    g_people.push_back({1, 6, 100});     // mother, parent id 100
    g_people.push_back({2, 6, 0});       // father
    g_people.push_back({100, 15, 0});    // parent kind 15 (deceased)
    CHECK_EQ(CutsceneCheckBirthParticipants(&slot), 0);
    CHECK_EQ(g_birthFailures, 1);
    CHECK_EQ(g_lastBirthFailId, 2);      // father id from partIds[1]
}

TEST(CutsceneMisc2, BirthNoParentQueuesFailure) {
    ResetBirth();
    Cutscene2Hooks h = BirthHooks();
    SetCutscene2Hooks(&h);
    CutsceneSlot slot{};
    slot.partIds[0] = 1;
    slot.partIds[1] = 2;
    g_people.push_back({1, 6, 999});     // parent 999 missing
    g_people.push_back({2, 6, 0});
    CHECK_EQ(CutsceneCheckBirthParticipants(&slot), 0);
    CHECK_EQ(g_birthFailures, 1);
}

TEST(CutsceneMisc2, BirthEligibleEmitsSpeech) {
    ResetBirth();
    Cutscene2Hooks h = BirthHooks();
    SetCutscene2Hooks(&h);
    CutsceneSlot slot{};
    slot.partIds[0] = 1;
    slot.partIds[1] = 2;
    g_people.push_back({1, 6, 100});     // mother kind 6
    g_people.push_back({2, 7, 0});
    g_people.push_back({100, 3, 0});     // alive parent kind 3
    CHECK_EQ(CutsceneCheckBirthParticipants(&slot), 1);
    CHECK_EQ(g_speechCalls, 1);
}

TEST(CutsceneMisc2, BirthOtherKindNoSpeech) {
    ResetBirth();
    Cutscene2Hooks h = BirthHooks();
    SetCutscene2Hooks(&h);
    CutsceneSlot slot{};
    slot.partIds[0] = 1;
    slot.partIds[1] = 2;
    g_people.push_back({1, 3, 100});     // mother kind 3 (not 6/7)
    g_people.push_back({2, 7, 0});
    g_people.push_back({100, 3, 0});
    CHECK_EQ(CutsceneCheckBirthParticipants(&slot), 1);
    CHECK_EQ(g_speechCalls, 0);          // not 6/7 -> no speech, still returns 1
}

// ---------------------------------------------------------------------------
// Teardown: nest-depth gate, script finish, busy-waits, decrement.
// ---------------------------------------------------------------------------
namespace {
int g_finishCalls = 0, g_sceneTeardownCalls = 0;
std::vector<i32> g_busyFlags;   // sequence returned by scriptHandleFlags
int g_flagIdx = 0;
void* MockTdFind(i32) { return reinterpret_cast<void*>(1); }
void MockTdFinish(void*) { g_finishCalls++; }
u8 MockTdFlags(i32) {
    if (g_flagIdx < (int)g_busyFlags.size()) return (u8)g_busyFlags[g_flagIdx++];
    return 0;
}
void MockSceneTeardown() { g_sceneTeardownCalls++; }
int MockTdPump(i32, i32, void*) { return 1; }
}

TEST(CutsceneMisc2, TeardownGatedByNestDepth) {
    ResetAll();
    g_finishCalls = g_sceneTeardownCalls = 0;
    Cutscene2Hooks h{};
    h.scriptFindByHandle = &MockTdFind;
    h.scriptFinish = &MockTdFinish;
    h.sceneTeardown = &MockSceneTeardown;
    SetCutscene2Hooks(&h);
    Cutscene2().nestDepth = 0;          // <= 0 -> no-op
    CutsceneTeardown();
    CHECK_EQ(g_finishCalls, 0);
    CHECK_EQ(g_sceneTeardownCalls, 0);
    CHECK_EQ(Cutscene2().nestDepth, 0);
}

TEST(CutsceneMisc2, TeardownFinishesAndDecrements) {
    ResetAll();
    g_finishCalls = g_sceneTeardownCalls = 0;
    g_busyFlags.clear(); g_flagIdx = 0;
    Cutscene2Hooks h{};
    h.scriptFindByHandle = &MockTdFind;
    h.scriptFinish = &MockTdFinish;
    h.scriptHandleFlags = &MockTdFlags;
    h.sceneTeardown = &MockSceneTeardown;
    h.pumpFrame = &MockTdPump;
    SetCutscene2Hooks(&h);
    Cutscene2().nestDepth = 2;
    Cutscene2().scriptHandleA = 5;      // primary -> finished
    Cutscene2().scriptHandleB = -1;     // no secondary busy-wait
    // scriptHandleA busy-wait flags: running once then idle
    g_busyFlags = {0};                  // not running -> wait exits immediately
    CutsceneTeardown();
    CHECK_EQ(g_finishCalls, 1);
    CHECK_EQ(g_sceneTeardownCalls, 1);
    CHECK_EQ(Cutscene2().nestDepth, 1); // decremented
}

TEST(CutsceneMisc2, TeardownReplayGateSkips) {
    ResetAll();
    g_finishCalls = 0;
    Cutscene2Hooks h{};
    h.scriptFindByHandle = &MockTdFind;
    h.scriptFinish = &MockTdFinish;
    SetCutscene2Hooks(&h);
    Cutscene2().replayGate = 1;
    Cutscene2().nestDepth = 5;
    CutsceneTeardown();
    CHECK_EQ(g_finishCalls, 0);
    CHECK_EQ(Cutscene2().nestDepth, 5);
}

// ---------------------------------------------------------------------------
// RestoreParticipantState: matches the participant id, fires broadcast.
// ---------------------------------------------------------------------------
namespace {
int g_restoreCalls = 0;
i32 g_restorePerson = -1, g_restoreSlot = -1;
void MockRestore(i32 person, i32 slotId) { g_restoreCalls++; g_restorePerson = person; g_restoreSlot = slotId; }
}

TEST(CutsceneMisc2, RestoreMatchesParticipant) {
    ResetAll();
    g_restoreCalls = 0;
    Cutscene2Hooks h{};
    h.restoreBroadcast = &MockRestore;
    SetCutscene2Hooks(&h);
    CutsceneSlot slot{};
    slot.id = 77;
    slot.partCount = 3;
    i32 ids[3] = {10, 20, 30};
    i32 n = CutsceneRestoreParticipantState(&slot, 20, ids, 3);
    CHECK_EQ(n, 3);                     // returns partCount
    CHECK_EQ(g_restoreCalls, 1);
    CHECK_EQ(g_restorePerson, 20);
    CHECK_EQ(g_restoreSlot, 77);
}

TEST(CutsceneMisc2, RestoreNoMatch) {
    ResetAll();
    g_restoreCalls = 0;
    Cutscene2Hooks h{};
    h.restoreBroadcast = &MockRestore;
    SetCutscene2Hooks(&h);
    CutsceneSlot slot{};
    slot.partCount = 2;
    i32 ids[2] = {1, 2};
    i32 n = CutsceneRestoreParticipantState(&slot, 99, ids, 2);
    CHECK_EQ(n, 2);
    CHECK_EQ(g_restoreCalls, 0);
}
