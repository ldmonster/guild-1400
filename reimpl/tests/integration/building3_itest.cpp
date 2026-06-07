#include "test.h"

// Integration: drive building3 against a REAL reconstructed sibling — no mock
// classifier. Two cross-module wirings are exercised:
//
//   1. Building3_UpdateOccupantCategory calls VIBE_BuildingType_GroupFromCode
//      (building_type.cpp, gilde.exe 0x58a4c8) INTERNALLY to pick the family
//      record's category slot. We assert the slot it writes is exactly the one
//      the genuine GroupFromCode maps the occupant's recCode to (no stub).
//
//   2. Building3_FindWorkProductObject routes its category decision through the
//      genuine Building_MapKindToCategory / Building_IsProductionKind siblings
//      (building_type.cpp) reading a real 589-byte type record; only the scene
//      query (VIBE_GameObject_QueryFind) is a hook. We assert the proto-id the
//      finder asks the scene for matches the category the real classifier
//      assigns to the record's KIND byte.
#include "sim/building3.h"
#include "sim/building_type.h"   // real BuildingType_GroupFromCode / MapKindToCategory

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Scene-query hook that records the proto the finder asks for.
struct ProtoRecorder : Building3Hooks {
    std::int32_t lastProto = -1;
    std::int32_t GameObjectQueryFind(std::int32_t, int, int, int,
                                     std::int32_t proto) override {
        lastProto = proto;
        return 12345;     // pretend the scene found something
    }
};

}  // namespace

// 1. UpdateOccupantCategory writes into the real-GroupFromCode-derived slot.
TEST(Building3IT, UpdateOccupantCategory_UsesRealGroupFromCode) {
    // For several recCodes, the family slot written must be 112 + GroupFromCode(recCode).
    const std::uint8_t codes[] = { 1, 7, 13, 19, 40, 70 };
    for (std::uint8_t code : codes) {
        std::uint8_t family[256];
        std::memset(family, 0, sizeof family);
        int famCat = -1, dirty = 0; std::uint8_t outDirect = 0;
        std::uint8_t directCat = 5;
        Building3_UpdateOccupantCategory(/*idx*/2, (std::int8_t)code, /*class*/6,
                                         directCat, family, &famCat, &dirty, &outDirect);
        // Reference: the genuine sibling.
        std::uint8_t group = BuildingType_GroupFromCode(code);
        CHECK_EQ((int)family[112 + group], (int)directCat);
        CHECK_EQ(famCat, (int)directCat);
        CHECK_EQ(dirty, 1);
    }
}

// 2. FindWorkProductObject's proto choice tracks the real category classifier.
TEST(Building3IT, FindWorkProduct_TracksRealCategoryClassifier) {
    // Build a real 589-byte type record; vary its KIND byte and assert the
    // finder asks the scene for the proto that the real MapKindToCategory dictates.
    struct Case { std::uint8_t kind; std::int32_t expectProto; };
    // kind 22 (0x16) -> category 1, kind==22 branch -> proto 302.
    // kind 4         -> category 8, kind==4  branch -> proto 84.
    // kind 7         -> category 4            -> proto 247.
    // kind 11        -> category 0, production -> proto 253.
    const Case cases[] = {
        { 0x16, 302 }, { 4, 84 }, { 7, 247 }, { 11, 253 },
    };
    for (const Case& cs : cases) {
        // Sanity: the real classifier agrees with our expectation.
        std::uint8_t cat = Building_MapKindToCategory(cs.kind);
        if (cs.kind == 0x16) CHECK_EQ((int)cat, 1);
        if (cs.kind == 4)    CHECK_EQ((int)cat, 8);
        if (cs.kind == 7)    CHECK_EQ((int)cat, 4);
        if (cs.kind == 11)   CHECK(Building_IsProductionKind(cs.kind));

        std::vector<std::uint8_t> typeTbl(589, 0);
        typeTbl[0] = cs.kind;
        Building3Arrays a; a.buildingTypeBase = typeTbl.data(); SetBuilding3Arrays(a);

        ProtoRecorder rec; SetBuilding3Hooks(&rec);
        std::uint8_t building[200]; std::memset(building, 0, sizeof building);
        std::int32_t container = 0xABCD; std::memcpy(building + 93, &container, 4);

        std::int32_t r = Building3_FindWorkProductObject(building);
        CHECK_EQ(r, 12345);
        CHECK_EQ(rec.lastProto, cs.expectProto);

        SetBuilding3Hooks(nullptr);
        SetBuilding3Arrays(Building3Arrays{});
    }
}

// 3. The inert default-hook path: with no hooks installed, scene queries return
//    0 so the finders degrade gracefully (the live wiring's "nothing found").
TEST(Building3IT, DefaultHooksAreInert) {
    SetBuilding3Hooks(nullptr);   // default inert hooks
    std::vector<std::uint8_t> typeTbl(589, 0);
    typeTbl[0] = 11;              // production
    Building3Arrays a; a.buildingTypeBase = typeTbl.data(); SetBuilding3Arrays(a);

    std::uint8_t building[200]; std::memset(building, 0, sizeof building);
    CHECK_EQ(Building3_FindWorkProductObject(building), 0);
    CHECK_EQ(Building3_FindActiveWorkSlot(0x10, 3), 0);

    SetBuilding3Arrays(Building3Arrays{});
}
