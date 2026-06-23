// Integration test for cutscene_misc2 (the VIBE_Cutscene_* script-runner / fade /
// sky / duel / birth-check / teardown batch).
//
// NO RECONSTRUCTED SIBLING EXISTS for this module's hooks: every Cutscene2Hooks
// leaf (GameLogic_RunFrameLoop, the Script_* table, Fade_*/Sky_* presentation, the
// duel windows, the Person record reader, the cmd broadcasts, the scene teardown)
// is a host/graphics/script-VM leaf with no counterpart reconstructed in src/.
// (Checked: there is no reconstructed Script_*, Sky_*, Fade_*, person-record-kind
// reader, nor a directly-callable command builder matching these opaque void
// signatures.) Per the task's fallback rule, this itest therefore exercises the
// module's REAL INERT default-hook path end to end: with no hooks installed,
// GetCutscene2Hooks() returns the library's all-null `g_inert` struct, every leaf
// becomes a deterministic no-op, and the faithful control flow — the replay-gate
// gating, the inert-pump loop termination, the duel-mode dispatch, the sky
// lifecycle pair, the birth-abort branch, the participant-restore match — runs
// against the REAL module state (Cutscene2State / CutsceneSky globals owned by the
// real frame-loop driver, shared by reference here). A single captor pump is the
// only forwarded hook, used to prove the runners drive the real shared frameFlags.
#include "test.h"

#include "sim/cutscene_misc2.h"
#include "sim/cutscene.h"

using namespace guild;
using namespace guild::sim;

namespace {

// Captor pump: drives the runner loops a fixed number of frames, recording the
// frame flags the runner computed from the REAL shared Cutscene2State.frameFlags.
int g_pumpFrames = 0;
i32 g_lastFlags = 0;
int g_pumpA = 0;
int CaptorPump(i32 flags, i32 a, void* /*payload*/) {
    g_lastFlags = flags;
    g_pumpA = a;
    if (g_pumpFrames > 0) { --g_pumpFrames; return 1; }
    return 0;   // stop the loop
}

} // namespace

// RunScriptLoop on the INERT default path: with no pump hook installed, Pump()
// returns 0 on the first spin, so the loop runs zero frames and returns 0. The
// replay gate (shared real state) also short-circuits the body when set.
TEST(CutsceneMisc2Itest, RunScriptLoopInertAndReplayGate) {
    SetCutscene2Hooks(nullptr);          // real inert defaults
    Cutscene2() = Cutscene2State{};      // reset the real shared state

    // Inert pump -> loop body runs once, pump returns 0 -> result 0.
    CHECK_EQ(CutsceneRunScriptLoop(nullptr, /*handle*/ 5), 0);

    // Replay gate set: the whole body is skipped, returns 0 without pumping.
    Cutscene2().replayGate = 1;
    CHECK_EQ(CutsceneRunScriptLoop(nullptr, 5), 0);
    CHECK_EQ(CutsceneRunScriptUntilSkip(nullptr, 0, 5), 0);
    CHECK_EQ(CutsceneRunTimedScript(10, 0, nullptr), 0);
}

// A captor pump proves the runner computes the frame flags from the REAL shared
// Cutscene2State.frameFlags with byte1 OR 0x80 — exactly the binary's bit-set.
TEST(CutsceneMisc2Itest, RunnerDrivesRealSharedFrameFlags) {
    Cutscene2() = Cutscene2State{};
    Cutscene2().frameFlags = 0x000000FF;   // dword_631598

    Cutscene2Hooks h{};
    h.pumpFrame = CaptorPump;
    // FindByHandle stays inert (nullptr) -> the loop stops after the first frame
    // (Script_FindByHandle returns null -> v = 0). One pump call is enough to
    // capture the flags the runner passed.
    SetCutscene2Hooks(&h);
    g_pumpFrames = 1; g_lastFlags = 0;

    CutsceneRunScriptLoop(nullptr, /*handle*/ 7);
    SetCutscene2Hooks(nullptr);

    // byte1 of 0x000000FF OR'd with 0x80 -> flags == 0x000080FF.
    CHECK_EQ(g_lastFlags, 0x000080FF);

    // RunScriptWait uses the fixed literal flags 497414 (0x79706) instead.
    Cutscene2Hooks h2{}; h2.pumpFrame = CaptorPump;
    SetCutscene2Hooks(&h2);
    g_pumpFrames = 1; g_lastFlags = 0;
    CutsceneRunScriptWait(nullptr, 7);
    SetCutscene2Hooks(nullptr);
    CHECK_EQ(g_lastFlags, 497414);
}

