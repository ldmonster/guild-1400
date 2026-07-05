// Unit tests for person_personnel2.{h,cpp} — Person/Personnel query, wealth,
// staff-book leaves. Golden vectors computed with python3; hooks installed per
// test so the library never depends on a test-defined symbol.
#include "sim/person_personnel2.h"
#include "sim/person.h"
#include "sim/entity.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Reset the whole person array to "free" (marker == -1) before each test.
void PP2_ResetPersons() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, sizeof(Person));
        g_persons[i].marker = -1;
    }
}

// Mark a slot occupied with a kind and id.
void PP2_MakePerson(int idx, u8 kind, i32 id) {
    g_persons[idx].marker = 0;
    PersonSetByte(&g_persons[idx], kPfKind, kind);
    PersonSetDword(&g_persons[idx], kPfId, id);
}

// --- test hooks -------------------------------------------------------------
struct PP2TestHooks : PersonPersonnel2Hooks {
    std::vector<int> distances;          // by slot idx
    std::vector<StaffBookRecord> book;
    int sumCurrency = 0;
    int totalWealth = 0;
    int buildingWorth = 0;
    Person* queryResult = nullptr;
    int lastQueryTag = -1;
    int lastQuerySlot = 0;
    Person* syncedMaster = nullptr;
    u8 openBuildingRet = 0;
    Person* openedRec = nullptr;
    std::vector<int> added;              // (kind<<24)|x ignored, just count
    std::vector<int> destroyed;

    int NearestDistance(int idx, int) override {
        if (idx >= 0 && idx < (int)distances.size()) return distances[idx];
        return 0x7fffffff;
    }
    const StaffBookRecord* StaffBook(int* outCount) override {
        if (outCount) *outCount = (int)book.size();
        return book.empty() ? nullptr : book.data();
    }
    int SumCurrencyHeld(const Person*) override { return sumCurrency; }
    int ComputeTotalWealth(const Person*) override { return totalWealth; }
    int OwnedBuildingWorth(const Person*) override { return buildingWorth; }
    u8 OpenBuildingForActiveChar(Person* rec) override { openedRec = rec; return openBuildingRet; }
    Person* QueryFirst(int slot, int tag) override {
        lastQueryTag = tag; lastQuerySlot = slot; return queryResult;
    }
    void SyncShopChildren(Person* m) override { syncedMaster = m; }
    int AddWidget(int kind, int x, int) override { added.push_back(x); return (int)added.size() - 1; }
    void DestroyWidget(int h) override { destroyed.push_back(h); }
};

struct HookGuard {
    HookGuard(PersonPersonnel2Hooks* h) { SetPersonPersonnel2Hooks(h); }
    ~HookGuard() { SetPersonPersonnel2Hooks(nullptr); }
};

}  // namespace

// ---------------------------------------------------------------------------
TEST(PersonPersonnel2, FindActiveByEntity_match_and_kindfilter) {
    PP2_ResetPersons();
    // slot 5: alive, entity 0x1234, kind 6 -> should match.
    PP2_MakePerson(5, 6, 100);
    PersonSetByte(&g_persons[5], kPf2EntityAlive, 1);
    PersonSetDword(&g_persons[5], kPf2EntityPtr, 0x1234);
    // slot 9: alive, same entity but kind 3 (not in {1,2,6,7}) -> skipped.
    PP2_MakePerson(9, 3, 101);
    PersonSetByte(&g_persons[9], kPf2EntityAlive, 1);
    PersonSetDword(&g_persons[9], kPf2EntityPtr, 0x1234);

    CHECK_EQ(PersonFindActiveByEntity(0x1234), &g_persons[5]);
    CHECK_EQ(PersonFindActiveByEntity(0), (Person*)nullptr);     // 0 short-circuits
    CHECK_EQ(PersonFindActiveByEntity(0x9999), (Person*)nullptr); // no match
}

TEST(PersonPersonnel2, FindActiveByEntity_requires_alive) {
    PP2_ResetPersons();
    PP2_MakePerson(3, 1, 5);
    PersonSetByte(&g_persons[3], kPf2EntityAlive, 0);  // not alive
    PersonSetDword(&g_persons[3], kPf2EntityPtr, 42);
    CHECK_EQ(PersonFindActiveByEntity(42), (Person*)nullptr);
}

