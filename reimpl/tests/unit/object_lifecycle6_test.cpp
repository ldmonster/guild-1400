// ===========================================================================
// object_lifecycle6_test.cpp — golden-vector unit tests for the VIBE_Object_*
// script-command + pointer-validation batch (object_lifecycle6.{h,cpp}).
// ===========================================================================
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "test.h"
#include "sim/character_query.h"      // g_activeUniverse
#include "sim/object_lifecycle6.h"

using namespace guild::sim;

namespace {

// deg->rad oracle matching the float32 evaluation order in the .cpp.
float DegToRad(int deg) {
    return static_cast<float>(deg) * kObj6Pi * kObj6DegPerRadInv;
}

// --- shared captor state ---------------------------------------------------
struct Captor {
    // orientation captures
    SceneNode6* node = nullptr;
    int   arg = 0;
    float px = 0, py = 0, pz = 0, ax = 0, ay = 0, az = 0;
    int   orientCalls = 0;
    // error captures
    std::string lastError;
    int   errorCalls = 0;
    // validation failures
    std::vector<void*> failures;
    // walk
    int   walkCalls = 0;
    // gray thunk
    int   grayCalls = 0;
};
Captor g_cap;

void OnOrient(SceneNode6* n, int arg, float px, float py, float pz,
              float ax, float ay, float az, int /*arg9*/) {
    g_cap.node = n; g_cap.arg = arg;
    g_cap.px = px; g_cap.py = py; g_cap.pz = pz;
    g_cap.ax = ax; g_cap.ay = ay; g_cap.az = az;
    g_cap.orientCalls++;
}
void OnError(const char* msg) { g_cap.lastError = msg ? msg : ""; g_cap.errorCalls++; }
void OnFail(void* p) { g_cap.failures.push_back(p); }
void OnGray(int, int) { g_cap.grayCalls++; }

ObjLife6Hooks MakeHooks() {
    ObjLife6Hooks h;
    h.sound3dSetOrientation = OnOrient;
    h.reportError           = OnError;
    h.validationFailure     = OnFail;
    h.lightSetGrayColorThunk = OnGray;
    return h;
}

void ResetCap() { g_cap = Captor(); }

}  // namespace

// ===========================================================================
// CmdRotateObject — absolute angle (deg->rad), pos kept, returns node.
// ===========================================================================
TEST(ObjLife6, RotateObjectAbsolute) {
    ResetCap();
    ObjLife6SetHooks(MakeHooks());
    SceneNode6 n;
    n.f(76) = 10.0f; n.f(80) = 20.0f; n.f(84) = 30.0f;  // world pos

    SceneNode6* r = ObjectCmdRotateObject(&n, /*x*/90, /*z*/45, /*y*/180, /*arg*/7);
    CHECK_EQ(r, &n);
    CHECK_EQ(g_cap.orientCalls, 1);
    CHECK_EQ(g_cap.arg, 7);
    // position triple = current world pos
    CHECK_EQ(g_cap.px, 10.0f);
    CHECK_EQ(g_cap.py, 20.0f);
    CHECK_EQ(g_cap.pz, 30.0f);
    // orientation triple = deg->rad (ax from x, ay from y, az from z)
    CHECK(std::fabs(g_cap.ax - DegToRad(90)) < 1e-6f);
    CHECK(std::fabs(g_cap.ay - DegToRad(180)) < 1e-6f);
    CHECK(std::fabs(g_cap.az - DegToRad(45)) < 1e-6f);
    CHECK(std::fabs(g_cap.ax - 1.5707963705f) < 1e-6f);  // golden
    ObjLife6ResetHooks();
}

TEST(ObjLife6, RotateObjectNullReportsError) {
    ResetCap();
    ObjLife6SetHooks(MakeHooks());
    SceneNode6* r = ObjectCmdRotateObject(nullptr, 1, 2, 3, 0);
    CHECK_EQ(r, (SceneNode6*)nullptr);
    CHECK_EQ(g_cap.orientCalls, 0);
    CHECK_EQ(g_cap.errorCalls, 1);
    CHECK(g_cap.lastError.find("RotateObject") != std::string::npos);
    ObjLife6ResetHooks();
}

