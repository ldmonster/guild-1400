// Unit tests for the office holder-table collection / eligibility rules.
// Golden vectors derived from the baked office-definition table (office.cpp):
//   reqCode(type=1..3)==1, bookCat(1)=1 bookCat(2)=2 bookCat(3)=3
//   reqCode(type=28..34)==7 (elective), bookCat(28..34)==4
//   reqCode(type=22)==6
#include "test.h"
#include "world/law_apply.h"
#include "world/office.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

// ---- Person resolver test backend -----------------------------------------
struct FakePerson { i32 id; u8 office358; u8 busy433; };
FakePerson g_people[16];
int        g_peopleCount = 0;

OfficePersonRecord LookupPerson(i32 id, void*) {
    OfficePersonRecord r{};
    if (id == -1) return r;
    for (int i = 0; i < g_peopleCount; ++i) {
        if (g_people[i].id == id) {
            r.present = true;
            r.office358 = g_people[i].office358;
            r.busy433 = g_people[i].busy433;
            return r;
        }
    }
    return r;
}

void ResetPeople() { g_peopleCount = 0; }
void AddPerson(i32 id, u8 office358, u8 busy433) {
    g_people[g_peopleCount++] = {id, office358, busy433};
}

// Fill holder slot `idx` (raw 24-byte record fields).
void SetHolder(int idx, u8 holderId, i32 city, u8 type, i32 rank, u8 state, i32 secondary) {
    OfficeHolder& h = g_officeHolders[idx];
    std::memset(&h, 0, sizeof(h));
    h.holder = holderId;
    h.city = city;
    h.type = type;
    h.rank = rank;
    h.state = state;
    h.secondary = secondary;
}

void SetupSuite() {
    OfficeHolderTableReset();
    OfficeSetPersonResolver(&LookupPerson, nullptr);
    ResetPeople();
}

} // namespace

// ---------------------------------------------------------------------------
TEST(LawApply, CollectByCategoryMatchesReqCode) {
    SetupSuite();
    // Slots with reqCode-1 types (1,2,3) and one elective (28).
    SetHolder(0, 100, 0, 1, 0, 3, -1);
    SetHolder(1, 101, 0, 2, 0, 3, -1);
    SetHolder(5, 105, 0, 28, 0, 3, -1);
    SetHolder(10, 110, 0, 3, 0, 3, -1);

    OfficeHolder out[8];
    int n = OfficeCollectByCategory(/*reqCode*/ 1, 8, out);
    // types 1,2,3 -> reqCode 1; type 28 -> reqCode 7 (excluded). Expect 3.
    CHECK_EQ(n, 3);
    // Top-down scan: slot 10 (type3) first, then slot1 (type2), then slot0.
    CHECK_EQ((int)out[0].type, 3);
    CHECK_EQ((int)out[1].type, 2);
    CHECK_EQ((int)out[2].type, 1);
}

TEST(LawApply, CollectByCategoryRespectsMax) {
    SetupSuite();
    SetHolder(0, 100, 0, 1, 0, 3, -1);
    SetHolder(1, 101, 0, 2, 0, 3, -1);
    SetHolder(2, 102, 0, 3, 0, 3, -1);
    OfficeHolder out[8];
    int n = OfficeCollectByCategory(1, 2, out);
    CHECK_EQ(n, 2);
}

TEST(LawApply, CollectByCategoryResolvedNeedsLivePerson) {
    SetupSuite();
    // type 1 (reqCode 1); slot's +4 id is the person id used for resolution.
    SetHolder(0, 100, /*city/id*/ 500, 1, 0, 3, -1);
    SetHolder(1, 101, /*id*/ 501, 1, 0, 3, -1);
    AddPerson(500, 1, 0);                 // 500 resolves, 501 does not
    OfficeHolder out[8];
    int n = OfficeCollectByCategoryResolved(1, 8, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0].city, 500);
}

