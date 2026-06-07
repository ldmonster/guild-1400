// Unit tests for the combat slot / target-selection leaves (combat_slots.cpp).
// Golden vectors computed with a python reference model of the Hex-Rays
// pseudocode (see the implementer report).
//
// NOTE: IsTargetUnderfull / CountActiveSlots / AssignGuardTarget / GetSelectionFlag
// / SelectBeatingTarget live in combat_orders.cpp and are tested there; this
// module owns the remaining leaves (FindUnitByEntity, GetUnitTarget,
// ResolveTargetObjekt, PickActiveTargetEntry, FindAvailableSquadSlot).
#include "sim/combat_slots.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// --------------------------------------------------------------------------
// FindUnitByEntity  (0x485ffc)
// --------------------------------------------------------------------------
TEST(CombatSlots, FindUnitByEntity) {
    // Two squad blocks, each 215 dwords.
    std::vector<i32> tbl(2 * kSquadBlockDwords, 0);
    auto slotAt = [&](int squad, int slot) -> int {
        return squad * kSquadBlockDwords + kOrderSlotDwordStride * slot + kOrderSlotBase;
    };
    tbl[slotAt(1, 5)] = 4242;   // id 4242 in squad 1, slot 5
    tbl[slotAt(0, 0)] = 7;      // id 7 in squad 0, slot 0

    CHECK(FindUnitByEntity(tbl.data(), 2, 4242) == &tbl[slotAt(1, 5)]);
    CHECK(FindUnitByEntity(tbl.data(), 2, 7) == &tbl[slotAt(0, 0)]);
    CHECK(FindUnitByEntity(tbl.data(), 2, 9999) == nullptr);   // absent
    // present but in a squad beyond activeSquads -> not found
    CHECK(FindUnitByEntity(tbl.data(), 1, 4242) == nullptr);
}

// --------------------------------------------------------------------------
// GetUnitTarget  (0x487090)
// --------------------------------------------------------------------------
TEST(CombatSlots, GetUnitTarget) {
    std::vector<u8> unit(600, 0);
    i32 want = 0x0BADF00D;
    std::memcpy(unit.data() + 512, &want, sizeof(want));
    CHECK_EQ(GetUnitTarget(unit.data()), want);
}

// --------------------------------------------------------------------------
// ResolveTargetObjekt  (0x57e92c)
// --------------------------------------------------------------------------
namespace {
struct RecordingQuery : IObjectQuery {
    struct Call { i32 handle; int prev; int kind; };
    std::vector<Call> calls;
    // Map of kind -> pointer to return (nullptr if absent).
    const void* ret19 = nullptr;
    const void* ret18 = nullptr;
    const void* ret288 = nullptr;
    const void* QueryFind(i32 handle, int prev, int kind) override {
        calls.push_back({handle, prev, kind});
        if (kind == 19) return ret19;
        if (kind == 18) return ret18;
        if (kind == 288) return ret288;
        return nullptr;
    }
};
// Build an entry with personIdx byte and a handle dword at +93.
std::vector<u8> MakeEntry(u8 personIdx, i32 handle) {
    std::vector<u8> e(kActiveRingStride, 0);
    e[kEntryPersonIdxOff] = personIdx;
    std::memcpy(e.data() + kEntryHandleOff, &handle, sizeof(handle));
    return e;
}
} // namespace

TEST(CombatSlots, ResolveTargetObjektTypeCode2PrefersKind19) {
    auto entry = MakeEntry(/*personIdx*/ 4, /*handle*/ 0xCAFE);
    std::vector<u8> types(kTypeTableStride * 256, 0);
    types[kTypeTableStride * 4] = 2;   // type code 2 -> kind 19 path
    int objA = 0;
    RecordingQuery q;
    q.ret19 = &objA;
    const void* r = ResolveTargetObjekt(entry.data(), types.data(), q);
    CHECK(r == &objA);
    CHECK_EQ(q.calls.size(), (size_t)1);
    CHECK_EQ(q.calls[0].kind, 19);
    CHECK_EQ(q.calls[0].handle, (i32)0xCAFE);
}

