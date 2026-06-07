// Unit tests for the persons & staff module:
//   src/sim/person.{h,cpp}, personnel.{h,cpp}, recruit.{h,cpp}, inventory.{h,cpp}
#include "sim/person.h"
#include "sim/personnel.h"
#include "sim/recruit.h"
#include "sim/inventory.h"
#include "sim/entity.h"
#include "test.h"

#include <climits>
#include <cmath>
#include <cstring>

using namespace guild::sim;
using guild::i16;
using guild::i32;
using guild::u8;

namespace {

// Place a live person at slot `idx`. marker is the alive marker AND (for live
// records) the slot index, matching the binary's convention.
Person* MakePerson(int idx, i32 id, u8 kind = 6, u8 isPlayer = 1) {
    Person& p = g_persons[idx];
    std::memset(&p, 0, sizeof(p));
    p.marker = static_cast<i16>(idx); // live: marker == slot index
    PersonSetByte(&p, kPfKind, kind);
    PersonSetDword(&p, kPfId, id);
    PersonSetByte(&p, kPfIsPlayer, isPlayer);
    g_personIds[idx] = id;
    g_personArrayLoaded = true;
    return &p;
}

void FreeSlot(int idx) {
    g_persons[idx].marker = -1;
}

ItemRec MakeItem(i16 type, int count) {
    ItemRec r{};
    r.type = type;
    std::memcpy(reinterpret_cast<u8*>(&r) + 0x0E, &count, sizeof(count));
    return r;
}

} // namespace

// ===== Person field offsets =====
TEST(SimPerson, FieldOffsetsByteExact) {
    Person p{};
    std::memset(&p, 0, sizeof(p));
    // Write through the named accessors, read back the raw bytes at the offsets.
    PersonSetByte(&p, kPfKind, 7);
    PersonSetDword(&p, kPfId, 0x11223344);
    PersonSetByte(&p, kPfIsPlayer, 1);
    PersonSetWord(&p, kPfCash, 1234);
    PersonSetDword(&p, kPfWealthScore, 999999);
    PersonSetByte(&p, kPfReputation, 200);
    PersonSetByte(&p, kPfOffice, 5);

    const u8* raw = reinterpret_cast<const u8*>(&p);
    CHECK_EQ(raw[0x02], 7);
    CHECK_EQ(raw[0x08], 1);
    CHECK_EQ(raw[0x80], 200);
    CHECK_EQ(raw[0x166], 5);
    i32 id; std::memcpy(&id, raw + 0x04, 4);
    CHECK_EQ(id, 0x11223344);
    i16 cash; std::memcpy(&cash, raw + 0x0A, 2);
    CHECK_EQ(cash, (i16)1234);
    i32 score; std::memcpy(&score, raw + 0x1AC, 4);
    CHECK_EQ(score, 999999);
    // The record stride is exactly 536.
    CHECK_EQ((int)sizeof(Person), 536);
}

// ===== IsValidActiveRecord =====
TEST(SimPerson, IsValidActiveRecord) {
    ResetEntityArrays();
    MakePerson(3, 100, /*kind*/6, /*isPlayer*/1);
    CHECK(PersonIsValidActiveRecord(3));

    MakePerson(4, 101, /*kind*/12, /*isPlayer*/0);   // not a live actor
    CHECK(!PersonIsValidActiveRecord(4));

    MakePerson(5, 102, /*kind*/10, /*isPlayer*/1);   // kind >= 10
    CHECK(!PersonIsValidActiveRecord(5));

    MakePerson(6, 103);
    FreeSlot(6);                                      // marker == -1
    CHECK(!PersonIsValidActiveRecord(6));
}

// ===== GetCashAmount =====
TEST(SimPerson, GetCashAmount) {
    ResetEntityArrays();
    Person* p = MakePerson(2, 50);
    PersonSetWord(p, kPfCash, 4321);
    CHECK_EQ(PersonGetCashAmount(2), 4321.0);
    PersonSetWord(p, kPfCash, 0);
    CHECK_EQ(PersonGetCashAmount(2), 0.0);
}

