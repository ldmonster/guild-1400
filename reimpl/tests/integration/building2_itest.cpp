#include "test.h"

// Integration: drive the building2 room-slot collectors / category resolver
// against a REAL reconstructed sibling — VIBE_BuildingType_GroupFromCode
// (building_type.cpp, gilde.exe 0x58a4c8) — no mock classifier.
//
// building2 exposes its cross-module object/person query subsystem through the
// IBuilding2QueryHooks struct (default inert). The live wiring routes a room
// slot's stored code through the scene/type classifier; here we forward the
// GameObjectQueryFind hook into the genuine BuildingType_GroupFromCode group
// map, so Building_CollectObjectSlots walks the real type-record room list and
// classifies each slot via the real sibling. We also exercise
// Building_GetCategoryForObject, which calls BuildingType_GroupFromCode
// internally, asserting the two views of the real classifier agree.
#include "sim/building2.h"
#include "sim/building_type.h"   // real BuildingType_GroupFromCode (0x58a4c8)

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A real 589-byte building-type record (dword_13CE294 stride). Only the fields
// the collectors read are populated: +0 type byte, +34 slot count, +35 word[64]
// room list, +356/+358/+361 match bytes.
struct TypeRecBuf {
    std::uint8_t bytes[589];
    TypeRecBuf() { std::memset(bytes, 0, sizeof bytes); }
    void SetTypeByte(std::uint8_t v)      { bytes[0] = v; }
    void SetSlotCount(std::uint8_t v)     { bytes[34] = v; }
    void SetRoom(int i, std::uint16_t v) {
        std::memcpy(bytes + 35 + 2 * i, &v, 2);
    }
    void SetMatchByte(std::uint8_t v)     { bytes[356] = v; }
};

// Object-TYPE table (dword_13CE27C stride 65): byte +0 is the category code.
struct ObjTypeBuf {
    std::vector<std::uint8_t> bytes;
    explicit ObjTypeBuf(int count) : bytes(static_cast<std::size_t>(count) * 65, 0) {}
    void SetCategory(int id, std::uint8_t cat) { bytes[static_cast<std::size_t>(65) * id] = cat; }
};

// Real-sibling-backed query hook: forward each room slot's id (passed as
// protoId) through the genuine BuildingType_GroupFromCode classifier. A slot
// whose code maps to a non-zero group is reported "found" (handle = 1000+group);
// an unclassified code (group 0) is reported as a miss (0), exactly as the live
// scene query would only return a real placed object for a recognized type.
struct RealClassifierHooks : IBuilding2QueryHooks {
    int lastFlag = 0, lastGroupA = 0, lastGroupB = 0;
    std::int16_t GameObjectQueryFind(std::int32_t /*containerHandle*/, int groupA,
                                     int groupB, int flag,
                                     std::int32_t protoId) override {
        lastGroupA = groupA; lastGroupB = groupB; lastFlag = flag;
        std::uint8_t group = BuildingType_GroupFromCode(
            static_cast<std::uint8_t>(protoId));   // REAL sibling
        if (!group) return 0;                       // unclassified => miss
        return static_cast<std::int16_t>(1000 + group);
    }
};

} // namespace

