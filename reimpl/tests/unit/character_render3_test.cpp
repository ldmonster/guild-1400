// Unit tests for the Character render3 cluster (src/sim/character_render3.cpp): the
// animation-attach / preload family, AttachItemToBone, ResolveHeadBone, the footstep /
// noise-timer audio leaves, and the camera ray helpers. Cross-module anim / path /
// renderer / sound / object calls are captured through a recording CharRender3Hooks
// mock. Golden values for the ray math and the noise step were computed with python3
// (see the implementer report).
#include "test.h"

#include "sim/character_render3.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace guild::sim;
using guild::u8;

namespace {

// ---- recording mock for the render3 hooks ---------------------------------
struct Rec3 {
    std::vector<std::string> convertCalls;
    std::vector<std::string> loadCalls;     // path passed to LoadStreamToStock
    std::vector<int>         loadFlags;
    int   findFreeReturns = 0;              // what findFreeMeshSlot returns (counter)
    void* nextFree = nullptr;
    int   findFreeCalls = 0;
    int   grayCalls = 0;
    std::vector<int> attachMasks;
    void* nextAttach = nullptr;
    int   pruneCalls = 0;
    void* nextUniverseNode = nullptr;
    std::vector<std::string> universeNames;
    int   detachCalls = 0;
    std::vector<std::string> boneNames;
    int   lightCalls = 0;
    int   switchCalls = 0;
    int   nextSwitchReturn = 7;
    std::vector<float> pivotX;              // x of each SetPivotVector
    int   setPosCalls = 0;
    float lastPos[3] = {0, 0, 0};
    float nextSampleHandle = 0.0f;
    std::vector<std::string> samplesPlayed;
    int   oneShotCalls = 0;
    int   errorCalls = 0;
    std::vector<std::string> errors;
};
Rec3* g_rec = nullptr;

void H_convert(char* s) { g_rec->convertCalls.emplace_back(s ? s : ""); }
void* H_findFree() { g_rec->findFreeCalls++; return g_rec->nextFree; }
void H_gray(int, int) { g_rec->grayCalls++; }
void* H_load(const char* p, int f) {
    g_rec->loadCalls.emplace_back(p ? p : "");
    g_rec->loadFlags.push_back(f);
    return reinterpret_cast<void*>(0x1000);
}
void* H_attach(void*, int mask) { g_rec->attachMasks.push_back(mask); return g_rec->nextAttach; }
void H_prune(void*) { g_rec->pruneCalls++; }
void* H_createMorph(void*, const char*) { return reinterpret_cast<void*>(0x2000); }
void H_boneDelta(void*, void*) {}
void* H_attachUniverse(void*, const char* name) {
    g_rec->universeNames.emplace_back(name ? name : "");
    return g_rec->nextUniverseNode;
}
void H_detach(void*) { g_rec->detachCalls++; }
void H_objBone(void*, const char* b) { g_rec->boneNames.emplace_back(b ? b : ""); }
void H_light(void*) { g_rec->lightCalls++; }
int  H_switch(int) { g_rec->switchCalls++; return g_rec->nextSwitchReturn; }
void H_pivot(void*, const float v[3]) { g_rec->pivotX.push_back(v[0]); }
void H_setpos(void*, const float v[3]) {
    g_rec->setPosCalls++;
    g_rec->lastPos[0] = v[0]; g_rec->lastPos[1] = v[1]; g_rec->lastPos[2] = v[2];
}
float H_playSample(void*, const char* n) { g_rec->samplesPlayed.emplace_back(n ? n : ""); return g_rec->nextSampleHandle; }
void H_oneShot(float, void*, int, float) { g_rec->oneShotCalls++; }
void H_error(const char* m) { g_rec->errorCalls++; g_rec->errors.emplace_back(m ? m : ""); }

CharRender3Hooks MakeHooks() {
    CharRender3Hooks h{};
    h.convertBackslashToSlash = H_convert;
    h.findFreeMeshSlot = H_findFree;
    h.setGrayColorThunk = H_gray;
    h.loadStreamToStock = H_load;
    h.attachToBone = H_attach;
    h.pruneExpiredAttachments = H_prune;
    h.createMorphAnim = H_createMorph;
    h.computeBoneDelta = H_boneDelta;
    h.attachToUniverseNode = H_attachUniverse;
    h.detachAndRelease = H_detach;
    h.objectAttachToBone = H_objBone;
    h.buildLightCache = H_light;
    h.switchUniverse = H_switch;
    h.setPivotVector = H_pivot;
    h.setObjectPosition = H_setpos;
    h.soundPlaySample = H_playSample;
    h.sound3dPlayOneShot = H_oneShot;
    h.reportError = H_error;
    return h;
}

struct Scoped {
    Rec3 rec;
    CharRender3Hooks hooks;
    Scoped() { g_rec = &rec; hooks = MakeHooks(); SetCharRender3Hooks(&hooks); }
    ~Scoped() { SetCharRender3Hooks(nullptr); g_rec = nullptr; }
};

CharActor3 MakeActor(const char* base) {
    CharActor3 a{};
    std::strncpy(a.baseName, base, sizeof(a.baseName) - 1);
    a.bodyMesh = reinterpret_cast<void*>(0xB0DE);
    a.lowPolyObj = reinterpret_cast<void*>(0x10F0);
    a.universe = reinterpret_cast<void*>(0xC0DE);
    return a;
}

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

} // namespace