// ===========================================================================
// CmdMoveObjectRelative — pos += delta, angle kept, returns 0.
// ===========================================================================
TEST(ObjLife6, MoveObjectRelative) {
    ResetCap();
    ObjLife6SetHooks(MakeHooks());
    SceneNode6 n;
    n.f(76) = 100.0f; n.f(80) = 200.0f; n.f(84) = 300.0f;
    n.f(132) = 0.5f; n.f(136) = 0.6f; n.f(140) = 0.7f;

    int rv = ObjectCmdMoveObjectRelative(&n, /*dx*/5, /*dy*/-10, /*dz*/3, /*arg*/2);
    CHECK_EQ(rv, 0);
    CHECK_EQ(g_cap.orientCalls, 1);
    CHECK_EQ(g_cap.px, 105.0f);   // 100 + 5
    CHECK_EQ(g_cap.py, 190.0f);   // 200 + (-10)
    CHECK_EQ(g_cap.pz, 303.0f);   // 300 + 3
    CHECK_EQ(g_cap.ax, 0.5f);     // angle unchanged
    CHECK_EQ(g_cap.ay, 0.6f);
    CHECK_EQ(g_cap.az, 0.7f);
    ObjLife6ResetHooks();
}

// ===========================================================================
// CmdRotateObjectRelative — angle += deg->rad delta, pos kept, returns 1.
// ===========================================================================
TEST(ObjLife6, RotateObjectRelative) {
    ResetCap();
    ObjLife6SetHooks(MakeHooks());
    SceneNode6 n;
    n.f(76) = 1.0f; n.f(80) = 2.0f; n.f(84) = 3.0f;
    n.f(132) = 1.0f; n.f(136) = 2.0f; n.f(140) = 3.0f;

    int rv = ObjectCmdRotateObjectRelative(&n, /*x*/90, /*z*/45, /*y*/180, /*arg*/0);
    CHECK_EQ(rv, 1);
    CHECK_EQ(g_cap.orientCalls, 1);
    CHECK_EQ(g_cap.px, 1.0f);     // pos kept
    CHECK_EQ(g_cap.py, 2.0f);
    CHECK_EQ(g_cap.pz, 3.0f);
    CHECK(std::fabs(g_cap.ax - (DegToRad(90) + 1.0f)) < 1e-6f);
    CHECK(std::fabs(g_cap.ay - (DegToRad(180) + 2.0f)) < 1e-6f);
    CHECK(std::fabs(g_cap.az - (DegToRad(45) + 3.0f)) < 1e-6f);
    ObjLife6ResetHooks();
}

// ===========================================================================
// CmdGetObjectHandle / CmdGetSubObjectHandle — FindByHandle pass-through + miss.
// ===========================================================================
namespace {
int g_findReturn = 0;
int g_findRoot = -123, g_findFlags = 0;
std::string g_findName;
int FindHook(int root, int flags, const char* name, int /*z*/, int /*extra*/) {
    g_findRoot = root; g_findFlags = flags; g_findName = name ? name : "";
    return g_findReturn;
}
}

TEST(ObjLife6, GetObjectHandleFound) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.objFindByHandle = FindHook;
    ObjLife6SetHooks(h);
    g_findReturn = 0x4242;
    int r = ObjectCmdGetObjectHandle("door_A");
    CHECK_EQ(r, 0x4242);
    CHECK_EQ(g_findRoot, 0);       // root 0 = active graph
    CHECK_EQ(g_findFlags, 320);
    CHECK_EQ(g_cap.errorCalls, 0);
    ObjLife6ResetHooks();
}

TEST(ObjLife6, GetObjectHandleMissReportsError) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.objFindByHandle = FindHook;
    ObjLife6SetHooks(h);
    g_findReturn = 0;
    int r = ObjectCmdGetObjectHandle("ghost");
    CHECK_EQ(r, 0);
    CHECK_EQ(g_cap.errorCalls, 1);
    CHECK(g_cap.lastError.find("Could not find object 'ghost'") != std::string::npos);
    ObjLife6ResetHooks();
}

TEST(ObjLife6, GetSubObjectHandleRootedAtParent) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.objFindByHandle = FindHook;
    ObjLife6SetHooks(h);
    g_findReturn = 99;
    int r = ObjectCmdGetSubObjectHandle(/*parent*/777, "wheel");
    CHECK_EQ(r, 99);
    CHECK_EQ(g_findRoot, 777);
    CHECK_EQ(g_findFlags, 320);
    ObjLife6ResetHooks();
}

// ===========================================================================
// CollectMatchingHandle — appends on match, stops at 32.
// ===========================================================================
TEST(ObjLife6, CollectMatchingHandleAppends) {
    HandleCollector c;
    CHECK(ObjectCollectMatchingHandle(0x10, &c, true));
    CHECK(ObjectCollectMatchingHandle(0x20, &c, true));
    CHECK(ObjectCollectMatchingHandle(0x30, &c, false));  // not matched
    CHECK_EQ(c.count, 2);
    CHECK_EQ(c.slots[0], 0x10);
    CHECK_EQ(c.slots[1], 0x20);
}

