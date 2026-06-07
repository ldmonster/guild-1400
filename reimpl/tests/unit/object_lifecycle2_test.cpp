// Unit tests for src/sim/object_lifecycle2.cpp (VIBE_Object_* lifecycle leaves).
// Golden vectors computed with python3 (float32, exact bit math).
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

#include "sim/object_lifecycle2.h"
#include "sim/building.h"
#include "sim/building_production.h"
#include "sim/entity.h"

using namespace guild::sim;

namespace {

// Captured side effects from the mockable hooks.
struct Capture {
    int buildCalls = 0;
    ObjectRec* lastObj = nullptr;
    SceneObject* lastModel = nullptr;
    guild::u8 lastMode = 0;
    int texCalls = 0;
    int lastSet = -999;
    int errCalls = 0;
};
Capture g_cap;

void onBuild(SceneObject* m, ObjectRec* o, guild::u8 mode) {
    g_cap.buildCalls++; g_cap.lastModel = m; g_cap.lastObj = o; g_cap.lastMode = mode;
}
void onTex(SceneObject* /*o*/, int set) { g_cap.texCalls++; g_cap.lastSet = set; }
void onErr(const char* /*msg*/) { g_cap.errCalls++; }

void installHooks() {
    ObjLifeHooks h;
    h.buildModelName = onBuild;
    h.selectTextureSet = onTex;
    h.reportError = onErr;
    ObjLifeSetHooks(h);
    g_cap = Capture{};
}

bool feq(float a, float b) { return std::fabs(a - b) <= 1e-6f * (1.f + std::fabs(b)); }

}  // namespace

// ---------------------------------------------------------------------------
TEST(ObjLifeFlags, SetBlocked) {
    installHooks();
    SceneObject n;
    n.flags530 = 0xFF;
    CHECK_EQ(ObjectSetBlocked(&n, 1), 1);
    CHECK_EQ(n.flags530, 0xFF);              // bit4 stays set
    n.flags530 = 0xFF;
    CHECK_EQ(ObjectSetBlocked(&n, 0), 1);
    CHECK_EQ(n.flags530, 0xEF);              // bit4 cleared
    n.flags530 = 0x00;
    CHECK_EQ(ObjectSetBlocked(&n, 1), 1);
    CHECK_EQ(n.flags530, 0x10);              // bit4 set
    // Null object -> error path, returns 0.
    CHECK_EQ(ObjectSetBlocked(nullptr, 1), 0);
    CHECK_EQ(g_cap.errCalls, 1);
}

TEST(ObjLifeFlags, SetTransient) {
    installHooks();
    SceneObject n;
    n.flags530 = 0xFF;
    CHECK_EQ(ObjectSetTransient(&n, 3), 1);
    CHECK_EQ(n.flags530, 0xFF);
    n.flags530 = 0x00;
    CHECK_EQ(ObjectSetTransient(&n, 2), 1);
    CHECK_EQ(n.flags530, 0x08);
    n.flags530 = 0xFF;
    CHECK_EQ(ObjectSetTransient(&n, 1), 1);
    CHECK_EQ(n.flags530, 0xF7);
    CHECK_EQ(ObjectSetTransient(nullptr, 1), 0);
    CHECK_EQ(g_cap.errCalls, 1);
}

TEST(ObjLifeFlags, ClearVisualFlag) {
    SceneObject n;
    n.flags531 = 0xFF;
    CHECK_EQ(ObjectClearVisualFlag(&n), 1);
    CHECK_EQ(n.flags531, 0xFB);   // ~4 cleared
    n.flags531 = 0x04;
    ObjectClearVisualFlag(&n);
    CHECK_EQ(n.flags531, 0x00);
}

TEST(ObjLifeFlags, CmdSetActiveHandle) {
    SceneObject a, b;
    CHECK_EQ(ObjectCmdSetActiveHandle(&a), 1);
    CHECK(g_objActiveHandle == &a);
    ObjectCmdSetActiveHandle(&b);
    CHECK(g_objActiveHandle == &b);
}