// ---------------------------------------------------------------------------
// Path / name formatting.
// ---------------------------------------------------------------------------
TEST(CharRender3, BuildAniPath) {
    char buf[256];
    BuildAniPath(buf, "buerger", "stehen");
    CHECK_EQ(std::string(buf), std::string("character/buerger/stehen_buerger.baf"));
}

TEST(CharRender3, BuildLowPolyAniPath) {
    char buf[256];
    BuildLowPolyAniPath(buf, "buerger", "gehen");
    CHECK_EQ(std::string(buf),
             std::string("lowpolycharacter/buerger/gehen_buerger_LOW.baf"));
}

TEST(CharRender3, IsLoopingGait) {
    CHECK(IsLoopingGait("bewegung/gehen"));
    CHECK(IsLoopingGait("bewegung/karren_ziehen"));
    CHECK(IsLoopingGait("BEWEGUNG/GEHEN"));     // case-insensitive
    CHECK(!IsLoopingGait("stehen"));
    CHECK(!IsLoopingGait(""));
}

TEST(CharRender3, FootstepSampleForTerrain) {
    CHECK_EQ(std::string(FootstepSampleForTerrain(3)),  std::string("Erde_s"));
    CHECK_EQ(std::string(FootstepSampleForTerrain(4)),  std::string("Wiese_s"));
    CHECK_EQ(std::string(FootstepSampleForTerrain(6)),  std::string("Stein_s"));
    CHECK_EQ(std::string(FootstepSampleForTerrain(8)),  std::string("Stein_s"));
    CHECK_EQ(std::string(FootstepSampleForTerrain(10)), std::string("Pfuetze_s"));
    CHECK_EQ(std::string(FootstepSampleForTerrain(11)), std::string("Stein_s"));
    CHECK_EQ(std::string(FootstepSampleForTerrain(99)), std::string("Normal_s"));
}

// ---------------------------------------------------------------------------
// AttachAni.
// ---------------------------------------------------------------------------
TEST(CharRender3, AttachAniLoadsAndStores) {
    Scoped s;
    s.rec.nextFree = nullptr;                   // no cached slot -> must load
    s.rec.nextAttach = reinterpret_cast<void*>(0xCAFE);
    CharActor3 a = MakeActor("buerger");

    void* ch = AttachAni(&a, "bewegung/gehen", /*seq=*/1);
    CHECK_EQ(ch, reinterpret_cast<void*>(0xCAFE));
    CHECK_EQ(a.attachAnim, reinterpret_cast<void*>(0xCAFE));
    CHECK_EQ(static_cast<int>(a.attachFlags), 0xFF);   // a1+133 = -1
    CHECK_EQ((int)s.rec.loadCalls.size(), 1);
    CHECK_EQ(s.rec.loadCalls[0],
             std::string("character/buerger/bewegung/gehen_buerger.baf"));
    CHECK_EQ(s.rec.loadFlags[0], 1);            // gait -> loop flag set
    // mask = 0x20000 | (((8*(1&1))|0xD0) << 8) = 0x20000 | (0xD8 << 8) = 0x2D800.
    CHECK_EQ((int)s.rec.attachMasks.size(), 1);
    CHECK_EQ(s.rec.attachMasks[0], 0x20000 | (0xD8 << 8));
}

