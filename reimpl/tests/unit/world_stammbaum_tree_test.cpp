#include "test.h"

#include "world/stammbaum_tree.h"

#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
StammPerson MkPerson(i32 id, i16 portrait, u8 cat) {
    StammPerson p;
    p.id = id;
    p.portrait = portrait;
    p.category = cat;
    return p;
}
} // namespace

// A small dynasty:
//   P1  father of P10     (portrait 101)
//   P2  mother of P10     (portrait 102)
//   P20 spouse of P10     (portrait 120)
//   P10 focus             (portrait 110), children {P30, P31, P40 absent slot}
//   P30 child (cat 5)     (portrait 130)
//   P31 child (cat 12)    -> filtered out of OWN row (category >= 10)
//   P40 spouse's child    (portrait 140, cat 6)
TEST(WorldStammbaum, GatherFullTree) {
    std::vector<StammPerson> recs;
    StammPerson p1  = MkPerson(1,  101, 5);
    StammPerson p2  = MkPerson(2,  102, 5);
    StammPerson p20 = MkPerson(20, 120, 5);
    StammPerson p10 = MkPerson(10, 110, 5);
    StammPerson p30 = MkPerson(30, 130, 5);
    StammPerson p31 = MkPerson(31, 131, 12);  // category >= 10 -> filtered from own row
    StammPerson p40 = MkPerson(40, 140, 6);

    p10.father = 1; p10.mother = 2; p10.spouse = 20;
    p10.children[0] = 30; p10.children[1] = 31;       // own children
    p20.children[0] = 30; p20.children[1] = 40;       // spouse's children (30 == focus's)

    recs = {p1, p2, p20, p10, p30, p31, p40};
    StammWorld world{recs.data(), static_cast<int>(recs.size())};

    StammTree t = StammbaumGatherTree(world, 10);

    CHECK_EQ(t.focusPortrait, static_cast<i16>(110));

    CHECK(t.fatherShown);
    CHECK_EQ(t.fatherPortrait, static_cast<i16>(101));
    CHECK(t.motherKnown);
    CHECK_EQ(t.motherPortrait, static_cast<i16>(102));
    CHECK(t.spouseKnown);
    CHECK_EQ(t.spousePortrait, static_cast<i16>(120));

    // Own children: P30 kept (cat 5), P31 filtered (cat 12) -> only one.
    CHECK_EQ(static_cast<int>(t.ownChildren.size()), 1);
    CHECK_EQ(t.ownChildren[0], static_cast<i16>(130));

    // Spouse's children: P30 (== focus id 10? no, child id 30 != focus 10) kept,
    // P40 kept. Neither equals the focus person's id (10), so both appear.
    CHECK_EQ(static_cast<int>(t.spouseChildren.size()), 2);
    CHECK_EQ(t.spouseChildren[0], static_cast<i16>(130));
    CHECK_EQ(t.spouseChildren[1], static_cast<i16>(140));
}

// The father edge is hidden when EITHER the focus or the father carries flag 0x4.
TEST(WorldStammbaum, FatherFlagHidesEdge) {
    std::vector<StammPerson> recs;
    StammPerson dad   = MkPerson(1, 101, 5);
    StammPerson focus = MkPerson(10, 110, 5);
    focus.father = 1;

    // Case A: father has the hide flag.
    dad.flags = kStammFlagHideFather;
    recs = {dad, focus};
    {
        StammWorld w{recs.data(), static_cast<int>(recs.size())};
        StammTree t = StammbaumGatherTree(w, 10);
        CHECK(!t.fatherShown);
    }

    // Case B: focus has the hide flag.
    recs[0].flags = 0;
    recs[1].flags = kStammFlagHideFather;
    {
        StammWorld w{recs.data(), static_cast<int>(recs.size())};
        StammTree t = StammbaumGatherTree(w, 10);
        CHECK(!t.fatherShown);
    }

    // Case C: neither flag -> shown.
    recs[1].flags = 0;
    {
        StammWorld w{recs.data(), static_cast<int>(recs.size())};
        StammTree t = StammbaumGatherTree(w, 10);
        CHECK(t.fatherShown);
        CHECK_EQ(t.fatherPortrait, static_cast<i16>(101));
    }
}

