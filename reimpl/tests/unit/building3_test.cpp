#include "test.h"

// Unit tests for the building3 slice: open-hours / type-name / type-string-id
// table lookups, the scene-query occupant finders (against installed mock
// hooks), occupant-category sync (against the real BuildingType_GroupFromCode),
// and the lifecycle unlink/release/reset functions. Golden values precomputed
// from the recovered gilde.exe constant tables (see building3.cpp comments).
#include "sim/building3.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// 589-byte building-type record (dword_13CE294 stride). Field setters only for
// the bytes the building3 functions read.
struct B3TypeRec {
    std::uint8_t b[589];
    B3TypeRec() { std::memset(b, 0, sizeof b); }
    void Kind(std::uint8_t v)          { b[0] = v; }
    void Room(int i, std::uint16_t v)  { std::memcpy(b + 35 + 2 * i, &v, 2); }
};

// Object-type table (dword_13CE27C stride 65): +0 = category byte.
struct B3ObjTable {
    std::vector<std::uint8_t> t;
    explicit B3ObjTable(int n) : t(static_cast<size_t>(n) * 65, 0) {}
    void Cat(int id, std::uint8_t v) { t[static_cast<size_t>(id) * 65] = v; }
};

// Recording mock hooks for the query/scene/mem leaves.
struct B3Mock : Building3Hooks {
    struct QFCall { std::int32_t container; int ga, gb, flag; std::int32_t proto; };
    std::vector<QFCall> qf;
    std::int32_t qfReturn = 0;
    std::int32_t qfMatchActorReturn = 0;   // for the (groupA=2,groupB=6) probe

    std::int32_t GameObjectQueryFind(std::int32_t c, int ga, int gb, int flag,
                                     std::int32_t proto) override {
        qf.push_back({c, ga, gb, flag, proto});
        if (ga == 2 && gb == 6) return qfMatchActorReturn;
        return qfReturn;
    }

    std::vector<const std::uint8_t*> iterSeq;
    size_t iterPos = 0;
    const std::uint8_t* PersonQueryBegin(std::int32_t, int, int, std::int32_t) override {
        iterPos = 0;
        return iterPos < iterSeq.size() ? iterSeq[iterPos++] : nullptr;
    }
    const std::uint8_t* PersonIterNext() override {
        return iterPos < iterSeq.size() ? iterSeq[iterPos++] : nullptr;
    }

    int freeDebugCalls = 0;
    std::int32_t lastFreedPtr = 0;
    void MemoryFreeDebug(std::int32_t p, std::int32_t) override {
        ++freeDebugCalls; lastFreedPtr = p;
    }
    int freeChildCalls = 0;
    void GameObjectFreeChildList(std::int32_t*) override { ++freeChildCalls; }

    int changeActionCalls = 0, cancelEntityCalls = 0, cancelEventCalls = 0, releaseCalls = 0;
    std::uint16_t lastActor = 0;
    void CharacterChangePlayerAction(int, int, int, std::uint16_t a) override {
        ++changeActionCalls; lastActor = a;
    }
    void CharActionCancelEntityActions(const std::uint8_t*) override { ++cancelEntityCalls; }
    void EventCancelMatchingActors() override { ++cancelEventCalls; }
    std::int32_t OfficeReleaseCharacterHoldings(std::int32_t r) override {
        ++releaseCalls; return r;
    }

    int removeCalls = 0;
    void BuildingRemoveAndCleanup(int, int) override { ++removeCalls; }
    int lightCalls = 0;
    void LightSetGrayColorThunk(int, int, std::int16_t*) override { ++lightCalls; }

    std::vector<std::uint32_t*> heSeq;
    size_t hePos = 0;
    const std::uint32_t* HeFindFirstHandlerByFilter(int, int, int) override {
        hePos = 0;
        return hePos < heSeq.size() ? heSeq[hePos++] : nullptr;
    }
    const std::uint32_t* HeFindNextMatchingHandler() override {
        return hePos < heSeq.size() ? heSeq[hePos++] : nullptr;
    }
};

