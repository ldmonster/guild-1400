#include "test.h"

// End-to-end: a "player walks up to a building" flow stitched across the
// building3 slice, with a single scriptable hook set standing in for the live
// scene/query subsystems. Steps:
//   1. Look up the building's display name (LookupTypeName) and string id
//      (LookupTypeStringId).
//   2. Gate entry on opening hours (CheckTimeWindowOpen) at two game hours.
//   3. Resolve the building's work-product object (FindWorkProductObject) and
//      its active work slot (FindActiveWorkSlot) through the scene hook.
//   4. Sync the occupant's category into the family record
//      (UpdateOccupantCategory) and then tear the building down
//      (ReleaseOccupantHoldings + FreeAndUnlink), observing the side effects.
#include "sim/building3.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct E2EHooks : Building3Hooks {
    int qfCount = 0;
    std::int32_t GameObjectQueryFind(std::int32_t, int ga, int gb, int,
                                     std::int32_t proto) override {
        ++qfCount;
        // The work-product query (groupA=1) for proto 253 resolves to handle 900.
        if (ga == 1 && gb == 0 && proto == 253) return 900;
        // The work-slot probes (146..151) all resolve; the actor-match probe
        // (groupA=2) hits on the first slot; the 278 query returns 278900.
        if (ga == 1 && gb == 0 && proto >= 146 && proto <= 151) return 10 + proto;
        if (ga == 2 && gb == 6) return 1;
        if (ga == 1 && gb == 0 && proto == 278) return 278900;
        return 0;
    }
    int releaseCalls = 0;
    std::int32_t OfficeReleaseCharacterHoldings(std::int32_t r) override {
        ++releaseCalls; return r;
    }
    int changeAction = 0, cancelEntity = 0, cancelEvent = 0;
    void CharacterChangePlayerAction(int, int, int, std::uint16_t) override { ++changeAction; }
    void CharActionCancelEntityActions(const std::uint8_t*) override { ++cancelEntity; }
    void EventCancelMatchingActors() override { ++cancelEvent; }
    int freeBuf = 0, freeChild = 0;
    void MemoryFreeDebug(std::int32_t, std::int32_t) override { ++freeBuf; }
    void GameObjectFreeChildList(std::int32_t*) override { ++freeChild; }
};

}  // namespace

TEST(Building3E2E, EnterResolveAndTeardownFlow) {
    E2EHooks hk;
    SetBuilding3Hooks(&hk);

    // ---- 1. Identify the building (a production "bk_HANDWERK" type, code 0x0b). ----
    char name[40]; std::memset(name, 0, sizeof name);
    int named = Building3_LookupTypeName(0x0b, name);
    CHECK_EQ(named, 1);
    CHECK_EQ(std::strcmp(name, "bk_HANDWERK"), 0);

    // A container object-type (category 2) at typeIndex 0x13 has string id 1434.
    int strId = Building3_LookupTypeStringId(0x13, /*objectCategory*/2);
    CHECK_EQ(strId, 1434);

    // ---- 2. Opening-hours gate. The type uses open-hours code 0x14 (open 6, close 7). ----
    SetBuilding3GameHour(6);
    int openH = -1, closeH = -1;
    bool openNow = Building3_CheckTimeWindowOpen(0x14, &openH, &closeH);
    CHECK(openNow);
    CHECK_EQ(openH, 6);
    CHECK_EQ(closeH, 7);

    SetBuilding3GameHour(20);                  // outside [6,7)
    CHECK(!Building3_CheckTimeWindowOpen(0x14, nullptr, nullptr));

    // ---- 3. Resolve scene objects for a production building. ----
    std::vector<std::uint8_t> typeTbl(589, 0);
    typeTbl[0] = 11;                           // production kind
    Building3Arrays arr; arr.buildingTypeBase = typeTbl.data();
    SetBuilding3Arrays(arr);

    std::uint8_t building[200]; std::memset(building, 0, sizeof building);
    std::int32_t container = 0x5000; std::memcpy(building + 93, &container, 4);

    std::int32_t product = Building3_FindWorkProductObject(building);
    CHECK_EQ(product, 900);                    // proto 253 -> handle 900

    std::int32_t slot = Building3_FindActiveWorkSlot(container, /*actor*/77);
    CHECK_EQ(slot, 278900);                    // active slot resolved to 278-handle

    // ---- 4. Sync the occupant category, then tear down. ----
    std::uint8_t family[256]; std::memset(family, 0, sizeof family);
    int famCat = -1, dirty = 0; std::uint8_t outDirect = 0;
    int token = Building3_UpdateOccupantCategory(/*idx*/4, /*recCode*/7, /*class*/5,
                                                 /*directCat*/3, family,
                                                 &famCat, &dirty, &outDirect);
    CHECK_EQ(token, 67 * 4);
    CHECK_EQ(dirty, 1);
    CHECK_EQ(famCat, 3);

    // Release the occupant (valid actor 88), then free+unlink the type-30 record.
    std::uint8_t occ[80]; std::memset(occ, 0, sizeof occ);
    std::uint16_t actor = 88; std::memcpy(occ, &actor, 2);
    const std::uint8_t* rr = Building3_ReleaseOccupantHoldings(occ, actor);
    CHECK(rr == occ);
    CHECK_EQ(hk.releaseCalls, 1);
    CHECK_EQ(hk.changeAction, 1);

    std::uint8_t rec[200]; std::memset(rec, 0, sizeof rec);
    rec[0] = 30;
    std::int32_t buf = 0x6000; std::memcpy(rec + 113, &buf, 4);
    std::int32_t child = 9; std::memcpy(rec + 93, &child, 4);
    std::vector<std::int32_t> ia(2 * 134, 0);
    std::int32_t recId = 0x99; ia[0] = recId;
    int rc = Building3_FreeAndUnlink(rec, recId, ia.data(), nullptr, 2);
    CHECK_EQ(rc, 0);
    CHECK_EQ(hk.freeBuf, 1);
    CHECK_EQ(hk.freeChild, 1);
    CHECK_EQ(ia[0], 0);
    CHECK_EQ((int)rec[0], 0);

    SetBuilding3Hooks(nullptr);
    SetBuilding3Arrays(Building3Arrays{});
}