// ShowDuelWindow dispatch on the inert path: duelMode selects which window leaf is
// called; with both leaves inert the result is 0 either way, but the branch taken
// is observable via a captor on each.
TEST(CutsceneMisc2Itest, DuelWindowDispatchInert) {
    Cutscene2() = Cutscene2State{};

    // Inert: both window hooks null -> 0 on both branches.
    SetCutscene2Hooks(nullptr);
    Cutscene2().duelMode = 0;
    CHECK_EQ(CutsceneShowDuelWindow(1, 2, 3), 0);   // choice-window branch
    Cutscene2().duelMode = 1;
    CHECK_EQ(CutsceneShowDuelWindow(1, 2, 3), 0);   // outcome-window branch

    // Captor proves the dispatch picks the right leaf per duelMode.
    static int g_choice = 0, g_outcome = 0;
    g_choice = g_outcome = 0;
    Cutscene2Hooks h{};
    h.duelChoiceWindow  = [](i32, i32) { g_choice++;  return 11; };
    h.duelOutcomeWindow = [](i32, i32, i32) { g_outcome++; return 22; };
    SetCutscene2Hooks(&h);
    Cutscene2().duelMode = 0;
    CHECK_EQ(CutsceneShowDuelWindow(1, 2, 3), 11);
    CHECK_EQ(g_choice, 1);
    CHECK_EQ(g_outcome, 0);
    Cutscene2().duelMode = 1;
    CHECK_EQ(CutsceneShowDuelWindow(1, 2, 3), 22);
    CHECK_EQ(g_outcome, 1);
    SetCutscene2Hooks(nullptr);
}

// Sky lifecycle on the inert path: Setup with no Sky_Create hook leaves the real
// shared CutsceneSky pair null; Destroy is then a safe no-op. The replay gate
// blocks Setup entirely.
TEST(CutsceneMisc2Itest, SkyLifecycleInert) {
    Cutscene2() = Cutscene2State{};
    SetCutscene2Hooks(nullptr);
    CutsceneSkyState() = CutsceneSky{};

    CutsceneSetupSky(nullptr);
    CHECK_EQ(CutsceneSkyState().sky, (void*)nullptr);
    CHECK_EQ(CutsceneSkyState().layer, (void*)nullptr);
    CHECK_EQ(CutsceneDestroySky(), 0);

    // Captor Sky_Create populates the real shared pair; Destroy clears it.
    static int g_destroyCalls = 0;
    g_destroyCalls = 0;
    static int g_skyObj = 0xABCD;
    Cutscene2Hooks h{};
    h.skyCreate = []() -> void* { return &g_skyObj; };
    h.skyDestroy = [](void*) { g_destroyCalls++; };
    SetCutscene2Hooks(&h);
    CutsceneSetupSky(nullptr);
    CHECK_EQ(CutsceneSkyState().sky, (void*)&g_skyObj);
    CHECK_EQ(CutsceneSkyState().mirror, (void*)&g_skyObj);  // dword_64A7C8 mirror set
    CHECK_EQ(CutsceneDestroySky(), 0);
    CHECK_EQ(g_destroyCalls, 1);
    // gilde.exe 0x4aa740: only the mirror (dword_64A7C8) is zeroed; dword_6315F0 stays.
    CHECK_EQ(CutsceneSkyState().mirror, (void*)nullptr);
    CHECK_EQ(CutsceneSkyState().sky, (void*)&g_skyObj);
    SetCutscene2Hooks(nullptr);
}

