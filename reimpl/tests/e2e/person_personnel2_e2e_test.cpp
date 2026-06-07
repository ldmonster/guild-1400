// End-to-end flow across the person_personnel2 leaves: build a small household,
// resolve employment relations, score assets, check solvency, and run the
// query-driven dialog/book-row helpers — all through one installed hook set.
#include "sim/person_personnel2.h"
#include "sim/person.h"
#include "sim/entity.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct E2EHooks : PersonPersonnel2Hooks {
    int sumCurrency = 0;
    int totalWealth = 0;
    int buildingWorth = 0;
    Person* queryResult = nullptr;
    std::vector<StaffBookRecord> book;
    int SumCurrencyHeld(const Person*) override { return sumCurrency; }
    int ComputeTotalWealth(const Person*) override { return totalWealth; }
    int OwnedBuildingWorth(const Person*) override { return buildingWorth; }
    Person* QueryFirst(int, int) override { return queryResult; }
    void SyncShopChildren(Person*) override {}
    u8 OpenBuildingForActiveChar(Person*) override { return 1; }
    const StaffBookRecord* StaffBook(int* c) override {
        if (c) *c = (int)book.size();
        return book.empty() ? nullptr : book.data();
    }
};

void ResetAll() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, sizeof(Person));
        g_persons[i].marker = -1;
    }
}

}  // namespace

TEST(PersonPersonnel2E2E, household_employment_and_wealth_flow) {
    ResetAll();
    E2EHooks h;
    SetPersonPersonnel2Hooks(&h);

    // Build a household: head (slot 7, kind 5, id 700) and a worker (slot 0,
    // kind 4, id 100) whose family slot points at the head. Plus an employer
    // record (slot 3, kind 6, id 600) whose primary relation references the
    // worker, and bind the worker to a scene entity.
    g_persons[7].marker = 0;
    PersonSetByte(&g_persons[7], kPfKind, 5);
    PersonSetDword(&g_persons[7], kPfId, 700);

    // Worker is kind 1 (employee): in the {1,2,6,7} active set so it is findable
    // by entity, and not 6/7 so FindEmploymentRelation scans for an employer.
    g_persons[0].marker = 0;
    PersonSetByte(&g_persons[0], kPfKind, 1);
    PersonSetDword(&g_persons[0], kPfId, 100);
    PersonSetWord(&g_persons[0], kPf2FamilyWord, (i16)7);
    PersonSetByte(&g_persons[0], kPf2EntityAlive, 1);
    PersonSetDword(&g_persons[0], kPf2EntityPtr, 0xCAFE);

    g_persons[3].marker = 0;
    PersonSetByte(&g_persons[3], kPfKind, 6);
    PersonSetDword(&g_persons[3], kPfId, 600);
    PersonSetDword(&g_persons[3], kPf2RelArray + 0, 100);  // employs worker 100

    // 1) Find the worker by its bound entity id.
    Person* found = PersonFindActiveByEntity(0xCAFE);
    CHECK_EQ(found, &g_persons[0]);

    // 2) Employment relation: worker has an employer (slot 3) -> 0 (relation OK).
    CHECK_EQ(PersonFindEmploymentRelation(&g_persons[0]), 0);

    // 3) Asset worth: household head currency + owned buildings.
    h.sumCurrency = 2000;
    h.buildingWorth = 750;
    CHECK_EQ(PersonComputeAssetWorth(&g_persons[0], 1), 2750);

    // 4) Solvency: heavily in debt -> critical for this worker (kind 1 -> 0.20
    //    threshold). Reset currency to 0 so the reserve drives the net balance.
    h.sumCurrency = 0;
    h.totalWealth = 1000;
    CHECK_EQ(PersonCheckDebtRatioCritical(&g_persons[0], -300), true);   // 0.2307 > 0.20
    //    A smaller debt keeps it below the 0.20 threshold.
    CHECK_EQ(PersonCheckDebtRatioCritical(&g_persons[0], -200), false);  // 0.1666 < 0.20

    // 5) Nearest-by-distance picks an occupied slot via the hook.
    // (No distance hook installed here -> all default large -> first occupied
    //  still loses to none; install a quick override.)
    struct DistHooks : E2EHooks { int NearestDistance(int idx, int) override { return idx; } } dh;
    dh.book = h.book;
    SetPersonPersonnel2Hooks(&dh);
    // occupied slots are 0, 3, 7; min distance == slot 0.
    CHECK_EQ(PersonFindNearestByDistance(0), 0);
    SetPersonPersonnel2Hooks(&h);

    // 6) Query-driven dialog helper: with a match it opens the building (low 1).
    h.queryResult = &g_persons[3];
    CHECK_EQ((int)PersonBeginQueryThenOpenBuilding(1, 0), 1);

    // 7) Set-field helper stamps +41 == 270 on the matched record.
    Person* r = PersonBeginQueryThenSetField270(nullptr, 0);
    CHECK_EQ(r, &g_persons[3]);
    CHECK_EQ((int)(u16)PersonGetWord(&g_persons[3], 41), 270);

    // 8) Book-row lifecycle: build then destroy.
    PersonnelBookRow row;
    std::memset(&row, 0xFF, sizeof(row));
    PersonnelBuildBookRow(100, 50, &row);
    CHECK_EQ(row.state, 0);
    CHECK_EQ(row.slider, -1);
    row.slider = 9;            // pretend a slider got attached
    int destroyed = PersonnelDestroyBookRowWidgets(&row);
    CHECK_EQ(destroyed, 9);
    CHECK_EQ(row.slider, -1);

    SetPersonPersonnel2Hooks(nullptr);
}

TEST(PersonPersonnel2E2E, orphaned_worker_falls_back_to_staff_book) {
    ResetAll();
    E2EHooks h;
    SetPersonPersonnel2Hooks(&h);

    // Nearly fill the table so the slow staff-book path is taken.
    for (int i = 0; i < 760; ++i) {
        g_persons[i].marker = 0;
        PersonSetByte(&g_persons[i], kPfKind, 4);
        PersonSetDword(&g_persons[i], kPfId, 2000 + i);
    }
    Person* worker = &g_persons[0];
    PersonSetDword(worker, kPfId, 4242);

    // No booking -> orphaned -> 1.
    CHECK_EQ(PersonFindEmploymentRelation(worker), 1);

    // Add an active booking for the worker -> relation satisfied -> 0.
    StaffBookRecord rec{};
    rec.personId = 4242;
    rec.active = 1;
    h.book.push_back(rec);
    CHECK_EQ(PersonFindEmploymentRelation(worker), 0);

    SetPersonPersonnel2Hooks(nullptr);
}
