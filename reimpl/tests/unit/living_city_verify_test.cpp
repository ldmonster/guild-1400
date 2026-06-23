// =============================================================================
// VERIFICATION-WAVE golden tests (agent V-C, 2026-06-11 — IDA back online).
// Pin the binary-verified fixes of the living-city chain:
//
//  1. The daily director's dispatch writebacks land on BYTE 1 of +456
//     (dword_12CEAD8+1 @0x4e8184/0x4e82a5/0x4e8578/0x4e8767/0x4e89ab):
//     work = 0x1000, social = 0x800 — the SAME bits the gates test, so the
//     sweeps are self-limiting.
//  2. The person-record BUILDING-COLUMN POPULATER in Person_CreateAndSpawn
//     @0x58da70 (0x58dc8c..0x58e85b): the parent building record (a5) writes
//     +368 when its type-class byte is 1, +364 when the class is 4..22
//     excluding 10/15/17.
//  3. The .cty/save person-column relink — RelinkPersonRecordColumns, the
//     person slice (0x5abbe2..0x5abc4f) of VIBE_Save_RelinkLoadedPointers
//     @0x5abb84: player-record gate, -1/miss -> 0, +380 cleared on the partial
//     path, +388 zeroed unconditionally.
//  4. Building_SetObjectParent @0x58820c record/column slice (the workBld
//     populater for ownership changes): bld +0x25/+0x27 words, the old-owner
//     column clear @0x58827c, the class-2 column write with the SIGNED type
//     compare @0x5884b5, and the kind-6/7 staff clear (+357/+436, +364 for
//     class 5/9 buildings) @0x5884d6..0x5884f3.
// =============================================================================
#include "tests/framework/test.h"

#include "io/save_world_load.h"
#include "sim/building.h"
#include "sim/command.h"
#include "sim/command_apply4.h"
#include "sim/entity.h"
#include "sim/npc_daily.h"
#include "sim/npcaction.h"
#include "sim/person_create.h"

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

i32 ColI32(int slot, int off) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(&g_persons[slot]) + off, 4);
    return v;
}
void SetColI32(int slot, int off, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(&g_persons[slot]) + off, &v, 4);
}

// Stage a live building record: g_objects[slot] = {typeIdx, id}; the type-def
// class byte (dword_13CE294 + 589*type, byte +0) = cls.
ObjectRec* StageBuilding(int slot, u8 typeIdx, i32 id, u8 cls) {
    g_objects[slot].alive = typeIdx;
    g_objects[slot].id = id;
    g_buildingTypes[typeIdx].kind = cls;
    g_buildingTypesLoaded = true;
    return &g_objects[slot];
}

// Stage a live person record (marker = slot, id mirrored into g_personIds).
void StagePerson(int slot, i32 id, u8 kind) {
    u8* rec = reinterpret_cast<u8*>(&g_persons[slot]);
    std::memset(rec, 0, kPersonStride);
    g_persons[slot].marker = static_cast<i16>(slot);
    g_persons[slot].kind = kind;
    g_persons[slot].id = id;
    g_personIds[slot] = id;
}

