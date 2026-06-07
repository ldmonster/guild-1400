#include "test.h"

// Unit tests for world_economy3 — the deterministic guild/office dialog kernels.
// Golden values computed against the recovered thresholds / switch tables.
#include "world/world_economy3.h"

using namespace guild::world;

// --- shared office-name text base (promoted ? +560 : +525) -----------------
TEST(WorldEconomy3, OfficeNameTextId) {
    CHECK_EQ(GuildOfficeNameTextId(0, 30), 555);   // 30 + 525
    CHECK_EQ(GuildOfficeNameTextId(1, 30), 590);   // 30 + 560
    CHECK_EQ(GuildOfficeNameTextId(0, 0), 525);
    CHECK_EQ(GuildOfficeNameTextId(1, 0), 560);
    CHECK_EQ(GuildOfficeNameTextId(7, 33), 593);   // any-nonzero promoted -> +560
    // def-kind read as unsigned byte: 0x100|30 truncates to 30.
    CHECK_EQ(GuildOfficeNameTextId(0, 0x11E), 555);
}

// --- candidate held/free office name id -------------------------------------
TEST(WorldEconomy3, CandidateOfficeNameId) {
    CHECK_EQ(GuildCandidateOfficeNameId(0), 1597);   // free
    CHECK_EQ(GuildCandidateOfficeNameId(1), 1596);   // held
    CHECK_EQ(GuildCandidateOfficeNameId(200), 1596); // any nonzero -> held
    CHECK_EQ(GuildCandidateOfficeNameId(0x100), 1597); // low byte zero -> free
}

// --- candidate output rating tier (100/350/650 -> 1605/1604/1603/1602) ------
TEST(WorldEconomy3, CandidateOutputRatingId) {
    CHECK_EQ(GuildCandidateOutputRatingId(0.0f), 1605);
    CHECK_EQ(GuildCandidateOutputRatingId(99.99f), 1605);
    CHECK_EQ(GuildCandidateOutputRatingId(100.0f), 1604);   // boundary inclusive
    CHECK_EQ(GuildCandidateOutputRatingId(349.99f), 1604);
    CHECK_EQ(GuildCandidateOutputRatingId(350.0f), 1603);
    CHECK_EQ(GuildCandidateOutputRatingId(649.99f), 1603);
    CHECK_EQ(GuildCandidateOutputRatingId(650.0f), 1602);
    CHECK_EQ(GuildCandidateOutputRatingId(99999.0f), 1602);
}

// --- election title text id select ------------------------------------------
TEST(WorldEconomy3, ElectionTitleTextId) {
    CHECK_EQ(GuildElectionTitleTextId(true), 0x9D);  // 157
    CHECK_EQ(GuildElectionTitleTextId(false), 0);    // -> "$Z$[%s$]" string form
}

// --- election collect-mode gate ---------------------------------------------
TEST(WorldEconomy3, ElectionCollectMode) {
    CHECK(GuildElectionCollectMode(0) == ElectionCollectMode::kAbort);
    CHECK(GuildElectionCollectMode(-3) == ElectionCollectMode::kAbort);
    CHECK(GuildElectionCollectMode(1) == ElectionCollectMode::kByCategory);
    CHECK(GuildElectionCollectMode(6) == ElectionCollectMode::kByCategory);
    CHECK(GuildElectionCollectMode(7) == ElectionCollectMode::kElectiveOffices);
    CHECK(GuildElectionCollectMode(8) == ElectionCollectMode::kAbort);
}

// --- Level3 rank-check dispatch ---------------------------------------------
TEST(WorldEconomy3, Level3RankAction) {
    CHECK(GuildLevel3RankAction(1) == Level3Action::kShowOfficeDialog);
    CHECK(GuildLevel3RankAction(-1) == Level3Action::kShowMessageBox);
    CHECK(GuildLevel3RankAction(0) == Level3Action::kCheckSkill);
    CHECK(GuildLevel3RankAction(2) == Level3Action::kCheckSkill);
    CHECK(GuildLevel3RankAction(-2) == Level3Action::kCheckSkill);
}

