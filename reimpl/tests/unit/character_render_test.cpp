// Unit tests for the Character render-leaf cluster (src/sim/character_render.cpp).
// Golden vectors for the pure attach-offset geometry and head-variant modulo were
// computed independently with python3 (see the implementer report). Cross-module /
// render calls are captured through a recording CharRenderHooks mock.
#include "test.h"

#include "sim/character_render.h"
#include "sim/character_state.h"   // SetTurnState / TurnState for the candidate gate

#include <cstring>

using namespace guild::sim;
using guild::u8;

namespace {

// ---- recording mock for the render/anim/sound/script hooks -----------------
struct Rec {
    int  selectTexCalls = 0;
    int  lastVariant = -1;
    int  setVisibleCalls = 0;
    int  lastVisible = -1;
    RenderActor* lastVisibleActor = nullptr;
    int  standUpCalls = 0;
    int  unlinkCalls = 0;
    int  unlinkBudget = 0;   // returns non-null this many times then null
    int  setLoopCalls = 0;
    int  clearLoopCalls = 0;
    int  attachItemCalls = 0;
    int  lastAttachBone = -1;
    const char* lastAttachName = nullptr;
    int  reportErrorCalls = 0;
    const char* lastError = nullptr;
    float rootTrans[3] = {0, 0, 0};
    float lastPos[3] = {0, 0, 0};
    float lastRot[3] = {0, 0, 0};
    float listenerPos[3] = {0, 0, 0};
    float listenerRot[3] = {0, 0, 0};
    float oriFwd[3] = {0, 0, 0};
    float oriUp[3] = {0, 0, 0};
};
Rec g_rec;

void mPivot(void*, const float in[3], float out[3]) { out[0]=in[0]; out[1]=in[1]; out[2]=in[2]; }
void mRoot(void*, float out[3]) { out[0]=g_rec.rootTrans[0]; out[1]=g_rec.rootTrans[1]; out[2]=g_rec.rootTrans[2]; }
void mSetWorldTrans(const float rot[3]) { std::memcpy(g_rec.lastRot, rot, sizeof(float)*3); }
void mSetPos(const float pos[3]) { std::memcpy(g_rec.lastPos, pos, sizeof(float)*3); }
void mSetListenerVecs(const float pos[3], const float rot[3]) {
    std::memcpy(g_rec.listenerPos, pos, sizeof(float)*3);
    std::memcpy(g_rec.listenerRot, rot, sizeof(float)*3);
}
void mSetListenerOri(const float fwd[3], const float up[3]) {
    std::memcpy(g_rec.oriFwd, fwd, sizeof(float)*3);
    std::memcpy(g_rec.oriUp, up, sizeof(float)*3);
}
int  mSelectTex(void*, int variant) { g_rec.selectTexCalls++; g_rec.lastVariant = variant; return 0x99; }
void mSetVisible(RenderActor* a, int v) { g_rec.setVisibleCalls++; g_rec.lastVisible = v; g_rec.lastVisibleActor = a; }
void mStandUp(RenderActor*) { g_rec.standUpCalls++; }
void* mUnlink(void*) { g_rec.unlinkCalls++; return (g_rec.unlinkCalls <= g_rec.unlinkBudget) ? (void*)1 : nullptr; }
void mSetLoop(void*) { g_rec.setLoopCalls++; }
void mClearLoop(void*) { g_rec.clearLoopCalls++; }
void mAttachItem(RenderActor*, int bone, const char* name) { g_rec.attachItemCalls++; g_rec.lastAttachBone = bone; g_rec.lastAttachName = name; }
void mReportError(const char* msg) { g_rec.reportErrorCalls++; g_rec.lastError = msg; }

const CharRenderHooks g_mock = {
    mPivot, mRoot, mSetWorldTrans, mSetPos, mSetListenerVecs, mSetListenerOri,
    mSelectTex, mSetVisible, mStandUp, mUnlink, mSetLoop, mClearLoop,
    mAttachItem, mReportError,
};

void Install() { g_rec = Rec{}; SetCharRenderHooks(&g_mock); }

bool feq(float a, float b) { return a == b; }  // golden values are exact constants

} // namespace