void ResetAll() {
    ResetEntityArrays();
    ResetBuildings();
    ResetPersonCreate();
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Dispatch bits: BYTE1 |= 0x10 / |= 8 == bits 0x1000 / 0x800 (disasm
//    @0x4e8184 `or byte ptr ds:(dword_12CEAD8+1)[esi], 10h`).
// ---------------------------------------------------------------------------
TEST(LivingCityVerify, DispatchBitsAreByte1OfTurnBits) {
    CHECK_EQ((u32)kDailyDispWork, 0x1000u);
    CHECK_EQ((u32)kDailyDispSocial, 0x800u);
    // The writeback bits ARE the gate bits — self-limiting sweeps.
    CHECK_EQ((u32)kDailyDispWork, (u32)kDailySkip);
    CHECK_EQ((u32)kDailyDispSocial, (u32)kDailySocialReq);
}

namespace {
// Minimal director rig for the self-limit check.
struct SweepRec { int chrmove = 0; };
SweepRec g_sw;
DailyPersonRow g_swRow;
int  SwCount() { return 1; }
DailyPersonRow SwRow(int) { return g_swRow; }
void SwBits(int, u32 b) { g_swRow.turnBits = b; }
int  SwCarry(int, i32* u, i32* o) { *u = 100; *o = 200; return 1; }
bool SwDoor(int, i32* a, i32* b) { *a = 1; *b = 1; return true; }
bool SwProd(int) { return true; }
bool SwMesh(int) { return false; }
void SwMove(i32, i32, i32, const char*) { ++g_sw.chrmove; }
} // namespace

TEST(LivingCityVerify, MorningSweepSelfLimitsViaBit0x1000) {
    g_sw = SweepRec{};
    g_swRow = DailyPersonRow{};
    g_swRow.valid = true; g_swRow.activeB = 1;
    g_swRow.homeBld = 10; g_swRow.workBld = 20; g_swRow.destBld = 30;
    g_swRow.personId = 7;

    SetNpcClock(GameTime{0, 6, 0, 0});
    NpcDailyHooks h{};
    h.personCount = SwCount; h.personRow = SwRow; h.setTurnBits = SwBits;
    h.findCarryTarget = SwCarry; h.destDoorIds = SwDoor;
    h.homeIsProduction = SwProd; h.homeHasMesh = SwMesh;
    h.requestChrMoveToUniverse = SwMove;
    SetNpcDailyHooks(&h);

    HeRecord rec{};
    He_State(&rec) = 0;
    NpcDaily_DailyRoutineStep(&rec);
    CHECK_EQ(g_sw.chrmove, 1);
    CHECK_EQ(g_swRow.turnBits & 0x1000u, 0x1000u);   // BYTE1 |= 0x10

    // Second step: the freshly set bit gates the SAME person out (& 0x1000
    // @0x4e7f52) — no second dispatch, the original's self-limiting round.
    NpcDaily_DailyRoutineStep(&rec);
    CHECK_EQ(g_sw.chrmove, 1);
    SetNpcDailyHooks(nullptr);
}

// ---------------------------------------------------------------------------
// 2. Person_CreateAndSpawn building-column populater (0x58dc8c..0x58e85b).
// ---------------------------------------------------------------------------
TEST(LivingCityVerify, PersonCreateWritesBuildingColumns) {
    ResetAll();
    g_personArrayLoaded = true;

    struct Case { u8 cls; int wantOff; };   // wantOff 0 == neither column
    const Case cases[] = {
        {1, 368},                 // class 1 -> +368 (dword_12CEA80) @0x58dcba
        {4, 364}, {22, 364},      // class 4..22 -> +364 (dword_12CEA7C) @0x58e849
        {2, 0},  {3, 0},          // class <= 3 (and != 1) -> neither
        {10, 0}, {15, 0}, {17, 0},// excluded classes
        {23, 0},                  // class >= 0x17 -> neither
    };
    int bslot = 0;
    i32 bid = 900;
    for (const Case& c : cases) {
        ObjectRec* b = StageBuilding(bslot, static_cast<u8>(40 + bslot), bid, c.cls);
        PersonSpawnArgs a{};
        a.kind = 2;
        a.queryRec = b;
        u16 idx = Person_CreateAndSpawn(a);
        CHECK(idx != 0xFFFF);
        const i32 home = ColI32(idx, 364);
        const i32 work = ColI32(idx, 368);
        if (c.wantOff == 368) {
            CHECK_EQ(work, bid);
            CHECK_EQ(home, 0);
        } else if (c.wantOff == 364) {
            CHECK_EQ(home, bid);
            CHECK_EQ(work, 0);
        } else {
            CHECK_EQ(home, 0);
            CHECK_EQ(work, 0);
        }
        ++bslot;
        ++bid;
    }

    // No parent record -> both columns stay 0 (the @0x58db73/@0x58db9d init).
    PersonSpawnArgs a{};
    a.kind = 2;
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);
    CHECK_EQ(ColI32(idx, 364), 0);
    CHECK_EQ(ColI32(idx, 368), 0);
    ResetAll();
}

// ---------------------------------------------------------------------------
// 3. RelinkPersonRecordColumns — the 0x5abbe2 person slice.
// ---------------------------------------------------------------------------
TEST(LivingCityVerify, RelinkNormalizesPersonLinkColumns) {
    ResetAll();
    g_personArrayLoaded = true;
    StageBuilding(0, 50, 777, 11);   // a live building id 777
    StagePerson(0, 510, 6);          // the local player record (dword_6498E4)
    StagePerson(1, 511, 2);

    // Saved IDs as the .cty loader leaves them: -1 sentinels, a dangling id,
    // a resolvable id, plus stale He/live-char links.
    SetColI32(0, 364, -1); SetColI32(0, 368, -1);
    SetColI32(0, 380, 9); SetColI32(0, 388, 12345);
    SetColI32(1, 364, 777);   // resolves (BuildingFindById hit @0x583bb2 model)
    SetColI32(1, 368, 999);   // dangling -> 0 (resolve miss leaves *a1 == 0)
    SetColI32(1, 380, -1); SetColI32(1, 388, 1);

    // Gate (@0x5abb8e..0x5abb9a): an unresolvable player id relinks NOTHING.
    CHECK(!io::RelinkPersonRecordColumns(424242));
    CHECK_EQ(ColI32(0, 364), -1);
    CHECK_EQ(ColI32(1, 388), 1);

    CHECK(io::RelinkPersonRecordColumns(510));
    CHECK_EQ(ColI32(0, 364), 0);     // -1 -> 0  @0x5abc06
    CHECK_EQ(ColI32(0, 368), 0);     // -1 -> 0  @0x5abc1b
    CHECK_EQ(ColI32(0, 380), 0);     // partial path: He lookup misses -> 0
    CHECK_EQ(ColI32(0, 388), 0);     // live-char ptr zeroed @0x5abc3d
    CHECK_EQ(ColI32(1, 364), 777);   // resolvable id kept (pointer-as-id model)
    CHECK_EQ(ColI32(1, 368), 0);     // dangling id -> 0
    CHECK_EQ(ColI32(1, 380), 0);
    CHECK_EQ(ColI32(1, 388), 0);
    ResetAll();
}

// ---------------------------------------------------------------------------
// 4. Building_SetObjectParent record/column slice (0x58820c), driven through
//    the real opcode-0x38 applier ExSetObjectParent @0x49AE60 with the default
//    (now faithful) leaf backend: g_setParent(owner->id, *newParent, *child).
// ---------------------------------------------------------------------------
namespace {
u16 BldWord(const ObjectRec* b, int off) {
    u16 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(b) + off, 2);
    return v;
}
int ApplySetParent(i32 bldId, i32 parentPersonId, i32 childPersonId) {
    CommandPacket p{};
    p.opcode() = kOp4SetObjectParent;
    p.put32(0x10, static_cast<u32>(bldId));
    p.put32(0x14, static_cast<u32>(parentPersonId));   // a2 = *newParent word
    p.put32(0x18, static_cast<u32>(childPersonId));    // a3 = *child word
    AckEntry ack{};
    return ExSetObjectParent(p, &ack);
}
} // namespace

