#include "test.h"

#include "sim/command_unit_orders.h"

#include <cstring>

// Golden-vector unit tests for the four Command unit-order execution leaves
// (gilde.exe 0x491054 / 0x49110C / 0x491148 / 0x49CD10). Each test seeds the
// modeled unit set, drives one leaf, and checks the exact gate decision + the
// observable mutations / banner ids the original produces.

using namespace guild;
using namespace guild::sim;

namespace {
UnitOrderRecord MakeRec(i32 unitId, bool active) {
    UnitOrderRecord r{};
    r.unitId = unitId;
    r.active = active ? 1 : 0;
    return r;
}
} // namespace

// --- ExecPickupGroundItem ---------------------------------------------------

TEST(SimUnitOrders, PickupGatedByActiveFlag) {
    ResetUnitOrderState();
    UnitOrderActor u{}; u.unitId = 5; u.actorPtr = 0x1000; SeedUnit(u);
    // active == 0 -> the gate fails, nothing happens.
    CHECK(!ExecPickupGroundItem(MakeRec(5, false)));
    CHECK_EQ(UnitOrderLogRef().standUpCount, 0);
    CHECK_EQ(UnitOrderLogRef().pickupActionCount, 0);
}

TEST(SimUnitOrders, PickupGatedByMinusOneUnit) {
    ResetUnitOrderState();
    CHECK(!ExecPickupGroundItem(MakeRec(-1, true)));
    CHECK_EQ(UnitOrderLogRef().standUpCount, 0);
}

TEST(SimUnitOrders, PickupStandsUpAndQueuesAction) {
    ResetUnitOrderState();
    UnitOrderActor u{}; u.unitId = 9; u.actorPtr = 0x4242; u.carriedObject = 0;
    SeedUnit(u);
    CHECK(ExecPickupGroundItem(MakeRec(9, true)));
    const UnitOrderLog& L = UnitOrderLogRef();
    CHECK_EQ(L.standUpCount, 1);
    CHECK_EQ(L.lastStandUpActor, 0x4242);
    CHECK_EQ(L.releaseCarriedCount, 0);     // no carried object -> no release
    CHECK_EQ(L.pickupActionCount, 1);
    CHECK_EQ(L.lastPickupActor, 0x4242);
}

TEST(SimUnitOrders, PickupReleasesCarriedObjectAndClears) {
    ResetUnitOrderState();
    UnitOrderActor u{}; u.unitId = 9; u.actorPtr = 0x4242; u.carriedObject = 0x900;
    UnitOrderActor* slot = SeedUnit(u);
    CHECK(ExecPickupGroundItem(MakeRec(9, true)));
    const UnitOrderLog& L = UnitOrderLogRef();
    CHECK_EQ(L.releaseCarriedCount, 1);
    CHECK_EQ(slot->carriedObject, 0);       // unit[107] cleared to 0
    CHECK_EQ(L.pickupActionCount, 1);
}

// --- ExecUnitSelectSound ----------------------------------------------------

TEST(SimUnitOrders, SelectSoundShowsBannerWithNameIndex) {
    ResetUnitOrderState();
    // default person resolver: nameIndex == low 16 bits of id, found when id>=0.
    CHECK(ExecUnitSelectSound(MakeRec(0x1234, true)));
    const UnitOrderLog& L = UnitOrderLogRef();
    CHECK_EQ(L.bannerCount, 1);
    CHECK_EQ(L.lastBannerTextId, 3610);
    CHECK_EQ(L.lastBannerPerson, 0x1234);
}

TEST(SimUnitOrders, SelectSoundMissingPersonNoBanner) {
    ResetUnitOrderState();
    CHECK(!ExecUnitSelectSound(MakeRec(-1, true))); // id<0 -> not found
    CHECK_EQ(UnitOrderLogRef().bannerCount, 0);
}

// --- ExecConquerFlag --------------------------------------------------------

TEST(SimUnitOrders, ConquerFlagGatedByActive) {
    ResetUnitOrderState();
    CHECK(!ExecConquerFlag(MakeRec(7, false)));
    CHECK_EQ(UnitOrderLogRef().flagAttachCount, 0);
}

