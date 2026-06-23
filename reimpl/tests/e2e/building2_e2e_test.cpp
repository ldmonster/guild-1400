// End-to-end flow for src/sim/building2.{h,cpp}: assemble a synthetic building
// world (type catalog + object-type table + char array), then drive a
// room-enumeration / office-lookup flow across the translated functions the way
// the entry/info-panel code would.
#include "sim/building2.h"

#include <cstring>
#include <vector>

#include "test.h"

using namespace guild::sim;

namespace {

// A scene with one production building (type 1) that owns a mixed room list, a
// dungeon building type (type 4 kind), and an object-type table classifying the
// room contents. Wired through a scene query hook that resolves slot -> handle.
struct Scene {
    std::vector<std::uint8_t> typeTable;   // dword_13CE294, stride 589
    std::vector<std::uint8_t> objTable;    // dword_13CE27C, stride 65
    std::vector<std::uint8_t> charArray;   // dword_13CE298, stride 169

    Scene() : typeTable(589 * 8, 0), objTable(65 * 64, 0), charArray(169 * 256, 0) {}

    std::uint8_t* type(int i) { return typeTable.data() + 589 * i; }
    void setRoom(int t, int slot, std::uint16_t v) {
        std::memcpy(type(t) + 35 + 2 * slot, &v, 2);
    }
    void setSlotCount(int t, std::uint8_t c) { type(t)[34] = c; }
    void setCat(int objId, std::uint8_t c) { objTable[65 * objId] = c; }

    void bind() {
        BuildingArrayBindings b;
        b.buildingTypeBase = typeTable.data();
        b.objectTypeBase   = objTable.data();
        b.charArrayBase    = charArray.data();
        SetBuildingArrayBindings(b);
    }
};

// Scene query hook: a QueryFind hit returns a handle == 1000 + protoId for the
// "production" container, and the office lookup is scripted per the test.
struct SceneQuery : IBuilding2QueryHooks {
    std::int32_t officeContainer = -1;
    std::uint8_t officeKind = 0;

    std::int16_t GameObjectQueryFind(std::int32_t container, int, int, int,
                                     std::int32_t proto) override {
        if (container < 0) return 0;
        return static_cast<std::int16_t>(1000 + proto);
    }
    std::int32_t PersonQueryByGoodType(std::uint8_t, int, std::uint8_t* kindOut) override {
        if (kindOut) *kindOut = officeKind;
        return officeContainer;
    }
};

}  // namespace