// A spouse's child that equals the focus person's id is skipped (dedup rule).
TEST(WorldStammbaum, SpouseChildSkipsFocusId) {
    std::vector<StammPerson> recs;
    StammPerson focus  = MkPerson(10, 110, 5);
    StammPerson spouse = MkPerson(20, 120, 5);
    StammPerson child  = MkPerson(40, 140, 5);
    focus.spouse = 20;
    spouse.children[0] = 10;   // == focus id -> skipped
    spouse.children[1] = 40;   // kept

    recs = {focus, spouse, child};
    StammWorld w{recs.data(), static_cast<int>(recs.size())};
    StammTree t = StammbaumGatherTree(w, 10);

    CHECK_EQ(static_cast<int>(t.spouseChildren.size()), 1);
    CHECK_EQ(t.spouseChildren[0], static_cast<i16>(140));
}

// Own-children row caps at kStammMaxRowOwn (5) even with more child slots filled.
TEST(WorldStammbaum, OwnChildrenCap) {
    std::vector<StammPerson> recs;
    StammPerson focus = MkPerson(10, 110, 5);
    for (int k = 0; k < kStammChildSlots; ++k)
        focus.children[k] = 100 + k;
    recs.push_back(focus);
    for (int k = 0; k < kStammChildSlots; ++k)
        recs.push_back(MkPerson(100 + k, static_cast<i16>(200 + k), 5));

    StammWorld w{recs.data(), static_cast<int>(recs.size())};
    StammTree t = StammbaumGatherTree(w, 10);
    CHECK_EQ(static_cast<int>(t.ownChildren.size()), kStammChildSlots);
    CHECK_EQ(t.ownChildren[0], static_cast<i16>(200));
    CHECK_EQ(t.ownChildren[4], static_cast<i16>(204));
}

TEST(WorldStammbaum, UnknownFocusYieldsEmpty) {
    std::vector<StammPerson> recs = {MkPerson(10, 110, 5)};
    StammWorld w{recs.data(), static_cast<int>(recs.size())};
    StammTree t = StammbaumGatherTree(w, 999);
    CHECK_EQ(t.focusPortrait, static_cast<i16>(0));
    CHECK(!t.fatherShown);
    CHECK(!t.spouseKnown);
    CHECK_EQ(static_cast<int>(t.ownChildren.size()), 0);
}

// GetFamilyRecord owning-house resolver.
TEST(WorldStammbaum, GetFamilyRecordIndex) {
    // Category must be 5/6/7 and signByte < 0; index = houseWord & 0x0F.
    FamilyRecordKey ok{6, static_cast<i8>(-1), 0x0A37};  // low nibble 7
    CHECK_EQ(PersonGetFamilyRecordIndex(ok), 7);

    FamilyRecordKey ok2{5, static_cast<i8>(-128), 0xFF0F};  // low nibble 0xF
    CHECK_EQ(PersonGetFamilyRecordIndex(ok2), 15);

    FamilyRecordKey ok3{7, static_cast<i8>(-1), 0x0000};
    CHECK_EQ(PersonGetFamilyRecordIndex(ok3), 0);

    // Wrong category -> none.
    FamilyRecordKey badCat{4, static_cast<i8>(-1), 0x0A};
    CHECK_EQ(PersonGetFamilyRecordIndex(badCat), kStammNone);

    // Non-negative sign byte -> none.
    FamilyRecordKey badSign{6, 0, 0x0A};
    CHECK_EQ(PersonGetFamilyRecordIndex(badSign), kStammNone);

    // Resolve against a table.
    std::vector<HouseRecord> houses(16);
    houses[7].word[0] = 4242;
    const HouseRecord* h = PersonGetFamilyRecord(ok, houses.data(),
                                                 static_cast<int>(houses.size()));
    CHECK(h != nullptr);
    CHECK_EQ(h->word[0], static_cast<i16>(4242));

    // Out-of-range table -> nullptr.
    const HouseRecord* none = PersonGetFamilyRecord(ok2, houses.data(), 4);
    CHECK(none == nullptr);
}