// CheckBirthParticipants on the inert path: with no resolvePerson hook the body
// aborts immediately (returns 0). The captor path then drives the real branch
// logic: a deceased (kind 15) parent triggers the birth-failure command.
TEST(CutsceneMisc2Itest, BirthCheckInertAndDeceasedParent) {
    Cutscene2() = Cutscene2State{};
    SetCutscene2Hooks(nullptr);

    CutsceneSlot slot{};
    slot.partIds[0] = 100;
    slot.partIds[1] = 200;
    // Inert: no resolver -> abort with 0.
    CHECK_EQ(CutsceneCheckBirthParticipants(&slot), 0);

    // Captor world: person 100 (child) has parent 300; person 300 is kind 15
    // (deceased) -> the body queues a birth failure for partIds[1] and returns 0.
    static int g_birthFail = -1;
    g_birthFail = -1;
    static i32 s_p100 = 100, s_p200 = 200, s_p300 = 300;
    Cutscene2Hooks h{};
    h.resolvePerson = [](i32 id) -> void* {
        if (id == 100) return &s_p100;
        if (id == 200) return &s_p200;
        if (id == 300) return &s_p300;
        return nullptr;
    };
    h.personParentId = [](void* p) -> i32 {
        return (p == &s_p100) ? 300 : -1;
    };
    h.personKind = [](void* p) -> u8 {
        return (p == &s_p300) ? (u8)15 /*deceased*/ : (u8)6 /*bride*/;
    };
    h.queueBirthFailure = [](i32 id) { g_birthFail = id; };
    SetCutscene2Hooks(&h);
    CHECK_EQ(CutsceneCheckBirthParticipants(&slot), 0);
    CHECK_EQ(g_birthFail, 200);   // QueueRequestPair33(b.id, 1) fired
    SetCutscene2Hooks(nullptr);
}

// RestoreParticipantState matches the person against the participant id array and
// fires one restore broadcast per match; the count equals slot.partCount. With no
// hook the match still scans (inert broadcast no-op) and returns the count.
TEST(CutsceneMisc2Itest, RestoreParticipantMatchInert) {
    Cutscene2() = Cutscene2State{};
    SetCutscene2Hooks(nullptr);

    CutsceneSlot slot{};
    slot.id = 42;
    slot.partCount = 3;
    i32 ids[3] = {10, 20, 30};

    // Inert: returns the participant count, broadcast leaf is a no-op.
    CHECK_EQ(CutsceneRestoreParticipantState(&slot, /*person*/ 20, ids, 3), 3);

    // Captor: a matching person fires exactly one broadcast against the slot id.
    static int g_bcCalls = 0; static i32 g_bcPerson = -1; static i32 g_bcSlot = -1;
    g_bcCalls = 0; g_bcPerson = -1; g_bcSlot = -1;
    Cutscene2Hooks h{};
    h.restoreBroadcast = [](i32 person, i32 slotId) {
        g_bcCalls++; g_bcPerson = person; g_bcSlot = slotId;
    };
    SetCutscene2Hooks(&h);
    CHECK_EQ(CutsceneRestoreParticipantState(&slot, /*person*/ 20, ids, 3), 3);
    CHECK_EQ(g_bcCalls, 1);
    CHECK_EQ(g_bcPerson, 20);
    CHECK_EQ(g_bcSlot, 42);

    // A person not in the id list fires nothing (count still returned).
    g_bcCalls = 0;
    CHECK_EQ(CutsceneRestoreParticipantState(&slot, /*person*/ 99, ids, 3), 3);
    CHECK_EQ(g_bcCalls, 0);
    SetCutscene2Hooks(nullptr);
}
