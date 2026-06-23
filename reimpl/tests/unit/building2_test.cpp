// Unit tests for src/sim/building2.{h,cpp} — building query / type-record /
// room-slot collectors. Golden vectors computed from the recovered tables and a
// synthetic type/object/char array populated to drive each branch.
#include "sim/building2.h"

#include <cstring>
#include <vector>

#include "test.h"

using namespace guild::sim;

namespace {

// Build a synthetic building-TYPE table (stride 589). `types` is a list of
// (kind/state byte, roomList words, +34 slotCount, +356 typeByte, +358/+361 prof).
struct TypeRecBuilder {
    std::vector<std::uint8_t> buf;
    explicit TypeRecBuilder(int n) : buf(589 * n, 0) {}
    std::uint8_t* rec(int i) { return buf.data() + 589 * i; }
    void setKind(int i, std::uint8_t k) { rec(i)[0] = k; }
    void setSlotCount(int i, std::uint8_t c) { rec(i)[34] = c; }
    void setRoom(int i, int slot, std::uint16_t v) {
        std::memcpy(rec(i) + 35 + 2 * slot, &v, 2);
    }
};

// Build a synthetic person/family record table (gilde.exe word_12CE910, byte
// stride 536) read by MatchTypeCode/MatchProfessionCode at fields +356/+358/+361.
struct PersonRecBuilder {
    std::vector<std::uint8_t> buf;
    explicit PersonRecBuilder(int n) : buf(536 * n, 0) {}
    std::uint8_t* rec(int i) { return buf.data() + 536 * i; }
    void setTypeByte(int i, std::uint8_t v) { rec(i)[356] = v; }
    void setProf(int i, std::uint8_t p0, std::uint8_t p1) {
        rec(i)[358] = p0; rec(i)[361] = p1;
    }
};

// 65-stride object-type table: byte +0 == category.
struct ObjTableBuilder {
    std::vector<std::uint8_t> buf;
    explicit ObjTableBuilder(int n) : buf(65 * n, 0) {}
    void setCat(int id, std::uint8_t c) { buf[65 * id] = c; }
};

// Install bindings for a test and restore on scope exit.
struct BindScope {
    BindScope(const std::uint8_t* type, const std::uint8_t* obj,
              const std::uint8_t* chr, const std::uint8_t* person = nullptr) {
        BuildingArrayBindings b;
        b.buildingTypeBase = type;
        b.objectTypeBase   = obj;
        b.charArrayBase    = chr;
        b.personFamilyBase = person;
        SetBuildingArrayBindings(b);
    }
    ~BindScope() { SetBuildingArrayBindings(BuildingArrayBindings{}); }
};

// Mock query hook: GameObjectQueryFind returns a deterministic handle keyed by
// (containerHandle, protoId); PersonQueryByGoodType returns a scripted result.
struct MockQuery : IBuilding2QueryHooks {
    std::int32_t personContainer = -1;
    std::uint8_t personKind = 0;
    int findCalls = 0;
    std::int32_t lastProto = -999;

    std::int16_t GameObjectQueryFind(std::int32_t container, int, int, int,
                                     std::int32_t proto) override {
        ++findCalls;
        lastProto = proto;
        // Return (container ^ proto) low 16 bits, but 0 for proto == 999 (miss).
        if (proto == 999) return 0;
        return static_cast<std::int16_t>((container + proto) & 0x7FFF);
    }
    std::int32_t PersonQueryByGoodType(std::uint8_t, int, std::uint8_t* kindOut) override {
        if (kindOut) *kindOut = personKind;
        return personContainer;
    }
};

struct HookScope {
    explicit HookScope(IBuilding2QueryHooks* h) { SetBuilding2QueryHooks(h); }
    ~HookScope() { SetBuilding2QueryHooks(nullptr); }
};

}  // namespace

// ---------------------------------------------------------------------------
// Type-record table lookups (golden vectors from kTypeRecordTableA/B).
// ---------------------------------------------------------------------------
TEST(Building2_LookupTypeRecord, GoldenA) {
    TypeRecord r;
    Building_LookupTypeRecordA(0, &r);
    CHECK_EQ(r.dword0, 0u);
    CHECK_EQ(r.word4, 0u);

    Building_LookupTypeRecordA(1, &r);
    CHECK_EQ(r.dword0, 0x93bd6969u);
    CHECK_EQ(r.word4, 0x0469u);

    Building_LookupTypeRecordA(2, &r);
    CHECK_EQ(r.dword0, 0x69933f3fu);
    CHECK_EQ(r.word4, 0x033fu);

    // Out-of-range falls back to record 0 (all zero).
    Building_LookupTypeRecordA(76, &r);
    CHECK_EQ(r.dword0, 0u);
    CHECK_EQ(r.word4, 0u);
    Building_LookupTypeRecordA(200, &r);
    CHECK_EQ(r.dword0, 0u);
}

