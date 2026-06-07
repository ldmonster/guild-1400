// Unit tests for the deferred CharAction misc steps + builders + fade math +
// the idle/social proximity scan. Drives each state machine in isolation with a
// recording mock hook table; uses a seeded RNG for the social/group spawns.
#include "sim/charaction_misc.h"
#include "sim/charaction.h"
#include "sim/character.h"
#include "sim/character_social.h"
#include "sim/actionqueue.h"
#include "sim/he.h"

#include "crt/rand.h"
#include "util/math_rng_float.h"

#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- recording misc hooks -------------------------------------------------
struct MiscRec {
    int attachMovement = 0, attachAni = 0, stepCalls = 0, queueReady = 0;
    int stopSample = 0;
    int motionResult = -1;            // StepMotionQueue return
    int queueReadyResult = 1;
    int worldOk = 1;
    Character* nearby = nullptr;
    char lastName[72] = {};
};
MiscRec g_mr;

void* MRAttachMovement(Character* ch, const char* name, int) {
    ++g_mr.attachMovement;
    std::strncpy(g_mr.lastName, name ? name : "", sizeof(g_mr.lastName) - 1);
    ch->motion = reinterpret_cast<ActionNode*>(1);
    return reinterpret_cast<void*>(1);
}
void* MRAttachAni(Character* ch, const char* name, int) {
    ++g_mr.attachAni;
    std::strncpy(g_mr.lastName, name ? name : "", sizeof(g_mr.lastName) - 1);
    return reinterpret_cast<void*>(2);
}
int  MRStep(Character*) { ++g_mr.stepCalls; return g_mr.motionResult; }
int  MRReady(Character*) { ++g_mr.queueReady; return g_mr.queueReadyResult; }
void MRStop(Character*) { ++g_mr.stopSample; }
int  MRSlot(void*) { return 7; }
int  MRWorld(Character*, int, int* c, int* r) { if (c) *c = 3; if (r) *r = 4; return g_mr.worldOk; }
Character* MRNearby(Character*, float) { return g_mr.nearby; }

const MiscActionHooks kMiscHooks = {
    MRAttachMovement, MRAttachAni, MRStep, MRReady, MRStop, MRSlot, MRWorld, MRNearby,
};

void Setup() {
    RegisterHandlers();
    ResetCharacters();
    g_gameTick = 0;
    g_mr = MiscRec{};
    SetMiscActionHooks(&kMiscHooks);
}

} // namespace

// ===========================================================================
// Sound action.
// ===========================================================================
TEST(CharActionMisc, SoundActionStateMachine) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    // Endless sound (count -1): first call sets dirty-anim + attaches movement.
    ActionNode* n = CreateSoundAction(&ch, "noise/clang", -1);
    CHECK(n != nullptr);
    CHECK_EQ(n->type, (u8)46);
    CHECK_EQ(n->args[1], -1);

    SoundActionUpdate(n);                      // callCount 0
    CHECK_EQ(g_mr.attachMovement, 1);
    CHECK((ch.flagsA & 0x02) != 0);            // dirty-anim set (count == -1)
    CHECK(std::strcmp(g_mr.lastName, "noise/clang") == 0);

    // Later call on an endless (-1) sound with NO abort: the teardown guard
    // (abort || count != -1) is false, so the node loops forever (faithful).
    n->callCount = 1;
    g_mr.queueReadyResult = 1;
    SoundActionUpdate(n);
    CHECK((ch.flagsA & 0x02) != 0);            // still dirty-anim (no teardown)
    CHECK_EQ(ch.actions, n);                   // still queued

    // Set abort: now the guard passes, count==-1 clears dirty-anim; CheckQueueReady
    // gates the free.
    ch.abort = 1;
    g_mr.queueReadyResult = 0;                 // not ready -> stays
    SoundActionUpdate(n);
    CHECK((ch.flagsA & 0x02) == 0);            // cleared
    CHECK_EQ(ch.actions, n);                   // still queued

    g_mr.queueReadyResult = 1;                 // now ready -> free
    SoundActionUpdate(n);
    CHECK(ch.actions == nullptr);
}

