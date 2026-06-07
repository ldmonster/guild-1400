#include "test.h"

#include "sim/person_lifecycle.h"
#include "sim/entity.h"
#include "sim/building.h"

#include <cstring>

using namespace guild::sim;

// ---------------------------------------------------------------------------
// Test fixtures: write into the type table (g_buildingTypes, stride 589) and the
// person/object arrays via their byte-offset views, matching the binary's reads.
// ---------------------------------------------------------------------------
namespace {

void SetTypeRecord(int idx, guild::u8 kind, const char* name) {
    guild::u8* base = reinterpret_cast<guild::u8*>(&g_buildingTypes[0]);
    guild::u8* rec  = base + idx * kBuildingTypeStride;
    rec[0] = kind;                                  // kind byte @+0
    std::memset(rec + 1, 0, 34);                    // name field @+1..+34
    if (name) std::strncpy(reinterpret_cast<char*>(rec + 1), name, 33);
}

void WritePersonI32(Person* p, int off, guild::i32 v) {
    std::memcpy(reinterpret_cast<guild::u8*>(p) + off, &v, 4);
}
void WritePersonU16(Person* p, int off, guild::u16 v) {
    std::memcpy(reinterpret_cast<guild::u8*>(p) + off, &v, 2);
}
void WriteObjI32(ObjectRec* o, int off, guild::i32 v) {
    std::memcpy(reinterpret_cast<guild::u8*>(o) + off, &v, 4);
}

} // namespace

// ===========================================================================
// VIBE_Person_CountActiveSlots (0x587b60) — name -> type index
// ===========================================================================
TEST(SimPersonLifecycle, CountActiveSlots_NameLookup) {
    ResetBuildings();
    g_buildingTypesLoaded = true;
    for (int i = 0; i < kBuildingTypeScanCount; ++i)
        SetTypeRecord(i, 0, "");          // all empty/uniqueless
    SetTypeRecord(0, 3, "Bakery");
    SetTypeRecord(5, 9, "Smithy");
    SetTypeRecord(40, 1, "Tavern");

    CHECK_EQ(PersonCountActiveSlots("Bakery"), guild::u8(0));
    CHECK_EQ(PersonCountActiveSlots("smithy"), guild::u8(5));   // case-insensitive
    CHECK_EQ(PersonCountActiveSlots("TAVERN"), guild::u8(40));
    // No match -> the original's >=42408 exit returns 0 (collides with index 0).
    CHECK_EQ(PersonCountActiveSlots("Nonexistent"), guild::u8(0));
}

// ===========================================================================
// VIBE_Person_CollectByType (0x587b9c) — collect type ids whose kind == arg
// ===========================================================================
TEST(SimPersonLifecycle, CollectByType_CollectsMatchingKinds) {
    ResetBuildings();
    g_buildingTypesLoaded = true;
    for (int i = 0; i < kBuildingTypeScanCount; ++i)
        SetTypeRecord(i, 0, "");
    SetTypeRecord(2, 11, "");
    SetTypeRecord(7, 11, "");
    SetTypeRecord(50, 11, "");
    SetTypeRecord(3, 12, "");

    guild::u8 out[kBuildingTypeScanCount] = {0};
    int n = PersonCollectByType(11, out);
    CHECK_EQ(n, 3);
    CHECK_EQ(out[0], guild::u8(2));
    CHECK_EQ(out[1], guild::u8(7));
    CHECK_EQ(out[2], guild::u8(50));

    // kind 0 matches everything else (the 68 records left at 0).
    guild::u8 out0[kBuildingTypeScanCount] = {0};
    int n0 = PersonCollectByType(0, out0);
    CHECK_EQ(n0, kBuildingTypeScanCount - 4);
}

// ===========================================================================
// VIBE_Person_FindByObjectRef (0x586a40) — walk objects, match +101 back-ref
// ===========================================================================
TEST(SimPersonLifecycle, FindByObjectRef_FindsMatch) {
    ResetEntityArrays();
    ResetBuildings();
    g_personArrayLoaded = true;          // QueryBegin guard

    // Three live objects with alive/type byte == 71; back-ref dword @+101.
    for (int i = 0; i < 3; ++i) {
        g_objects[i].alive = 71;
        WriteObjI32(&g_objects[i], 1, 1000 + i);      // id @+1
        WriteObjI32(&g_objects[i], 101, 5000 + i);    // back-ref @+101
    }

    ObjectRec* hit = PersonFindByObjectRef(5001, 0);
    CHECK(hit == &g_objects[1]);

    ObjectRec* hit2 = PersonFindByObjectRef(5002, 0);
    CHECK(hit2 == &g_objects[2]);

    ObjectRec* miss = PersonFindByObjectRef(9999, 0);
    CHECK(miss == nullptr);
}

TEST(SimPersonLifecycle, FindByObjectRef_GuardWhenUnloaded) {
    ResetEntityArrays();
    g_personArrayLoaded = false;          // QueryBegin bails -> nullptr
    CHECK(PersonFindByObjectRef(123, 0) == nullptr);
}