TEST(LivingCityVerify, SetObjectParentMaintainsWorkColumn) {
    ResetAll();
    g_personArrayLoaded = true;
    // Class-2 (the @0x58828d/@0x588299 production-kind test) buildings with
    // SIGNED type bytes: type 60 and type 70 (70 > 60).
    ObjectRec* b1 = StageBuilding(0, 60, 800, 2);
    ObjectRec* b2 = StageBuilding(1, 70, 801, 2);
    g_buildingTypes[70].kind = 2;
    StagePerson(2, 820, 2);          // the new owner (a3 -> marker/slot 2)
    StagePerson(3, 830, 2);          // the old owner (slot 3)
    StagePerson(5, 850, 2);          // the a2 source (marker word 5)

    // Seed b1's owner word +0x27 = old owner slot 3, whose column holds b1.
    u16 oldOwner = 3;
    std::memcpy(reinterpret_cast<u8*>(b1) + 0x27, &oldOwner, 2);
    SetColI32(3, 368, 800);

    SetSetObjectParentHook(nullptr);   // the faithful default backend
    CHECK_EQ(ApplySetParent(800, 850, 820), 0);
    CHECK_EQ(BldWord(b1, 0x25), 5u);           // +0x25 = a2     @0x588240
    CHECK_EQ(BldWord(b1, 0x27), 2u);           // +0x27 = a3     @0x588289
    CHECK_EQ(ColI32(3, 368), 0);               // old owner cleared @0x58827c
    CHECK_EQ(ColI32(2, 368), 800);             // class 2 -> column write @0x5882df

    // Re-parent the HIGHER-type b2 onto the same owner: existing column type
    // (60) <= new (70) -> replaced (movsx cmp @0x5884b5, jle -> write).
    CHECK_EQ(ApplySetParent(801, 850, 820), 0);
    CHECK_EQ(ColI32(2, 368), 801);

    // Re-parent b1 (type 60) again: existing column type (70) > 60 -> kept.
    CHECK_EQ(ApplySetParent(800, 850, 820), 0);
    CHECK_EQ(BldWord(b1, 0x27), 2u);
    CHECK_EQ(ColI32(2, 368), 801);

    // a3 == 0xFFFF (child id -1): only the +0x25 word is written
    // (@0x58824a early out).
    u16 before27 = BldWord(b2, 0x27);
    CHECK_EQ(ApplySetParent(801, 850, -1), 0);
    CHECK_EQ(BldWord(b2, 0x25), 5u);
    CHECK_EQ(BldWord(b2, 0x27), before27);
    ResetAll();
}