// --- Level3 def-kind -> dialog ----------------------------------------------
TEST(WorldEconomy3, Level3DialogForDefKind) {
    CHECK(GuildLevel3DialogForDefKind(30) == Level3Dialog::kDialogA);
    CHECK(GuildLevel3DialogForDefKind(31) == Level3Dialog::kDialogB);
    CHECK(GuildLevel3DialogForDefKind(33) == Level3Dialog::kDialogB);
    CHECK(GuildLevel3DialogForDefKind(32) == Level3Dialog::kDialogC);
    CHECK(GuildLevel3DialogForDefKind(29) == Level3Dialog::kNone);
    CHECK(GuildLevel3DialogForDefKind(34) == Level3Dialog::kNone);
}

// --- Level3 contact status id -----------------------------------------------
TEST(WorldEconomy3, Level3ContactStatusId) {
    CHECK_EQ(GuildLevel3ContactStatusId(30), 4751);
    CHECK_EQ(GuildLevel3ContactStatusId(31), 4733);
    CHECK_EQ(GuildLevel3ContactStatusId(33), 4733);
    CHECK_EQ(GuildLevel3ContactStatusId(32), 4743);
    CHECK_EQ(GuildLevel3ContactStatusId(29), 0);
    CHECK_EQ(GuildLevel3ContactStatusId(0), 0);
}

// --- Level2 join fee: max(160, wealth * 0.01) truncated ---------------------
TEST(WorldEconomy3, Level2JoinFee) {
    CHECK_EQ(GuildLevel2JoinFee(0), 160);
    CHECK_EQ(GuildLevel2JoinFee(5), 160);
    CHECK_EQ(GuildLevel2JoinFee(16000), 160);     // 159.99.. -> floor 160
    CHECK_EQ(GuildLevel2JoinFee(16001), 160);     // 160.0099 > floor -> trunc 160
    CHECK_EQ(GuildLevel2JoinFee(1000000), 9999);  // 9999.99 -> 9999
    CHECK_EQ(GuildLevel2JoinFee(2000000), 19999);
    CHECK_EQ(GuildLevel2JoinFee(-1000000), 160);  // negative -> below floor
}

// --- agenda bucket from holder +16 state byte -------------------------------
TEST(WorldEconomy3, AgendaBucket) {
    CHECK(GuildAgendaBucket(2) == AgendaBucket::kMember);
    CHECK(GuildAgendaBucket(3) == AgendaBucket::kSuccessor);
    CHECK(GuildAgendaBucket(0) == AgendaBucket::kSkip);
    CHECK(GuildAgendaBucket(1) == AgendaBucket::kSkip);
    CHECK(GuildAgendaBucket(4) == AgendaBucket::kSkip);
}

// --- hook-routed convenience wrappers, default (inert) path -----------------
TEST(WorldEconomy3, ViaHookDefaultsAreInert) {
    WorldEconomy3Hooks empty{};
    WorldEconomy3Hooks prev = WorldEconomy3SetHooks(empty);
    // default output 0 -> rating tier 1605; default wealth 0 -> fee floor 160.
    CHECK_EQ(GuildCandidateRatingViaHook(nullptr), 1605);
    CHECK_EQ(GuildLevel2JoinFeeViaHook(42, nullptr), 160);
    WorldEconomy3SetHooks(prev);
}

// --- hook-routed wrappers with installed hooks ------------------------------
namespace {
float FakeOutput(const void*) { return 400.0f; }       // -> tier 1603
int   FakeWealth(int, const void*) { return 5000000; } // -> 49999
}
TEST(WorldEconomy3, ViaHookInstalled) {
    WorldEconomy3Hooks h{};
    h.buildingCurrentOutput = &FakeOutput;
    h.personTotalWealth = &FakeWealth;
    WorldEconomy3Hooks prev = WorldEconomy3SetHooks(h);
    CHECK_EQ(GuildCandidateRatingViaHook(nullptr), 1603);
    CHECK_EQ(GuildLevel2JoinFeeViaHook(1, nullptr), 49999);
    WorldEconomy3SetHooks(prev);
}