// ---------------------------------------------------------------------------
TEST(ObjLifeTransform, SetAngle) {
    installHooks();
    SceneObject n;
    n.flags528 = 0;
    // x=90, z=180, y=45  -> golden 1.5707964, 0.78539819, 3.1415927
    CHECK_EQ(ObjectSetAngle(&n, 90.0f, 180.0f, 45.0f), 0);
    CHECK(feq(n.angle[0], 1.5707963705062866f));
    CHECK(feq(n.angle[1], 0.7853981852531433f));
    CHECK(feq(n.angle[2], 3.1415927410125732f));
    CHECK_EQ(n.flags528 & 4u, 4u);
    // x=360, z=0, y=-90
    SceneObject m;
    ObjectSetAngle(&m, 360.0f, 0.0f, -90.0f);
    CHECK(feq(m.angle[0], 6.2831854820251465f));
    CHECK(feq(m.angle[1], -1.5707963705062866f));
    CHECK(feq(m.angle[2], 0.0f));
    // Null -> error path, no crash.
    CHECK_EQ(ObjectSetAngle(nullptr, 1.f, 2.f, 3.f), 0);
    CHECK_EQ(g_cap.errCalls, 1);
}

// ---------------------------------------------------------------------------
TEST(ObjLifeEmitter, InitParticleEmitters) {
    SceneObject n;
    // Non-emitter node: nothing changes, returns node.
    n.nodeType = 3;
    Emitter ems[7] = {};
    ems[0].f[8] = 0.0f; ems[0].f[6] = 2.0f; ems[0].f[7] = 4.0f; ems[0].f[9] = 8.0f;
    NodeEmitter own{2.0f, 4.0f, 0.0f, 8.0f};
    CHECK(ObjectInitParticleEmitters(&n, ems, &own) == &n);
    CHECK(feq(ems[0].f[8], 0.0f));   // untouched (nodeType != 6)

    // Emitter node: seed the emitter with |life|==0.
    n.nodeType = 6;
    ObjectInitParticleEmitters(&n, ems, &own);
    CHECK(feq(ems[0].f[8], 0.30000001192092896f));  // (2+4)*0.05
    CHECK(feq(ems[0].f[7], 3.200000047683716f));     // 4*0.8
    CHECK(feq(ems[0].f[9], 4.0f));                    // 8*0.5
    CHECK(feq(own.life, 0.30000001192092896f));       // (2+4)*0.05
    CHECK(feq(own.spawnB, 3.200000047683716f));        // 4*0.8
    CHECK(feq(own.decay, 4.0f));                        // 8*0.5

    // Already-seeded emitter (life != 0) is left alone.
    Emitter ems2[7] = {};
    ems2[1].f[8] = 5.0f; ems2[1].f[6] = 2.0f; ems2[1].f[7] = 4.0f; ems2[1].f[9] = 8.0f;
    NodeEmitter own2{2.0f, 4.0f, 9.0f, 8.0f};
    ObjectInitParticleEmitters(&n, ems2, &own2);
    CHECK(feq(ems2[1].f[8], 5.0f));   // unchanged
    CHECK(feq(ems2[1].f[7], 4.0f));   // unchanged
    CHECK(feq(own2.life, 9.0f));      // unchanged
}

// ---------------------------------------------------------------------------
TEST(ObjLifeChain, UnlinkFromChain) {
    // node.ownerKey != 0 -> returns node.parent immediately, no walk.
    SceneObject p, n;
    n.parent = &p;
    n.ownerKey = 7;
    p.ownerKey = 99;
    CHECK(ObjectUnlinkFromChain(&n) == &p);
    CHECK_EQ(n.ownerKey, 7);   // unchanged

    // node.ownerKey == 0 -> walk parents copying their ownerKey until non-zero.
    SceneObject g, mid, leaf;
    g.ownerKey = 0; g.parent = nullptr;
    mid.ownerKey = 0; mid.parent = &g;
    leaf.ownerKey = 0; leaf.parent = &mid;
    g.ownerKey = 0;       // grandparent also zero key
    mid.ownerKey = 0;
    // Set mid's key to 0 but g's key to 0 too -> chain exhausts at nullptr.
    SceneObject* r = ObjectUnlinkFromChain(&leaf);
    CHECK(r == nullptr);  // walked off the end

    // Now a parent with a non-zero key stops the walk.
    SceneObject root, child;
    root.ownerKey = 55; root.parent = nullptr;
    child.ownerKey = 0;  child.parent = &root;
    SceneObject* r2 = ObjectUnlinkFromChain(&child);
    CHECK_EQ(child.ownerKey, 55);  // copied from root
    CHECK(r2 == nullptr);          // result advanced past root (root.parent)
}

