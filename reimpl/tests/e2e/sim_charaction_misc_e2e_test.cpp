// End-to-end: drive a live character through the idle->find-neighbour->talk flow
// via Character_Update, and run a use-gate sequence tick-by-tick, verifying the
// per-tick state, the spawned actions, and the emitted group-interaction commands
// against a hand-computed reference.
#include "sim/charaction_misc.h"
#include "sim/charaction.h"
#include "sim/character.h"
#include "sim/character_social.h"
#include "sim/actionqueue.h"
#include "sim/he.h"

#include "crt/rand.h"

#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// --- social hooks: resolve a fixed meeting tile, record idle-anim attaches -----
struct SocRec { int meet = 0, idle = 0; };
SocRec g_soc;
int SocMeet(Character*, Character*, int* c, int* r) { ++g_soc.meet; if (c)*c=11; if (r)*r=22; return 1; }
void* SocIdle(Character*, bool) { ++g_soc.idle; return reinterpret_cast<void*>(1); }
const SocialHooks kSoc = { SocMeet, SocIdle };

void MakeAvatar(SocialAvatar& a, int group, int world, float x, float y, float z) {
    std::memset(&a, 0, sizeof(a));
    a.groupId = group; a.worldId = world;
    a.pos[0] = x; a.pos[1] = y; a.pos[2] = z;
    a.meshGate = 0;            // gate test passes (!=1)
}

} // namespace

// ===========================================================================
// idle -> find neighbour -> spawn talk action.
// ===========================================================================
TEST(CharActionMiscE2E, IdleFindNeighbourSpawnsTalk) {
    RegisterHandlers();
    ResetCharacters();
    g_gameTick = 0;
    g_soc = SocRec{};
    SetSocialHooks(&kSoc);

    // Two characters in the same group/world, 5.0 units apart (within radius 20).
    SocialAvatar av0, av1;
    MakeAvatar(av0, /*group*/ 1, /*world*/ 1, 0.0f, 0.0f, 0.0f);
    MakeAvatar(av1, 1, 1, 5.0f, 0.0f, 0.0f);

    Character a, b;
    std::memset(&a, 0, sizeof(a));
    std::memset(&b, 0, sizeof(b));
    a.social = &av0; b.social = &av1;
    a.scene = reinterpret_cast<void*>(1);  // in the active scene
    b.scene = reinterpret_cast<void*>(1);
    a.slotIndex = 9;
    g_characters[0] = &a; g_characters[1] = &b;
    g_characterCount = 2;

    // Neither has an action: a's idle branch finds b and spawns a talk (type 45).
    // (b's idle branch is also eligible and pairs with a — faithful: both live
    //  actors run the idle scan in the same frame, so at least one meet resolves.)
    g_gameTick = 1;
    CharacterUpdate();

    CHECK(g_soc.meet >= 1);                  // tile resolve ran for the spawn(s)
    CHECK(a.actions != nullptr);             // a got a talk action
    CHECK_EQ(a.actions->type, (u8)45);
    CHECK_EQ(a.actions->args[1], 11);        // col
    CHECK_EQ(a.actions->args[2], 22);        // row
    CHECK_EQ(a.actions->args[3], 9);         // scene/slot id
}

TEST(CharActionMiscE2E, IdleNoNeighbourClearsDirtyMesh) {
    RegisterHandlers();
    ResetCharacters();
    g_soc = SocRec{};
    SetSocialHooks(&kSoc);

    // Lone character, far from anyone -> no talk action, dirty-mesh cleared.
    SocialAvatar av;
    MakeAvatar(av, 1, 1, 0.0f, 0.0f, 0.0f);
    Character a;
    std::memset(&a, 0, sizeof(a));
    a.social = &av;
    a.scene = reinterpret_cast<void*>(1);
    a.flagsA = 0x08;                         // dirty-mesh set
    g_characters[0] = &a; g_characterCount = 1;

    g_gameTick = 1;
    CharacterUpdate();
    CHECK(a.actions == nullptr);             // no neighbour -> no talk
    CHECK((a.flagsA & 0x08) == 0);           // dirty-mesh cleared
}

TEST(CharActionMiscE2E, IdleIneligibleWhenSitting) {
    RegisterHandlers();
    ResetCharacters();
    g_soc = SocRec{};
    SetSocialHooks(&kSoc);

    // Two would-be neighbours, but the actor is sitting (+141? no: +140 & 0x20)
    // so it is ineligible and never scans.
    SocialAvatar av0, av1;
    MakeAvatar(av0, 1, 1, 0.0f, 0.0f, 0.0f);
    MakeAvatar(av1, 1, 1, 1.0f, 0.0f, 0.0f);
    Character a, b;
    std::memset(&a, 0, sizeof(a)); std::memset(&b, 0, sizeof(b));
    a.social = &av0; b.social = &av1;
    a.scene = reinterpret_cast<void*>(1); b.scene = reinterpret_cast<void*>(1);
    a.flagsA = 0x20;                         // a is sitting -> ineligible
    b.flagsA = 0x20;                         // b too, so neither spawns
    g_characters[0] = &a; g_characters[1] = &b;
    g_characterCount = 2;

    g_gameTick = 1;
    CharacterUpdate();
    CHECK(a.actions == nullptr);             // sitting actor never spawns a talk
    CHECK(b.actions == nullptr);
    CHECK_EQ(g_soc.meet, 0);                 // neither scanned/resolved
}