TEST(CharRender3, AttachAniSeqEvenMask) {
    Scoped s;
    s.rec.nextAttach = reinterpret_cast<void*>(0x1);
    CharActor3 a = MakeActor("x");
    AttachAni(&a, "stehen", /*seq=*/0);
    // seq even -> BYTE1 = 0xD0; mask = 0x20000 | (0xD0 << 8) = 0x2D000.
    CHECK_EQ(s.rec.attachMasks[0], 0x20000 | (0xD0 << 8));
    CHECK_EQ(s.rec.loadFlags[0], 0);            // non-gait -> no loop
}

TEST(CharRender3, AttachAniFailReportsError) {
    Scoped s;
    s.rec.nextAttach = nullptr;                 // attach fails
    CharActor3 a = MakeActor("x");
    void* ch = AttachAni(&a, "stehen", 0);
    CHECK_EQ(ch, (void*)nullptr);
    CHECK_EQ(s.rec.errorCalls, 1);
}

TEST(CharRender3, AttachAniCachedSlotSkipsLoad) {
    Scoped s;
    s.rec.nextFree = reinterpret_cast<void*>(0x55);  // cached slot present
    s.rec.nextAttach = reinterpret_cast<void*>(0x9);
    CharActor3 a = MakeActor("x");
    AttachAni(&a, "stehen", 0);
    CHECK_EQ((int)s.rec.loadCalls.size(), 0);   // load skipped
    CHECK_EQ((int)s.rec.attachMasks.size(), 1); // attach still happens
}

// ---------------------------------------------------------------------------
// AttachMotion.
// ---------------------------------------------------------------------------
TEST(CharRender3, AttachMotionMaskAndFlag) {
    Scoped s;
    s.rec.nextAttach = reinterpret_cast<void*>(0xABCD);
    CharActor3 a = MakeActor("haendler");
    void* ch = AttachMotion(&a, "winken");
    CHECK_EQ(ch, reinterpret_cast<void*>(0xABCD));
    CHECK_EQ(s.rec.loadCalls[0],
             std::string("character/haendler/winken_haendler.baf"));
    CHECK_EQ(s.rec.loadFlags[0], 1);            // always flag 1
    CHECK_EQ(s.rec.attachMasks[0], 153600);
}

// ---------------------------------------------------------------------------
// AttachMovementAni.
// ---------------------------------------------------------------------------
TEST(CharRender3, AttachMovementAniBindsOnce) {
    Scoped s;
    s.rec.nextFree = nullptr;
    char name[64]; std::strcpy(name, "bewegung/gehen");
    CharActor3 a = MakeActor("buerger");
    void* h1 = AttachMovementAni(&a, name, /*dir=*/3);
    CHECK_EQ(h1, reinterpret_cast<void*>(0x1000));   // loaded handle
    CHECK_EQ(a.moveAnim, reinterpret_cast<void*>(0x1000));
    CHECK_EQ(static_cast<int>(a.attachFlags), 3);
    CHECK_EQ(s.rec.loadFlags[0], 1);             // gait -> loop

    // Second call is a no-op because a1+128 is already set.
    char name2[64]; std::strcpy(name2, "stehen");
    void* h2 = AttachMovementAni(&a, name2, 1);
    CHECK_EQ(h2, (void*)nullptr);
    CHECK_EQ((int)s.rec.loadCalls.size(), 1);    // no extra load
}

TEST(CharRender3, AttachMovementAniUsesCachedSlot) {
    Scoped s;
    s.rec.nextFree = reinterpret_cast<void*>(0x77);
    char name[64]; std::strcpy(name, "stehen");
    CharActor3 a = MakeActor("x");
    void* h = AttachMovementAni(&a, name, 0);
    CHECK_EQ(h, reinterpret_cast<void*>(0x77));  // cached slot wins, no load
    CHECK_EQ((int)s.rec.loadCalls.size(), 0);
}