// === ComputeAttachOffset: golden constant tables per slot ====================
TEST(CharRenderAttach, Slot0Standing) {
    Install();
    RenderActor a{}; a.flagsA = 0;
    AttachGeom g{};
    u8 r = ComputeAttachOffset(&a, 0, &g);
    CHECK_EQ((int)r, 0);
    CHECK(feq(g.offset[0], -3.0f) && feq(g.offset[1], 63.0f) && feq(g.offset[2], 36.0f));
    CHECK(feq(g.rotation[0], 0.0f) && feq(g.rotation[1], 3.14159274f) && feq(g.rotation[2], 0.0f));
}

TEST(CharRenderAttach, Slot0Sitting) {
    Install();
    RenderActor a{}; a.flagsA = kRaSitting;
    AttachGeom g{};
    ComputeAttachOffset(&a, 0, &g);
    CHECK(feq(g.offset[1], 48.0f));   // 63 + (-15)
}

TEST(CharRenderAttach, Slot1And2Hands) {
    Install();
    RenderActor a{};
    AttachGeom g1{}, g2{};
    ComputeAttachOffset(&a, 1, &g1);
    ComputeAttachOffset(&a, 2, &g2);
    CHECK(feq(g1.offset[0], -10.0f) && feq(g1.offset[1], 63.0f) && feq(g1.offset[2], -14.0f));
    CHECK(feq(g2.offset[0],  10.0f) && feq(g2.offset[1], 63.0f) && feq(g2.offset[2], -14.0f));
    CHECK(feq(g1.rotation[0], 0.08726646f) && feq(g1.rotation[1], 0.0f));
    CHECK(feq(g2.rotation[0], 0.08726646f) && feq(g2.rotation[1], 0.0f));
}

TEST(CharRenderAttach, Slot3HeadAndSit) {
    Install();
    RenderActor stand{}; AttachGeom g{};
    ComputeAttachOffset(&stand, 3, &g);
    CHECK(feq(g.offset[0], 0.0f) && feq(g.offset[1], 65.0f) && feq(g.offset[2], 0.0f));
    RenderActor sit{}; sit.flagsA = kRaSitting; AttachGeom gs{};
    ComputeAttachOffset(&sit, 3, &gs);
    CHECK(feq(gs.offset[1], 50.0f));  // 65 - 15
}

TEST(CharRenderAttach, RootTranslationAdded) {
    Install();
    g_rec.rootTrans[0] = 100.0f; g_rec.rootTrans[1] = 200.0f; g_rec.rootTrans[2] = 300.0f;
    RenderActor a{}; AttachGeom g{};
    ComputeAttachOffset(&a, 0, &g);
    CHECK(feq(g.offset[0], 97.0f) && feq(g.offset[1], 263.0f) && feq(g.offset[2], 336.0f));
}

TEST(CharRenderAttach, NullActorIsNoOp) {
    Install();
    AttachGeom g{}; g.offset[0] = 7.0f;
    u8 r = ComputeAttachOffset(nullptr, 0, &g);
    CHECK_EQ((int)r, 0);
    CHECK(feq(g.offset[0], 7.0f));  // untouched
}

TEST(CharRenderAttach, ApplyAttachPushesRotThenPos) {
    Install();
    RenderActor a{}; ApplyAttachOffset(&a, 0);
    CHECK(feq(g_rec.lastRot[1], 3.14159274f));
    CHECK(feq(g_rec.lastPos[0], -3.0f) && feq(g_rec.lastPos[1], 63.0f) && feq(g_rec.lastPos[2], 36.0f));
}

TEST(CharRenderAttach, SetupAttachCameraFeedsListener) {
    Install();
    RenderActor a{}; SetupAttachCamera(&a, 1);
    CHECK(feq(g_rec.listenerPos[0], -10.0f));
    CHECK(feq(g_rec.listenerRot[0], 0.08726646f));
    // null actor: no listener call (pos stays zero)
    Install();
    SetupAttachCamera(nullptr, 1);
    CHECK(feq(g_rec.listenerPos[0], 0.0f));
}

TEST(CharRenderAttach, ApplyBoneTransformReadsBoneVectors) {
    Install();
    float mesh[40] = {0};
    mesh[23] = 1.0f; mesh[24] = 2.0f; mesh[25] = 3.0f;   // forward bone
    mesh[36] = 4.0f; mesh[37] = 5.0f; mesh[38] = 6.0f;   // up bone
    ApplyBoneTransform(mesh);
    CHECK(feq(g_rec.oriFwd[0], 1.0f) && feq(g_rec.oriFwd[1], 2.0f) && feq(g_rec.oriFwd[2], 3.0f));
    CHECK(feq(g_rec.oriUp[0], 4.0f) && feq(g_rec.oriUp[1], 5.0f) && feq(g_rec.oriUp[2], 6.0f));
}