TEST(CombatSlots, ResolveTargetObjektTypeCode2FallbackTo288) {
    auto entry = MakeEntry(4, 0xCAFE);
    std::vector<u8> types(kTypeTableStride * 256, 0);
    types[kTypeTableStride * 4] = 2;
    int objFallback = 0;
    RecordingQuery q;
    q.ret19 = nullptr;          // 19 misses
    q.ret288 = &objFallback;    // 288 hits
    const void* r = ResolveTargetObjekt(entry.data(), types.data(), q);
    CHECK(r == &objFallback);
    CHECK_EQ(q.calls.size(), (size_t)2);
    CHECK_EQ(q.calls[0].kind, 19);
    CHECK_EQ(q.calls[1].kind, 288);
}

TEST(CombatSlots, ResolveTargetObjektOtherTypePrefersKind18) {
    auto entry = MakeEntry(9, 0x1111);
    std::vector<u8> types(kTypeTableStride * 256, 0);
    types[kTypeTableStride * 9] = 7;   // not 2 -> kind 18 path
    int objB = 0;
    RecordingQuery q;
    q.ret18 = &objB;
    const void* r = ResolveTargetObjekt(entry.data(), types.data(), q);
    CHECK(r == &objB);
    CHECK_EQ(q.calls.size(), (size_t)1);
    CHECK_EQ(q.calls[0].kind, 18);
}

// --------------------------------------------------------------------------
// PickActiveTargetEntry  (0x57e9a4) — golden vectors from the python model.
// --------------------------------------------------------------------------
namespace {
struct RingFixture {
    std::vector<u8> ring;
    std::vector<u8> typeTable;
    int cursor = 0;
    RingFixture()
        : ring(kActiveRingStride * kActiveRingCount, 0),
          typeTable(kTypeTableStride * 256, 0) {}
    void SetEntry(int idx, u8 typeIndexByte, i32 dword1) {
        u8* e = ring.data() + kActiveRingStride * idx;
        e[0] = typeIndexByte;
        std::memcpy(e + 1, &dword1, sizeof(dword1));
    }
    void SetTypeCode(int signedIdx, u8 code) {
        typeTable[kTypeTableStride * signedIdx] = code;
    }
    ActiveRing View() { return ActiveRing{ring.data(), typeTable.data(), &cursor}; }
};
} // namespace

TEST(CombatSlots, PickActiveRingV1) {
    RingFixture f;
    f.cursor = 0;
    f.SetEntry(5, 7, 0x11223344);
    f.SetTypeCode(7, 22);
    ActiveRing r = f.View();
    ActiveTargetResult res = PickActiveTargetEntry(r);
    CHECK(res.entry == f.ring.data() + kActiveRingStride * 5);
    CHECK_EQ(res.entityRef, -1);
    CHECK_EQ(res.typeId, (i32)287454020);   // 0x11223344
    CHECK_EQ(f.cursor, 6);
}

TEST(CombatSlots, PickActiveRingV2Wrap) {
    RingFixture f;
    f.cursor = 250;
    f.SetEntry(252, 9, 0x55);
    f.SetTypeCode(9, 15);
    ActiveRing r = f.View();
    ActiveTargetResult res = PickActiveTargetEntry(r);
    CHECK(res.entry == f.ring.data() + kActiveRingStride * 252);
    CHECK_EQ(res.entityRef, -1);
    CHECK_EQ(res.typeId, (i32)85);
    CHECK_EQ(f.cursor, 253);
}

TEST(CombatSlots, PickActiveRingV3None) {
    RingFixture f;
    f.cursor = 0;
    f.SetEntry(10, 3, 0xDEAD);
    f.SetTypeCode(3, 99);   // not in {3,22,15}
    ActiveRing r = f.View();
    ActiveTargetResult res = PickActiveTargetEntry(r);
    CHECK(res.entry == nullptr);
    CHECK_EQ(f.cursor, 0);  // cursor untouched
}

TEST(CombatSlots, PickActiveRingV4FullWrap) {
    RingFixture f;
    f.cursor = 100;
    f.SetEntry(99, 1, 0x7FFFFFFF);   // reached on the last (256th) try
    f.SetTypeCode(1, 3);
    ActiveRing r = f.View();
    ActiveTargetResult res = PickActiveTargetEntry(r);
    CHECK(res.entry == f.ring.data() + kActiveRingStride * 99);
    CHECK_EQ(res.typeId, (i32)0x7FFFFFFF);
    CHECK_EQ(f.cursor, 100);   // 99 + 1
}