// ===== ComputePriceMultiplier (golden) =====
TEST(SimPerson, ComputePriceMultiplier) {
    ResetEntityArrays();
    Person* p = MakePerson(1, 10);
    PersonSetByte(p, kPfReputation, 42);            // <= 42 -> 1.0
    CHECK_EQ(PersonComputePriceMultiplier(1), 1.0);
    PersonSetByte(p, kPfReputation, 100);
    // Golden: float(100 * 0.25 * (1/252) + 1.0) = 1.0992063283920288
    CHECK(std::fabs(PersonComputePriceMultiplier(1) - 1.0992063283920288) < 1e-9);
    PersonSetByte(p, kPfReputation, 200);
    CHECK(std::fabs(PersonComputePriceMultiplier(1) - 1.1984126567840576) < 1e-9);
}

// ===== ComputeWealthRank =====
TEST(SimPerson, ComputeWealthRank) {
    ResetEntityArrays();
    // 4 valid persons with distinct wealth scores.
    PersonSetDword(MakePerson(0, 1), kPfWealthScore, 100);
    PersonSetDword(MakePerson(1, 2), kPfWealthScore, 300);
    PersonSetDword(MakePerson(2, 3), kPfWealthScore, 200);
    PersonSetDword(MakePerson(3, 4), kPfWealthScore, 400);

    // Richest (score 400) -> rank 1; poorest (100) -> rank 4.
    CHECK_EQ(PersonComputeWealthRank(3, 0), 1);
    CHECK_EQ(PersonComputeWealthRank(1, 0), 2);
    CHECK_EQ(PersonComputeWealthRank(2, 0), 3);
    CHECK_EQ(PersonComputeWealthRank(0, 0), 4);
    // Bonus lifts slot 0's effective score to 400 (ties count as >=).
    CHECK_EQ(PersonComputeWealthRank(0, 300), 2);
    // Out of range.
    CHECK_EQ(PersonComputeWealthRank(0x300, 0), 0);
}

// ===== ComputeOfficeRank (with office-definition hook) =====
namespace {
int OfficeDefStub(u8 officeId, u8 out[24]) {
    // office id 0 -> no office; else level = officeId (cap meaning at 10).
    std::memset(out, 0, 24);
    if (officeId == 0) return 0;
    out[2] = officeId; // BYTE2 = level
    return 1;
}
} // namespace

TEST(SimPerson, ComputeOfficeRank) {
    ResetEntityArrays();
    PersonSetOfficeDefinitionHook(&OfficeDefStub);

    Person* a = MakePerson(0, 10);
    PersonSetByte(a, kPfOffice, 3);                 // level 3 -> rank 4
    PersonSetDword(a, kPfSuperiorId, -1);           // no superior
    CHECK_EQ(PersonComputeOfficeRank(0, 0), 4);

    // No office -> rank 1.
    Person* b = MakePerson(1, 20);
    PersonSetByte(b, kPfOffice, 0);
    CHECK_EQ(PersonComputeOfficeRank(1, 0), 1);

    // Free / not-actor slot -> 0.
    Person* c = MakePerson(2, 30);
    PersonSetDword(c, kPfId, -1);
    CHECK_EQ(PersonComputeOfficeRank(2, 0), 0);

    PersonSetOfficeDefinitionHook(nullptr);
}

// ===== Personnel wage (golden) =====
namespace {
double BaseValueStub(int, int, int, int) { return 12.5; }
u8 g_catReturn = 0;
u8 CategoryStub(u8) { return g_catReturn; }
} // namespace