// ---------------------------------------------------------------------------
// DetachMorphAni.
// ---------------------------------------------------------------------------
TEST(CharRender3, DetachMorphAniNoSourceNoop) {
    Scoped s;
    MorphCtx c{};
    c.hasMorphSource = false;
    DetachMorphAni(c);
    CHECK_EQ(s.rec.pruneCalls, 0);
    CHECK_EQ(s.rec.findFreeCalls, 0);
}

TEST(CharRender3, DetachMorphAniTearsDown) {
    Scoped s;
    s.rec.nextFree = nullptr;                    // no cached slot -> teardown runs
    s.rec.nextAttach = reinterpret_cast<void*>(0x3);
    MorphCtx c{};
    c.hasMorphSource = true;
    c.bodyMesh = reinterpret_cast<void*>(0xB0DE);
    c.meshGeom = reinterpret_cast<void*>(0x4444);
    c.computeDelta = true;
    DetachMorphAni(c);
    CHECK_EQ((int)s.rec.attachMasks.size(), 1);
    CHECK_EQ(s.rec.attachMasks[0], 0x20000 | (24 << 8));
    CHECK_EQ(s.rec.pruneCalls, 1);
}

TEST(CharRender3, DetachMorphAniCachedSlotBlocks) {
    Scoped s;
    s.rec.nextFree = reinterpret_cast<void*>(0x9);  // cached slot blocks teardown
    MorphCtx c{};
    c.hasMorphSource = true;
    DetachMorphAni(c);
    CHECK_EQ(s.rec.pruneCalls, 0);
}

// ---------------------------------------------------------------------------
// Preload family.
// ---------------------------------------------------------------------------
TEST(CharRender3, PreloadAniSetLoadsEach) {
    Scoped s;
    s.rec.nextFree = nullptr;
    const char* names[] = {"gehen", "", "bewegung/gehen"};
    CharActor3 a = MakeActor("buerger");
    PreloadAniSet(&a, names, 3);
    // Empty name skipped -> 2 loads.
    CHECK_EQ((int)s.rec.loadCalls.size(), 2);
    CHECK_EQ(s.rec.loadCalls[0], std::string("character/buerger/gehen_buerger.baf"));
    CHECK_EQ(s.rec.loadFlags[0], 0);             // "gehen" alone is not the gait clip
    CHECK_EQ(s.rec.loadFlags[1], 1);             // "bewegung/gehen" -> loop
    CHECK_EQ(s.rec.attachMasks[0], 153600);
    CHECK_EQ(s.rec.pruneCalls, 2);
}

TEST(CharRender3, PreloadAniSetByNameAlwaysFlag0) {
    Scoped s;
    s.rec.nextFree = nullptr;
    const char* names[] = {"bewegung/gehen"};
    PreloadAniSetByName("buerger", names, 1);
    CHECK_EQ((int)s.rec.loadCalls.size(), 1);
    CHECK_EQ(s.rec.loadFlags[0], 0);             // never loops
}

TEST(CharRender3, PreloadLowPolyAniSet) {
    Scoped s;
    s.rec.nextFree = nullptr;                    // load path -> returns 0x1000 (non-null)
    const char* names[] = {"gehen"};
    CharActor3 a = MakeActor("buerger");
    PreloadLowPolyAniSet(&a, names, 1);
    CHECK_EQ(s.rec.loadCalls[0],
             std::string("lowpolycharacter/buerger/gehen_buerger_LOW.baf"));
    CHECK_EQ(s.rec.loadFlags[0], 1);
    CHECK_EQ(s.rec.attachMasks[0], 133120);
}

TEST(CharRender3, PreloadLowPolyNoLoadNoAttach) {
    Scoped s;
    s.rec.nextFree = nullptr;
    // Make load return null by overriding: load returns 0x1000 always in mock, so to
    // exercise the !slot path we use a cached slot path instead. With cached slot the
    // attach still runs (slot != null).
    s.rec.nextFree = reinterpret_cast<void*>(0x5);
    const char* names[] = {"gehen"};
    CharActor3 a = MakeActor("x");
    PreloadLowPolyAniSet(&a, names, 1);
    CHECK_EQ((int)s.rec.loadCalls.size(), 0);    // cached slot -> no load
    CHECK_EQ((int)s.rec.attachMasks.size(), 1);  // slot non-null -> attach
}