// ---------------------------------------------------------------------------
TEST(ObjLifeRecord, IsBuildingType) {
    ResetBuildings();
    // Populate scene type table.
    g_sceneTypesLoaded = true;
    for (int i = 0; i < kSceneTypeCapacity; ++i) g_sceneTypes[i].kind = 0;
    g_sceneTypes[1].kind = 1;     // building
    g_sceneTypes[2].kind = 26;    // building
    g_sceneTypes[3].kind = 5;     // NOT a building kind
    g_sceneTypes[4].kind = 27;    // building
    g_sceneTypes[5].kind = 9;     // NOT
    CHECK(ObjectIsBuildingType(1));
    CHECK(ObjectIsBuildingType(2));
    CHECK(!ObjectIsBuildingType(3));
    CHECK(ObjectIsBuildingType(4));
    CHECK(!ObjectIsBuildingType(5));
    // Unloaded table -> kind 0 -> not a building.
    g_sceneTypesLoaded = false;
    CHECK(!ObjectIsBuildingType(1));
}

TEST(ObjLifeRecord, CollectMatchingProts) {
    ResetBuildings();
    // Scene types loaded gate.
    g_sceneTypesLoaded = false;
    guild::i8 remap[731];
    CHECK_EQ(ObjectCollectMatchingProts(5, remap), 1);  // unloaded -> 1

    g_sceneTypesLoaded = true;
    g_buildingTypesLoaded = true;
    for (int i = 0; i < kBuildingTypeCapacity; ++i) g_buildingTypes[i].kind = 0;
    // building type byte = 10, its kind:
    g_buildingTypes[10].kind = 7;
    // matching remaps must have kind 7 and remap <= 10.
    g_buildingTypes[3].kind = 7;   // remap 3 <= 10, kind matches -> include
    g_buildingTypes[8].kind = 7;   // remap 8 <= 10 -> include
    g_buildingTypes[12].kind = 7;  // remap 12 > 10 -> exclude
    g_buildingTypes[5].kind = 4;   // kind mismatch -> exclude

    for (int i = 0; i < 731; ++i) remap[i] = 72;  // all "hidden" sentinel
    remap[100] = 3;   // include
    remap[200] = 8;   // include
    remap[300] = 12;  // exclude (remap > type)
    remap[400] = 5;   // exclude (kind mismatch)
    remap[500] = 72;  // explicit sentinel skip

    g_objSpawnCount = 0xAAAA;
    CHECK_EQ(ObjectCollectMatchingProts(10, remap), 0);
    CHECK_EQ(g_objSpawnCount, 2);
    CHECK_EQ(g_objSpawnList[0], 100);
    CHECK_EQ(g_objSpawnList[1], 200);
}

TEST(ObjLifeRecord, ResetSpawnTables) {
    for (int i = 0; i < kSpawnTableProtCount; ++i) g_objSpawnOwner[i] = 5;
    for (int j = 0; j < kSpawnTableSlotCount; ++j) g_objSlotOwner[j] = 5;
    CHECK_EQ(ObjectResetSpawnTables(), 0);
    CHECK_EQ(g_objSpawnOwner[0], -1);
    CHECK_EQ(g_objSpawnOwner[kSpawnTableProtCount - 1], -1);
    CHECK_EQ(g_objSlotOwner[0], -1);
    CHECK_EQ(g_objSlotOwner[kSpawnTableSlotCount - 1], -1);
}

TEST(ObjLifeRecord, RebuildModelByOwner) {
    installHooks();
    for (int i = 0; i < kObjectCapacity; ++i) { g_objects[i].alive = 0; g_objects[i].id = 0; }
    g_objects[3].alive = 1; g_objects[3].id = 4242;
    g_objects[7].alive = 1; g_objects[7].id = 99;
    g_objects[9].alive = 0; g_objects[9].id = 4242;   // dead, skipped
    g_objects[11].alive = 1; g_objects[11].id = 4242; // matches

    SceneObject model;
    model.ownerKey = 4242;
    CHECK_EQ(ObjectRebuildModelByOwner(&model), 1);
    CHECK_EQ(g_cap.buildCalls, 2);   // slots 3 and 11
    CHECK_EQ(g_cap.lastMode, 2);
    CHECK(g_cap.lastModel == &model);
}