TEST(SimPersonnel, ComputeWageByCategory) {
    PersonnelSetBuildingBaseValueHook(&BaseValueStub);
    PersonnelSetActionCategoryHook(&CategoryStub);

    g_catReturn = 2;   // production -> *3.0
    CHECK_EQ(PersonnelComputeWageByCategory(0, 1, 0, 0), 37.5);
    g_catReturn = 10;  // administrative -> *9.0
    CHECK_EQ(PersonnelComputeWageByCategory(0, 1, 0, 0), 112.5);
    g_catReturn = 12;  // administrative -> *9.0
    CHECK_EQ(PersonnelComputeWageByCategory(0, 1, 0, 0), 112.5);

    PersonnelSetBuildingBaseValueHook(nullptr);
    PersonnelSetActionCategoryHook(nullptr);
}

// ===== Recruit proximity =====
namespace {
bool NeverUnderfull(guild::u16) { return false; }
} // namespace

TEST(SimRecruit, CheckRecruitProximity) {
    ResetEntityArrays();
    PersonSetOfficeDefinitionHook(&OfficeDefStub);

    Person* rec = MakePerson(0, 1000);
    Person* cand = MakePerson(1, 2000);
    PersonSetByte(rec, kPfOffice, 3);
    PersonSetByte(cand, kPfOffice, 5);
    PersonSetDword(rec, kPfRelationBase, -1);   // unbound
    PersonSetDword(cand, kPfRelationBase, -1);  // unbound

    // |4 - 6| = 2 < 5 -> 1.
    CHECK_EQ(RecruitCheckRecruitProximity(1000, 2000), 1);

    // Far apart ranks: office 3 vs office 9 -> |4-10|=6 -> 0.
    PersonSetByte(cand, kPfOffice, 9);
    CHECK_EQ(RecruitCheckRecruitProximity(1000, 2000), 0);

    // Candidate bound to another employer.
    PersonSetByte(cand, kPfOffice, 5);
    PersonSetDword(cand, kPfRelationBase, 7777);
    CHECK_EQ(RecruitCheckRecruitProximity(1000, 2000), -1027);
    PersonSetDword(cand, kPfRelationBase, 1000); // bound to recruiter -> ok
    CHECK_EQ(RecruitCheckRecruitProximity(1000, 2000), 1);

    // Not a live actor.
    PersonSetByte(cand, kPfIsPlayer, 0);
    CHECK_EQ(RecruitCheckRecruitProximity(1000, 2000), -1026);

    // Unknown ids.
    CHECK_EQ(RecruitCheckRecruitProximity(1, 2), -1024);

    PersonSetOfficeDefinitionHook(nullptr);
}

// ===== IsFamilyMemberEligible =====
TEST(SimPerson, IsFamilyMemberEligible) {
    ResetEntityArrays();
    PersonSetTargetUnderfullHook(&NeverUnderfull);

    // reference is a live actor at id 4000; candidate references it in relation[0]
    // (-> v7 == 0 -> class 2 -> eligible after the relative scan with no live kin).
    Person* ref = MakePerson(0, 4000, /*kind*/6, /*isPlayer*/1);
    Person* cand = MakePerson(1, 5000, /*kind*/6, /*isPlayer*/1);
    PersonSetByte(ref, kPfAge, 30);
    PersonSetByte(cand, kPfAge, 30);
    // candidate relation[0] == reference id -> v7 == 0 (class 2).
    PersonSetDword(cand, kPfRelationBase, 4000);
    for (int s = 1; s < 8; ++s)
        PersonSetDword(cand, kPfRelationBase + 4 * s, -1);  // no other kin
    CHECK(PersonIsFamilyMemberEligible(cand, ref));

    // If the reference is not a live actor, ineligible.
    PersonSetByte(ref, kPfIsPlayer, 0);
    CHECK(!PersonIsFamilyMemberEligible(cand, ref));
    PersonSetByte(ref, kPfIsPlayer, 1);

    // Candidate not related and different household/age -> ineligible.
    Person* stranger = MakePerson(2, 6000, 6, 1);
    PersonSetByte(stranger, kPfAge, 99);
    for (int s = 0; s < 8; ++s)
        PersonSetDword(stranger, kPfRelationBase + 4 * s, -1);
    PersonSetWord(stranger, kPfFamilyWord, 7);
    PersonSetWord(ref, kPfFamilyWord, 9);
    CHECK(!PersonIsFamilyMemberEligible(stranger, ref));

    PersonSetTargetUnderfullHook(nullptr);
}