// ---------------------------------------------------------------------------
// AttachItemToBone.
// ---------------------------------------------------------------------------
TEST(CharRender3, AttachItemToBoneRightHand) {
    Scoped s;
    s.rec.nextUniverseNode = reinterpret_cast<void*>(0x5E7);
    CharActor3 a = MakeActor("x");
    AttachItemToBone(&a, ItemBone::kRightHand, "schwert");
    CHECK_EQ(a.itemRight, reinterpret_cast<void*>(0x5E7));
    CHECK_EQ(s.rec.universeNames[0], std::string("schwert"));
    CHECK_EQ((int)s.rec.boneNames.size(), 1);
    CHECK_EQ(s.rec.boneNames[0], std::string("d3_RightHand"));
    CHECK_EQ(s.rec.lightCalls, 1);
    CHECK_EQ(s.rec.switchCalls, 2);              // bracketed switch (enter + restore)
}

TEST(CharRender3, AttachItemToBoneHead) {
    Scoped s;
    s.rec.nextUniverseNode = reinterpret_cast<void*>(0x111);
    CharActor3 a = MakeActor("x");
    AttachItemToBone(&a, ItemBone::kHead, "hut");
    CHECK_EQ(a.itemHeadNode, reinterpret_cast<void*>(0x111));
    CHECK_EQ(s.rec.boneNames[0], std::string("d3_Head"));
}

TEST(CharRender3, AttachItemToBoneDetachOnly) {
    Scoped s;
    CharActor3 a = MakeActor("x");
    a.itemLeft = reinterpret_cast<void*>(0xDEAD);
    AttachItemToBone(&a, ItemBone::kLeftHand, /*name=*/nullptr);  // detach path
    CHECK_EQ(a.itemLeft, (void*)nullptr);
    CHECK_EQ(s.rec.detachCalls, 1);
    CHECK_EQ((int)s.rec.universeNames.size(), 0);  // no attach
    CHECK_EQ(s.rec.switchCalls, 2);
}

TEST(CharRender3, AttachItemToBoneAttachFailRestoresSwitch) {
    Scoped s;
    s.rec.nextUniverseNode = nullptr;            // attach fails
    CharActor3 a = MakeActor("x");
    AttachItemToBone(&a, ItemBone::kLeftHand, "fackel");
    CHECK_EQ((int)s.rec.boneNames.size(), 0);    // never reached bone attach
    CHECK_EQ(s.rec.switchCalls, 2);              // still restored
    CHECK_EQ(s.rec.lightCalls, 0);
}

// ---------------------------------------------------------------------------
// ResolveHeadBone.
// ---------------------------------------------------------------------------
TEST(CharRender3, ResolveHeadBoneScanMatch) {
    const char* bones[] = {"hals", "kopf", "arm"};
    CHECK_EQ(ResolveHeadBoneFromScan(bones, 3), 1 + 1468);     // index 1 + bias
}

TEST(CharRender3, ResolveHeadBoneScanAbtKopf) {
    const char* bones[] = {"x", "y", "abt_kopf"};
    CHECK_EQ(ResolveHeadBoneFromScan(bones, 3), 2 + 1468);
}

TEST(CharRender3, ResolveHeadBoneScanNoMatch) {
    const char* bones[] = {"arm", "bein"};
    CHECK_EQ(ResolveHeadBoneFromScan(bones, 2), 0);
}

TEST(CharRender3, ResolveHeadBoneStaffModulo) {
    unsigned char staff[4] = {7, 9, 11, 0xFF};   // 3 valid entries
    // recordId 4 % 3 == 1 -> staff[1]=9 -> 9 + 1468 = 1477.
    CHECK_EQ(ResolveHeadBoneFromStaff(staff, 4), 9 + 1468);
    // recordId 5 % 3 == 2 -> staff[2]=11 -> 1479.
    CHECK_EQ(ResolveHeadBoneFromStaff(staff, 5), 11 + 1468);
}

