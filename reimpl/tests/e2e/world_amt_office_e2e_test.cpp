// End-to-end flows across the Amt office slot table, the guild-rank eligibility
// ladder, and the History chronicle second-pass pipeline. Exercises whole
// sequences rather than single functions.
#include "test.h"

#include <cstring>

#include "world/amt_slot_table.h"
#include "world/guild_rank.h"
#include "world/guild.h"
#include "world/history_second_pass.h"

using namespace guild;
using namespace guild::world;

namespace {
AmtSlot FreeSlot() {
    AmtSlot s;
    std::memset(&s, 0, sizeof(s));
    s.marker = kAmtSlotFreeHi;
    return s;
}
AmtSlot OccupiedSlot(u8 x, u8 y, i32 objectId, i32 key, u8 size) {
    AmtSlot s;
    std::memset(&s, 0, sizeof(s));
    std::memcpy(&s, &key, sizeof(key)); // +0 dword key
    s.x = x; s.y = y; s.size = size; s.objectId = objectId; s.marker = 0x00;
    return s;
}
} // namespace

// A placement -> lookup -> hit-test flow on one slot table.
TEST(world_amt_office_e2e, PlaceThenLocate) {
    AmtSlot slots[kAmtSlotCount];
    for (int i = 0; i < kAmtSlotCount; ++i)
        slots[i] = FreeSlot();

    // Initially every coordinate is placeable (no occupied slot at the anchor).
    CHECK_EQ(AmtFindFreePlacement(slots, 40, 40, 4), 1);

    // "Place" an office object at (40,40): occupy a slot with id + key + size.
    slots[12] = OccupiedSlot(40, 40, /*objectId=*/0x1001, /*key=*/0xABCDEF01,
                             /*size=*/6);

    // Now all three lookups resolve to the same slot.
    int byCoord = AmtFindSlotByCoord(slots, 40, 40);
    int byId    = AmtFindSlotByObjectId(slots, 0x1001);
    int byKey   = AmtFindRecordByKey(slots, 0xABCDEF01);
    CHECK_EQ(byCoord, 12);
    CHECK_EQ(byId, 12);
    CHECK_EQ(byKey, 12);

    // The hit-test box (half-extent 3) contains nearby points and rejects far ones.
    CHECK_EQ(AmtFindSlotAtPoint(slots, 43, 43), 12);
    CHECK_EQ(AmtFindSlotAtPoint(slots, 37, 40), 12);
    CHECK_EQ(AmtFindSlotAtPoint(slots, 44, 40), -1);

    // Re-placing at the now-occupied anchor is blocked.
    CHECK_EQ(AmtFindFreePlacement(slots, 40, 40, 4), 0);

    // A miss for unknown id/key/coord.
    CHECK_EQ(AmtFindSlotByObjectId(slots, 0x9999), -1);
    CHECK_EQ(AmtFindRecordByKey(slots, 0x0), -1);
    CHECK_EQ(AmtFindSlotByCoord(slots, 41, 40), -1);
}

// The office-type record finder driven across a small type table, then mapped
// through the office-def book/category lookup.
TEST(world_amt_office_e2e, TypeRecordThenBookCat) {
    AmtTypeRecord recs[5] = {
        {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 0},
    };
    for (i16 t = 1; t <= 5; ++t)
        CHECK_EQ(AmtFindOfficeTypeRecord(recs, 5, t), t - 1);
    CHECK_EQ(AmtFindOfficeTypeRecord(recs, 5, 6), -1);

    // AmtGetOfficeType resolves a rank to its book/category (shared dword_62EC8E).
    // Ranks 4,5,6 all map to book/category 1,2,3 in the recovered table.
    CHECK_EQ(AmtGetOfficeType(4), (u8)1);
    CHECK_EQ(AmtGetOfficeType(5), (u8)2);
    CHECK_EQ(AmtGetOfficeType(6), (u8)3);
}

// The full guild-rank eligibility ladder L1 -> L2 -> L3 for one rising character.
TEST(world_amt_office_e2e, GuildEligibilityLadder) {
    auto masterYes = [](int, void*) { return true; };

    GuildPlayer p{};
    p.rank = 30;            // entry guild rank
    p.flags459 = 0;
    p.money = 10;

    // Level 1: no master query; money>=3 -> eligible.
    CHECK(GuildGetEligibility(p) == GuildEligibility::kEligible);
    // Level 2: needs a master (folds standing>=2); money>=5 -> eligible.
    CHECK(GuildCheckRankLevel2(p, masterYes, nullptr) == GuildEligibility::kEligible);
    // Level 3: needs a master (folds standing>=3); money>=8 -> eligible.
    CHECK(GuildCheckRankLevel3(p, masterYes, nullptr) == GuildEligibility::kEligible);

    // As the character joins each tier the per-level flag flips that tier's result.
    GuildPlayer joined = p;
    joined.flags459 = 0x4;  // L1 joined-flag
    CHECK(GuildGetEligibility(joined) == GuildEligibility::kAlreadyInGuild);
    CHECK(GuildCheckRankLevel2(joined, masterYes, nullptr) == GuildEligibility::kEligible);
    CHECK(GuildCheckRankLevel3(joined, masterYes, nullptr) == GuildEligibility::kEligible);

    joined.flags459 = 0x4 | 0x8 | 0x10; // joined at all three tiers
    CHECK(GuildGetEligibility(joined) == GuildEligibility::kAlreadyInGuild);
    CHECK(GuildCheckRankLevel2(joined, masterYes, nullptr) == GuildEligibility::kAlreadyInGuild);
    CHECK(GuildCheckRankLevel3(joined, masterYes, nullptr) == GuildEligibility::kAlreadyInGuild);

    // Poorer character: each tier rejects on its own cost threshold (3/5/8).
    GuildPlayer poor = p;
    poor.flags459 = 0; poor.money = 4;
    CHECK(GuildGetEligibility(poor) == GuildEligibility::kEligible);            // >=3
    CHECK(GuildCheckRankLevel2(poor, masterYes, nullptr) == GuildEligibility::kInsufficientFunds); // <5
    CHECK(GuildCheckRankLevel3(poor, masterYes, nullptr) == GuildEligibility::kInsufficientFunds); // <8
}

// A chronicle label flows through the second-pass pipeline: collapse the bracket
// regions, then run the pass-orchestrator gate using the collapse result.
TEST(world_amt_office_e2e, ChronicleSecondPassPipeline) {
    struct Case { const char* label; const char* expect; bool ok; };
    const Case cases[] = {
        {"The [mayor#bishop] arrived",        "The mayor arrived", true},
        {"[He#She] left and [it#they] stayed","He left and it stayed", true},
        {"No markup here",                    "No markup here", true},
        {"[empty#x]",                         "empty", true},
        {"broken#label",                      "", false},   // '#' before '['
        {"[unterminated#region",              "", false},
    };

    for (const Case& c : cases) {
        char out[128];
        HistoryCollapseResult r = HistoryCollapseLabel(c.label, out);
        bool firstOk = (r == HistoryCollapseResult::kRendered);
        CHECK(firstOk == c.ok);
        if (firstOk)
            CHECK(std::strcmp(out, c.expect) == 0);

        // Drive the orchestrator: first-pass success == collapse success; assume the
        // second pass succeeds when the first does, with the commandline flag set.
        bool ranCmd = false;
        int passResult = HistoryRunLabelPasses(firstOk, firstOk, /*cmd=*/true, &ranCmd);
        CHECK_EQ(passResult, firstOk ? 1 : 0);
        CHECK_EQ(ranCmd, firstOk); // commandline fires only on full success
    }
}