TEST(LawApply, CollectByCategoryResolvedZeroReqShortCircuits) {
    SetupSuite();
    OfficeHolder out[8];
    CHECK_EQ(OfficeCollectByCategoryResolved(0, 8, out), 0);
}

TEST(LawApply, CollectElectiveOffices) {
    SetupSuite();
    // Elective types are 28..34 (reqCode 7). Slots scanned are 30..36
    // (offset 720..222 stepping 24 -> indices 30..36).
    SetHolder(30, 130, -1, 28, 0, 3, -1);  // vacant id (-1)
    SetHolder(31, 131, 700, 29, 0, 3, -1); // filled id
    SetHolder(32, 132, -1, 5, 0, 3, -1);   // not elective (reqCode 2)

    OfficeHolder out[8];
    // allowVacant=1 -> both elective slots collected.
    int n1 = OfficeCollectElectiveOffices(1, 8, out);
    CHECK_EQ(n1, 2);
    // allowVacant=0 -> only the filled (id != -1) elective slot.
    int n0 = OfficeCollectElectiveOffices(0, 8, out);
    CHECK_EQ(n0, 1);
    CHECK_EQ((int)out[0].type, 29);
}

TEST(LawApply, CollectHoldersByCategoryWritesTypeBytes) {
    SetupSuite();
    // bookCat 4 types: includes 10,11,12,16,17,18,28..34.
    SetHolder(0, 1, 0, 10, 0, 3, -1);   // bookCat 4
    SetHolder(1, 2, 0, 1, 0, 3, -1);    // bookCat 1
    SetHolder(2, 3, 0, 16, 0, 3, -1);   // bookCat 4
    u8 out[8] = {0};
    int n = OfficeCollectHoldersByCategory(4, 8, out);
    CHECK_EQ(n, 2);
    CHECK_EQ((int)out[0], 10);
    CHECK_EQ((int)out[1], 16);
}

TEST(LawApply, LookupHolderCharacter) {
    SetupSuite();
    // Scan steps slots 0..36 by type at +8; first matching type's +4 id resolved.
    SetHolder(3, 9, /*id*/ 777, /*type*/ 22, 0, 3, -1);
    AddPerson(777, 22, 0);
    OfficePersonRecord rec{};
    CHECK(OfficeLookupHolderCharacter(22, &rec));
    CHECK(rec.present);
    CHECK_EQ((int)rec.office358, 22);
    // No such type -> false.
    CHECK(!OfficeLookupHolderCharacter(99, &rec));
}

TEST(LawApply, LookupHolderCharacterUnresolvedReturnsFalse) {
    SetupSuite();
    SetHolder(2, 9, 888, 7, 0, 3, -1);  // type 7 present, but 888 not a person
    OfficePersonRecord rec{};
    CHECK(!OfficeLookupHolderCharacter(7, &rec));
    CHECK(!rec.present);
}

TEST(LawApply, FindHighestVacantRank) {
    SetupSuite();
    // OfficeGetEntryByHolder scans for type(+8)==rank; we want a vacant slot
    // (state 3, rank<=1) at type 20, none at 21..27.
    SetHolder(0, 0, 0, 20, /*rank*/ 0, /*state*/ 3, -1);
    // Start at 27 -> should walk down to 20.
    CHECK_EQ((int)OfficeFindHighestVacantRank(27), 20);
    // startRank 0 -> 0.
    CHECK_EQ((int)OfficeFindHighestVacantRank(0), 0);
}

TEST(LawApply, CopyEntriesByIndex) {
    SetupSuite();
    SetHolder(5, 55, 0, 4, 0, 3, -1);
    SetHolder(9, 99, 0, 8, 0, 3, -1);
    OfficeHolder buf[2];
    std::memset(buf, 0, sizeof(buf));
    reinterpret_cast<u8*>(&buf[0])[0] = 5;  // index 5
    reinterpret_cast<u8*>(&buf[1])[0] = 9;  // index 9
    int r = OfficeCopyEntriesByIndex(2, buf);
    CHECK_EQ(r, 2);
    CHECK_EQ((int)buf[0].holder, 55);
    CHECK_EQ((int)buf[0].type, 4);
    CHECK_EQ((int)buf[1].holder, 99);
    CHECK_EQ((int)buf[1].type, 8);
}

