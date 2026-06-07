#include "test.h"

// e2e: drive a full Level-3 guild-office contact -> dialog flow plus an election
// candidate-window pass across the world_economy3 kernels, exactly as the shells
// chain them: def-kind selects the contact status id, the rank-check code gates the
// dialog, the def-kind picks A/B/C, and the office name base is rendered with the
// promoted flag; then a candidate window classifies offices and rates outputs.
#include "world/world_economy3.h"

#include <vector>

using namespace guild::world;

// Replays the Level3 contact->dialog decision the loop makes for a def-kind.
namespace {
struct Level3Outcome {
    int contactId;
    Level3Action action;
    Level3Dialog dialog;
    int nameTextId;
};

Level3Outcome RunLevel3(unsigned defKind, int rankCheckCode, unsigned promoted) {
    Level3Outcome o{};
    o.contactId  = GuildLevel3ContactStatusId(defKind);
    o.action     = GuildLevel3RankAction(rankCheckCode);
    o.dialog     = (o.action == Level3Action::kShowOfficeDialog)
                       ? GuildLevel3DialogForDefKind(defKind)
                       : Level3Dialog::kNone;
    o.nameTextId = GuildOfficeNameTextId(promoted, defKind);
    return o;
}
}  // namespace

TEST(WorldEconomy3E2E, Level3ContactToDialogFlow) {
    // def-kind 30, rank check passes (1), promoted: contact 4751, dialog A, name 590.
    Level3Outcome a = RunLevel3(30, 1, 1);
    CHECK_EQ(a.contactId, 4751);
    CHECK(a.action == Level3Action::kShowOfficeDialog);
    CHECK(a.dialog == Level3Dialog::kDialogA);
    CHECK_EQ(a.nameTextId, 590);  // 30 + 560

    // def-kind 31, rank check passes, not promoted: contact 4733, dialog B, name 556.
    Level3Outcome b = RunLevel3(31, 1, 0);
    CHECK_EQ(b.contactId, 4733);
    CHECK(b.dialog == Level3Dialog::kDialogB);
    CHECK_EQ(b.nameTextId, 556);  // 31 + 525

    // def-kind 32, rank check fails with -1: messagebox, no dialog opened.
    Level3Outcome c = RunLevel3(32, -1, 0);
    CHECK_EQ(c.contactId, 4743);
    CHECK(c.action == Level3Action::kShowMessageBox);
    CHECK(c.dialog == Level3Dialog::kNone);

    // def-kind 33 -> dialog B (shares 31's id), rank check "other" -> skill check.
    Level3Outcome d = RunLevel3(33, 0, 1);
    CHECK_EQ(d.contactId, 4733);
    CHECK(d.action == Level3Action::kCheckSkill);
    CHECK(d.dialog == Level3Dialog::kNone);  // not the office-dialog branch
}

// A candidate row, as the election window builds it: name id (held/free) + rating.
TEST(WorldEconomy3E2E, ElectionCandidateWindowPass) {
    // The window aborts for category 0, collects-by-category for 1..6, elective for 7.
    CHECK(GuildElectionCollectMode(0) == ElectionCollectMode::kAbort);
    CHECK(GuildElectionCollectMode(3) == ElectionCollectMode::kByCategory);
    CHECK(GuildElectionCollectMode(7) == ElectionCollectMode::kElectiveOffices);

    // Three candidates: (held, out=80), (free, out=400), (held, out=700).
    struct Cand { unsigned held; float out; };
    std::vector<Cand> cands = {{1, 80.0f}, {0, 400.0f}, {1, 700.0f}};
    std::vector<int> nameIds, ratingIds;
    for (const auto& c : cands) {
        nameIds.push_back(GuildCandidateOfficeNameId(c.held));
        ratingIds.push_back(GuildCandidateOutputRatingId(c.out));
    }
    CHECK_EQ(nameIds[0], 1596);   // held
    CHECK_EQ(nameIds[1], 1597);   // free
    CHECK_EQ(nameIds[2], 1596);
    CHECK_EQ(ratingIds[0], 1605); // 80 < 100
    CHECK_EQ(ratingIds[1], 1603); // 350 <= 400 < 650
    CHECK_EQ(ratingIds[2], 1602); // 700 >= 650

    // Title text-id select for the window header.
    CHECK_EQ(GuildElectionTitleTextId(false), 0);
    CHECK_EQ(GuildElectionTitleTextId(true), 0x9D);
}

// Agenda window buckets a holder table into member / successor rows.
TEST(WorldEconomy3E2E, AgendaWindowBuckets) {
    std::vector<guild::u8> states = {2, 3, 0, 2, 3, 1, 2};
    int members = 0, successors = 0, skipped = 0;
    for (auto s : states) {
        switch (GuildAgendaBucket(s)) {
            case AgendaBucket::kMember:    ++members; break;
            case AgendaBucket::kSuccessor: ++successors; break;
            case AgendaBucket::kSkip:      ++skipped; break;
        }
    }
    CHECK_EQ(members, 3);
    CHECK_EQ(successors, 2);
    CHECK_EQ(skipped, 2);
}

// Level2 join: fee floor vs 1% of wealth, then office name for the join dialog.
TEST(WorldEconomy3E2E, Level2JoinDialogFee) {
    // 50000 * (float)0.01 == 499.99998.. (float-precision rate) -> truncates to 499.
    CHECK_EQ(GuildLevel2JoinFee(50000), 499);
    CHECK_EQ(GuildLevel2JoinFee(10000), 160);   // 100.0 < 160 -> floor
    // poorest player still pays the floor.
    CHECK_EQ(GuildLevel2JoinFee(0), 160);
}