TEST(CharActionMisc, SoundActionFiniteCountClamp) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    // Finite count 0 is clamped to 1 (mode passed to AttachMovementAni) and does
    // NOT set the dirty-anim flag.
    ActionNode* n = CreateSoundAction(&ch, "x", 0);
    SoundActionUpdate(n);
    CHECK_EQ(g_mr.attachMovement, 1);
    CHECK((ch.flagsA & 0x02) == 0);            // count != -1 -> no dirty-anim
}

// ===========================================================================
// Play-sample / sample-loop.
// ===========================================================================
TEST(CharActionMisc, PlaySampleAttachThenFinish) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    ActionNode* n = CreatePlaySampleAction(&ch, "voice/hello");
    CHECK_EQ(n->type, (u8)47);
    PlaySampleActionUpdate(n);                 // first call: attach
    CHECK_EQ(g_mr.attachAni, 1);
    CHECK(ch.motion != nullptr);

    n->callCount = 1;
    g_mr.motionResult = 0;                     // queue still playing
    PlaySampleActionUpdate(n);
    CHECK_EQ(ch.actions, n);

    g_mr.motionResult = -1;                    // queue done -> finish
    PlaySampleActionUpdate(n);
    CHECK(ch.actions == nullptr);
    CHECK((ch.flagsA & 0x10) != 0);            // idle-anim pending set on finish
}

TEST(CharActionMisc, SampleLoopRequiresIdlePending) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    // Without the idle-anim-pending flag the builder returns null.
    CHECK(CreateSampleLoopAction(&ch, "amb/loop") == nullptr);

    ch.flagsA |= 0x10u;                        // idle-anim pending
    ActionNode* n = CreateSampleLoopAction(&ch, "amb/loop");
    CHECK(n != nullptr);
    CHECK_EQ(n->type, (u8)48);

    // Avatar mid-anim + node not started -> defer (callCount -> -1).
    ch.motion = reinterpret_cast<ActionNode*>(9);
    SampleLoopActionUpdate(n);
    CHECK_EQ(n->callCount, -1);

    // Avatar idle, started flag set on first real call -> attaches loop.
    ch.motion = nullptr;
    n->callCount = 0;
    SampleLoopActionUpdate(n);
    CHECK((n->state & 1) != 0);                // marked started
    CHECK_EQ(g_mr.attachAni, 1);
    CHECK(ch.motion != nullptr);
}

// ===========================================================================
// Use-gate.
// ===========================================================================
TEST(CharActionMisc, UseGateAlreadyAtRoom) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    // sceneSlotIndex hook returns 7; gate room 7 == already there -> free.
    ActionNode* n = CreateUseGateAction(&ch, "door", /*room*/ 7, /*scene*/ 0, 1.0f);
    CHECK_EQ(n->type, (u8)52);
    UseGateActionUpdate(n);
    CHECK(ch.actions == nullptr);              // freed immediately
}

TEST(CharActionMisc, UseGateChainsWalkAndRelocate) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    // gate room 99 != slot 7 -> resolve tile, chain walk + fade + relocate.
    ActionNode* n = CreateUseGateAction(&ch, "door", /*room*/ 99, /*scene*/ 2, 1.0f);
    UseGateActionUpdate(n);
    // The gate node frees itself; the chained actions remain on the queue.
    CHECK(ch.actions != nullptr);
    // Verify a walk (type 45) and a relocate (type 51) were chained.
    bool sawWalk = false, sawReloc = false;
    for (ActionNode* it = ch.actions; it; it = it->next_link) {
        if (it->type == 45) sawWalk = true;
        if (it->type == 51) sawReloc = true;
    }
    CHECK(sawWalk);
    CHECK(sawReloc);
}

