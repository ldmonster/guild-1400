// End-to-end flow for the Character render-leaf cluster: spawn an actor + an
// attached head actor, resolve a head variant, compute and apply the attach offset
// (hands), wire the 3D-sound listener, then drive visibility/anim/queue control and
// the per-turn accident eligibility gate. Uses one recording CharRenderHooks mock.
#include "test.h"

#include "sim/character_render.h"
#include "sim/character_state.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::u8;

namespace {

struct E2ERec {
    std::vector<int> texVariants;
    std::vector<int> visibleCalls;   // visible flag values
    float listenerPos[3] = {0, 0, 0};
    float listenerRot[3] = {0, 0, 0};
    int   standUp = 0;
    int   unlink = 0;
    int   unlinkBudget = 0;
    int   setLoop = 0, clearLoop = 0;
    int   attachItem = 0;
    float root[3] = {0, 0, 0};
};
E2ERec R;

void hPivot(void*, const float in[3], float out[3]) { out[0]=in[0]; out[1]=in[1]; out[2]=in[2]; }
void hRoot(void*, float o[3]) { o[0]=R.root[0]; o[1]=R.root[1]; o[2]=R.root[2]; }
void hSetWT(const float[3]) {}
void hSetPos(const float[3]) {}
void hListVecs(const float p[3], const float r[3]) { std::memcpy(R.listenerPos,p,12); std::memcpy(R.listenerRot,r,12); }
void hListOri(const float[3], const float[3]) {}
int  hSelTex(void*, int v) { R.texVariants.push_back(v); return v; }
void hVis(RenderActor*, int v) { R.visibleCalls.push_back(v); }
void hStand(RenderActor*) { R.standUp++; }
void* hUnlink(void*) { R.unlink++; return (R.unlink <= R.unlinkBudget) ? (void*)1 : nullptr; }
void hSetLoop(void*) { R.setLoop++; }
void hClearLoop(void*) { R.clearLoop++; }
void hAttach(RenderActor*, int, const char*) { R.attachItem++; }
void hErr(const char*) {}

const CharRenderHooks kHooks = {
    hPivot, hRoot, hSetWT, hSetPos, hListVecs, hListOri, hSelTex,
    hVis, hStand, hUnlink, hSetLoop, hClearLoop, hAttach, hErr,
};

} // namespace

TEST(CharRenderE2E, SpawnAttachVisibilityFlow) {
    R = E2ERec{};
    SetCharRenderHooks(&kHooks);
    SetTurnState(TurnState{0, -1, 0, 0});   // standalone

    // --- spawn an actor with an attached head sub-actor ---------------------
    RenderActor head{};
    head.universe = (void*)0x100;            // a real (non-wild) universe
    head.mesh     = (void*)0x200;

    RenderActor actor{};
    actor.marker  = 1;
    actor.type    = 0;
    actor.alive   = 1;
    actor.id      = 10;
    actor.mesh    = (void*)0x300;
    actor.attached = &head;
    actor.flagsA  = 0;

    // --- resolve a head variant; the active scene mesh equals this actor ----
    // headCount 6 (>4) -> id & 3 -> 10 & 3 == 2; activeMeshId == id -> apply.
    u8 variant = ApplyHeadVariant(&actor, 6, /*activeMeshId*/ 10);
    CHECK_EQ((int)variant, 2);
    CHECK_EQ((int)R.texVariants.size(), 1);
    CHECK_EQ(R.texVariants[0], 2);

    // --- compute + apply the left-hand attach offset (slot 1) ---------------
    R.root[0] = 1000.0f; R.root[1] = 0.0f; R.root[2] = 0.0f;
    AttachGeom hand{};
    ComputeAttachOffset(&actor, 1, &hand);
    CHECK(hand.offset[0] == 990.0f);   // -10 + 1000
    CHECK(hand.offset[1] == 63.0f);
    CHECK(hand.offset[2] == -14.0f);

    // attach an item to the right hand
    CHECK_EQ(AttachItemToBone2(&actor, "torch"), 1);
    CHECK_EQ(R.attachItem, 1);

    // --- wire the 3D listener from the attach geometry ----------------------
    SetupAttachCamera(&actor, 0);   // slot 0
    CHECK(R.listenerRot[1] == 3.14159274f);   // case-0 rotation Y == pi

    // --- sit the actor and recompute slot-3 head offset (sit bump) ----------
    actor.flagsA |= kRaSitting;
    AttachGeom seat{};
    ComputeAttachOffset(&actor, 3, &seat);
    CHECK(seat.offset[0] == 1000.0f);   // 0 + root X
    CHECK(seat.offset[1] == 50.0f);     // 65 - 15 + root Y(0)

    // --- pause then resume the actor's animation ----------------------------
    // gate is +296 (action) && +112 (handle112); the +52 mesh is body-only.
    actor.action = (void*)1;
    actor.handle112 = (void*)1;
    ToggleAniPlayback(&actor, true);
    CHECK((actor.flagsA & kRaAnimPaused) != 0);
    CHECK_EQ(R.clearLoop, 1);
    ToggleAniPlayback(&actor, false);
    CHECK((actor.flagsA & kRaAnimPaused) == 0);
    CHECK_EQ(R.setLoop, 1);

    // --- hide the attached actor --------------------------------------------
    RenderActor host{};
    host.attached = &head;
    RenderActor wrapper{};
    wrapper.attachHost = &host;
    HideAttachedActor(&wrapper);
    CHECK_EQ((int)R.visibleCalls.size(), 1);
    CHECK_EQ(R.visibleCalls[0], 0);

    // --- accident eligibility (standalone => owner-for-turn always true) ----
    RenderActor ownerObj{};
    actor.owner = &ownerObj; actor.hasOwner = true;
    actor.ownerColumn0 = 5; actor.ownerHandle44 = 5;   // attached+44 == owner.column0
    CHECK(IsAccidentCandidate(&actor));

    // --- stop + drain the action queue --------------------------------------
    R.unlinkBudget = 2;
    Stop(&actor);
    CHECK_EQ(R.standUp, 1);
    CHECK_EQ(CmdKillCharacterAnimations(&actor), 0);
    CHECK_EQ(R.unlink, 3);   // 2 non-null + terminating null
}