struct HookGuard {
    HookGuard(Building3Hooks* h) { SetBuilding3Hooks(h); }
    ~HookGuard() { SetBuilding3Hooks(nullptr); }
};

}  // namespace

// ---------------------------------------------------------------------------
// CheckTimeWindowOpen — open-hours table.
// ---------------------------------------------------------------------------
TEST(Building3, TimeWindow_InsideAndOutside) {
    SetBuilding3GameHour(7);
    int o = -1, c = -1;
    CHECK(Building3_CheckTimeWindowOpen(0x06, &o, &c));   // 0x06 open 5 close 9, hour 7
    CHECK_EQ(o, 5);
    CHECK_EQ(c, 9);

    SetBuilding3GameHour(4);
    CHECK(!Building3_CheckTimeWindowOpen(0x06, nullptr, nullptr));   // before open
    SetBuilding3GameHour(9);
    CHECK(!Building3_CheckTimeWindowOpen(0x06, nullptr, nullptr));   // at close (>=)
}

TEST(Building3, TimeWindow_FirstMatchWins) {
    SetBuilding3GameHour(6);
    int o = -1, c = -1;
    // 0x14 first appears as record {0x14, 6, 7}; that record wins.
    CHECK(Building3_CheckTimeWindowOpen(0x14, &o, &c));
    CHECK_EQ(o, 6);
    CHECK_EQ(c, 7);
}

TEST(Building3, TimeWindow_AbsentIsAlwaysOpen) {
    SetBuilding3GameHour(0);
    CHECK(Building3_CheckTimeWindowOpen(0x99, nullptr, nullptr));    // not in table
}

// ---------------------------------------------------------------------------
// LookupTypeName — 12x12 code grid.
// ---------------------------------------------------------------------------
TEST(Building3, LookupTypeName_Hits) {
    char name[40] = {0};
    CHECK_EQ(Building3_LookupTypeName(0x08, name), 1);
    CHECK_EQ(std::strcmp(name, "bk_BRUNNEN"), 0);

    std::memset(name, 0, sizeof name);
    CHECK_EQ(Building3_LookupTypeName(0x14, name), 1);              // -> row 4
    CHECK_EQ(std::strcmp(name, "bk_KIRCHE_KL"), 0);

    std::memset(name, 0, sizeof name);
    CHECK_EQ(Building3_LookupTypeName(0x04, name), 1);              // -> row 9
    CHECK_EQ(std::strcmp(name, "bk_WOHNSITZ_KL"), 0);

    std::memset(name, 0, sizeof name);
    CHECK_EQ(Building3_LookupTypeName(0x43, name), 1);              // -> row 11
    CHECK_EQ(std::strcmp(name, "bk_ZUNFTHAUS"), 0);
}

TEST(Building3, LookupTypeName_Miss) {
    char name[40];
    std::memset(name, 0x7e, sizeof name);
    CHECK_EQ(Building3_LookupTypeName(0xF0, name), 0);             // no such code
}

// ---------------------------------------------------------------------------
// LookupTypeStringId — type/string pair table, container-only.
// ---------------------------------------------------------------------------
TEST(Building3, LookupTypeStringId_ContainerHits) {
    CHECK_EQ(Building3_LookupTypeStringId(0x13, 2), 1 + 1433);     // 1434
    CHECK_EQ(Building3_LookupTypeStringId(0x54, 2), 2 + 1433);     // 1435
    CHECK_EQ(Building3_LookupTypeStringId(0x14, 2), 9 + 1433);     // 1442
}

TEST(Building3, LookupTypeStringId_NonContainerAndMiss) {
    CHECK_EQ(Building3_LookupTypeStringId(0x13, 6), -1);          // not a container
    CHECK_EQ(Building3_LookupTypeStringId(0x4242, 2), -1);        // not in table
}

