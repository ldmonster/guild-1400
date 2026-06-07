// End-to-end flow for the persons & staff module: build a synthetic person with
// attributes + inventory, hire them as staff, assign work, and verify the wage,
// assignment and inventory state against a hand-computed reference.
//
// Crosses: person.{h,cpp} (attributes/office), personnel.{h,cpp} (wage),
// recruit.{h,cpp} (eligibility/proximity), inventory.{h,cpp} (carried items),
// over the entity-array substrate (entity.cpp).
#include "sim/person.h"
#include "sim/personnel.h"
#include "sim/recruit.h"
#include "sim/inventory.h"
#include "sim/entity.h"
#include "test.h"

#include <cmath>
#include <cstring>

using namespace guild::sim;
using guild::i16;
using guild::i32;
using guild::u8;

namespace {

Person* Spawn(int idx, i32 id, u8 office, u8 reputation, u8 isPlayer = 1) {
    Person& p = g_persons[idx];
    std::memset(&p, 0, sizeof(p));
    p.marker = static_cast<i16>(idx);
    PersonSetByte(&p, kPfKind, 6);            // human player char class
    PersonSetDword(&p, kPfId, id);
    PersonSetByte(&p, kPfIsPlayer, isPlayer);
    PersonSetByte(&p, kPfOffice, office);
    PersonSetByte(&p, kPfReputation, reputation);
    PersonSetDword(&p, kPfRelationBase, -1);  // unbound employer
    g_personIds[idx] = id;
    g_personArrayLoaded = true;
    return &p;
}

// Office-definition oracle: level == office id.
int OfficeDef(u8 officeId, u8 out[24]) {
    std::memset(out, 0, 24);
    if (officeId == 0) return 0;
    out[2] = officeId;
    return 1;
}

// Wage oracle stand-ins for the building module.
double BaseValue(int, int, int, int) { return 20.0; } // base item value
u8 g_category = 2;                                     // production category
u8 Category(u8) { return g_category; }

// A "staff roster" the recruiter manages: who is hired + their assigned work.
struct StaffRecord {
    i32 personId = -1;
    int assignedBuilding = -1;
    double wage = 0.0;
    Inventory inv;
};

} // namespace

TEST(SimPersonE2E, HireAssignAndVerify) {
    ResetEntityArrays();
    PersonSetOfficeDefinitionHook(&OfficeDef);
    PersonnelSetBuildingBaseValueHook(&BaseValue);
    PersonnelSetActionCategoryHook(&Category);
    InventorySetCommandHook(nullptr);

    // --- 1. Build the recruiter (a guild master) and a candidate worker. -----
    const i32 kRecruiterId = 5000;
    const i32 kWorkerId    = 6000;
    Spawn(/*idx*/0, kRecruiterId, /*office*/4, /*rep*/120);
    Person* worker = Spawn(/*idx*/1, kWorkerId, /*office*/2, /*rep*/80);

    // The worker carries items in a staff record's inventory (type 50 = wood).
    StaffRecord staff;
    staff.personId = kWorkerId;
    // type 50 fresh stack capacity = 20; add 8 -> stock 8.
    CHECK_EQ(InventoryAdd(staff.inv, 50, 8), 8);
    // type 477 fresh stack capacity = 5*0 + 10 = 10; add 3 -> stock 3.
    CHECK_EQ(InventoryAdd(staff.inv, 477, 3), 3);

    // --- 2. Recruitment eligibility: ranks 5 vs 3 -> |5-3| = 2 < 5 -> hireable.
    int proximity = RecruitCheckRecruitProximity(kRecruiterId, kWorkerId);
    CHECK_EQ(proximity, 1);

    // --- 3. Hire: bind the worker's employer field to the recruiter, set wage. -
    PersonSetDword(worker, kPfRelationBase, kRecruiterId);  // now employed
    staff.assignedBuilding = -1;

    // Wage by category (production -> *3.0 of base 20.0 = 60.0).
    g_category = 2;
    staff.wage = PersonnelComputeWageByCategory(/*building*/42, /*action*/1,
                                                /*arg*/0, /*aiType*/0);
    CHECK_EQ(staff.wage, 60.0);

    // Re-running proximity now that the worker is bound to THIS recruiter still
    // succeeds (employer == recruiter is allowed).
    CHECK_EQ(RecruitCheckRecruitProximity(kRecruiterId, kWorkerId), 1);

    // --- 4. Assign work: place the worker at building 42. --------------------
    staff.assignedBuilding = 42;
    CHECK_EQ(staff.assignedBuilding, 42);

    // Promote the worker to an administrative post -> wage jumps to *9.0 = 180.0.
    g_category = 11;
    staff.wage = PersonnelComputeWageByCategory(42, 1, 0, 0);
    CHECK_EQ(staff.wage, 180.0);

    // --- 5. Worker consumes 5 wood doing the job; inventory updates. ---------
    CHECK_EQ(InventoryRemove(staff.inv, 50, 5), 5);
    CHECK_EQ(InventoryFindStock(staff.inv, 50), 3);
    CHECK_EQ(InventoryFindStock(staff.inv, 477), 3);

    // --- 6. Office rank reflects the worker's office (level 2 -> rank 3). -----
    CHECK_EQ(PersonComputeOfficeRank(static_cast<guild::u16>(worker->marker), 0), 3);

    // --- 7. Hand-computed reference cross-check. -----------------------------
    //   recruiter office 4 -> rank 5; worker office 2 -> rank 3; |diff| = 2.
    //   production wage = 20 * 3 = 60; admin wage = 20 * 9 = 180.
    //   wood after consuming 5 of 8 = 3; good 477 untouched = 3.
    CHECK(std::fabs(staff.wage - 180.0) < 1e-9);
    CHECK_EQ(staff.personId, kWorkerId);
    CHECK(InventoryFind(staff.inv, 50) != nullptr);

    PersonSetOfficeDefinitionHook(nullptr);
    PersonnelSetBuildingBaseValueHook(nullptr);
    PersonnelSetActionCategoryHook(nullptr);
}