TEST(Building2_LookupTypeRecord, GoldenB) {
    TypeRecord r;
    Building_LookupTypeRecordB(0, &r);
    CHECK_EQ(r.dword0, 0u);

    // record 1 = 2a 00 7e 7e 15 00
    Building_LookupTypeRecordB(1, &r);
    CHECK_EQ(r.dword0, 0x7e7e002au);
    CHECK_EQ(r.word4, 0x0015u);

    Building_LookupTypeRecordB(27, &r);  // out of range -> record 0
    CHECK_EQ(r.dword0, 0u);
}

// ---------------------------------------------------------------------------
// MatchTypeCode / MatchProfessionCode
// ---------------------------------------------------------------------------
TEST(Building2_Match, TypeCode) {
    PersonRecBuilder t(4);
    t.setTypeByte(2, 0x55);
    t.setProf(2, 0x11, 0x22);
    BindScope bind(nullptr, nullptr, nullptr, t.buf.data());

    std::uint8_t codes[] = {0x10, 0x55, 0x99};
    CHECK_EQ(Building_MatchTypeCode(2, 3, codes), 1);   // 0x55 present
    std::uint8_t miss[] = {0x10, 0x99};
    CHECK_EQ(Building_MatchTypeCode(2, 2, miss), 0);
    // count <= 0 short-circuits.
    CHECK_EQ(Building_MatchTypeCode(2, 0, codes), 0);
    CHECK_EQ(Building_MatchTypeCode(2, -1, codes), 0);
}

TEST(Building2_Match, ProfessionCode) {
    PersonRecBuilder t(4);
    t.setProf(1, 0x07, 0x0C);
    BindScope bind(nullptr, nullptr, nullptr, t.buf.data());

    std::uint8_t a[] = {0x01, 0x0C};
    CHECK_EQ(Building_MatchProfessionCode(1, 2, a), 1);  // matches +361
    std::uint8_t b[] = {0x07};
    CHECK_EQ(Building_MatchProfessionCode(1, 1, b), 1);  // matches +358
    std::uint8_t c[] = {0x02, 0x03};
    CHECK_EQ(Building_MatchProfessionCode(1, 2, c), 0);
    // count == 0 / negative => 0.
    CHECK_EQ(Building_MatchProfessionCode(1, 0, a), 0);
    CHECK_EQ(Building_MatchProfessionCode(1, -5, a), 0);
}

// Null type table => matchers return 0.
TEST(Building2_Match, NullTable) {
    SetBuildingArrayBindings(BuildingArrayBindings{});
    std::uint8_t codes[] = {0x55};
    CHECK_EQ(Building_MatchTypeCode(2, 1, codes), 0);
    CHECK_EQ(Building_MatchProfessionCode(2, 1, codes), 0);
}

// ---------------------------------------------------------------------------
// MapTypeToState — exhaustive over the recovered switch.
// ---------------------------------------------------------------------------
TEST(Building2_MapTypeToState, Switch) {
    TypeRecBuilder t(40);
    // state byte at +0 == typeCode index value for each.
    for (int i = 0; i < 40; ++i) t.setKind(i, static_cast<std::uint8_t>(i));
    BindScope bind(t.buf.data(), nullptr, nullptr);

    auto check = [&](std::uint8_t code, int wantRet, int wantState) {
        std::uint8_t st = 0xEE;
        int r = Building_MapTypeToState(code, &st);
        CHECK_EQ(r, wantRet);
        CHECK_EQ(static_cast<int>(st), wantState);
    };
    // state-byte value v2 == code here. (state, ret)
    check(4, 0, 7);    // v2==4   -> *=7 ret0
    check(7, 0, 6);    // v2==7   -> *=6 ret0
    check(15, 1, 1);   // v2==15  -> *=1 ret1
    check(16, 0, 7);   // v2==16  -> *=7 ret0
    check(23, 1, 2);   // v2==23  -> *=2 ret1
    check(24, 1, 3);   // v2==24  -> *=3 ret1
    check(25, 1, 4);   // v2==25  -> *=4 ret1
    check(26, 1, 5);   // v2==26  -> *=5 ret1
    // No-write cases: ret 0, state untouched (0xEE).
    check(3, 0, 0xEE);
    check(17, 0, 0xEE);
    check(27, 0, 0xEE);
    check(8, 0, 0xEE);
}