// --------------------------------------------------------------------------
// FindAvailableSquadSlot  (0x57e76c)
// --------------------------------------------------------------------------
namespace {
struct SlotStubPolicy : ISlotPolicy {
    int typeGroupResult = 0;
    int classifyResult = 0;
    int relationResult = 0;
    int IsTypeInGroup(u8) override { return typeGroupResult; }
    int ClassifyTypeFlag(u8) override { return classifyResult; }
    int RelationMatrix(u16, int) override { return relationResult; }
};
SlotRecord MakeFreeSlot() {
    SlotRecord s;
    std::memset(&s, 0, sizeof(s));
    s.marker = 0;          // != -1 => occupied/usable
    return s;
}
// The original scans the full static 768-slot table; build one with all slots
// empty (marker -1) so only the explicitly-configured slots qualify.
std::vector<SlotRecord> MakeTable() {
    std::vector<SlotRecord> tbl(kSlotCapacity);
    std::memset(tbl.data(), 0, tbl.size() * sizeof(SlotRecord));
    for (auto& s : tbl) s.marker = -1;
    return tbl;
}
} // namespace

TEST(CombatSlots, FindSquadSlotWantTypeMatch) {
    std::vector<SlotRecord> tbl = MakeTable();
    tbl[2] = MakeFreeSlot();
    tbl[2].fillCount = 5; tbl[2].requiredCap = 5.0f;  // 5 >= 5 ok
    tbl[2].ownerClassByte = 0x42;                     // == wantFlag
    tbl[2].reservedBy = 0;                            // unreserved ok
    tbl[2].groupClass = 9;
    SlotStubPolicy pol;
    pol.typeGroupResult = 9;
    SlotRecord* r = FindAvailableSquadSlot(tbl.data(), 0xFFFF, 7, 0x42, pol);
    CHECK(r == &tbl[2]);
    CHECK((r->flags & kSlotClaimed) != 0);
}

TEST(CombatSlots, FindSquadSlotWildcardGroup) {
    std::vector<SlotRecord> tbl = MakeTable();
    tbl[1] = MakeFreeSlot();
    tbl[1].fillCount = 2; tbl[1].requiredCap = 2.0f;
    tbl[1].ownerClassByte = 5;
    tbl[1].groupClass = 77;   // mismatched, but wildcard (g==2) accepts
    SlotStubPolicy pol;
    pol.typeGroupResult = 2;
    SlotRecord* r = FindAvailableSquadSlot(tbl.data(), 0xFFFF, 1, 5, pol);
    CHECK(r == &tbl[1]);
}

TEST(CombatSlots, FindSquadSlotRejectClaimedAndRelation) {
    std::vector<SlotRecord> tbl = MakeTable();
    for (int i = 0; i < 2; ++i) {
        tbl[i] = MakeFreeSlot();
        tbl[i].fillCount = 3; tbl[i].requiredCap = 1.0f;
        tbl[i].ownerClassByte = 1; tbl[i].groupClass = 4;
    }
    tbl[0].flags = kSlotClaimed;   // already claimed -> skip
    SlotStubPolicy pol;
    pol.typeGroupResult = 4;
    pol.relationResult = -50;      // <= -30 -> hostile reject when rel enabled
    CHECK(FindAvailableSquadSlot(tbl.data(), /*rel*/ 5, 2, 1, pol) == nullptr);
    // With relation disabled (0xFFFF), slot 1 qualifies.
    CHECK(FindAvailableSquadSlot(tbl.data(), 0xFFFF, 2, 1, pol) == &tbl[1]);
}

TEST(CombatSlots, FindSquadSlotFlagOnlyBranch) {
    std::vector<SlotRecord> tbl = MakeTable();
    tbl[2] = MakeFreeSlot();
    tbl[2].ownerClassByte = 0x33;
    tbl[2].groupClass = 12;
    tbl[2].fillCount = 0; tbl[2].requiredCap = 99.0f;  // ignored in this branch
    SlotStubPolicy pol;
    pol.classifyResult = 12;
    SlotRecord* r = FindAvailableSquadSlot(tbl.data(), 0xFFFF, /*wantType*/ 0,
                                           /*wantFlag*/ 0x33, pol);
    CHECK(r == &tbl[2]);
}

TEST(CombatSlots, FindSquadSlotBothZero) {
    std::vector<SlotRecord> tbl = MakeTable();
    SlotStubPolicy pol;
    CHECK(FindAvailableSquadSlot(tbl.data(), 0xFFFF, 0, 0, pol) == nullptr);
}