// ---------------------------------------------------------------------------
TEST(PersonPersonnel2, FindNearestByDistance_min_and_ties) {
    PP2_ResetPersons();
    PP2TestHooks h;
    h.distances.assign(kPersonCapacity, 0x7fffffff);
    // occupy slots 2,4,7 with distances 50,10,10 -> min is slot 4 (earlier of ties)
    for (int s : {2, 4, 7}) g_persons[s].marker = 0;
    h.distances[2] = 50; h.distances[4] = 10; h.distances[7] = 10;
    HookGuard g(&h);
    CHECK_EQ(PersonFindNearestByDistance(0), 4);
}

TEST(PersonPersonnel2, FindNearestByDistance_none_occupied) {
    PP2_ResetPersons();
    PP2TestHooks h;
    HookGuard g(&h);
    CHECK_EQ(PersonFindNearestByDistance(0), -1);
}

// ---------------------------------------------------------------------------
TEST(PersonPersonnel2, FindEmploymentRelation_self_employer) {
    PP2_ResetPersons();
    PP2_MakePerson(0, 6, 1);   // kind 6 -> immediately returns 0
    CHECK_EQ(PersonFindEmploymentRelation(&g_persons[0]), 0);
    PP2_MakePerson(1, 7, 2);   // kind 7 too
    CHECK_EQ(PersonFindEmploymentRelation(&g_persons[1]), 0);
}

TEST(PersonPersonnel2, FindEmploymentRelation_primary_relation_match) {
    PP2_ResetPersons();
    // worker: kind 4, id 100.
    PP2_MakePerson(0, 4, 100);
    // employer: kind 6, id 200, primary relation +96 == worker id 100.
    PP2_MakePerson(1, 6, 200);
    PersonSetDword(&g_persons[1], kPf2RelArray + 4, 100);
    CHECK_EQ(PersonFindEmploymentRelation(&g_persons[0]), 0);  // relation found
}

TEST(PersonPersonnel2, FindEmploymentRelation_extended_array_match) {
    PP2_ResetPersons();
    PP2_MakePerson(0, 4, 100);
    PP2_MakePerson(2, 6, 300);
    // extended array entry (+108) == worker id; others -1 so v3 stays < 5.
    PersonSetDword(&g_persons[2], kPf2RelArray + 0, -1);
    PersonSetDword(&g_persons[2], kPf2RelArray + 4, -1);
    PersonSetDword(&g_persons[2], kPf2RelArray + 8, -1);
    for (int off = kPf2RelArray + 12; off < kPf2RelArray + 32; off += 4)
        PersonSetDword(&g_persons[2], off, -1);
    PersonSetDword(&g_persons[2], kPf2RelArray + 16, 100);  // +108 match
    CHECK_EQ(PersonFindEmploymentRelation(&g_persons[0]), 0);
}

TEST(PersonPersonnel2, FindEmploymentRelation_orphaned_book_full) {
    PP2_ResetPersons();
    // Fill table so (768 - live) < 32 -> takes the staff-book slow path.
    for (int i = 0; i < 750; ++i) { g_persons[i].marker = 0; PersonSetByte(&g_persons[i], kPfKind, 4); PersonSetDword(&g_persons[i], kPfId, 1000 + i); }
    Person* worker = &g_persons[0];
    PersonSetByte(worker, kPfKind, 4);
    PersonSetDword(worker, kPfId, 55);
    PP2TestHooks h;
    HookGuard g(&h);
    // empty book -> orphaned -> 1.
    CHECK_EQ(PersonFindEmploymentRelation(worker), 1);
    // book contains the worker, active -> 0.
    StaffBookRecord r{}; r.personId = 55; r.active = 1;
    h.book.push_back(r);
    CHECK_EQ(PersonFindEmploymentRelation(worker), 0);
    // book contains worker but inactive -> still orphaned -> 1.
    h.book[0].active = 0;
    CHECK_EQ(PersonFindEmploymentRelation(worker), 1);
}