// ---------------------------------------------------------------------------
// GetCategoryForObject
// ---------------------------------------------------------------------------
TEST(Building2_GetCategoryForObject, Branches) {
    // BuildingType_GroupFromCode(0) == 0 (codes outside table). Choose recCode 0
    // and matchCode 0 to take the equal branch -> directCat.
    CHECK_EQ(static_cast<int>(Building_GetCategoryForObject(0, 0, 0x42, -1)), 0x42);
    // matchCode != group(recCode) and no family record -> 0.
    CHECK_EQ(static_cast<int>(Building_GetCategoryForObject(0, 5, 0x42, -1)), 0);
    // family fallback.
    CHECK_EQ(static_cast<int>(Building_GetCategoryForObject(0, 5, 0x42, 0x77)), 0x77);
}

// ---------------------------------------------------------------------------
// Slot collectors
// ---------------------------------------------------------------------------
TEST(Building2_CollectObjectSlots, AppendsHits) {
    TypeRecBuilder t(2);
    // building type 1, rooms: 10, 0x8014 (hi-bit set -> masks to 0x14), 0 term.
    t.setRoom(1, 0, 10);
    t.setRoom(1, 1, 0x8014);
    t.setRoom(1, 2, 0);
    BindScope bind(t.buf.data(), nullptr, nullptr);
    MockQuery m;
    HookScope hs(&m);

    std::int16_t out[8] = {0};
    int n = Building_CollectObjectSlots(/*typeCode*/1, /*container*/100, out);
    CHECK_EQ(n, 2);
    // container 100 + slot 10 = 110 ; 100 + 0x14(20) = 120.
    CHECK_EQ(static_cast<int>(out[0]), 110);
    CHECK_EQ(static_cast<int>(out[1]), 120);
    CHECK_EQ(m.findCalls, 2);
}

TEST(Building2_CollectObjectSlots, EmptyRoomList) {
    TypeRecBuilder t(2);  // type 1 has roomList[0] == 0
    BindScope bind(t.buf.data(), nullptr, nullptr);
    MockQuery m; HookScope hs(&m);
    std::int16_t out[4] = {0};
    CHECK_EQ(Building_CollectObjectSlots(1, 50, out), 0);
    CHECK_EQ(m.findCalls, 0);
}

TEST(Building2_CollectStorableSlots, SkipsWalls) {
    TypeRecBuilder t(2);
    // rooms: 5, 6, 7, 0. object cats: id5=33(wall), id6=2, id7=33(wall).
    t.setRoom(1, 0, 5);
    t.setRoom(1, 1, 6);
    t.setRoom(1, 2, 7);
    t.setRoom(1, 3, 0);
    ObjTableBuilder o(16);
    o.setCat(5, 33);
    o.setCat(6, 2);
    o.setCat(7, 33);
    BindScope bind(t.buf.data(), o.buf.data(), nullptr);

    // Only the non-33 entry (id6) is counted.
    CHECK_EQ(Building_CollectStorableSlots(1, nullptr), 1);
}

TEST(Building2_CollectSlotsAfterObject, AfterTarget) {
    TypeRecBuilder t(2);
    t.setSlotCount(1, 5);
    // rooms: 10, 20, 30, 40, 0
    t.setRoom(1, 0, 10);
    t.setRoom(1, 1, 20);
    t.setRoom(1, 2, 30);
    t.setRoom(1, 3, 40);
    t.setRoom(1, 4, 0);
    ObjTableBuilder o(64);
    // categories: 30->normal(1), 40->normal(1). none are 2/6/33.
    BindScope bind(t.buf.data(), o.buf.data(), nullptr);

    std::int16_t out[8] = {0};
    // find slot 20, collect after -> {30, 40} (stop at 0 terminator).
    int n = Building_CollectSlotsAfterObject(1, 5, 20, out);
    CHECK_EQ(n, 2);

    // target not found -> 0.
    CHECK_EQ(Building_CollectSlotsAfterObject(1, 5, 99, out), 0);
}

