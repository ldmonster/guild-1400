// Unit tests for world/law_apply2 — the office promotion-list insertion sort,
// GetHolderEntryByCity, EvalApplyForCandidacy, and TryPromoteCharacter gate.
//
// Golden vectors for the insertion sort are produced by /tmp/gen_golden.py, which
// mirrors the decompiled algorithm against the REAL baked office-definition table
// + promotion-cost matrix from src/world/office.cpp. The C++ under test drives the
// real OfficeCanPromoteRank / OfficeDefBookCat, so the goldens validate the exact
// recovered insertion-sort logic (including its quirky non-monotonic ordering and
// the cap behavior).
#include "world/law_apply2.h"

#include <cstring>

#include "test.h"
#include "world/office.h"

using namespace guild;
using namespace guild::world;

namespace {

// Person with held office type 1, rank 1 (promotable into types 2, 5, 8; all
// bookCat 2, costs 0.0 / -2.5 / -5.0 from the real promotion-cost matrix).
OfficePerson MakePerson() {
    OfficePerson p{};
    p.ownerId    = 4242;
    p.officeType = 1;
    p.rank       = 1;
    p.candidacy  = 0;
    p.valid      = true;
    return p;
}

// Install the golden holder layout. Slots:
//   0: state3 rank0 type8     (cat2, cost-5.0)
//   1: state3 rank1 type2     (cat2, cost 0.0)
//   2: state3 rank2 type5     (cat2, cost-2.5)
//   3: state3 rank0 type8     (dup of slot 0 -> skipped)
//   4: state0 rank0 type2     (state != 3 -> skipped)
//   5: state3 rank4 type5     (rank>=4 -> nonfilt skip / filt rank-blocked flag)
//   6: state3 rank3 type99    (type 99 not promotable -> skipped)
void InstallGoldenHolders() {
    OfficeHolderTableReset();
    auto set = [](int i, int state, int rank, int type, i32 holderId, i32 city) {
        g_officeHolders[i].state  = static_cast<guild::u8>(state);
        g_officeHolders[i].rank   = rank;
        g_officeHolders[i].type   = static_cast<guild::u8>(type);
        g_officeHolders[i].holder = static_cast<guild::u8>(holderId);
        g_officeHolders[i].city   = city;
    };
    set(0, 3, 0,  8, 80, 1000);
    set(1, 3, 1,  2, 20, 1001);
    set(2, 3, 2,  5, 50, 1002);
    set(3, 3, 0,  8, 81, 1003);
    set(4, 0, 0,  2, 21, 1004);
    set(5, 3, 4,  5, 51, 1005);
    set(6, 3, 3, 99, 99, 1006);
}

const float kEmptyCost = -100.0f;  // bit pattern 0xC2C80000 == -1027080192

}  // namespace

TEST(LawApply2, PromotionListGoldenOrder) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    PromotionEntry out[6];
    int ret = OfficeBuildPromotionList(p, 6, out);

    CHECK_EQ(ret, 3);
    // Golden: [type5 cat2 -2.5][type2 cat2 0.0][type8 cat2 -5.0] then empties.
    CHECK_EQ(out[0].type, static_cast<int>(5));  CHECK_EQ(out[0].category, static_cast<int>(2));
    CHECK(out[0].cost == -2.5f);
    CHECK_EQ(out[1].type, static_cast<int>(2));  CHECK_EQ(out[1].category, static_cast<int>(2));
    CHECK(out[1].cost == 0.0f);
    CHECK_EQ(out[2].type, static_cast<int>(8));  CHECK_EQ(out[2].category, static_cast<int>(2));
    CHECK(out[2].cost == -5.0f);
    for (int i = 3; i < 6; ++i) {
        CHECK_EQ(out[i].type, static_cast<int>(0));
        CHECK_EQ(out[i].category, static_cast<int>(0));
        CHECK(out[i].cost == kEmptyCost);
    }
}

TEST(LawApply2, PromotionListFilteredSetsRankBlocked) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    PromotionEntry out[6];
    int rankBlocked = -7;
    int ret = OfficeBuildPromotionListFiltered(p, 6, &rankBlocked, out);

    CHECK_EQ(ret, 3);
    CHECK_EQ(rankBlocked, 1);  // slot 5 (rank>=4, promotable) trips the flag
    // Same ordering as the non-filtered variant.
    CHECK_EQ(out[0].type, static_cast<int>(5));
    CHECK_EQ(out[1].type, static_cast<int>(2));
    CHECK_EQ(out[2].type, static_cast<int>(8));
}

TEST(LawApply2, PromotionListFilteredNullFlagOk) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    PromotionEntry out[6];
    int ret = OfficeBuildPromotionListFiltered(p, 6, nullptr, out);
    CHECK_EQ(ret, 3);
    CHECK_EQ(out[0].type, static_cast<int>(5));
}

TEST(LawApply2, PromotionListCappedAtMax) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    PromotionEntry out[2];
    int ret = OfficeBuildPromotionList(p, 2, out);
    // Golden cap: ret=2, list [type5 -2.5][type8 -5.0]; type2 pushed out.
    CHECK_EQ(ret, 2);
    CHECK_EQ(out[0].type, static_cast<int>(5));  CHECK(out[0].cost == -2.5f);
    CHECK_EQ(out[1].type, static_cast<int>(8));  CHECK(out[1].cost == -5.0f);
}

TEST(LawApply2, PromotionListMaxZero) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    PromotionEntry out[1];
    CHECK_EQ(OfficeBuildPromotionList(p, 0, out), 0);
    int rb = -1;
    CHECK_EQ(OfficeBuildPromotionListFiltered(p, 0, &rb, out), 0);
    CHECK_EQ(rb, 0);
}