// ===========================================================================
// Walk builders.
// ===========================================================================
TEST(CharActionMisc, QueueWalkToTargetBuildsWalk) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    ActionNode* n = QueueWalkToTarget(&ch, nullptr);
    CHECK(n != nullptr);
    CHECK_EQ(n->type, (u8)45);
    CHECK_EQ(n->args[1], 3);                    // col (FillActionNode shifts to args[1])
    CHECK_EQ(n->args[2], 4);                    // row
    CHECK((n->args[10] & 0x10) != 0);          // idle-walk tag (+345)

    // Tile-resolve failure -> null.
    g_mr.worldOk = 0;
    CHECK(QueueWalkToTarget(&ch, nullptr) == nullptr);
}

TEST(CharActionMisc, QueueWalk2RndDummyWrongSlotFrees) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    // Build a host node whose destSlot (args[1]) != the hook slot (7) -> frees.
    ActionNode* host = InsertActionVararg(&ch, 0, nullptr, 0);
    host->args[1] = 3;                          // dest slot 3 != 7
    QueueWalk2RndDummy(host, nullptr);
    CHECK(ch.actions == nullptr);
}

TEST(CharActionMisc, QueueWalk2RndDummySameSlotChainsWalk) {
    Setup();
    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    g_characters[0] = &ch; g_characterCount = 1;

    ActionNode* host = InsertActionVararg(&ch, 0, nullptr, 0);
    host->args[1] = 7;                          // dest slot == hook slot 7
    QueueWalk2RndDummy(host, nullptr);
    // host freed; a walk chained.
    bool sawWalk = false;
    for (ActionNode* it = ch.actions; it; it = it->next_link)
        if (it->type == 45 && std::strcmp(it->animBuf, "Walk2RndDummy") == 0)
            sawWalk = true;
    CHECK(sawWalk);
}

// ===========================================================================
// Fade interpolation (t = 0 / 0.5 / 1).
// ===========================================================================
TEST(CharActionMisc, FadeMathEndpoints) {
    Setup();
    // Fade-in ramp (fadeIn=true): t = 1 - elapsed*0.02.
    //   elapsed 0   -> 1.0     (alpha 255)
    //   elapsed 25  -> 0.5     (alpha 128 after round)
    //   elapsed 50  -> 0.0     (alpha 0)
    CHECK(std::fabs(FadeParam(/*end*/0, /*start*/0.0f, true) - 1.0) < 1e-9);
    CHECK(std::fabs(FadeParam(25, 0.0f, true) - 0.5) < 1e-9);
    CHECK(std::fabs(FadeParam(50, 0.0f, true) - 0.0) < 1e-9);

    // Fade-out ramp (fadeIn=false): t = elapsed*0.02.
    CHECK(std::fabs(FadeParam(0, 0.0f, false) - 0.0) < 1e-9);
    CHECK(std::fabs(FadeParam(25, 0.0f, false) - 0.5) < 1e-9);
    CHECK(std::fabs(FadeParam(50, 0.0f, false) - 1.0) < 1e-9);

    // Over/undershoot clamps.
    CHECK(std::fabs(FadeParam(100, 0.0f, false) - 1.0) < 1e-9);
    CHECK(std::fabs(FadeParam(100, 0.0f, true) - 0.0) < 1e-9);

    // Alpha byte mapping (round-to-nearest * 255).
    CHECK_EQ(FadeAlpha(0.0), 0);
    CHECK_EQ(FadeAlpha(0.5), 128);             // round(127.5) -> 128
    CHECK_EQ(FadeAlpha(1.0), 255);
}

// ===========================================================================
// Group-interaction coroutine.
// ===========================================================================
namespace {
struct GiRec {
    int free = 0, enqueue = 0, req39 = 0;
    i32 enqFrom = 0, enqTo = 0;
    int memberCount = 5;        // how many ids resolve to a record
};
GiRec g_gi;

void* GiFind(i32 id) {
    if (id < 0) return nullptr;
    // ids 0..(memberCount-1) resolve; partner id (1000) always resolves.
    if (id == 1000) return reinterpret_cast<void*>(0xABCD);
    if (id < g_gi.memberCount) return reinterpret_cast<void*>(static_cast<intptr_t>(id + 1));
    return nullptr;
}
int  GiReady(void*) { return 1; }
void GiFree(HeRecord*) { ++g_gi.free; }
void GiEnq(i32 a, i32 b, i32) { ++g_gi.enqueue; g_gi.enqFrom = a; g_gi.enqTo = b; }
void GiReq39(i32, i32, i32) { ++g_gi.req39; }
const GroupInteractHooks kGiHooks = { GiFind, GiReady, GiFree, GiEnq, GiReq39 };
} // namespace

