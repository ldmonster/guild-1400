// ===========================================================================
// object_lifecycle6_e2e_test.cpp — end-to-end flow across the VIBE_Object_*
// script-command + validation batch: a script "spawn -> find -> move/rotate ->
// validate -> kill" sequence wired through one cohesive set of hooks.
// ===========================================================================
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "test.h"
#include "sim/character_query.h"
#include "sim/object_lifecycle6.h"

using namespace guild::sim;

namespace {

// A tiny in-process "scene" the hooks drive: a handle table mapping handle ids
// to SceneNode6, a name->handle index, a visited-node list for walks, and an
// alive set so KillObject can prune it.
struct MiniScene {
    std::map<int, SceneNode6*> byHandle;
    std::map<std::string, int> byName;
    std::vector<int>           liveHandles;     // walk visits these
    std::vector<void*>         validationFailures;
    int                        nextHandle = 0x1000;
    int                        randValue = 0;
};
MiniScene g_scene;

// FindByHandle: the command handlers pass the name string in the name slot.
int SceneFind(int /*root*/, int /*flags*/, const char* name, int /*z*/,
              int /*extra*/) {
    auto it = g_scene.byName.find(name ? name : "");
    return it == g_scene.byName.end() ? 0 : it->second;
}

// WalkAndInvoke: feed every live handle to the collector, all "matched".
char SceneWalk(void* /*root*/, int /*a2*/, bool (*cb)(int, int), int /*flags*/,
               int /*ctx*/) {
    char last = 1;
    for (int h : g_scene.liveHandles) last = cb(h, 1) ? 1 : 0;
    return last;
}

int  SceneRand() { return g_scene.randValue; }
void SceneError(const char* /*msg*/) {}
void SceneFail(void* p) { g_scene.validationFailures.push_back(p); }
int  SceneInvalid(void*) { return 0; }   // for the failure-injection case

// orientation sink: write the new transform back into the node so a later
// read sees the applied move/rotate.
void SceneOrient(SceneNode6* n, int /*arg*/, float px, float py, float pz,
                 float ax, float ay, float az, int /*arg9*/) {
    n->f(76) = px;  n->f(80) = py;  n->f(84) = pz;
    n->f(132) = ax; n->f(136) = ay; n->f(140) = az;
}

int Spawn(const char* name, SceneNode6* node) {
    int h = g_scene.nextHandle++;
    g_scene.byHandle[h] = node;
    g_scene.byName[name] = h;
    g_scene.liveHandles.push_back(h);
    return h;
}

ObjLife6Hooks SceneHooks() {
    ObjLife6Hooks h;
    h.objFindByHandle        = SceneFind;
    h.sceneGraphWalkAndInvoke = SceneWalk;
    h.randNext               = SceneRand;
    h.reportError            = SceneError;
    h.validationFailure      = SceneFail;
    h.sound3dSetOrientation  = SceneOrient;
    return h;
}

}  // namespace

// ===========================================================================
// Full flow: spawn two named nodes -> get a handle by name -> move it relative
// -> rotate it relative -> find a random match -> validate the live-actor
// record -> re-find by name. Exercises CmdGetObjectHandle, CmdMoveObjectRelative,
// CmdRotateObjectRelative, CmdFindRandomByName and ValidatePointers across one
// shared scene wired through the hooks.
// ===========================================================================
TEST(ObjLife6E2E, SpawnFindMoveRotateValidate) {
    g_scene = MiniScene();
    ObjLife6SetHooks(SceneHooks());

    SceneNode6 crate, barrel;
    crate.f(76) = 0.0f; crate.f(80) = 0.0f; crate.f(84) = 0.0f;
    crate.f(132) = 0.0f; crate.f(136) = 0.0f; crate.f(140) = 0.0f;
    barrel.f(76) = 50.0f;

    int hCrate  = Spawn("crate", &crate);
    int hBarrel = Spawn("barrel", &barrel);
    CHECK(hCrate != 0);
    CHECK(hBarrel != 0);

    // --- find by name ---
    int found = ObjectCmdGetObjectHandle("crate");
    CHECK_EQ(found, hCrate);
    int miss = ObjectCmdGetObjectHandle("statue");   // not present
    CHECK_EQ(miss, 0);

    // --- move the crate relative (+10,+20,+30) ---
    ObjectCmdMoveObjectRelative(&crate, 10, 20, 30, /*arg*/0);
    CHECK_EQ(crate.f(76), 10.0f);
    CHECK_EQ(crate.f(80), 20.0f);
    CHECK_EQ(crate.f(84), 30.0f);

    // --- rotate the crate relative by 90deg about its first axis ---
    ObjectCmdRotateObjectRelative(&crate, 90, 0, 0, /*arg*/0);
    CHECK(std::fabs(crate.f(132) - (kObj6Pi * kObj6DegPerRadInv * 90.0f)) < 1e-6f);
    // pos preserved through the rotate
    CHECK_EQ(crate.f(76), 10.0f);

    // --- find a random match (rand 1 -> slots[1 % 2] == barrel) ---
    g_scene.randValue = 1;
    int rnd = ObjectCmdFindRandomByName("any");
    CHECK_EQ(rnd, hBarrel);

    // --- validate: all pointers null/valid => no failures ---
    ValidationRecord recCrate;   // all owned pointers null
    recCrate.selfValid = true;
    ObjectValidatePointers(&recCrate);
    CHECK_EQ((int)g_scene.validationFailures.size(), 0);

    // --- re-find the crate by name after its transform changed (handle stable) ---
    CHECK_EQ(ObjectCmdGetObjectHandle("crate"), hCrate);

    // --- a sub-object lookup rooted at the crate handle finds the barrel ---
    CHECK_EQ(ObjectCmdGetSubObjectHandle(hCrate, "barrel"), hBarrel);

    // --- pick the first match deterministically (rand 0 -> slots[0] == crate) ---
    g_scene.randValue = 0;
    CHECK_EQ(ObjectCmdFindRandomByName("any"), hCrate);

    ObjLife6ResetHooks();
}

// ===========================================================================
// Validation sweep over a small fleet with injected invalid pointers, then the
// scene-graph walk runs (RunValidationPass), exercising the same hook wiring.
// ===========================================================================
TEST(ObjLife6E2E, ValidationSweepCountsFailures) {
    g_scene = MiniScene();
    ObjLife6Hooks h = SceneHooks();
    h.memoryIsValidPointer = SceneInvalid;   // every owned pointer "invalid"
    ObjLife6SetHooks(h);

    int dummy = 0;
    ValidationRecord a0, a1, b0;
    a0.mesh13 = &dummy;            // 1 failure
    a1.ptr30  = &dummy;            // 1 failure
    a1.ptr32  = &dummy;            // 1 failure
    b0.selfValid = false;          // 1 failure (self)
    ValidationRecord* actors[2] = {&a0, &a1};
    bool alive[1] = {true};
    ValidationRecord* bldg[1] = {&b0};

    char walkResult = ObjectValidateAllPointers(actors, 2, alive, bldg, 1);
    CHECK_EQ(walkResult, (char)1);          // SceneWalk returns 1
    CHECK_EQ((int)g_scene.validationFailures.size(), 4);

    // standalone passes also walk the graph
    CHECK_EQ(ObjectRunValidationPass(), (char)1);
    CHECK_EQ(ObjectRunValidationPassDup(), (char)1);

    ObjLife6ResetHooks();
}