TEST(LivingCityVerify, SetObjectParentClearsStaffForPlayerOwner) {
    ResetAll();
    g_personArrayLoaded = true;
    // A class-9 building (staff +364 cleared @0x5884f3 for class 5/9; class 9
    // is NOT class 2, so no column write happens).
    StageBuilding(0, 80, 900, 9);
    StagePerson(1, 910, 6);   // new owner (a3), kind 6 (the @0x5882fe gate)
    StagePerson(4, 940, 2);   // the a2 source
    // The staff person FindActiveByEntity @0x5920b0 matches: +356 alive flag,
    // +364 == building id, kind in {1,2,6,7}.
    StagePerson(2, 920, 2);
    reinterpret_cast<u8*>(&g_persons[2])[356] = 1;
    reinterpret_cast<u8*>(&g_persons[2])[357] = 1;
    reinterpret_cast<u8*>(&g_persons[2])[436] = 0xFF;
    SetColI32(2, 364, 900);

    SetSetObjectParentHook(nullptr);
    CHECK_EQ(ApplySetParent(900, 940, 910), 0);
    const u8* staff = reinterpret_cast<const u8*>(&g_persons[2]);
    CHECK_EQ(staff[357], 0);                   // @0x5884dc
    CHECK_EQ(staff[436], 0xFE);                // &= 0xFE @0x5884e6
    CHECK_EQ(ColI32(2, 364), 0);               // class 9 -> +364 cleared @0x5884f3
    CHECK_EQ(ColI32(1, 368), 0);               // class 9 != 2 -> no column write
    ResetAll();
}
