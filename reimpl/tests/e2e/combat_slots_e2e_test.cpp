// End-to-end flow across the combat slot / target-selection leaves this module
// owns: allocate squad slots from the person/character slot table, register the
// allocated units in an order block and look one up by entity, then drive the
// active-target ring picker and resolve the chosen entry to a scene object.
#include "sim/combat_slots.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
struct E2EPolicy : ISlotPolicy {
    int IsTypeInGroup(u8) override { return 2; }     // wildcard: any group
    int ClassifyTypeFlag(u8) override { return 2; }  // wildcard
    int RelationMatrix(u16, int) override { return 0; } // friendly (> -30)
};
struct E2EQuery : IObjectQuery {
    const void* obj;
    int sawKind = -1;
    explicit E2EQuery(const void* o) : obj(o) {}
    const void* QueryFind(i32, int, int kind) override {
        sawKind = kind;
        return obj;   // first query hits
    }
};
} // namespace

TEST(CombatSlotsE2E, SlotAllocationAndTargetFlow) {
    // --- 1. Build the full 768-slot person/character table (the allocator scans
    //        all kSlotCapacity slots, as in the original). Configure the first 5;
    //        only slots full enough (fillCount >= requiredCap) qualify. ---
    std::vector<SlotRecord> table(kSlotCapacity);
    std::memset(table.data(), 0, table.size() * sizeof(SlotRecord));
    for (auto& s : table) s.marker = -1;            // empty by default
    for (int i = 0; i < 5; ++i) {
        SlotRecord& s = table[i];
        s.marker = 0;
        s.ownerClassByte = 0x10;
        s.groupClass = 1;
        s.fillCount = static_cast<u16>(i);          // 0,1,2,3,4
        s.requiredCap = 3.0f;                       // need fill >= 3
    }

    E2EPolicy pol;
    // --- 2. First allocation: slots 0..2 are underfull (fill < 3), slot 3 is the
    //        first full one (3 >= 3). ---
    SlotRecord* a = FindAvailableSquadSlot(table.data(), 0xFFFF, 2, 0x10, pol);
    CHECK(a == &table[3]);
    CHECK((a->flags & kSlotClaimed) != 0);

    // --- 3. Re-allocating skips the now-claimed slot 3 -> slot 4. ---
    SlotRecord* b = FindAvailableSquadSlot(table.data(), 0xFFFF, 2, 0x10, pol);
    CHECK(b == &table[4]);

    // --- 4. A third allocation finds nothing (0..2 underfull, 3/4 claimed). ---
    SlotRecord* c = FindAvailableSquadSlot(table.data(), 0xFFFF, 2, 0x10, pol);
    CHECK(c == nullptr);

    // --- 5. Register the two allocated unit ids in an order block, then look one
    //        up by entity (the order-issuing layer's bookkeeping). ---
    std::vector<i32> orderBlock(kSquadBlockDwords, 0);
    orderBlock[kOrderSlotDwordStride * 0 + kOrderSlotBase] = 300;
    orderBlock[kOrderSlotDwordStride * 1 + kOrderSlotBase] = 400;
    const i32* found = FindUnitByEntity(orderBlock.data(), 1, 400);
    CHECK(found == &orderBlock[kOrderSlotDwordStride * 1 + kOrderSlotBase]);
    CHECK(FindUnitByEntity(orderBlock.data(), 1, 555) == nullptr);

    // --- 6. Active-target ring: round-robin pick of an acceptable target. ---
    std::vector<u8> ring(kActiveRingStride * kActiveRingCount, 0);
    std::vector<u8> types(kTypeTableStride * 256, 0);
    int cursor = 0;
    u8* e3 = ring.data() + kActiveRingStride * 3;
    e3[kEntryPersonIdxOff] = 5;                 // person-type index 5
    i32 handle = 0x1234ABCD;
    std::memcpy(e3 + kEntryHandleOff, &handle, sizeof(handle));   // entry +93 handle
    // entry dword +1 == typeId payload
    i32 typeIdPayload = 0x00ABCDEF;
    std::memcpy(e3 + 1, &typeIdPayload, sizeof(typeIdPayload));
    types[kTypeTableStride * 5] = 22;           // acceptable ring type code

    ActiveRing aring{ring.data(), types.data(), &cursor};
    ActiveTargetResult pick = PickActiveTargetEntry(aring);
    CHECK(pick.entry == e3);
    CHECK_EQ(pick.entityRef, -1);
    CHECK_EQ(cursor, 4);                        // advanced past entry 3

    // --- 7. Resolve the picked entry to a scene object. The entry's person index
    //        (5) selects type code 2 in the type table -> the kind-19 path. ---
    types[kTypeTableStride * 5] = 2;            // type code 2 for the resolver
    int sceneObject = 0;
    E2EQuery query(&sceneObject);
    const void* resolved = ResolveTargetObjekt(pick.entry, types.data(), query);
    CHECK(resolved == &sceneObject);
    CHECK_EQ(query.sawKind, 19);                // type code 2 -> kind 19

    // --- 8. After the only acceptable entry is cleared, a fresh pick exhausts
    //        the ring and returns nullptr. ---
    e3[kEntryPersonIdxOff] = 0;
    ActiveTargetResult pick2 = PickActiveTargetEntry(aring);
    CHECK(pick2.entry == nullptr);
}