// Building_CollectObjectSlots walks the real building-type record's +35 room
// list and asks the query hook for each. Wired to the real GroupFromCode
// classifier: slots with codes that map to a real group are collected; an
// unclassified code (e.g. 200 -> group 0) is dropped. Verifies the room-list
// walk drives the real sibling and honors its classification verbatim.
TEST(Building2Itest, CollectObjectSlotsThroughRealGroupClassifier) {
    TypeRecBuf rec;
    rec.SetTypeByte(5);                 // typeCode used as the record index
    // Room list codes: 2 -> group 11 (hit), 65 -> group 3 (hit),
    //                  200 -> group 0 (miss), then 0 terminator.
    rec.SetRoom(0, 2);
    rec.SetRoom(1, 65);
    rec.SetRoom(2, 200);
    rec.SetRoom(3, 0);

    // Bind the building-type table so TypeRec(5) addresses our record. The base
    // is offset back by 589*5 so index 5 lands on rec.
    std::vector<std::uint8_t> table(static_cast<std::size_t>(589) * 6, 0);
    std::memcpy(table.data() + static_cast<std::size_t>(589) * 5, rec.bytes, 589);
    BuildingArrayBindings b;
    b.buildingTypeBase = table.data();
    SetBuildingArrayBindings(b);

    RealClassifierHooks hooks;
    SetBuilding2QueryHooks(&hooks);

    std::int16_t out[64] = {0};
    int n = Building_CollectObjectSlots(/*typeCode=*/5, /*containerHandle=*/77, out);

    CHECK_EQ(n, 2);                     // codes 2 and 65 classified; 200 dropped
    CHECK_EQ(static_cast<int>(out[0]), 1000 + 11);   // group of code 2
    CHECK_EQ(static_cast<int>(out[1]), 1000 + 3);    // group of code 65
    // The collector asked with the original's (groupA=2, groupB=6, flag=0).
    CHECK_EQ(hooks.lastGroupA, 2);
    CHECK_EQ(hooks.lastGroupB, 6);
    CHECK_EQ(hooks.lastFlag, 0);

    SetBuilding2QueryHooks(nullptr);
}

// Building_FindOfficeStorage chains PersonQueryByGoodType -> GameObjectQueryFind.
// We back the query find with the real GroupFromCode classifier and resolve the
// kind so the proto id (277 / 322) is itself a code the real sibling classifies.
TEST(Building2Itest, FindOfficeStorageProtoThroughRealClassifier) {
    struct OfficeHooks : RealClassifierHooks {
        std::int32_t PersonQueryByGoodType(std::uint8_t /*goodType*/, int /*ctx*/,
                                           std::uint8_t* kindOut) override {
            if (kindOut) *kindOut = 1;   // kind 1 => proto 277
            return 42;                   // a real container handle
        }
    } hooks;
    SetBuilding2QueryHooks(&hooks);

    // proto 277 -> GroupFromCode(277 & 0xFF = 21) ... but protoId is i32, the
    // sibling takes a u8: 277 truncates to 21 -> group 6 (codes 19..24). The
    // returned handle therefore encodes the real classifier's verdict.
    std::int16_t h = Building_FindOfficeStorage(/*goodType=*/3, /*ctx=*/0);
    CHECK_EQ(static_cast<int>(h), 1000 + 6);

    SetBuilding2QueryHooks(nullptr);
}

// Building_GetCategoryForObject calls the SAME real BuildingType_GroupFromCode
// internally. Assert its branch matches the sibling's group output: when the
// record code's group equals matchCode it returns the direct category, else the
// family fallback (or 0).
TEST(Building2Itest, GetCategoryForObjectAgreesWithRealGroup) {
    // code 20 -> GroupFromCode(20) == 6 (codes 19..24).
    const std::int8_t code = 20;
    const std::uint8_t realGroup = BuildingType_GroupFromCode(20);   // REAL sibling
    CHECK_EQ(static_cast<int>(realGroup), 6);

    // matchCode == group => return directCat verbatim.
    std::uint8_t direct = Building_GetCategoryForObject(code, /*matchCode=*/6,
                                                        /*directCat=*/0x55,
                                                        /*familyCat=*/0x12);
    CHECK_EQ(static_cast<int>(direct), 0x55);

    // matchCode != group, familyCat >= 0 => family fallback.
    std::uint8_t fam = Building_GetCategoryForObject(code, /*matchCode=*/3,
                                                     /*directCat=*/0x55,
                                                     /*familyCat=*/0x12);
    CHECK_EQ(static_cast<int>(fam), 0x12);

    // matchCode != group, no family => 0.
    std::uint8_t none = Building_GetCategoryForObject(code, /*matchCode=*/3,
                                                      /*directCat=*/0x55,
                                                      /*familyCat=*/-1);
    CHECK_EQ(static_cast<int>(none), 0);
}