TEST(Building2_CollectSlotsAfterObject, StopsAtContainer) {
    TypeRecBuilder t(2);
    t.setSlotCount(1, 5);
    t.setRoom(1, 0, 10);
    t.setRoom(1, 1, 11);   // target
    t.setRoom(1, 2, 12);   // cat 1 -> collected
    t.setRoom(1, 3, 13);   // cat 6 -> stop (not collected)
    t.setRoom(1, 4, 14);
    ObjTableBuilder o(64);
    o.setCat(13, 6);
    BindScope bind(t.buf.data(), o.buf.data(), nullptr);
    std::int16_t out[8] = {0};
    CHECK_EQ(Building_CollectSlotsAfterObject(1, 5, 11, out), 1);
}

TEST(Building2_CollectFlagNodeCallback, AppendsFlagNodes) {
    ObjTableBuilder o(64);
    o.setCat(7, 33);   // slot 7 is a wall/flag
    o.setCat(8, 2);    // slot 8 not a flag
    BindScope bind(nullptr, o.buf.data(), nullptr);

    FlagNodeAccumulator acc{};
    // packed: kind in high byte, slot in low word. kind=1, slot=7 -> append.
    std::int32_t packed = (1 << 24) | 7;
    CHECK(Building_CollectFlagNodeCallback(0xABCD, packed, &acc));
    CHECK_EQ(acc.count, 1);
    CHECK_EQ(static_cast<int>(acc.slots[0]), 7);
    CHECK_EQ(acc.handles[0], 0xABCD);

    // kind != 1 -> no append.
    std::int32_t notkind = (2 << 24) | 7;
    CHECK(Building_CollectFlagNodeCallback(0x1, notkind, &acc));
    CHECK_EQ(acc.count, 1);

    // kind 1 but slot category != 33 -> no append.
    std::int32_t notwall = (1 << 24) | 8;
    CHECK(Building_CollectFlagNodeCallback(0x2, notwall, &acc));
    CHECK_EQ(acc.count, 1);
}

// ---------------------------------------------------------------------------
// FindOwnedDungeonSlot
// ---------------------------------------------------------------------------
TEST(Building2_FindOwnedDungeonSlot, Hit) {
    TypeRecBuilder t(8);
    t.setKind(3, 4);   // type 3 is a dungeon (kind 4)
    // char array: 256 slots of 169 bytes; slot 5 alive with type 3 + owner.
    std::vector<std::uint8_t> chr(169 * 256, 0);
    chr[169 * 5 + 0] = 3;                 // alive byte + type code
    std::int32_t owner = 0x12345678;
    std::memcpy(chr.data() + 169 * 5 + 101, &owner, 4);
    BindScope bind(t.buf.data(), nullptr, chr.data());

    // matching owner -> returns 0 (owner ^ owner).
    CHECK_EQ(Building_FindOwnedDungeonSlot(true, 0x12345678), 0);
    // non-matching owner -> scan exhausts -> 1.
    CHECK_EQ(Building_FindOwnedDungeonSlot(true, 0x99999999), 1);
    // flag clear -> 0 without scanning.
    CHECK_EQ(Building_FindOwnedDungeonSlot(false, 0x12345678), 0);
}

// ---------------------------------------------------------------------------
// Office helpers (cross-module via hooks)
// ---------------------------------------------------------------------------
TEST(Building2_Office, HasActiveOffice) {
    MockQuery m; HookScope hs(&m);
    m.personContainer = -1;
    CHECK_EQ(Building_HasActiveOffice(5, 0), false);
    m.personContainer = 42;
    CHECK_EQ(Building_HasActiveOffice(5, 0), true);
}

TEST(Building2_Office, FindOfficeStorageProto) {
    MockQuery m; HookScope hs(&m);
    m.personContainer = 1000;
    m.personKind = 1;
    Building_FindOfficeStorage(5, 0);
    CHECK_EQ(m.lastProto, 277);   // kind 1 -> proto 277

    m.personKind = 0;
    Building_FindOfficeStorage(5, 0);
    CHECK_EQ(m.lastProto, 322);   // else -> proto 322
}

// Inert defaults (no hook installed) are safe.
TEST(Building2_Office, InertDefaults) {
    SetBuilding2QueryHooks(nullptr);
    CHECK_EQ(Building_HasActiveOffice(1, 0), false);
    CHECK_EQ(static_cast<int>(Building_FindOfficeStorage(1, 0)), 0);
}