TEST(SimUnitOrders, ConquerFlagAttachesMeshAndBanners) {
    ResetUnitOrderState();
    UnitOrderActor u{}; u.unitId = 7; u.actorPtr = 0x10; u.personType = 5; u.flagMesh = 0;
    UnitOrderActor* slot = SeedUnit(u);
    CHECK(ExecConquerFlag(MakeRec(7, true)));
    const UnitOrderLog& L = UnitOrderLogRef();
    CHECK_EQ(L.bannerCount, 1);
    CHECK_EQ(L.lastBannerTextId, 3611);
    CHECK_EQ(L.flagAttachCount, 1);
    CHECK_EQ(L.lastFlagUnit, 7);
    CHECK(slot->flagMesh != 0);             // a mesh handle was stored
}

TEST(SimUnitOrders, ConquerFlagReplacesPriorMesh) {
    ResetUnitOrderState();
    UnitOrderActor u{}; u.unitId = 7; u.flagMesh = 0x999; // already holds a flag
    UnitOrderActor* slot = SeedUnit(u);
    CHECK(ExecConquerFlag(MakeRec(7, true)));
    const UnitOrderLog& L = UnitOrderLogRef();
    CHECK_EQ(L.flagDetachCount, 1);         // old mesh released first
    CHECK_EQ(L.flagAttachCount, 1);         // new mesh attached
    CHECK(slot->flagMesh != 0 && slot->flagMesh != 0x999);
}

TEST(SimUnitOrders, ConquerFlagNoAnchorClears) {
    ResetUnitOrderState();
    // unitId -1 -> no new holder; the "!v3" clear path returns false, no attach.
    CHECK(!ExecConquerFlag(MakeRec(-1, true)));
    CHECK_EQ(UnitOrderLogRef().flagAttachCount, 0);
}

// --- CharacterAttachToScene -------------------------------------------------

TEST(SimUnitOrders, AttachToSceneNoSceneObjectIsNoop) {
    ResetUnitOrderState();
    float p[3] = {0, 0, 0}, d[3] = {0, 0, 0};
    CharacterAttachToScene(0, p, d, p, d);
    CHECK_EQ(UnitOrderLogRef().scenePosUpdates, 0);
    CHECK_EQ(UnitOrderLogRef().sceneDirUpdates, 0);
}

TEST(SimUnitOrders, AttachToSceneWithinToleranceNoWrites) {
    ResetUnitOrderState();
    float cur[3] = {100.0f, 0.0f, 50.0f};
    float tgt[3] = {104.0f, 7.0f, 47.0f};   // each axis within 8.0
    CharacterAttachToScene(0x55, cur, cur, tgt, tgt);
    CHECK_EQ(UnitOrderLogRef().scenePosUpdates, 0);
    CHECK_EQ(UnitOrderLogRef().sceneDirUpdates, 0);
}

TEST(SimUnitOrders, AttachToSceneOutOfTolerancePositionWrite) {
    ResetUnitOrderState();
    float cur[3]    = {100.0f, 0.0f, 50.0f};
    float tgtPos[3] = {120.0f, 0.0f, 50.0f};  // dx=20 > 8 -> position write
    float tgtDir[3] = {0.0f, 0.0f, 0.0f};     // dir matches cur (within tol? dx=100)
    CharacterAttachToScene(0x55, cur, cur, tgtPos, tgtDir);
    CHECK_EQ(UnitOrderLogRef().scenePosUpdates, 1);
    CHECK_EQ(UnitOrderLogRef().sceneDirUpdates, 1); // cur(100,..) vs (0,..) > 8
}

TEST(SimUnitOrders, VectorToleranceGolden) {
    float a[3] = {0.0f, 0.0f, 0.0f};
    float b[3] = {8.0f, -8.0f, 0.0f};
    CHECK(VectorWithinTolerance(a, b, 8.0f));   // exactly at the boundary
    float c[3] = {8.001f, 0.0f, 0.0f};
    CHECK(!VectorWithinTolerance(a, c, 8.0f));  // just past
}