// ---------------------------------------------------------------------------
TEST(PersonPersonnel2, ComputeAssetWorth_family_currency) {
    PP2_ResetPersons();
    PP2_MakePerson(0, 4, 100);
    PersonSetWord(&g_persons[0], kPf2FamilyWord, (i16)7);   // family head slot 7
    PP2_MakePerson(7, 5, 200);                              // head kind 5 (<10)
    PP2TestHooks h;
    h.sumCurrency = 1234;
    h.buildingWorth = 500;
    HookGuard g(&h);
    CHECK_EQ(PersonComputeAssetWorth(&g_persons[0], 0), 1234);          // no buildings
    CHECK_EQ(PersonComputeAssetWorth(&g_persons[0], 1), 1234 + 500);    // + buildings
}

TEST(PersonPersonnel2, ComputeAssetWorth_no_family) {
    PP2_ResetPersons();
    PP2_MakePerson(0, 4, 100);
    PersonSetWord(&g_persons[0], kPf2FamilyWord, (i16)0xFFFF);  // no household
    PP2TestHooks h;
    h.sumCurrency = 999;   // should be ignored (no family)
    h.buildingWorth = 60;
    HookGuard g(&h);
    CHECK_EQ(PersonComputeAssetWorth(&g_persons[0], 0), 0);
    CHECK_EQ(PersonComputeAssetWorth(&g_persons[0], 1), 60);
}

TEST(PersonPersonnel2, ComputeAssetWorth_family_head_not_person) {
    PP2_ResetPersons();
    PP2_MakePerson(0, 4, 100);
    PersonSetWord(&g_persons[0], kPf2FamilyWord, (i16)7);
    PP2_MakePerson(7, 12, 200);   // head kind 12 (>=10) -> currency skipped
    PP2TestHooks h;
    h.sumCurrency = 777;
    HookGuard g(&h);
    CHECK_EQ(PersonComputeAssetWorth(&g_persons[0], 0), 0);
}

// ---------------------------------------------------------------------------
// Golden vectors (computed with python3, float-exact):
//   A solvent(reserve100,cur50,wealth1000,k4)            = false
//   B critical(reserve-300,cur0,wealth1000,k4)           = true  (0.2307>0.20)
//   C below   (reserve-200,cur0,wealth1000,k4)           = false (0.1666<0.20)
//   D critical(reserve-100,cur0,wealth1000,k6)           = true  (0.0909>0.07)
//   E below   (reserve-50, cur0,wealth1000,k6)           = false (0.0476<0.07)
//   F net==0  (reserve0,  cur0,wealth1000,k4)            = false
TEST(PersonPersonnel2, CheckDebtRatioCritical_golden) {
    PP2_ResetPersons();
    PP2TestHooks h;
    HookGuard g(&h);

    auto run = [&](int reserve, int cur, int wealth, u8 kind) {
        PP2_MakePerson(0, kind, 1);
        h.sumCurrency = cur;
        h.totalWealth = wealth;
        return PersonCheckDebtRatioCritical(&g_persons[0], reserve);
    };

    CHECK_EQ(run(100, 50, 1000, 4), false);   // A
    CHECK_EQ(run(-300, 0, 1000, 4), true);    // B
    CHECK_EQ(run(-200, 0, 1000, 4), false);   // C
    CHECK_EQ(run(-100, 0, 1000, 6), true);    // D
    CHECK_EQ(run(-50, 0, 1000, 6), false);    // E
    CHECK_EQ(run(0, 0, 1000, 4), false);      // F
}

// ---------------------------------------------------------------------------
// 0x594c94 iterates EVERY object matching the {op 0, value 30} filter via the
// real QueryBegin/IterNext pair (0x594ca7 for-loop) — not just the first.
TEST(PersonPersonnel2, SyncMasterShopObjects) {
    PP2_ResetPersons();
    std::memset(g_objects, 0, sizeof(ObjectRec) * kObjectCapacity);
    bool wasLoaded = g_personArrayLoaded;
    g_personArrayLoaded = true;
    g_objects[3].alive = 30;   // master A
    g_objects[5].alive = 12;   // non-matching kind
    g_objects[9].alive = 30;   // master B

    struct SyncHooks : PP2TestHooks {
        std::vector<Person*> synced;
        void SyncShopChildren(Person* m) override { synced.push_back(m); }
    } h;
    HookGuard g(&h);
    PersonSyncMasterShopObjects(42);
    CHECK_EQ((int)h.synced.size(), 2);
    if (h.synced.size() == 2) {
        CHECK_EQ(h.synced[0], reinterpret_cast<Person*>(&g_objects[3]));
        CHECK_EQ(h.synced[1], reinterpret_cast<Person*>(&g_objects[9]));
    }

    // No match -> no sync.
    h.synced.clear();
    g_objects[3].alive = 0;
    g_objects[9].alive = 0;
    PersonSyncMasterShopObjects(7);
    CHECK_EQ((int)h.synced.size(), 0);

    std::memset(g_objects, 0, sizeof(ObjectRec) * kObjectCapacity);
    g_personArrayLoaded = wasLoaded;
}