TEST(ObjLife6, CollectMatchingHandleStopsAt32) {
    HandleCollector c;
    bool cont = true;
    for (int i = 0; i < 40; ++i)
        cont = ObjectCollectMatchingHandle(i + 1, &c, true);
    CHECK_EQ(c.count, 40);            // count keeps incrementing (faithful)
    CHECK(!cont);                     // but the walk stops once count >= 32
    // first 32 slots recorded, the rest overflow-guarded
    CHECK_EQ(c.slots[0], 1);
    CHECK_EQ(c.slots[31], 32);
}

// ===========================================================================
// CmdFindRandomByName — walk-driven collect, random pick.
// ===========================================================================
namespace {
// A walk hook that visits a fixed set of node handles, all "matched".
std::vector<int> g_walkNodes;
char WalkHook(void* root, int /*a2*/, bool (*cb)(int, int), int flags, int /*ctx*/) {
    CHECK_EQ(root, (void*)&g_activeUniverse);
    (void)flags;
    char last = 1;
    for (int n : g_walkNodes) last = cb(n, 1) ? 1 : 0;
    return last;
}
int g_rand = 0;
int RandHook() { return g_rand; }
}

TEST(ObjLife6, FindRandomByNamePicksMatch) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.sceneGraphWalkAndInvoke = WalkHook;
    h.randNext = RandHook;
    ObjLife6SetHooks(h);
    g_walkNodes = {0xA, 0xB, 0xC};
    g_rand = 4;                       // 4 % 3 == 1 => slots[1] == 0xB
    int r = ObjectCmdFindRandomByName("box");
    CHECK_EQ(r, 0xB);
    CHECK_EQ(g_cap.grayCalls, 1);     // SetGrayColorThunk side effect
    ObjLife6ResetHooks();
}

TEST(ObjLife6, FindRandomByNameNoMatchReturnsZero) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.sceneGraphWalkAndInvoke = WalkHook;
    h.randNext = RandHook;
    ObjLife6SetHooks(h);
    g_walkNodes.clear();
    CHECK_EQ(ObjectCmdFindRandomByName("nope"), 0);
    ObjLife6ResetHooks();
}

// ===========================================================================
// CmdFindRandomVisibleByName — null/invalid root short-circuits.
// ===========================================================================
namespace {
int InvalidPtrHook(void*) { return 0; }
}

TEST(ObjLife6, FindRandomVisibleNullRoot) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.sceneGraphWalkAndInvoke = WalkHook;
    ObjLife6SetHooks(h);
    CHECK_EQ(ObjectCmdFindRandomVisibleByName(0, "x"), 0);
    CHECK_EQ(g_cap.grayCalls, 0);     // never reached the body
    ObjLife6ResetHooks();
}

TEST(ObjLife6, FindRandomVisibleInvalidRoot) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.sceneGraphWalkAndInvoke = WalkHook;
    h.memoryIsValidPointer = InvalidPtrHook;
    ObjLife6SetHooks(h);
    CHECK_EQ(ObjectCmdFindRandomVisibleByName(0x1000, "x"), 0);
    CHECK_EQ(g_cap.grayCalls, 0);
    ObjLife6ResetHooks();
}

TEST(ObjLife6, FindRandomVisibleValidRootCollects) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.sceneGraphWalkAndInvoke = WalkHook;
    h.randNext = RandHook;            // default valid-pointer policy (non-null => valid)
    ObjLife6SetHooks(h);
    g_walkNodes = {0x55, 0x66};
    g_rand = 1;                       // 1 % 2 == 1 => 0x66
    CHECK_EQ(ObjectCmdFindRandomVisibleByName(0x2000, "y"), 0x66);
    ObjLife6ResetHooks();
}

// ===========================================================================
// FindObjectById — first occupied slot whose id matches.
// ===========================================================================
TEST(ObjLife6, FindObjectByIdHit) {
    // The original scans the full 8192-slot table; provide one that size.
    std::vector<ObjIdRecord> table(kObjIdTableSlots);
    table[0].kind = 0; table[0].id = 5;     // unoccupied => skipped even if id matches
    table[3].kind = 1; table[3].id = 42;
    table[5].kind = 2; table[5].id = 5;
    CHECK_EQ(ObjectFindObjectById(table.data(), 42), &table[3]);
    CHECK_EQ(ObjectFindObjectById(table.data(), 5), &table[5]);   // not table[0] (kind 0)
    CHECK_EQ(ObjectFindObjectById(table.data(), 999), (ObjIdRecord*)nullptr);
}