// ---------------------------------------------------------------------------
// FindWorkProductObject — category routing + room scan (hooked scene).
// ---------------------------------------------------------------------------
TEST(Building3, FindWorkProduct_Kind22QueriesProto302) {
    B3TypeRec tr; tr.Kind(0x16);            // kind 22 -> category 1
    Building3Arrays a; a.buildingTypeBase = nullptr; SetBuilding3Arrays(a);

    // Build a one-entry type table so TypeRec(0) is `tr`.
    std::vector<std::uint8_t> typeTbl(589, 0);
    std::memcpy(typeTbl.data(), tr.b, 589);
    a.buildingTypeBase = typeTbl.data();
    SetBuilding3Arrays(a);

    B3Mock m; m.qfReturn = 7777; HookGuard g(&m);
    std::uint8_t building[200]; std::memset(building, 0, sizeof building);
    building[0] = 0;                         // type index 0 -> kind 22
    std::int32_t containerVal = 0x1234; std::memcpy(building + 93, &containerVal, 4);

    std::int32_t r = Building3_FindWorkProductObject(building);
    CHECK_EQ(r, 7777);
    if (!m.qf.empty()) {
        CHECK_EQ(m.qf[0].proto, 302);
        CHECK_EQ(m.qf[0].container, 0x1234);
    }
    SetBuilding3Arrays(Building3Arrays{});
}

TEST(Building3, FindWorkProduct_ProductionKindProto253) {
    std::vector<std::uint8_t> typeTbl(589, 0);
    typeTbl[0] = 11;                         // kind 11 -> production, category 0
    Building3Arrays a; a.buildingTypeBase = typeTbl.data(); SetBuilding3Arrays(a);

    B3Mock m; m.qfReturn = 42; HookGuard g(&m);
    std::uint8_t building[200]; std::memset(building, 0, sizeof building);
    std::int32_t r = Building3_FindWorkProductObject(building);
    CHECK_EQ(r, 42);
    if (!m.qf.empty()) CHECK_EQ(m.qf[0].proto, 253);
    SetBuilding3Arrays(Building3Arrays{});
}

TEST(Building3, FindWorkProduct_NullSafe) {
    B3Mock m; HookGuard g(&m);
    CHECK_EQ(Building3_FindWorkProductObject(nullptr), 0);
}

// ---------------------------------------------------------------------------
// FindActiveWorkSlot — six-proto probe + actor match -> proto 278.
// ---------------------------------------------------------------------------
TEST(Building3, FindActiveWorkSlot_MatchOnSecondSlot) {
    B3Mock m; HookGuard g(&m);
    // Slots resolve to non-zero for protos 146..151; the actor-match probe
    // (groupA=2) succeeds, so the final 278 query returns 555.
    m.qfReturn = 100;
    m.qfMatchActorReturn = 1;
    m.qf.clear();
    std::int32_t r = Building3_FindActiveWorkSlot(0x900, 12);
    // First six calls populate slots; then slot[0] active-probe hits and the
    // 278 query (qfReturn=100) is returned.
    CHECK_EQ(r, 100);
}

TEST(Building3, FindActiveWorkSlot_NoSelectedBuilding) {
    B3Mock m; HookGuard g(&m);
    CHECK_EQ(Building3_FindActiveWorkSlot(0, 5), 0);
}

// ---------------------------------------------------------------------------
// FindNearestVacantSameType — person iteration + distance pick.
// ---------------------------------------------------------------------------
TEST(Building3, FindNearestVacant_PicksClosestVacant) {
    // Two candidate records of the same type byte; both vacant; the nearer wins.
    auto makeRec = [](std::uint8_t type, std::uint16_t owner, float x) {
        auto* rec = new std::uint8_t[300];
        std::memset(rec, 0, 300);
        rec[0] = type;
        std::memcpy(rec + 39, &owner, 2);
        auto* mesh = new float[40];
        std::memset(mesh, 0, 40 * sizeof(float));
        mesh[19] = x;                    // +76 byte offset (76/4 = 19)
        std::int32_t meshPtr = static_cast<std::int32_t>(
            reinterpret_cast<std::intptr_t>(mesh) & 0xffffffff);
        // store full pointer at +97 (we read it back via memcpy of intptr-sized)
        std::memcpy(rec + 97, &mesh, sizeof(mesh));
        (void)meshPtr;
        return rec;
    };
    // self record at origin x=0.
    std::uint8_t* self = makeRec(7, 0xFFFF, 0.0f);

    std::uint8_t* far  = makeRec(7, 0xFFFF, 100.0f);
    std::uint8_t* near = makeRec(7, 0xFFFF, 10.0f);

    B3Mock m; HookGuard g(&m);
    m.iterSeq = { far, near };

    Building3Arrays a; SetBuilding3Arrays(a);    // null base -> TypeRec null, kind checks skipped
    const std::uint8_t* r = Building3_FindNearestVacantSameType(7, 0x55, self);
    CHECK(r == near);

    SetBuilding3Arrays(Building3Arrays{});
    // cleanup
    for (std::uint8_t* p : {self, far, near}) {
        float* mp; std::memcpy(&mp, p + 97, sizeof(mp)); delete[] mp; delete[] p;
    }
}