// ---------------------------------------------------------------------------
TEST(PersonPersonnel2, BeginQueryThenSetField270) {
    PP2_ResetPersons();
    PP2_MakePerson(0, 4, 10);
    PP2TestHooks h;
    h.queryResult = &g_persons[0];
    HookGuard g(&h);

    // cur non-null -> returned as-is, no query.
    h.lastQueryTag = -1;
    Person dummy{};
    CHECK_EQ(PersonBeginQueryThenSetField270(&dummy, 5), &dummy);
    CHECK_EQ(h.lastQueryTag, -1);

    // cur null -> query (tag 15) and set +41 word to 270.
    Person* r = PersonBeginQueryThenSetField270(nullptr, 8);
    CHECK_EQ(r, &g_persons[0]);
    CHECK_EQ(h.lastQueryTag, 15);
    CHECK_EQ((int)(u16)PersonGetWord(&g_persons[0], 41), 270);
}

TEST(PersonPersonnel2, BeginQueryThenOpenBuilding) {
    PP2_ResetPersons();
    PP2_MakePerson(0, 4, 10);
    PP2TestHooks h;
    h.queryResult = &g_persons[0];
    h.openBuildingRet = 5;
    HookGuard g(&h);

    // stateLow != 1 -> passthrough.
    CHECK_EQ((int)PersonBeginQueryThenOpenBuilding(0, 1), 0);

    // stateLow == 1, match -> open building, returns hook's low byte.
    CHECK_EQ((int)PersonBeginQueryThenOpenBuilding(1, 9), 5);
    CHECK_EQ(h.lastQueryTag, 10);
    CHECK_EQ(h.openedRec, &g_persons[0]);

    // stateLow == 1, no match -> 0: 0x596487 `LODWORD(a1) = QueryBegin(...)`
    // replaces the low byte with the null result, so al returns 0 (the old
    // "stays 1" pin matched a misreading of the register overlay).
    h.queryResult = nullptr;
    CHECK_EQ((int)PersonBeginQueryThenOpenBuilding(1, 9), 0);
}

// ---------------------------------------------------------------------------
TEST(PersonPersonnel2, BuildBookRow_initializes_fields) {
    PP2TestHooks h;
    HookGuard g(&h);
    PersonnelBookRow row;
    std::memset(&row, 0xAB, sizeof(row));   // garbage to prove init
    PersonnelBookRow* ret = PersonnelBuildBookRow(120, 64, &row);
    CHECK_EQ(ret, &row);
    CHECK_EQ(row.winX, 120);
    CHECK_EQ(row.winY, 64);
    CHECK_EQ(row.state, 0);
    CHECK_EQ((int)row.flag33, 0);
    CHECK_EQ(row.slider, -1);
    CHECK_EQ(row.extraStart, -1);
    CHECK_EQ(row.extraEnd, -1);
    CHECK_EQ((int)h.added.size(), 3);   // window + 2 labels
}

TEST(PersonPersonnel2, DestroyBookRowWidgets_destroys_and_clears) {
    PP2TestHooks h;
    HookGuard g(&h);
    PersonnelBookRow row{};
    row.extraStart = 11;
    row.slider = 22;
    row.extraEnd = 33;
    int result = PersonnelDestroyBookRowWidgets(&row);
    CHECK_EQ((int)h.destroyed.size(), 3);
    CHECK_EQ(row.extraStart, -1);
    CHECK_EQ(row.slider, -1);
    CHECK_EQ(row.extraEnd, -1);
    CHECK_EQ(result, 33);   // last destroyed handle

    // Nothing set -> no destroys, result 0.
    h.destroyed.clear();
    PersonnelBookRow empty{};
    empty.extraStart = -1; empty.slider = -1; empty.extraEnd = -1;
    CHECK_EQ(PersonnelDestroyBookRowWidgets(&empty), 0);
    CHECK_EQ((int)h.destroyed.size(), 0);
}