TEST(Building2_E2E, RoomEnumerationAndOfficeFlow) {
    Scene s;
    // Building type 1: dungeon? no, production. Room list: 2 storable + 1 wall.
    //   slot 0 -> objId 5  (cat 2  = container)
    //   slot 1 -> objId 6  (cat 1  = normal good)
    //   slot 2 -> objId 7  (cat 33 = wall)
    //   slot 3 -> terminator
    s.setSlotCount(1, 3);
    s.setRoom(1, 0, 5);
    s.setRoom(1, 1, 6);
    s.setRoom(1, 2, 7);
    s.setRoom(1, 3, 0);
    s.setCat(5, 2);
    s.setCat(6, 1);
    s.setCat(7, 33);
    // Building type 4 marked as a dungeon (kind byte at +0 == 4).
    s.type(4)[0] = 4;
    s.bind();

    SceneQuery q;
    SetBuilding2QueryHooks(&q);

    // 1. CollectObjectSlots: every present room (3) yields a scene handle.
    std::int16_t objs[8] = {0};
    int nObj = Building_CollectObjectSlots(/*typeCode*/1, /*container*/200, objs);
    CHECK_EQ(nObj, 3);
    CHECK_EQ(static_cast<int>(objs[0]), static_cast<int>(1000 + 5));
    CHECK_EQ(static_cast<int>(objs[2]), static_cast<int>(1000 + 7));

    // 2. CollectStorableSlots: the wall (objId 7, cat 33) is excluded.
    int nStore = Building_CollectStorableSlots(1, nullptr);
    CHECK_EQ(nStore, 2);

    // 3. CollectSlotsAfterObject from slot 5: collects {6}; stops at the
    //    container-or-wall boundary (objId 7 is cat 33 -> skipped, then 0 term).
    std::int16_t after[8] = {0};
    int nAfter = Building_CollectSlotsAfterObject(1, 3, 5, after);
    CHECK_EQ(nAfter, 1);             // only objId 6 counts (objId 7 is a wall)
    // Write-cursor quirk: the non-33 slot advances the cursor to out[1] and is
    // written there; the trailing wall (objId 7) re-writes the same cell without
    // advancing -> out[1] ends as 7. This mirrors the original's pre/post-inc.
    CHECK_EQ(static_cast<int>(after[1]), 7);

    // 4. Flag-node enumeration: only kind-1 nodes pointing at a wall (cat 33)
    //    accumulate. Feed the three room objects as flag-node candidates.
    FlagNodeAccumulator acc{};
    Building_CollectFlagNodeCallback(0xA0, (1 << 24) | 5, &acc);  // cat 2  -> skip
    Building_CollectFlagNodeCallback(0xA1, (1 << 24) | 7, &acc);  // cat 33 -> keep
    Building_CollectFlagNodeCallback(0xA2, (2 << 24) | 7, &acc);  // kind 2 -> skip
    CHECK_EQ(acc.count, 1);
    CHECK_EQ(acc.handles[0], 0xA0 + 1);  // 0xA1
    CHECK_EQ(static_cast<int>(acc.slots[0]), 7);

    // 5. Dungeon ownership: place an alive type-4 building in the char array
    //    owned by person 0xCAFE; FindOwnedDungeonSlot resolves it.
    s.charArray[169 * 9 + 0] = 4;     // alive + dungeon type code
    std::int32_t owner = 0xCAFE;
    std::memcpy(s.charArray.data() + 169 * 9 + 101, &owner, 4);
    s.bind();
    CHECK_EQ(Building_FindOwnedDungeonSlot(true, 0xCAFE), 0);   // owned -> 0
    CHECK_EQ(Building_FindOwnedDungeonSlot(true, 0xBEEF), 1);   // not owned -> 1

    // 6. Office storage routing: kind 1 -> proto 277 storage; the scene returns
    //    1000 + proto.
    q.officeContainer = 500;
    q.officeKind = 1;
    CHECK_EQ(static_cast<int>(Building_FindOfficeStorage(7, 0)), 1000 + 277);
    CHECK_EQ(Building_HasActiveOffice(7, 0), true);
    q.officeContainer = -1;
    CHECK_EQ(Building_HasActiveOffice(7, 0), false);

    SetBuilding2QueryHooks(nullptr);
    SetBuildingArrayBindings(BuildingArrayBindings{});
}

TEST(Building2_E2E, TypeRecordAndStateMapping) {
    // Drive the static type-record lookups + the state mapper as the UI build
    // panel would: a type whose +0 state byte selects a UI state, with its
    // +356 type byte and +358/+361 profession bytes matched against code lists.
    std::vector<std::uint8_t> tt(589 * 30, 0);
    // gilde.exe 0x589906 / 0x589998: Building_MatchProfessionCode and
    // Building_MatchTypeCode read word_12CE910[268*a1] — the person/family array
    // (byte stride 536, personFamilyBase) — NOT the 589-stride building-type
    // table. Bind a separate person/family table for those two matchers.
    std::vector<std::uint8_t> pf(536 * 30, 0);
    BuildingArrayBindings b;
    b.buildingTypeBase = tt.data();
    b.personFamilyBase = pf.data();
    SetBuildingArrayBindings(b);

    // type 24 -> state 3 (ret 1); type 7 -> state 6 (ret 0). MapTypeToState
    // reads buildingTypeBase (589*typeCode + 0), gilde.exe 0x592a5c.
    tt[589 * 24 + 0] = 24;
    tt[589 * 7 + 0] = 7;
    std::uint8_t st = 0;
    CHECK_EQ(Building_MapTypeToState(24, &st), 1);
    CHECK_EQ(static_cast<int>(st), 3);
    CHECK_EQ(Building_MapTypeToState(7, &st), 0);
    CHECK_EQ(static_cast<int>(st), 6);

    // type 24 profession bytes; match against a candidate list. These live in the
    // person/family record (stride 536): +358 / +361 profession, +356 type byte.
    pf[536 * 24 + 358] = 0x0A;
    pf[536 * 24 + 361] = 0x0B;
    pf[536 * 24 + 356] = 0x2A;
    std::uint8_t profCodes[] = {0x01, 0x0B};
    CHECK_EQ(Building_MatchProfessionCode(24, 2, profCodes), 1);
    std::uint8_t typeCodes[] = {0x2A};
    CHECK_EQ(Building_MatchTypeCode(24, 1, typeCodes), 1);

    // Static table cross-check against the e2e build flow.
    TypeRecord r;
    Building_LookupTypeRecordB(1, &r);
    CHECK_EQ(r.dword0, 0x7e7e002au);

    SetBuildingArrayBindings(BuildingArrayBindings{});
}