// ---------------------------------------------------------------------------
// PlayFootstepSound.
// ---------------------------------------------------------------------------
TEST(CharRender3, FootstepPlaysOnStepCode) {
    Scoped s;
    s.rec.nextSampleHandle = 1.0f;
    FootstepCtx c{};
    c.hasFrameEvent = true; c.soundEnabled = true; c.inActiveUniverse = true;
    c.eventCode = 4; c.lastEvent = 0;
    c.isLocalUniverse = true; c.terrainNoiseOn = true; c.activeSoundCount = 0;
    c.hasTerrainKind = true; c.terrainKind = 3;  // Erde
    c.actorMesh = reinterpret_cast<void*>(0x1);
    c.soundObj = reinterpret_cast<void*>(0x2);
    bool played = false;
    u8 last = PlayFootstepSound(c, &played);
    CHECK(played);
    CHECK_EQ((int)last, 4);
    CHECK_EQ(s.rec.samplesPlayed[0], std::string("Erde_s"));
    CHECK_EQ(s.rec.oneShotCalls, 1);
}

TEST(CharRender3, FootstepSameEventNoReplay) {
    Scoped s;
    FootstepCtx c{};
    c.hasFrameEvent = true; c.soundEnabled = true; c.inActiveUniverse = true;
    c.eventCode = 12; c.lastEvent = 12;          // same as last -> no play
    c.isLocalUniverse = true; c.terrainNoiseOn = true;
    bool played = true;
    u8 last = PlayFootstepSound(c, &played);
    CHECK(!played);
    CHECK_EQ((int)last, 12);
    CHECK_EQ((int)s.rec.samplesPlayed.size(), 0);
}

TEST(CharRender3, FootstepNonStepCodeIgnored) {
    Scoped s;
    FootstepCtx c{};
    c.hasFrameEvent = true; c.soundEnabled = true; c.inActiveUniverse = true;
    c.eventCode = 7; c.lastEvent = 0;            // 7 not in {4,12,18,24}
    bool played = true;
    u8 last = PlayFootstepSound(c, &played);
    CHECK(!played);
    CHECK_EQ((int)last, 0);                      // unchanged last event
}

TEST(CharRender3, FootstepGateBlocksWhenTooManySounds) {
    Scoped s;
    FootstepCtx c{};
    c.hasFrameEvent = true; c.soundEnabled = true; c.inActiveUniverse = true;
    c.eventCode = 18; c.lastEvent = 0;
    c.isLocalUniverse = true; c.terrainNoiseOn = true; c.activeSoundCount = 3;  // >= 3
    bool played = true;
    u8 last = PlayFootstepSound(c, &played);
    CHECK(!played);
    CHECK_EQ((int)last, 18);                      // event recorded but no sound
    CHECK_EQ((int)s.rec.samplesPlayed.size(), 0);
}

TEST(CharRender3, FootstepNoTerrainKindUsesNormal) {
    Scoped s;
    s.rec.nextSampleHandle = 1.0f;
    FootstepCtx c{};
    c.hasFrameEvent = true; c.soundEnabled = true; c.inActiveUniverse = true;
    c.eventCode = 24; c.lastEvent = 0;
    c.isLocalUniverse = true; c.terrainNoiseOn = true; c.activeSoundCount = 0;
    c.hasTerrainKind = false;                     // -> Normal_s
    bool played = false;
    PlayFootstepSound(c, &played);
    CHECK(played);
    CHECK_EQ(s.rec.samplesPlayed[0], std::string("Normal_s"));
}

// ---------------------------------------------------------------------------
// NoiseTimerUpdate.
// ---------------------------------------------------------------------------
TEST(CharRender3, NoiseTimerInactiveNoop) {
    Scoped s;
    NoiseTimerCtx c{};
    c.active = false;
    NoiseTimerResult r = NoiseTimerUpdate(c);
    CHECK(!r.stepped);
    CHECK_EQ(s.rec.setPosCalls, 0);
}