// === ApplyHeadVariant: id modulo head-count ==================================
TEST(CharRenderHead, ModuloSmallCount) {
    Install();
    RenderActor root{}; root.universe = (void*)1;       // not the wild universe
    RenderActor a{}; a.attached = &root; a.id = 7;
    // headCount 3 (<=4) -> id % 3 == 1; activeMeshId != id -> returns variant
    u8 v = ApplyHeadVariant(&a, 3, /*activeMeshId*/ 999);
    CHECK_EQ((int)v, 1);
    CHECK_EQ(g_rec.selectTexCalls, 0);
}

TEST(CharRenderHead, MaskLargeCount) {
    Install();
    RenderActor root{}; root.universe = (void*)1;
    RenderActor a{}; a.attached = &root; a.id = 13;
    // headCount 6 (>4) -> id & 3 == 1
    u8 v = ApplyHeadVariant(&a, 6, 999);
    CHECK_EQ((int)v, 1);
    a.id = 20;
    v = ApplyHeadVariant(&a, 6, 999);
    CHECK_EQ((int)v, 0);  // 20 & 3 == 0
}

TEST(CharRenderHead, ActiveMeshAppliesTexture) {
    Install();
    RenderActor root{}; root.universe = (void*)1;
    RenderActor a{}; a.attached = &root; a.id = 10;
    // activeMeshId == id -> SelectTextureSet path; hook returns 0x99
    u8 v = ApplyHeadVariant(&a, 6, /*activeMeshId*/ 10);
    CHECK_EQ((int)v, 0x99);
    CHECK_EQ(g_rec.selectTexCalls, 1);
    CHECK_EQ(g_rec.lastVariant, 2);   // 10 & 3 == 2 (headCount 6 > 4)
}

TEST(CharRenderHead, NullAndNoAttached) {
    Install();
    CHECK_EQ((int)ApplyHeadVariant(nullptr, 3, 0), 0);
    RenderActor a{}; a.attached = nullptr;
    CHECK_EQ((int)ApplyHeadVariant(&a, 3, 0), 0);
}

// === mode / flag accessors ===================================================
TEST(CharRenderFlags, FlagRedrawByMode) {
    RenderActor a{};
    for (int m = 0; m < 6; ++m) {
        a.mode = (guild::u8)m; a.redrawFlags = 0;
        FlagRedrawByMode(&a);
        bool expect = (m == 3 || m == 4 || m < 2);
        CHECK_EQ((a.redrawFlags & kRrRedraw) != 0, expect);
    }
}

TEST(CharRenderFlags, SetLowPolyToggles) {
    Install();
    RenderActor a{};
    CHECK_EQ(SetLowPoly(&a, true), 1);
    CHECK((a.flagsB & kRbLowPoly) != 0);
    CHECK_EQ(SetLowPoly(&a, false), 1);
    CHECK((a.flagsB & kRbLowPoly) == 0);
    // null -> reports error, still returns 1
    CHECK_EQ(SetLowPoly(nullptr, true), 1);
    CHECK_EQ(g_rec.reportErrorCalls, 1);
}

TEST(CharRenderFlags, ResetStateIfMode3) {
    RenderActor a{}; a.mode = 3; a.mode3State = 42;
    ResetStateIfMode3(&a);
    CHECK_EQ(a.mode3State, 0);
    a.mode = 2; a.mode3State = 42;
    ResetStateIfMode3(&a);
    CHECK_EQ(a.mode3State, 42);  // unchanged
}

TEST(CharRenderFlags, ResetAiTarget) {
    Install();
    RenderActor a{}; a.type = 2; a.isMaster = 1; a.aiTarget = 5; a.aiFlags = 0;
    ResetAiTarget(&a);
    CHECK_EQ(a.aiTarget, -1);
    CHECK((a.aiFlags & kAiResetBit) != 0);
    // wrong type: no change
    RenderActor b{}; b.type = 3; b.isMaster = 1; b.aiTarget = 5;
    ResetAiTarget(&b);
    CHECK_EQ(b.aiTarget, 5);
    // not master: no change
    RenderActor c{}; c.type = 1; c.isMaster = 0; c.aiTarget = 5;
    ResetAiTarget(&c);
    CHECK_EQ(c.aiTarget, 5);
}