// ===== Inventory: effective stock / capacity (golden) =====
TEST(SimInventory, EffectiveStockAndCapacity) {
    ItemRec reserve = MakeItem(kItemReserveA, 10); // 42 -> count-1
    CHECK_EQ(InventoryGetEffectiveStock(&reserve), 9);
    ItemRec normal = MakeItem(100, 10);
    CHECK_EQ(InventoryGetEffectiveStock(&normal), 10);

    ItemRec hi = MakeItem(kItemHighCap, 4);        // 477 -> 5*4+10 = 30
    CHECK_EQ(InventoryGetSlotCapacity(&hi), 30);
    ItemRec lvl3 = MakeItem(50, 3);                // count==3 -> 80
    CHECK_EQ(InventoryGetSlotCapacity(&lvl3), 80);
    ItemRec lvl2 = MakeItem(50, 2);                // 20*2 = 40
    CHECK_EQ(InventoryGetSlotCapacity(&lvl2), 40);
}

// ===== Inventory: UI slot table scan =====
TEST(SimInventory, FindSlotByItemId) {
    InvGridSlot slots[8] = {};
    slots[0].type = 100;
    slots[1].type = 200;
    slots[2].type = 300;
    slots[3].type = 0;   // terminator marker

    int idx = -1;
    InvGridSlot* hit = InventoryFindSlotIndexByItemId(slots, 8, 200, &idx);
    CHECK(hit == &slots[1]);
    CHECK_EQ(idx, 1);
    CHECK(InventoryFindSlotByItemId(slots, 8, 300) == &slots[2]);
    CHECK(InventoryFindSlotByItemId(slots, 8, 999) == nullptr);

    InvGridSlot empty[4] = {};
    CHECK(InventoryFindSlotByItemId(empty, 4, 100) == nullptr);
}

// ===== Inventory: add / remove / find / free space =====
TEST(SimInventory, AddRemoveFind) {
    InventorySetCommandHook(nullptr);
    Inventory inv;

    // Add normal good: slot capacity for a fresh stack (count default 1) -> 20.
    int added = InventoryAdd(inv, 50, 5);
    CHECK_EQ(added, 5);
    CHECK_EQ(InventoryFindStock(inv, 50), 5);
    ItemRec* rec = InventoryFind(inv, 50);
    CHECK(rec != nullptr);

    // Remove some.
    int removed = InventoryRemove(inv, 50, 3);
    CHECK_EQ(removed, 3);
    CHECK_EQ(InventoryFindStock(inv, 50), 2);

    // Remove more than present clamps.
    CHECK_EQ(InventoryRemove(inv, 50, 99), 2);
    CHECK_EQ(InventoryFindStock(inv, 50), 0);

    // Absent item: find -> null, stock -> 0, remove -> 0.
    CHECK(InventoryFind(inv, 777) == nullptr);
    CHECK_EQ(InventoryFindStock(inv, 777), 0);
    CHECK_EQ(InventoryRemove(inv, 777, 1), 0);
}

// ===== Inventory: command hook is invoked on mutation =====
namespace {
int g_hookDelta = 0;
i16 g_hookType = 0;
void HookCounter(Inventory&, i16 t, int d) { g_hookType = t; g_hookDelta += d; }
} // namespace

TEST(SimInventory, CommandHookRouting) {
    InventorySetCommandHook(&HookCounter);
    g_hookDelta = 0;
    Inventory inv;
    InventoryAdd(inv, 60, 4);
    CHECK_EQ(g_hookType, (i16)60);
    CHECK_EQ(g_hookDelta, 4);
    InventoryRemove(inv, 60, 1);
    CHECK_EQ(g_hookDelta, 3);   // +4 then -1
    InventorySetCommandHook(nullptr);
}