TEST(LawApply2, PromotionListEmptyWhenPersonInvalid) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    p.valid = false;  // CanPromoteRank rejects all -> no candidates
    PromotionEntry out[6];
    CHECK_EQ(OfficeBuildPromotionList(p, 6, out), 0);
    // Slots remain seeded.
    CHECK_EQ(out[0].type, static_cast<int>(0));
    CHECK(out[0].cost == kEmptyCost);
}

TEST(LawApply2, GetHolderEntryByCityMatchAndDecode) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    p.ownerId = 1002;  // matches slot 2 (.city == 1002, type 5)

    OfficeDef    def{};
    OfficeHolder holder{};
    int r = OfficeGetHolderEntryByCity(p, &def, &holder);
    CHECK_EQ(r, 1);
    CHECK_EQ(holder.type, static_cast<int>(5));
    CHECK_EQ(holder.city, 1002);
    // Def decoded for type 5: word0 byte +2 == bookCat(5) == 2.
    OfficeDef ref{};
    OfficeGetDefinition(5, &ref);
    CHECK_EQ(def.word0, ref.word0);
}

TEST(LawApply2, GetHolderEntryByCitySlotZero) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    p.ownerId = 1000;  // slot 0 fast path
    OfficeDef def{}; OfficeHolder holder{};
    CHECK_EQ(OfficeGetHolderEntryByCity(p, &def, &holder), 1);
    CHECK_EQ(holder.type, static_cast<int>(8));
}

TEST(LawApply2, GetHolderEntryByCityNoMatch) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    p.ownerId = 999999;  // no slot has this city
    OfficeDef def{}; OfficeHolder holder{};
    CHECK_EQ(OfficeGetHolderEntryByCity(p, &def, &holder), 0);
}

TEST(LawApply2, GetHolderEntryByCityInvalidGate) {
    InstallGoldenHolders();
    OfficePerson p = MakePerson();
    p.ownerId = 1000;
    OfficeDef def{}; OfficeHolder holder{};

    OfficePerson bad = p;
    bad.valid = false;            // !a1 / marker 0xFFFF gate
    CHECK_EQ(OfficeGetHolderEntryByCity(bad, &def, &holder), 0);

    OfficePerson noOffice = p;
    noOffice.officeType = 0;      // +358 == 0 gate
    CHECK_EQ(OfficeGetHolderEntryByCity(noOffice, &def, &holder), 0);
}

TEST(LawApply2, EvalApplyForCandidacyDispatch) {
    CHECK_EQ(OfficeEvalApplyForCandidacy(10, true), static_cast<int>(10));
    CHECK_EQ(OfficeEvalApplyForCandidacy(10, false), static_cast<int>(0));  // apply failed
    CHECK_EQ(OfficeEvalApplyForCandidacy(9, true), static_cast<int>(0));    // wrong opcode
    CHECK_EQ(OfficeEvalApplyForCandidacy(0, false), static_cast<int>(0));
}

TEST(LawApply2, TryPromoteCharacterGateRejects) {
    InstallGoldenHolders();
    OfficeSetPromoteCommandHook(nullptr, nullptr);
    OfficePromoteCommandLogReset();

    OfficePerson p = MakePerson();

    // Invalid person view -> -1, no command emitted.
    OfficePerson invalid = p; invalid.valid = false;
    CHECK_EQ(OfficeTryPromoteCharacter(invalid, p, p), -1);

    // from/to resolve to slots with different bookCat -> -1. slot 2 (type5,cat2)
    // vs a slot we point at a non-cat2 type. Build a person matching slot 0 city.
    OfficePerson from = p; from.ownerId = 1002;  // type5 cat2
    OfficePerson to   = p; to.ownerId   = 999999; // no slot -> resolve fails
    CHECK_EQ(OfficeTryPromoteCharacter(p, from, to), -1);

    int n = 0;
    OfficePromoteCommandLog(&n);
    CHECK_EQ(n, 0);
}

TEST(LawApply2, TryPromoteCharacterEmitsOnMatch) {
    // Two slots with the SAME type (so identical bookCat + reqCode) succeed.
    OfficeHolderTableReset();
    g_officeHolders[0].state = 3; g_officeHolders[0].rank = 0;
    g_officeHolders[0].type = 5;  g_officeHolders[0].holder = 70; g_officeHolders[0].city = 2000;
    g_officeHolders[1].state = 3; g_officeHolders[1].rank = 0;
    g_officeHolders[1].type = 5;  g_officeHolders[1].holder = 71; g_officeHolders[1].city = 2001;

    OfficeSetPromoteCommandHook(nullptr, nullptr);
    OfficePromoteCommandLogReset();

    OfficePerson p = MakePerson(); p.ownerId = 5555;
    OfficePerson from = MakePerson(); from.ownerId = 2000;
    OfficePerson to   = MakePerson(); to.ownerId   = 2001;

    int r = OfficeTryPromoteCharacter(p, from, to);
    CHECK_EQ(r, 0);  // default hook returns 0

    int n = 0;
    const OfficePromoteCommand* log = OfficePromoteCommandLog(&n);
    CHECK_EQ(n, 1);
    CHECK_EQ(log[0].personId, 5555);
    CHECK_EQ(log[0].fromType, static_cast<int>(70));  // holder char-id of `from` slot
    CHECK_EQ(log[0].toType, static_cast<int>(71));
    CHECK_EQ(log[0].tag, 6);
}