TEST(CharActionMisc, GroupInteractFullGroupFrees) {
    g_gi = GiRec{};
    g_gi.memberCount = 5;                        // all 5 members live
    SetGroupInteractHooks(&kGiHooks);

    HeRecord rec; std::memset(&rec, 0, sizeof(rec));
    Gi_State(&rec) = 0;
    GroupLeader leader{};
    leader.partnerId = 500; leader.objField = 600; leader.kindByte = 4;
    for (int i = 0; i < 5; ++i) leader.memberIds[i] = i; // 0..4 all resolve

    GroupInteractStep(&rec, &leader, /*partnerId*/ 1000);
    CHECK_EQ(g_gi.free, 1);                      // full group -> freed
}

TEST(CharActionMisc, GroupInteractStartsTalkingThenInteracts) {
    g_gi = GiRec{};
    g_gi.memberCount = 2;                        // only 2 members live (not full)
    SetGroupInteractHooks(&kGiHooks);
    crt::Srand(12345);                           // deterministic RNG

    HeRecord rec; std::memset(&rec, 0, sizeof(rec));
    Gi_State(&rec) = 0;
    Gi_Accum(&rec) = 100.0f;                     // huge accumulator -> low roll -> start
    GroupLeader leader{};
    leader.partnerId = 500; leader.objField = 600; leader.kindByte = 4; leader.roleByte = 0;
    leader.memberIds[0] = 0; leader.memberIds[1] = 1;
    for (int i = 2; i < 5; ++i) leader.memberIds[i] = -1;

    // state 0: with a large accumulator the roll forces the transition to state 1.
    GroupInteractStep(&rec, &leader, 1000);
    CHECK_EQ(Gi_State(&rec), 1);
    CHECK(Gi_Accum(&rec) == 0.0f);

    // state 1: dwell until 3.0, then emit the opcode-8 interaction and reset.
    for (int i = 0; i < 4 && Gi_State(&rec) == 1; ++i)
        GroupInteractStep(&rec, &leader, 1000);
    CHECK_EQ(g_gi.enqueue, 1);
    CHECK_EQ(g_gi.enqFrom, 500);                 // role 0 -> from = leader id (+1 field)
    CHECK_EQ(Gi_State(&rec), 0);                 // reset to join state
    CHECK_EQ(g_gi.req39, 0);                      // kind 4 != 6 -> no appointment
}

TEST(CharActionMisc, GroupInteractPlayerCharQueuesAppointment) {
    g_gi = GiRec{};
    g_gi.memberCount = 1;
    SetGroupInteractHooks(&kGiHooks);

    HeRecord rec; std::memset(&rec, 0, sizeof(rec));
    Gi_State(&rec) = 1;                          // already active
    Gi_Accum(&rec) = 100.0f;                     // past the 3.0 dwell threshold
    GroupLeader leader{};
    leader.partnerId = 500; leader.objField = 600; leader.kindByte = 6; // player char
    leader.memberIds[0] = 0; for (int i = 1; i < 5; ++i) leader.memberIds[i] = -1;

    GroupInteractStep(&rec, &leader, 1000);
    CHECK_EQ(g_gi.req39, 1);                      // kind 6 -> appointment queued
    CHECK_EQ(g_gi.enqueue, 1);
}

TEST(CharActionMisc, GroupInteractTerminalState) {
    g_gi = GiRec{};
    SetGroupInteractHooks(&kGiHooks);
    HeRecord rec; std::memset(&rec, 0, sizeof(rec));
    Gi_State(&rec) = -2;                         // terminal
    GroupLeader leader{};
    leader.partnerId = 500; leader.kindByte = 4;
    GroupInteractStep(&rec, &leader, 1000);
    CHECK_EQ(g_gi.free, 1);
}
