// End-to-end: a player opens the dynasty (Stammbaum) panel. We gather the family
// tree rooted at the patriarch, resolve his owning house record, and render the
// family's accumulated wealth with thousands separators — exercising the genuine
// genealogy gather (0x55ab84), the house resolver (0x58c408) and the money
// formatter (0x58f798) as one flow, checked against hand-computed references.
#include "test.h"

#include "world/stammbaum_tree.h"
#include "world/money_format.h"

#include <string>
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
std::string Show(const std::string& s) {
    std::string out;
    for (char c : s)
        out += (c == kCurrencyGlyph) ? std::string("<G>") : std::string(1, c);
    return out;
}
} // namespace

TEST(WorldStammbaumE2E, OpenDynastyPanel) {
    // ---- Build a three-generation dynasty -----------------------------------
    //   grandfather GF(1) -> father F(10) -> children C1(100), C2(101)
    //   F married to spouse SP(20); SP also raised C1 and an extra child C3(102).
    StammPerson gf  = MkPerson(1,  10, 5);
    StammPerson f   = MkPerson(10, 11, 6);   // category 6 (family head)
    StammPerson mo  = MkPerson(2,  12, 5);   // father's mother
    StammPerson sp  = MkPerson(20, 20, 5);   // spouse
    StammPerson c1  = MkPerson(100, 30, 5);
    StammPerson c2  = MkPerson(101, 31, 5);
    StammPerson c3  = MkPerson(102, 32, 7);
    StammPerson c4  = MkPerson(103, 33, 11); // category >= 10 -> filtered from own row

    f.father = 1; f.mother = 2; f.spouse = 20;
    f.children[0] = 100; f.children[1] = 101; f.children[2] = 103;  // own (103 filtered)
    sp.children[0] = 100; sp.children[1] = 102;                     // spouse's

    std::vector<StammPerson> recs = {gf, f, mo, sp, c1, c2, c3, c4};
    StammWorld world{recs.data(), static_cast<int>(recs.size())};

    // ---- Gather the family tree from the panel's focus person (F) -----------
    StammTree tree = StammbaumGatherTree(world, 10);

    CHECK_EQ(tree.focusPortrait, static_cast<i16>(11));
    CHECK(tree.fatherShown);
    CHECK_EQ(tree.fatherPortrait, static_cast<i16>(10));
    CHECK(tree.motherKnown);
    CHECK_EQ(tree.motherPortrait, static_cast<i16>(12));
    CHECK(tree.spouseKnown);
    CHECK_EQ(tree.spousePortrait, static_cast<i16>(20));

    // Own children: C1, C2 (C4 filtered for category 11).
    CHECK_EQ(static_cast<int>(tree.ownChildren.size()), 2);
    CHECK_EQ(tree.ownChildren[0], static_cast<i16>(30));
    CHECK_EQ(tree.ownChildren[1], static_cast<i16>(31));

    // Spouse's children: C1, C3 (neither equals focus id 10).
    CHECK_EQ(static_cast<int>(tree.spouseChildren.size()), 2);
    CHECK_EQ(tree.spouseChildren[0], static_cast<i16>(30));
    CHECK_EQ(tree.spouseChildren[1], static_cast<i16>(32));

    // ---- Resolve the family head's owning house record ----------------------
    // F is category 6 with a negative sign byte; house slot = houseWord & 0xF.
    FamilyRecordKey key{f.category, static_cast<i8>(-1), 0x00C5};  // low nibble 5
    std::vector<HouseRecord> houses(16);
    houses[5].word[0] = 7;          // a marker on the resolved house
    const HouseRecord* house =
        PersonGetFamilyRecord(key, houses.data(), static_cast<int>(houses.size()));
    CHECK(house != nullptr);
    CHECK_EQ(PersonGetFamilyRecordIndex(key), 5);
    CHECK_EQ(house->word[0], static_cast<i16>(7));

    // ---- Render the dynasty's accumulated wealth ----------------------------
    // Sum a per-member wealth figure, then format with separators (rate 1).
    i32 familyWealth = 1'250'000 + 84'300 + 999;   // 1_335_299
    CHECK_EQ(Show(MoneyFormatWithSeparators(familyWealth)), "1.335.299<G>");

    // A debt (negative) renders with a leading '-'.
    CHECK_EQ(Show(MoneyFormatWithSeparators(-50'000)), "-50.000<G>");

    // Wealth shown in a foreign currency: divide by the rate first (round-half-up).
    // 1_335_299 / 100 = 13352.99 -> 13353.
    CHECK_EQ(Show(MoneyFormatWithSeparators(familyWealth, 100)), "13.353<G>");
}