// === hide / stop wrappers ====================================================
TEST(CharRenderWrap, HideAttachedActor) {
    Install();
    RenderActor attached{};
    RenderActor host{}; host.attached = &attached;
    RenderActor outer{}; outer.attachHost = &host;
    HideAttachedActor(&outer);
    CHECK_EQ(g_rec.setVisibleCalls, 1);
    CHECK_EQ(g_rec.lastVisible, 0);
    CHECK(g_rec.lastVisibleActor == &attached);
}

TEST(CharRenderWrap, Stop) {
    Install();
    RenderActor a{};
    CHECK_EQ(Stop(&a), 0);
    CHECK_EQ(g_rec.standUpCalls, 1);
    CHECK_EQ(Stop(nullptr), 0);
    CHECK_EQ(g_rec.reportErrorCalls, 1);
}

TEST(CharRenderWrap, AttachItemToBone2) {
    Install();
    RenderActor a{};
    CHECK_EQ(AttachItemToBone2(&a, "sword"), 1);
    CHECK_EQ(g_rec.attachItemCalls, 1);
    CHECK_EQ(g_rec.lastAttachBone, 2);   // right hand
    CHECK_EQ(AttachItemToBone2(nullptr, "x"), 0);
    CHECK_EQ(g_rec.attachItemCalls, 1);  // not called again
}

// === anim / queue control ====================================================
TEST(CharRenderAnim, ToggleAniPlayback) {
    Install();
    RenderActor a{}; a.action = (void*)1; a.mesh = (void*)1; a.flagsA = 0;
    ToggleAniPlayback(&a, true);   // pause
    CHECK_EQ(g_rec.clearLoopCalls, 1);
    CHECK((a.flagsA & kRaAnimPaused) != 0);
    ToggleAniPlayback(&a, false);  // resume
    CHECK_EQ(g_rec.setLoopCalls, 1);
    CHECK((a.flagsA & kRaAnimPaused) == 0);
    // missing handles -> no-op
    Install();
    RenderActor b{}; b.action = nullptr; b.mesh = (void*)1;
    ToggleAniPlayback(&b, true);
    CHECK_EQ(g_rec.clearLoopCalls, 0);
}

TEST(CharRenderAnim, KillAnimationsDrainsQueue) {
    Install();
    g_rec.unlinkBudget = 3;   // 3 nodes then drained
    RenderActor a{}; a.action = (void*)1;
    CHECK_EQ(CmdKillCharacterAnimations(&a), 0);
    CHECK_EQ(g_rec.unlinkCalls, 4);   // 3 non-null + 1 null
    // null handle -> error path
    Install();
    CHECK_EQ(CmdKillCharacterAnimations(nullptr), 0);
    CHECK_EQ(g_rec.reportErrorCalls, 1);
    // no action head -> no unlink
    Install();
    RenderActor b{}; b.action = nullptr;
    CmdKillCharacterAnimations(&b);
    CHECK_EQ(g_rec.unlinkCalls, 0);
}

// === eligibility predicate ===================================================
TEST(CharRenderCand, IsAccidentCandidate) {
    Install();
    SetTurnState(TurnState{0, -1, 0, 0});  // standalone: owner-for-turn always true
    RenderActor owner{};
    RenderActor attached{};
    RenderActor a{};
    a.marker = 1; a.type = 0; a.alive = 1;
    a.owner = &owner; a.hasOwner = true;
    a.attached = &attached;
    a.ownerColumn0 = 77; a.ownerHandle44 = 77;  // match
    CHECK(IsAccidentCandidate(&a));
    // mismatch handle -> not a candidate
    a.ownerHandle44 = 78;
    CHECK(!IsAccidentCandidate(&a));
    a.ownerHandle44 = 77;
    // free slot marker
    a.marker = 0xFFFF;
    CHECK(!IsAccidentCandidate(&a));
    a.marker = 1;
    // wrong type
    a.type = 2;
    CHECK(!IsAccidentCandidate(&a));
    a.type = 0;
    // not alive
    a.alive = 0;
    CHECK(!IsAccidentCandidate(&a));
}