// ---------------------------------------------------------------------------
TEST(ObjLifeFoliage, HideFoliageDecor) {
    installHooks();
    SceneObject n;
    std::strcpy(n.name, "pfl_tree01");
    CHECK_EQ(ObjectHideFoliageDecor(&n, 5), 1);
    CHECK_EQ(g_cap.texCalls, 1);
    CHECK_EQ(g_cap.lastSet, 5);

    std::strcpy(n.name, "vg_bush");
    ObjectHideFoliageDecor(&n, 2);
    CHECK_EQ(g_cap.texCalls, 2);

    std::strcpy(n.name, "!vg_special");
    ObjectHideFoliageDecor(&n, 1);
    CHECK_EQ(g_cap.texCalls, 3);

    // Non-foliage name -> no select, returns 0.
    std::strcpy(n.name, "house_main");
    CHECK_EQ(ObjectHideFoliageDecor(&n, 0), 0);
    CHECK_EQ(g_cap.texCalls, 3);   // unchanged
}

TEST(ObjLifeFoliage, HideFoliageByState) {
    installHooks();
    ResetBuildings();
    g_buildingTypesLoaded = true;
    for (int i = 0; i < kBuildingTypeCapacity; ++i) { g_buildingTypes[i].kind = 0; g_buildingTypes[i].security = 0; }
    // type 4: kind != 2, security 3 -> v5 = 3-1 = 2 -> i in 0..2 (3 calls).
    g_buildingTypes[4].kind = 9;
    g_buildingTypes[4].security = 3;
    SceneObject n;
    std::strcpy(n.name, "house");
    n.visualState = 0;
    CHECK_EQ(ObjectHideFoliageByState(&n, 4), 1);
    CHECK_EQ(g_cap.texCalls, 3);   // i = 0,1,2

    // type 6: kind == 2 -> v5 = security (4) -> i in 0..4 (5 calls).
    g_cap.texCalls = 0;
    g_buildingTypes[6].kind = 2;
    g_buildingTypes[6].security = 4;
    ObjectHideFoliageByState(&n, 6);
    CHECK_EQ(g_cap.texCalls, 5);

    // visualState in {2,3,4} -> early return 1, no calls.
    g_cap.texCalls = 0;
    n.visualState = 3;
    CHECK_EQ(ObjectHideFoliageByState(&n, 4), 1);
    CHECK_EQ(g_cap.texCalls, 0);

    // foliage-name node -> skip loop.
    g_cap.texCalls = 0;
    n.visualState = 0;
    std::strcpy(n.name, "pfl_x");
    ObjectHideFoliageByState(&n, 4);
    CHECK_EQ(g_cap.texCalls, 0);
}

// ---------------------------------------------------------------------------
TEST(ObjLifeDoor, IsNearDoorCore) {
    DoorProximityInputs in{};
    in.hasActor = true;
    in.targetHasAnchor = true;
    in.doorDummyFound = true;
    in.doorPos[0] = 100; in.doorPos[1] = 100; in.doorPos[2] = 100;
    in.testPos[0] = 150; in.testPos[1] = 150; in.testPos[2] = 150;  // within 750
    in.doorOpenFlag = false;   // open -> near
    in.tolerance = 750.0f;
    in.actorOwnerId = 1; in.targetOwnerId = 2;
    CHECK(ObjectIsNearDoorCore(in));   // within tolerance + door open

    // Door "closed" flag set -> falls through to owner compare (mismatch).
    in.doorOpenFlag = true;
    CHECK(!ObjectIsNearDoorCore(in));
    // Owner ids match -> near regardless.
    in.targetOwnerId = 1;
    CHECK(ObjectIsNearDoorCore(in));

    // Out of tolerance + owner mismatch -> not near.
    in.testPos[0] = 5000;
    in.doorOpenFlag = false;
    in.targetOwnerId = 2;
    CHECK(!ObjectIsNearDoorCore(in));

    // No actor -> never near.
    in.hasActor = false;
    in.testPos[0] = 150;
    in.targetOwnerId = 1; in.actorOwnerId = 1;
    CHECK(!ObjectIsNearDoorCore(in));
}