// ---------------------------------------------------------------------------
// UpdateOccupantCategory — wired to the real BuildingType_GroupFromCode.
// ---------------------------------------------------------------------------
TEST(Building3, UpdateOccupantCategory_FamilyClassWrites) {
    // class 6 with a family record: writes directCat into the GroupFromCode slot.
    std::uint8_t family[256]; std::memset(family, 0, sizeof family);
    int famCat = -1, dirty = 0; std::uint8_t outDirect = 0;
    // recCode 1 -> BuildingType_GroupFromCode(1) == 11.
    int token = Building3_UpdateOccupantCategory(3, /*recCode*/1, /*class*/6,
                                                 /*directCat*/9, family,
                                                 &famCat, &dirty, &outDirect);
    CHECK_EQ(token, 67 * 3);
    CHECK_EQ(dirty, 1);
    CHECK_EQ(family[112 + 11], 9);          // group 11 slot
    CHECK_EQ(famCat, 9);
}

TEST(Building3, UpdateOccupantCategory_NoFamily) {
    int famCat = -1, dirty = 0; std::uint8_t outDirect = 0;
    int token = Building3_UpdateOccupantCategory(5, 1, 6, 4, nullptr,
                                                 &famCat, &dirty, &outDirect);
    CHECK_EQ(token, 0);
    CHECK_EQ(dirty, 0);
    CHECK_EQ((int)outDirect, 4);
}

TEST(Building3, UpdateOccupantCategory_OtherClass) {
    std::uint8_t family[256]; std::memset(family, 0, sizeof family);
    int dirty = 0; std::uint8_t outDirect = 0;
    int token = Building3_UpdateOccupantCategory(2, 1, /*class*/3, 7, family,
                                                 nullptr, &dirty, &outDirect);
    CHECK_EQ(token, 67 * 2);
    CHECK_EQ(dirty, 0);
    CHECK_EQ((int)outDirect, 7);
}

// ---------------------------------------------------------------------------
// FreeAndUnlink — validation, type-30 buffer free, index-array unlink.
// ---------------------------------------------------------------------------
TEST(Building3, FreeAndUnlink_NullAndZeroType) {
    B3Mock m; HookGuard g(&m);
    CHECK_EQ(Building3_FreeAndUnlink(nullptr, 1, nullptr, nullptr, 0), -1);
    std::uint8_t rec[200]; std::memset(rec, 0, sizeof rec);  // type byte 0
    CHECK_EQ(Building3_FreeAndUnlink(rec, 1, nullptr, nullptr, 0), -2);
}