TEST(LawApply, HasAvailableSuccessor) {
    SetupSuite();
    // rank 4 -> reqCode(4)==2, bookCat(4)==1. Provide a holder in reqCode-2 book
    // whose +4 id resolves to a person who is the next rank in category and not
    // busy. Next-rank rule (OfficeIsNextRankInCategory): reqCode must match and
    // the bookCat %3 progression must hold. We pick a person office358 whose
    // record makes IsNextRankInCategory(rank=4) true.
    // reqCode(4)==2 types are 4,5,6 (bookCat 1,2,3). Put holder type 4 in slot 0.
    SetHolder(0, 1, /*id*/ 900, 4, 0, 3, -1);
    // IsNextRankInCategory(p, 4): reqCode(p.office358)==reqCode(4)==2 and
    // (bookCat(4)%3 / bookCat(office)%3) progression. bookCat(4)=1 -> t=1; need
    // o==2 -> bookCat(office)%3==2 -> office bookCat 2 -> type 5.
    AddPerson(900, /*office358*/ 5, /*busy433*/ 0);
    CHECK(OfficeHasAvailableSuccessor(4));

    // Busy flag set -> no successor.
    ResetPeople();
    AddPerson(900, 5, 1);
    CHECK(!OfficeHasAvailableSuccessor(4));
}

TEST(LawApply, CollectCategoryRankList) {
    SetupSuite();
    // office358 = 1 -> reqCode 1, bookCat 1 (1%3==1 -> v21=0).
    // Collected holders are reqCode-1 types {1,2,3} (bookCat 1,2,3 -> %3 1,2,0).
    // Only the bookCat%3==0 entry (type 3) is appended.
    SetHolder(0, 1, 0, 1, 0, /*state*/ 3, -1);
    SetHolder(1, 2, 0, 2, 0, 3, -1);
    SetHolder(2, 3, 0, 3, 0, 3, -1);
    u8 out[8] = {0};
    int n = OfficeCollectCategoryRankList(/*office358*/ 1, 8, out);
    CHECK_EQ(n, 1);
    CHECK_EQ((int)out[0], 3);
    // office358 == 0 -> 0.
    CHECK_EQ(OfficeCollectCategoryRankList(0, 8, out), 0);
}

TEST(LawApply, GetSecondaryHolderEntry) {
    SetupSuite();
    // Slot 31 (v8 == 186) holds office type 5 with city/id 12345.
    SetHolder(31, 9, /*city/id*/ 12345, /*type*/ 5, 0, 3, -1);
    OfficeSecondaryPerson p{};
    p.present = true;
    p.marker  = 7;     // not 0xFFFF
    p.ownerId = 12345;
    p.has361  = 1;
    OfficeDef def{};
    OfficeHolder holder{};
    int r = OfficeGetSecondaryHolderEntry(p, &def, &holder);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)holder.type, 5);
    OfficeDef expect{};
    OfficeGetDefinition(5, &expect);
    CHECK_EQ(def.word0, expect.word0);
    CHECK_EQ(def.flag, expect.flag);
    CHECK_EQ(def.textId, expect.textId);

    // has361 == 0 -> 0.
    p.has361 = 0;
    CHECK_EQ(OfficeGetSecondaryHolderEntry(p, &def, &holder), 0);
    // Invalid marker -> 0.
    p.has361 = 1; p.marker = 0xFFFF;
    CHECK_EQ(OfficeGetSecondaryHolderEntry(p, &def, &holder), 0);
}