// ===========================================================================
// ValidatePointers — failure callback fires once per invalid owned pointer.
// ===========================================================================
TEST(ObjLife6, ValidatePointersAllValid) {
    ResetCap();
    ObjLife6SetHooks(MakeHooks());     // default policy: non-null => valid
    ValidationRecord rec;
    int dummy = 0;
    rec.mesh13 = &dummy; rec.ptr30 = &dummy; rec.ptr32 = &dummy;
    ObjectValidatePointers(&rec);
    CHECK_EQ((int)g_cap.failures.size(), 0);
    ObjLife6ResetHooks();
}

TEST(ObjLife6, ValidatePointersNullReturnsNull) {
    ResetCap();
    ObjLife6SetHooks(MakeHooks());
    CHECK_EQ(ObjectValidatePointers(nullptr), (void*)nullptr);
    CHECK_EQ((int)g_cap.failures.size(), 0);
    ObjLife6ResetHooks();
}

TEST(ObjLife6, ValidatePointersInvalidFires) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.memoryIsValidPointer = InvalidPtrHook;   // everything invalid
    ObjLife6SetHooks(h);
    ValidationRecord rec;
    rec.selfValid = false;          // self fails
    int dummy = 0;
    rec.mesh13 = &dummy;            // fails
    ValidationSub s28; s28.ptr104 = &dummy; rec.sub28 = &s28;   // fails
    rec.ptr30 = &dummy;             // fails
    rec.ptr32 = &dummy;             // fails
    ObjectValidatePointers(&rec);
    // self + mesh13 + sub28.+104 + ptr30 + ptr32 = 5 failures
    CHECK_EQ((int)g_cap.failures.size(), 5);
    ObjLife6ResetHooks();
}

TEST(ObjLife6, ValidatePointersNullSubsSkipped) {
    ResetCap();
    ObjLife6Hooks h = MakeHooks();
    h.memoryIsValidPointer = InvalidPtrHook;
    ObjLife6SetHooks(h);
    ValidationRecord rec;            // all owned pointers null => nothing checked
    rec.selfValid = true;
    ObjectValidatePointers(&rec);
    CHECK_EQ((int)g_cap.failures.size(), 0);
    ObjLife6ResetHooks();
}

// ===========================================================================
// ValidateAllPointers / RunValidationPass — iteration + scene-graph walk.
// ===========================================================================
namespace {
int g_walkInvoked = 0;
char CountWalk(void*, int, bool (*)(int, int), int, int) { g_walkInvoked++; return 1; }
}

TEST(ObjLife6, ValidateAllPointersIterates) {
    ResetCap();
    g_walkInvoked = 0;
    ObjLife6Hooks h = MakeHooks();
    h.memoryIsValidPointer = InvalidPtrHook;   // force failures
    h.sceneGraphWalkAndInvoke = CountWalk;
    ObjLife6SetHooks(h);

    int dummy = 0;
    ValidationRecord a0, a1, b0;
    a0.mesh13 = &dummy; a1.mesh13 = &dummy; b0.ptr30 = &dummy;
    ValidationRecord* actors[3] = {&a0, nullptr, &a1};   // null slot skipped
    bool alive[2] = {true, false};
    ValidationRecord* bldg[2] = {&b0, nullptr};

    char rv = ObjectValidateAllPointers(actors, 3, alive, bldg, 2);
    CHECK_EQ(rv, (char)1);
    CHECK_EQ(g_walkInvoked, 1);
    // a0(mesh13) + a1(mesh13) + b0(ptr30) = 3 failures (slot1 null, bldg1 dead skipped)
    CHECK_EQ((int)g_cap.failures.size(), 3);
    ObjLife6ResetHooks();
}

TEST(ObjLife6, RunValidationPassWalks) {
    g_walkInvoked = 0;
    ObjLife6Hooks h;
    h.sceneGraphWalkAndInvoke = CountWalk;
    ObjLife6SetHooks(h);
    CHECK_EQ(ObjectRunValidationPass(), (char)1);
    CHECK_EQ(ObjectRunValidationPassDup(), (char)1);
    CHECK_EQ(g_walkInvoked, 2);
    ObjLife6ResetHooks();
}

TEST(ObjLife6, ValidateCallbackStubReturnsOne) {
    CHECK_EQ(ObjectValidateCallbackStub(), (char)1);
}
