// End-to-end flow for the VIBE_Object_* lifecycle leaves: configure a building,
// classify it, collect its spawnable prototypes, spawn a model, rebuild it by
// owner, transform it, set flags, hide foliage, then tear down the spawn tables.
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

#include "sim/object_lifecycle2.h"
#include "sim/building.h"
#include "sim/building_production.h"
#include "sim/entity.h"

using namespace guild::sim;

namespace {
int g_buildCalls = 0;
int g_texCalls = 0;
void onBuild(SceneObject*, ObjectRec*, guild::u8) { g_buildCalls++; }
void onTex(SceneObject*, int) { g_texCalls++; }
bool feq(float a, float b) { return std::fabs(a - b) <= 1e-5f * (1.f + std::fabs(b)); }
}  // namespace

TEST(ObjLifeE2E, SpawnTransformFillQueryDestroy) {
    // --- world setup ----------------------------------------------------------
    ResetBuildings();
    g_sceneTypesLoaded = true;
    g_buildingTypesLoaded = true;
    for (int i = 0; i < kSceneTypeCapacity; ++i) g_sceneTypes[i].kind = 0;
    for (int i = 0; i < kBuildingTypeCapacity; ++i) { g_buildingTypes[i].kind = 0; g_buildingTypes[i].security = 0; }
    for (int i = 0; i < kObjectCapacity; ++i) { g_objects[i].alive = 0; g_objects[i].id = 0; }

    ObjLifeHooks h;
    h.buildModelName = onBuild;
    h.selectTextureSet = onTex;
    ObjLifeSetHooks(h);
    g_buildCalls = 0; g_texCalls = 0;

    // A building of scene type 1 (kind 1 -> "building"), building-type byte 10.
    const guild::u8 bldgType = 10;
    g_sceneTypes[1].kind = 1;
    g_buildingTypes[bldgType].kind = 7;
    g_buildingTypes[bldgType].security = 3;
    g_buildingTypes[3].kind = 7;   // a spawnable prototype of the same kind
    g_buildingTypes[8].kind = 7;

    // --- classify -------------------------------------------------------------
    CHECK(ObjectIsBuildingType(1));
    CHECK(!ObjectIsBuildingType(2));

    // --- reset + collect spawnable prototypes ---------------------------------
    CHECK_EQ(ObjectResetSpawnTables(), 0);
    CHECK_EQ(g_objSpawnOwner[0], -1);

    guild::i8 remap[731];
    for (int i = 0; i < 731; ++i) remap[i] = 72;
    remap[42]  = 3;   // include (kind 7, <= 10)
    remap[314] = 8;   // include
    ObjectCollectMatchingProts(bldgType, remap);
    CHECK_EQ(g_objSpawnCount, 2);
    CHECK_EQ(g_objSpawnList[0], 42);
    CHECK_EQ(g_objSpawnList[1], 314);

    // --- spawn a scene-object model and wire it up ----------------------------
    SceneObject model;
    std::strcpy(model.name, "gb_house");
    model.nodeType = 1;
    model.ownerKey = 5000;
    CHECK_EQ(ObjectCmdSetActiveHandle(&model), 1);
    CHECK(g_objActiveHandle == &model);

    // Two live object records owned by 5000 -> rebuild visits both.
    g_objects[2].alive = 1; g_objects[2].id = 5000;
    g_objects[6].alive = 1; g_objects[6].id = 5000;
    g_objects[9].alive = 1; g_objects[9].id = 777;   // different owner
    CHECK_EQ(ObjectRebuildModelByOwner(&model), 1);
    CHECK_EQ(g_buildCalls, 2);

    // --- transform: rotate the model ------------------------------------------
    model.flags528 = 0;
    ObjectSetAngle(&model, 90.0f, 180.0f, 45.0f);
    CHECK(feq(model.angle[0], 1.5707964f));
    CHECK(feq(model.angle[1], 0.7853982f));
    CHECK(feq(model.angle[2], 3.1415927f));
    CHECK_EQ(model.flags528 & 4u, 4u);   // dirty flag stamped

    // --- flags: block + transient + clear-visual ------------------------------
    model.flags530 = 0;
    ObjectSetBlocked(&model, 1);
    CHECK_EQ(model.flags530 & 0x10u, 0x10u);
    ObjectSetTransient(&model, 2);
    CHECK_EQ(model.flags530 & 0x0Cu, 0x08u);
    model.flags531 = 0xFF;
    ObjectClearVisualFlag(&model);
    CHECK_EQ(model.flags531 & 4u, 0u);

    // --- foliage hide on a vegetation node ------------------------------------
    SceneObject veg;
    std::strcpy(veg.name, "vg_grass");
    veg.visualState = 0;
    g_texCalls = 0;
    ObjectHideFoliageDecor(&veg, 7);
    CHECK_EQ(g_texCalls, 1);

    // A non-foliage building node, type 10 -> loop runs (security 3 -> i 0..2).
    SceneObject decor;
    std::strcpy(decor.name, "wall");
    decor.visualState = 0;
    g_texCalls = 0;
    ObjectHideFoliageByState(&decor, bldgType);
    CHECK_EQ(g_texCalls, 3);

    // --- door proximity query -------------------------------------------------
    DoorProximityInputs door{};
    door.hasActor = true; door.targetHasAnchor = true; door.doorDummyFound = true;
    door.doorPos[0] = 0; door.doorPos[1] = 0; door.doorPos[2] = 0;
    door.testPos[0] = 100; door.testPos[1] = 0; door.testPos[2] = 0;
    door.doorOpenFlag = false; door.tolerance = 750.0f;
    door.actorOwnerId = 1; door.targetOwnerId = 9;
    CHECK(ObjectIsNearDoorCore(door));   // within 750 + open

    // --- unlink the model from its (empty) parent chain -----------------------
    model.parent = nullptr; model.ownerKey = 0;
    CHECK(ObjectUnlinkFromChain(&model) == nullptr);

    // --- teardown -------------------------------------------------------------
    ObjectResetSpawnTables();
    CHECK_EQ(g_objSlotOwner[0], -1);
    ObjLifeResetHooks();
}
