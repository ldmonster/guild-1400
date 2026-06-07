// End-to-end flow across the Character render3 cluster: a single actor is dressed and
// animated the way the engine drives it during a tick — preload its anim set, bind the
// looping movement animation, attach a held item to the right hand, then resolve its
// head bone, play a footstep on a terrain step event, and step its noise-target pivot.
// All cross-module calls are captured through one recording CharRender3Hooks mock so the
// whole chain is exercised end to end.
#include "test.h"

#include "sim/character_render3.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace guild::sim;
using guild::u8;

namespace {

struct E2ERec {
    std::vector<std::string> loaded;
    std::vector<int>         loadFlags;
    std::vector<int>         masks;
    std::vector<std::string> universeNames;
    std::vector<std::string> bones;
    std::vector<std::string> samples;
    int prunes = 0;
    int oneShots = 0;
    int switches = 0;
    void* nextFree = nullptr;
    void* nextAttach = reinterpret_cast<void*>(0xA);
    void* nextNode = reinterpret_cast<void*>(0xD00D);
    float pivotX = -999.0f;
    int   pivotCalls = 0;
    int   setPos = 0;
    float sampleHandle = 1.0f;
};
E2ERec* g_e = nullptr;

void E_convert(char*) {}
void* E_findFree() { return g_e->nextFree; }
void E_gray(int, int) {}
void* E_load(const char* p, int f) { g_e->loaded.emplace_back(p); g_e->loadFlags.push_back(f); return reinterpret_cast<void*>(0x1000); }
void* E_attach(void*, int m) { g_e->masks.push_back(m); return g_e->nextAttach; }
void E_prune(void*) { g_e->prunes++; }
void* E_createMorph(void*, const char*) { return reinterpret_cast<void*>(0x2000); }
void E_boneDelta(void*, void*) {}
void* E_attachUniverse(void*, const char* n) { g_e->universeNames.emplace_back(n); return g_e->nextNode; }
void E_detach(void*) {}
void E_objBone(void*, const char* b) { g_e->bones.emplace_back(b); }
void E_light(void*) {}
int  E_switch(int) { g_e->switches++; return 3; }
void E_pivot(void*, const float v[3]) { g_e->pivotX = v[0]; g_e->pivotCalls++; }
void E_setpos(void*, const float[3]) { g_e->setPos++; }
float E_playSample(void*, const char* n) { g_e->samples.emplace_back(n); return g_e->sampleHandle; }
void E_oneShot(float, void*, int, float) { g_e->oneShots++; }
void E_error(const char*) {}

CharRender3Hooks MakeE2EHooks() {
    CharRender3Hooks h{};
    h.convertBackslashToSlash = E_convert;
    h.findFreeMeshSlot = E_findFree;
    h.setGrayColorThunk = E_gray;
    h.loadStreamToStock = E_load;
    h.attachToBone = E_attach;
    h.pruneExpiredAttachments = E_prune;
    h.createMorphAnim = E_createMorph;
    h.computeBoneDelta = E_boneDelta;
    h.attachToUniverseNode = E_attachUniverse;
    h.detachAndRelease = E_detach;
    h.objectAttachToBone = E_objBone;
    h.buildLightCache = E_light;
    h.switchUniverse = E_switch;
    h.setPivotVector = E_pivot;
    h.setObjectPosition = E_setpos;
    h.soundPlaySample = E_playSample;
    h.sound3dPlayOneShot = E_oneShot;
    h.reportError = E_error;
    return h;
}

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

} // namespace

TEST(CharRender3E2E, DressAnimateAndStepActor) {
    E2ERec rec;
    rec.nextNode = reinterpret_cast<void*>(0x5E7);
    g_e = &rec;
    CharRender3Hooks hooks = MakeE2EHooks();
    SetCharRender3Hooks(&hooks);

    CharActor3 a{};
    std::strcpy(a.baseName, "buerger");
    a.bodyMesh = reinterpret_cast<void*>(0xB0DE);
    a.lowPolyObj = reinterpret_cast<void*>(0x10F0);

    // 1) Preload the actor's idle/walk anim set.
    const char* set[] = {"stehen", "bewegung/gehen"};
    PreloadAniSet(&a, set, 2);
    CHECK_EQ((int)rec.loaded.size(), 2);
    CHECK_EQ(rec.loaded[0], std::string("character/buerger/stehen_buerger.baf"));
    CHECK_EQ(rec.loadFlags[1], 1);                 // gait clip loops
    CHECK_EQ(rec.prunes, 2);

    // 2) Bind the looping movement animation (once).
    char move[64]; std::strcpy(move, "bewegung/gehen");
    void* mv = AttachMovementAni(&a, move, /*dir=*/2);
    CHECK_EQ(mv, reinterpret_cast<void*>(0x1000));
    CHECK_EQ(static_cast<int>(a.attachFlags), 2);
    // A second bind attempt is a no-op.
    char move2[64]; std::strcpy(move2, "stehen");
    CHECK_EQ(AttachMovementAni(&a, move2, 1), (void*)nullptr);

    // 3) Attach a held item to the right hand.
    AttachItemToBone(&a, ItemBone::kRightHand, "schwert");
    CHECK_EQ(a.itemRight, reinterpret_cast<void*>(0x5E7));
    CHECK_EQ(rec.universeNames.back(), std::string("schwert"));
    CHECK_EQ(rec.bones.back(), std::string("d3_RightHand"));

    // 4) Resolve the head bone from the scanned bone list.
    const char* headBones[] = {"hals", "kopf"};
    CHECK_EQ(ResolveHeadBoneFromScan(headBones, 2), 1 + 1468);

    // 5) Play a footstep on a step-frame event over stone terrain.
    FootstepCtx fc{};
    fc.hasFrameEvent = true; fc.soundEnabled = true; fc.inActiveUniverse = true;
    fc.eventCode = 12; fc.lastEvent = 0;
    fc.isLocalUniverse = true; fc.terrainNoiseOn = true; fc.activeSoundCount = 1;
    fc.hasTerrainKind = true; fc.terrainKind = 8;  // Stein
    fc.actorMesh = a.bodyMesh; fc.soundObj = a.bodyMesh;
    bool played = false;
    u8 last = PlayFootstepSound(fc, &played);
    CHECK(played);
    CHECK_EQ((int)last, 12);
    CHECK_EQ(rec.samples.back(), std::string("Stein_s"));
    CHECK_EQ(rec.oneShots, 1);

    // 6) Step the actor's noise-target pivot one tick toward a distant goal.
    NoiseTimerCtx nc{};
    nc.active = true; nc.timer = 3;
    nc.noisePos[0] = 5; nc.target[0] = 100;
    nc.obj = a.bodyMesh;
    NoiseTimerResult nr = NoiseTimerUpdate(nc);
    CHECK(nr.stepped);
    CHECK(!nr.reset);
    CHECK(Near(rec.pivotX, 0.4f));                 // normalized step length
    CHECK_EQ((int)nr.newTimer, 2);

    SetCharRender3Hooks(nullptr);
    g_e = nullptr;
}