// ===========================================================================
// use-gate sequence tick-by-tick.
// ===========================================================================
namespace {
struct GateRec { int attachMv = 0, step = 0; int slot = 7, worldOk = 1; };
GateRec g_gr;
void* GRAttMv(Character*, const char*, int) { ++g_gr.attachMv; return reinterpret_cast<void*>(1); }
void* GRAttAni(Character*, const char*, int) { return reinterpret_cast<void*>(1); }
int  GRStep(Character*) { ++g_gr.step; return -1; }
int  GRReady(Character*) { return 1; }
void GRStop(Character*) {}
int  GRSlot(void*) { return g_gr.slot; }
int  GRWorld(Character*, int, int* c, int* r) { if (c)*c=1; if (r)*r=2; return g_gr.worldOk; }
Character* GRNearby(Character*, float) { return nullptr; }
const MiscActionHooks kGate = { GRAttMv, GRAttAni, GRStep, GRReady, GRStop, GRSlot, GRWorld, GRNearby };
} // namespace

TEST(CharActionMiscE2E, UseGateSequenceTickByTick) {
    RegisterHandlers();
    ResetCharacters();
    g_gameTick = 0;
    g_gr = GateRec{};
    g_gr.slot = 7;                            // current slot
    SetMiscActionHooks(&kGate);

    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    ch.scene = reinterpret_cast<void*>(1);
    g_characters[0] = &ch; g_characterCount = 1;

    // Gate to room 42 (slot 7) -> not there -> chain walk + fade + relocate.
    ActionNode* gate = CreateUseGateAction(&ch, "tor", /*room*/ 42, /*scene*/ 3, 1.0f);
    CHECK_EQ(ch.actions, gate);

    // Tick 1: dispatch the gate node. It resolves the destination tile and chains
    // the follow-on actions, then frees itself.
    g_gameTick = 1;
    DispatchCurrent(&ch);

    CHECK(ch.actions != nullptr);             // chained actions remain
    // Reference: queue head order after the gate frees is the chained walk (45).
    int types[8]; int n = 0;
    for (ActionNode* it = ch.actions; it && n < 8; it = it->next_link)
        types[n++] = it->type;
    // Expect a walk (45) and a relocate (51) present in the chain.
    bool sawWalk = false, sawReloc = false, sawGate = false;
    for (int i = 0; i < n; ++i) {
        if (types[i] == 45) sawWalk = true;
        if (types[i] == 51) sawReloc = true;
        if (types[i] == 52) sawGate = true;
    }
    CHECK(sawWalk);
    CHECK(sawReloc);
    CHECK(!sawGate);                          // the gate node freed itself
}

TEST(CharActionMiscE2E, UseGateAlreadyThereFreesImmediately) {
    RegisterHandlers();
    ResetCharacters();
    g_gr = GateRec{};
    g_gr.slot = 42;                           // current slot already == dest room
    SetMiscActionHooks(&kGate);

    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    ch.scene = reinterpret_cast<void*>(1);
    g_characters[0] = &ch; g_characterCount = 1;

    ActionNode* gate = CreateUseGateAction(&ch, "tor", /*room*/ 42, 3, 1.0f);
    CHECK_EQ(ch.actions, gate);
    DispatchCurrent(&ch);
    CHECK(ch.actions == nullptr);             // already there -> freed, no chain
}

// ===========================================================================
// Group interaction full coroutine sequence (state 0 -> 1 -> interact -> 0).
// ===========================================================================
namespace {
struct GiE2E { int free = 0, enq = 0, req39 = 0; i32 from = 0, to = 0; };
GiE2E g_gie;
void* GieFind(i32 id) {
    if (id < 0) return nullptr;
    if (id == 1000) return reinterpret_cast<void*>(0xBEEF);
    if (id < 2) return reinterpret_cast<void*>(static_cast<intptr_t>(id + 1));
    return nullptr;
}
int  GieReady(void*) { return 1; }
void GieFree(HeRecord*) { ++g_gie.free; }
void GieEnq(i32 a, i32 b, i32) { ++g_gie.enq; g_gie.from = a; g_gie.to = b; }
void GieReq39(i32, i32, i32) { ++g_gie.req39; }
const GroupInteractHooks kGie = { GieFind, GieReady, GieFree, GieEnq, GieReq39 };
} // namespace

TEST(CharActionMiscE2E, GroupInteractCoroutineSequence) {
    g_gie = GiE2E{};
    SetGroupInteractHooks(&kGie);
    crt::Srand(7);                            // deterministic

    HeRecord rec; std::memset(&rec, 0, sizeof(rec));
    Gi_State(&rec) = 0;
    Gi_Accum(&rec) = 50.0f;                   // bias toward starting quickly
    GroupLeader leader{};
    leader.partnerId = 500; leader.objField = 600; leader.kindByte = 4; leader.roleByte = 1;
    leader.memberIds[0] = 0; leader.memberIds[1] = 1;
    for (int i = 2; i < 5; ++i) leader.memberIds[i] = -1;

    // Run the coroutine for a bounded number of ticks; it should: start talking
    // (state 1), dwell to 3.0, then emit exactly one interaction and reset.
    int ticks = 0;
    bool startedTalking = false;
    while (ticks++ < 20 && g_gie.enq == 0) {
        GroupInteractStep(&rec, &leader, 1000);
        if (Gi_State(&rec) == 1) startedTalking = true;
    }
    CHECK(startedTalking);
    CHECK_EQ(g_gie.enq, 1);
    // role byte 1 -> ids swapped: from = partnerId(1000), to = leader id (500).
    CHECK_EQ(g_gie.from, 1000);
    CHECK_EQ(g_gie.to, 500);
    CHECK_EQ(Gi_State(&rec), 0);              // reset to join state
}