// ===========================================================================
// VIBE_BuildingType_GetGuildRankPair (0x589b40)
// ===========================================================================
TEST(SimPersonLifecycle, GuildRankPair_SwitchTable) {
    guild::u8 a = 0xEE, b = 0xEE;
    struct Case { guild::u8 code, a, b; };
    const Case cases[] = {
        {4, 1, 2}, {6, 0, 1}, {7, 4, 1}, {8, 0, 3}, {9, 0, 4},
        {10, 3, 2}, {12, 3, 2}, {11, 2, 3},
        {0, 1, 0}, {5, 1, 0}, {13, 1, 0}, {255, 1, 0},   // default
    };
    for (const auto& c : cases) {
        int r = BuildingTypeGetGuildRankPair(c.code, &a, &b);
        CHECK_EQ(r, 1);
        CHECK_EQ(a, c.a);
        CHECK_EQ(b, c.b);
    }
}

// ===========================================================================
// VIBE_Person_ComputeBirthDate (0x58be24) — golden vectors (Python oracle)
// ===========================================================================
TEST(SimPersonLifecycle, ComputeBirthDate_GoldenVectors) {
    // (person+38, person+48) -> (day, month, year)
    struct V { guild::i32 p38, p48; guild::u8 day, month; guild::u16 year; };
    const V vs[] = {
        {0x00050000, 0x12345678, 13, 12, 1405},
        {0x00130000, 0x00000000, 17,  3, 1419},
        {(guild::i32)0xFFFE0000, (guild::i32)0xDEADBEEF, 5, 4, 1398},
        {0x00000000, 0x00000000, 16,  1, 1400},
    };
    for (const auto& v : vs) {
        Person p;
        std::memset(&p, 0, sizeof(p));
        p.isPlayer = 0;                          // +8 == 0 -> derive date
        WritePersonI32(&p, 38, v.p38);
        WritePersonI32(&p, 48, v.p48);

        PersonBirthDate out;
        int r = PersonComputeBirthDate(&p, &out);
        CHECK_EQ(r, 1);
        CHECK_EQ(out.day,   v.day);
        CHECK_EQ(out.month, v.month);
        CHECK_EQ(out.year,  v.year);
    }
}

TEST(SimPersonLifecycle, ComputeBirthDate_LiveActorReturnsZero) {
    Person p;
    std::memset(&p, 0, sizeof(p));
    p.isPlayer = 1;                              // +8 != 0 -> no date
    PersonBirthDate out;
    std::memset(&out, 0xAB, sizeof(out));
    int r = PersonComputeBirthDate(&p, &out);
    CHECK_EQ(r, 0);
    // out is untouched on the zero path (the original writes nothing).
    CHECK_EQ(out.day, guild::u8(0xAB));
}

// ===========================================================================
// VIBE_Person_ComputeBirthDateFromRecord (0x58bd84) — golden vectors
// ===========================================================================
TEST(SimPersonLifecycle, ComputeBirthDateFromRecord_GoldenVectors) {
    ResetPersonLifecycle();

    // Case A: !p8 -> dateLow from person+38; subtract age word person+10.
    {
        g_gameDateSnapshot = GameDateSnapshot{};
        g_gameDateSnapshot.dateLow = 100;
        g_gameDateSnapshot.byte4 = 5;
        g_gameDateSnapshot.byte6 = 7;
        g_gameDateSnapshot.byte10[0] = 0x34;
        g_gameDateSnapshot.byte10[1] = 0x12;

        Person p;
        std::memset(&p, 0, sizeof(p));
        p.isPlayer = 0;                          // !p8 -> use person+38
        WritePersonI32(&p, 38, 0x00080000);      // >>16 == 8
        WritePersonU16(&p, 10, 3);               // age word
        WritePersonI32(&p, 48, (guild::i32)0xCAFEBABE);

        PersonBirthDate out;
        guild::u32 ret = PersonComputeBirthDateFromRecord(&p, &out);
        CHECK_EQ(out.day,   guild::u8(15));
        CHECK_EQ(out.month, guild::u8(4));
        CHECK_EQ(out.year,  guild::u16(1405));
        CHECK_EQ(ret, guild::u32(3800996));
    }

    // Case B: live actor (p8 != 0) -> dateLow from snapshot (0) minus age (0).
    {
        g_gameDateSnapshot = GameDateSnapshot{};   // all zero
        Person p;
        std::memset(&p, 0, sizeof(p));
        p.isPlayer = 1;                          // p8 != 0 -> use snapshot dateLow
        WritePersonU16(&p, 10, 0);
        WritePersonI32(&p, 48, 0);

        PersonBirthDate out;
        guild::u32 ret = PersonComputeBirthDateFromRecord(&p, &out);
        CHECK_EQ(out.day,   guild::u8(16));
        CHECK_EQ(out.month, guild::u8(7));
        CHECK_EQ(out.year,  guild::u16(1400));
        CHECK_EQ(ret, guild::u32(1));
    }
}