TEST(Building3, FreeAndUnlink_Type30FreesAndUnlinks) {
    B3Mock m; HookGuard g(&m);
    std::uint8_t rec[200]; std::memset(rec, 0, sizeof rec);
    rec[0] = 30;
    std::int32_t buf = 0xCAFE; std::memcpy(rec + 113, &buf, 4);
    std::int32_t child = 1; std::memcpy(rec + 93, &child, 4);

    // Two index arrays, 3 slots each (stride 134).
    std::vector<std::int32_t> ia(3 * 134, 0), ib(3 * 134, 0);
    std::int32_t recId = 0x777;
    ia[1 * 134] = recId;
    ib[2 * 134] = recId;

    int rc = Building3_FreeAndUnlink(rec, recId, ia.data(), ib.data(), 3);
    CHECK_EQ(rc, 0);
    CHECK_EQ(m.freeDebugCalls, 1);
    CHECK_EQ(m.lastFreedPtr, (std::int32_t)0xCAFE);
    CHECK_EQ(m.freeChildCalls, 1);
    CHECK_EQ(ia[1 * 134], 0);                // unlinked
    CHECK_EQ(ib[2 * 134], 0);                // unlinked
    CHECK_EQ((int)rec[0], 0);                // type cleared
}

// ---------------------------------------------------------------------------
// ReleaseOccupantHoldings — valid actor cancels everything.
// ---------------------------------------------------------------------------
TEST(Building3, ReleaseOccupant_ValidActor) {
    B3Mock m; HookGuard g(&m);
    std::uint8_t rec[80]; std::memset(rec, 0, sizeof rec);
    std::uint16_t actor = 42; std::memcpy(rec, &actor, 2);
    const std::uint8_t* r = Building3_ReleaseOccupantHoldings(rec, actor);
    CHECK(r == rec);
    CHECK_EQ(m.changeActionCalls, 1);
    CHECK_EQ((int)m.lastActor, 42);
    CHECK_EQ(m.cancelEntityCalls, 1);
    CHECK_EQ(m.cancelEventCalls, 1);
    CHECK_EQ(m.releaseCalls, 1);
}

TEST(Building3, ReleaseOccupant_InvalidActorNoop) {
    B3Mock m; HookGuard g(&m);
    std::uint8_t rec[80]; std::memset(rec, 0, sizeof rec);
    const std::uint8_t* r = Building3_ReleaseOccupantHoldings(rec, 0xFFFF);
    CHECK(r == rec);
    CHECK_EQ(m.changeActionCalls, 0);
    CHECK_EQ(m.releaseCalls, 0);
}

// ---------------------------------------------------------------------------
// CollectByCityHandle / CollectOwnedByPerson — handler enumeration cap at 9.
// ---------------------------------------------------------------------------
TEST(Building3, CollectByCityHandle_FiltersByCityField) {
    B3Mock m; HookGuard g(&m);
    // three handlers; +44 = city handle, +43 = sub-filter.
    std::uint32_t h0[48] = {0}, h1[48] = {0}, h2[48] = {0};
    h0[44] = 100; h0[43] = 5;
    h1[44] = 999; h1[43] = 5;    // wrong city
    h2[44] = 100; h2[43] = 5;
    m.heSeq = { h0, h1, h2 };

    std::uint32_t* out[9] = {0};
    int n = Building3_CollectByCityHandle(100, nullptr, out);
    CHECK_EQ(n, 2);
    CHECK(out[0] == h0);
    CHECK(out[1] == h2);
}

TEST(Building3, CollectByCityHandle_NoHandlers) {
    B3Mock m; HookGuard g(&m);
    std::uint32_t* out[9] = {0};
    CHECK_EQ(Building3_CollectByCityHandle(1, nullptr, out), 0);
}

// ---------------------------------------------------------------------------
// ResetAllBuildings — 768 removes, 16 light resets, selection clears.
// ---------------------------------------------------------------------------
TEST(Building3, ResetAllBuildings_CountsAndClears) {
    B3Mock m; HookGuard g(&m);
    std::vector<std::int16_t> lights(16 * 82, 7);
    int s0 = 1, s1 = 2, s2 = 3;
    Building3_ResetAllBuildings(lights.data(), &s0, &s1, &s2);
    CHECK_EQ(m.removeCalls, 768);
    CHECK_EQ(m.lightCalls, 16);
    CHECK_EQ((int)lights[0], -1);
    CHECK_EQ((int)lights[82], -1);
    CHECK_EQ(s0, 0);
    CHECK_EQ(s1, 0);
    CHECK_EQ(s2, 0);
}