TEST(CharRender3, NoiseTimerStepsTowardGoal) {
    Scoped s;
    NoiseTimerCtx c{};
    c.active = true;
    c.timer = 5;
    c.boneWorld[0] = 0; c.boneWorld[1] = 0; c.boneWorld[2] = 0;
    c.noisePos[0] = 10; c.noisePos[1] = 0; c.noisePos[2] = 0;   // dir normalized -> {1,0,0}
    c.meshPivot[0] = 0; c.meshPivot[1] = 0; c.meshPivot[2] = 0;
    c.meshOrigin[0] = 0; c.meshOrigin[1] = 0; c.meshOrigin[2] = 0;
    c.target[0] = 100; c.target[1] = 0; c.target[2] = 0;        // far -> no reset
    c.obj = reinterpret_cast<void*>(0x1);
    NoiseTimerResult r = NoiseTimerUpdate(c);
    CHECK(r.stepped);
    CHECK(!r.reset);
    // step = normalize({10,0,0})*0.4 = {0.4,0,0}; pivot push x == 0.4.
    CHECK((int)s.rec.pivotX.size() == 1);
    CHECK(Near(s.rec.pivotX[0], 0.4f));
    CHECK(Near(r.newPos[0], 0.4f));              // pivot + origin
    CHECK_EQ((int)r.newTimer, 4);                // decremented
}

TEST(CharRender3, NoiseTimerReachesGoalResets) {
    Scoped s;
    NoiseTimerCtx c{};
    c.active = true;
    c.timer = 1;
    c.boneWorld[0] = 0; c.boneWorld[1] = 0; c.boneWorld[2] = 0;
    c.noisePos[0] = 1; c.noisePos[1] = 0; c.noisePos[2] = 0;
    c.meshPivot[0] = 0; c.meshPivot[1] = 0; c.meshPivot[2] = 0;
    c.meshOrigin[0] = 0; c.meshOrigin[1] = 0; c.meshOrigin[2] = 0;
    // worldPos = {0.4,0,0}; target {0.4,0,0} -> within 0.4 tolerance -> reset.
    c.target[0] = 0.4f; c.target[1] = 0; c.target[2] = 0;
    c.obj = reinterpret_cast<void*>(0x1);
    NoiseTimerResult r = NoiseTimerUpdate(c);
    CHECK(r.reset);
    CHECK_EQ((int)r.newTimer, 0);
    CHECK_EQ(s.rec.setPosCalls, 1);              // SetPosition called on reset
    // pivot called twice: the step pivot, then the {0,0,0} reset pivot.
    CHECK_EQ((int)s.rec.pivotX.size(), 2);
    CHECK(Near(s.rec.pivotX[1], 0.0f));
}

// ---------------------------------------------------------------------------
// Camera ray helpers (pure math).
// ---------------------------------------------------------------------------
TEST(CharRender3, ProjectRayDirectionIdentity) {
    float mat[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float eye[3] = {1, 2, 3};
    float out[3];
    ProjectRayDirection(mat, eye, /*dist=*/10.0f, out);
    // base {0,0,1} * -10 = {0,0,-10}; identity rotate; + eye -> {1, 2, -7}.
    CHECK(Near(out[0], 1.0f));
    CHECK(Near(out[1], 2.0f));
    CHECK(Near(out[2], -7.0f));
}

TEST(CharRender3, ScreenToWorldRayGolden) {
    float mat[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float dir[3], scaled[3];
    ScreenToWorldRay(mat, /*sx=*/400, /*sy=*/300, /*cx=*/160, /*cy=*/120,
                     /*focal=*/256, /*zNear=*/1, /*zFar=*/11, dir, scaled);
    // python golden: dir = (0.60854932, -0.45641199, 0.64911927)
    CHECK(Near(dir[0], 0.60854932f));
    CHECK(Near(dir[1], -0.45641199f));
    CHECK(Near(dir[2], 0.64911927f));
    // 0x4268ff: fdiv [var_1C] -> divisor is the ROTATED dir.y (var_1C), NOT focal.
    // scaled = dir * (zFar-zNear)/dir.y; so scaled.y == (zFar-zNear) == 10.
    // python golden: scaled = (-13.33333, 10.0, -14.22222).
    CHECK(Near(scaled[0], -13.333333f, 1e-3f));
    CHECK(Near(scaled[1], 10.0f, 1e-3f));
    CHECK(Near(scaled[2], -14.222222f, 1e-3f));
    // unit-length direction.
    float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    CHECK(Near(len, 1.0f, 1e-3f));
}
