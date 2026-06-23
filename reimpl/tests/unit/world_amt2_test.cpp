// Unit tests for the deferred Amt + guild-governance passes:
//   amt_enforcement   (law-violation enforcement sweep)
//   office_prosperity (prosperity update math, golden vectors)
//   guild_election    (full guild-master election, CalcZuenfte, UpdateOffices,
//                      production cadence, wage cycle, Aemter serialization)
#include <cmath>
#include <cstring>
#include <vector>

#include "crt/rand.h"
#include "tests/framework/test.h"
#include "world/amt_enforcement.h"
#include "world/guild_election.h"
#include "world/law.h"
#include "world/office.h"
#include "world/office_prosperity.h"

using namespace guild;
using namespace guild::world;

namespace {
bool feq(float a, float b) { return std::fabs(a - b) <= 1e-5f; }
} // namespace

// ===========================================================================
// office_prosperity — golden math.
// ===========================================================================
TEST(WorldAmt2Prosperity, AverageAndScoreGolden) {
    ProsperityInput in;
    in.ownerWealth = 1000;
    in.room[0] = 300; in.room[1] = 0; in.room[2] = 200;
    in.cityMax = 5000;
    // avg = (1000 + 500) / (2 nonzero + 1) = 500
    CHECK_EQ(ProsperityAverageWealth(in), 500);
    // score = (500/1000)*0.5 + 0.5*(1 - 1000/5000) = 0.25 + 0.4 = 0.65
    CHECK(feq(ProsperityCompute(in), 0.6499999761581421f));
}

TEST(WorldAmt2Prosperity, RatioClampedAtOne) {
    ProsperityInput in;
    in.ownerWealth = 100;
    in.room[0] = 1000; in.room[1] = 0; in.room[2] = 0; // avg = (100+1000)/2 = 550 > wealth
    in.cityMax = 1000;
    // ratio clamps to 1.0 -> score = 0.5 + 0.5*(1 - 100/1000) = 0.5 + 0.45 = 0.95
    CHECK(feq(ProsperityCompute(in), 0.95f));
}

TEST(WorldAmt2Prosperity, CommitSequenceAndDeltas) {
    struct Rec { ProsperityCommit which; i32 obj; float val; };
    static std::vector<Rec> log;
    log.clear();
    ProsperitySetCommitHook([](ProsperityCommit w, i32 o, float v, void*) {
        log.push_back({w, o, v});
    }, nullptr);

    ProsperityInput in;
    in.ownerWealth = 1000; in.room[0]=300; in.room[1]=0; in.room[2]=200; in.cityMax=5000;
    ProsperityResult r = ProsperityUpdateBuilding(in, 77, /*cur480*/0.3f, /*ai180*/2.0f);

    CHECK_EQ(r.averageWealth, 500);
    CHECK(feq(r.score, 0.6499999761581421f));
    CHECK(feq(r.prosperityDelta, 0.34999996423721313f)); // -(0.3 - 0.65)
    CHECK(feq(r.aiDecayDelta, -0.10000002384185791f));    // -(2.0 - 2.0*0.95)

    // Commit order: WealthField, ProsperityDelta, AiDecayDelta.
    CHECK_EQ(log.size(), static_cast<size_t>(3));
    CHECK(log[0].which == ProsperityCommit::WealthField);
    // gilde.exe 0x57b84c: AppendDeltaField(4,1,&v15,+476) commits the ORIGINAL ownerWealth
    // (v15=1000), not the average (v25=500). v15 is never reassigned to the average.
    CHECK(feq(log[0].val, 1000.0f));
    CHECK_EQ(log[0].obj, 77);
    CHECK(log[1].which == ProsperityCommit::ProsperityDelta);
    CHECK(feq(log[1].val, r.prosperityDelta));
    CHECK(log[2].which == ProsperityCommit::AiDecayDelta);
    CHECK(feq(log[2].val, r.aiDecayDelta));

    ProsperitySetCommitHook(nullptr, nullptr);
}

// ===========================================================================
// amt_enforcement — cadence + qualifying predicate + branch select.
// ===========================================================================
TEST(WorldAmt2Enforce, CadenceGate) {
    LawTableResetDefaults();
    EnforcePerson p{};
    // gameTurn not divisible by 4 -> the pass does not run.
    EnforceResult r = EnforceRun(3, &p, 1);
    CHECK(!r.ran);
    r = EnforceRun(8, &p, 1);
    CHECK(r.ran);
}

TEST(WorldAmt2Enforce, QualifyingPredicate) {
    EnforcePerson p{};
    p.present = true; p.profClass = 5; p.officeType = 1; p.reputationWord = 0x10;
    CHECK(EnforceQualifies(p));
    p.officeType = 7; CHECK(!EnforceQualifies(p)); // exempt class
    p.officeType = 1; p.reputationWord = 0x0B; CHECK(!EnforceQualifies(p)); // < 12
    p.reputationWord = 0x10; p.profClass = 0; CHECK(!EnforceQualifies(p)); // profClass 0
    p.profClass = 10; CHECK(!EnforceQualifies(p)); // profClass >= 10
    p.profClass = 5; p.present = false; CHECK(!EnforceQualifies(p)); // empty slot
}

// Default law table: law 2 threshold == 7 (record {0x02,..,threshold@+24=0x07}).
// So the default branch is the law-VIOLATION sweep (not the decay branch).
TEST(WorldAmt2Enforce, ViolationSweepGoldenWithSeededRng) {
    LawTableResetDefaults();
    // Confirm law-2's threshold drives the violation branch (threshold != 2).
    LawRecord law{};
    CHECK(GesetzGetRecord(2, &law) == 1);
    // The binary's default law 2 has threshold == 0 (op 7), so the violation
    // branch runs with value=1, guardSel=0.
    CHECK_EQ(static_cast<int>(law.threshold), 0);

    // Force the violation evaluator to a deterministic "caught" outcome: no guards
    // (max-wanted 0) and a RNG that always rolls 1.0 (>= 0 -> caught).
    GesetzSetGuardStationSumFn([](i32){ return 0; });
    GesetzSetRandomFloatFn([](){ return 1.0; });

    struct CmdRec { i32 id; int field; float val; };
    static std::vector<CmdRec> cmds; cmds.clear();
    EnforceSetRepCommandHook([](i32 id, int f, float v, void*){
        cmds.push_back({id, f, v});
    }, nullptr);

    // Two qualifying persons. guardSel for threshold 0 is 0, so guardHighByte
    // >> 24 must != 0 to be swept. Person A has rep 0.5 (> 0.1 -> clamp queued);
    // person B has rep 0.0 (no clamp). A third person is exempt (officeType 6).
    EnforcePerson people[3]{};
    people[0].present=true; people[0].profClass=5; people[0].officeType=1;
    people[0].reputationWord=0x20; people[0].reputation=0.5f; people[0].personId=11;
    people[0].guardHighByte=(1<<24); // >>24 == 1 != guardSel(0) -> swept
    people[1].present=true; people[1].profClass=5; people[1].officeType=1;
    people[1].reputationWord=0x20; people[1].reputation=0.0f; people[1].personId=22;
    people[1].guardHighByte=(1<<24);
    people[2].present=true; people[2].profClass=5; people[2].officeType=6; // exempt
    people[2].reputationWord=0x20; people[2].reputation=0.9f; people[2].personId=33;
    people[2].guardHighByte=(1<<24);

    EnforceResult r = EnforceRun(4, people, 3);
    CHECK(r.ran);
    CHECK(!r.decayBranch);
    CHECK_EQ(r.swept, 2);       // A + B (C exempt)
    CHECK_EQ(r.penalties, 2);   // both evaluated
    CHECK_EQ(r.caught, 2);      // RNG forced caught for both
    CHECK_EQ(r.repClamps, 1);   // only A (rep 0.5 > 0.1)

    // Exactly one rep command (A's -0.1 clamp).
    CHECK_EQ(cmds.size(), static_cast<size_t>(1));
    CHECK_EQ(cmds[0].id, 11);
    CHECK_EQ(cmds[0].field, 460);
    CHECK(feq(cmds[0].val, -0.1f));

    EnforceSetRepCommandHook(nullptr, nullptr);
    GesetzSetGuardStationSumFn(nullptr);
    GesetzSetRandomFloatFn(nullptr);
}

TEST(WorldAmt2Enforce, GuardSelGatesViolationSweep) {
    LawTableResetDefaults();
    GesetzSetGuardStationSumFn([](i32){ return 0; });
    GesetzSetRandomFloatFn([](){ return 1.0; });
    // threshold 0 -> guardSel 0. A person whose guardHighByte>>24 == 0 is skipped.
    EnforcePerson p{};
    p.present=true; p.profClass=5; p.officeType=1; p.reputationWord=0x20;
    p.reputation=0.0f; p.personId=5; p.guardHighByte = 0; // >>24 == 0 == guardSel
    EnforceResult r = EnforceRun(4, &p, 1);
    CHECK_EQ(r.swept, 0);
    GesetzSetGuardStationSumFn(nullptr);
    GesetzSetRandomFloatFn(nullptr);
}

// ===========================================================================
// guild_election — full guild-master election with seeded candidates.
// ===========================================================================
TEST(WorldAmt2Election, FullElectionPicksWealthiest) {
    OfficeHolderTableReset();
    // Seed a guild-master seat (type 34) at index 5, currently vacant (city -1).
    g_officeHolders[5].holder = 70; // slot key
    g_officeHolders[5].type   = kGuildMasterOfficeType;
    g_officeHolders[5].city   = -1; // incumbent id

    int installedSlot = -1; i32 installedWinner = -1;
    static std::vector<std::pair<bool,i32>> notes; notes.clear();
    struct Ctx { int* slot; i32* winner; } ctx{&installedSlot, &installedWinner};
    GuildSetElectionHooks(
        [](int slot, i32 w, void* c){ auto* x=(Ctx*)c; *x->slot=slot; *x->winner=w; },
        [](bool inc, i32 id, int msg, void*){ notes.push_back({inc, id}); (void)msg; },
        &ctx);

    // Pool: 4 employed non-flagged persons (quorum >= 3). Three are candidates
    // (employer office-type in [13..18]); the wealthiest is id 200.
    GuildPerson pool[4]{};
    for (auto& p : pool) { p.employer = 1; p.flagged = false; }
    pool[0].personId=100; pool[0].empOfficeType=13; pool[0].totalWealth=500;
    pool[1].personId=200; pool[1].empOfficeType=15; pool[1].totalWealth=900; // winner
    pool[2].personId=300; pool[2].empOfficeType=18; pool[2].totalWealth=700;
    pool[3].personId=400; pool[3].empOfficeType=5;  pool[3].totalWealth=9999; // not a candidate

    GuildElectionResult r = ElectGuildMasterFull(pool, 4);
    CHECK_EQ(r.members, 4);
    CHECK_EQ(r.candidates, 3);
    CHECK_EQ(r.winnerId, 200);
    CHECK_EQ(r.incumbentId, -1);
    CHECK(r.install);
    CHECK_EQ(installedWinner, 200);
    CHECK_EQ(installedSlot, 70);
    // Vacant incumbent (-1) -> only the new-holder notify fires.
    CHECK_EQ(notes.size(), static_cast<size_t>(1));
    CHECK(!notes[0].first);
    CHECK_EQ(notes[0].second, 200);

    GuildSetElectionHooks(nullptr, nullptr, nullptr);
}

TEST(WorldAmt2Election, NoInstallWhenWinnerIsIncumbent) {
    OfficeHolderTableReset();
    g_officeHolders[2].holder = 50;
    g_officeHolders[2].type   = kGuildMasterOfficeType;
    g_officeHolders[2].city   = 200; // incumbent == eventual winner id
    GuildPerson pool[3]{};
    for (auto& p : pool) { p.employer = 1; p.empOfficeType = 14; }
    pool[0].personId=100; pool[0].totalWealth=300;
    pool[1].personId=200; pool[1].totalWealth=900;
    pool[2].personId=300; pool[2].totalWealth=400;
    GuildElectionResult r = ElectGuildMasterFull(pool, 3);
    CHECK_EQ(r.winnerId, 200);
    CHECK_EQ(r.incumbentId, 200);
    CHECK(!r.install);
}

TEST(WorldAmt2Election, QuorumNotMet) {
    OfficeHolderTableReset();
    GuildPerson pool[2]{};
    for (auto& p : pool) { p.employer = 1; p.empOfficeType = 14; p.totalWealth = 1; }
    pool[0].personId=1; pool[1].personId=2;
    GuildElectionResult r = ElectGuildMasterFull(pool, 2); // only 2 members < 3
    CHECK(!r.install);
    CHECK_EQ(r.winnerId, -1);
}

TEST(WorldAmt2Zuenfte, RankCategoryTable) {
    ZunftCategories z = ZunftCategoriesForRank(0x1E);
    CHECK(z.valid);
    CHECK_EQ((int)z.memberQuery, 14);
    CHECK_EQ((int)z.masterQuery, 23);
    CHECK_EQ((int)z.officeHigh, 34);
    CHECK_EQ((int)z.officeLow, 39);
    z = ZunftCategoriesForRank(0x21);
    CHECK_EQ((int)z.memberQuery, 21);
    CHECK_EQ((int)z.masterQuery, 26);
    z = ZunftCategoriesForRank(0x10);
    CHECK(!z.valid);
}

TEST(WorldAmt2Zuenfte, ElectionUsesRankBand) {
    OfficeHolderTableReset();
    g_officeHolders[0].holder = 9;
    g_officeHolders[0].type   = 0x1E; // CalcZunftElection installs into type==rank
    g_officeHolders[0].city   = -1;
    // band for 0x1E is [34..39]; only persons whose rankHighByte is in that band
    // are candidates.
    GuildPerson pool[4]{};
    for (auto& p : pool) { p.employer = 1; }
    pool[0].personId=10; pool[0].rankHighByte=34; pool[0].totalWealth=100;
    pool[1].personId=20; pool[1].rankHighByte=39; pool[1].totalWealth=800; // winner
    pool[2].personId=30; pool[2].rankHighByte=36; pool[2].totalWealth=400;
    pool[3].personId=40; pool[3].rankHighByte=10; pool[3].totalWealth=9999; // out of band
    GuildElectionResult r = CalcZunftElection(0x1E, pool, 4);
    CHECK_EQ(r.members, 4);
    CHECK_EQ(r.candidates, 3);
    CHECK_EQ(r.winnerId, 20);
}

TEST(WorldAmt2Assign, RelationDeltaTables) {
    CHECK_EQ(GuildAssignDeltaWin(0), -20);
    CHECK_EQ(GuildAssignDeltaWin(1), 20);
    CHECK_EQ(GuildAssignDeltaWin(2), 4);
    GuildAssignDeltaLose l0 = GuildAssignDeltaLoseFor(0);
    CHECK(l0.emitNeg10);
    CHECK_EQ(l0.delta, -4); // approach 0 -> delta -4 (and an extra -10 first)
    GuildAssignDeltaLose l1 = GuildAssignDeltaLoseFor(1);
    CHECK(!l1.emitNeg10);
    CHECK_EQ(l1.delta, 10);
    GuildAssignDeltaLose l2 = GuildAssignDeltaLoseFor(2);
    CHECK_EQ(l2.delta, -4);
}

TEST(WorldAmt2Assign, SuccessorUniqueMax) {
    i32 tallies[4] = {2, 5, 1, 3};
    CHECK_EQ(GuildSuccessorPick(tallies, 4), 1); // unique max at index 1
}

TEST(WorldAmt2Assign, SuccessorTieBreakDeterministic) {
    // Two candidates share the max (index 1 and 3 == 5). Seed RNG so the forward
    // walk lands on a known index.
    i32 tallies[4] = {2, 5, 1, 5};
    crt::Srand(1);
    int pick = GuildSuccessorPick(tallies, 4);
    CHECK(pick == 1 || pick == 3); // a max-valued index, chosen via RNG
    CHECK_EQ(tallies[pick], 5);
}

TEST(WorldAmt2Counting, CountByState) {
    OfficeHolderTableReset();
    g_officeHolders[0].state = 2;
    g_officeHolders[1].state = 3;
    g_officeHolders[2].state = 2;
    g_officeHolders[3].state = 3;
    g_officeHolders[4].state = 3;
    CHECK_EQ(GuildCountByState(g_officeHolders, kOfficeDefCount, 2), 2);
    CHECK_EQ(GuildCountByState(g_officeHolders, kOfficeDefCount, 3), 3);
}

TEST(WorldAmt2Offices, RankBandTable) {
    OfficeRankBand b = OfficeRankBandFor(0x1C);
    CHECK(b.valid); CHECK_EQ((int)b.low, 1); CHECK_EQ((int)b.high, 6);
    b = OfficeRankBandFor(0x22);
    CHECK_EQ((int)b.low, 13); CHECK_EQ((int)b.high, 18);
    b = OfficeRankBandFor(0x00);
    CHECK(!b.valid);
}

TEST(WorldAmt2Offices, ShouldRemove) {
    OfficeValidatePerson p{};
    p.exists=true; p.hasBuilding=true; p.heldOfficeType=0x1E; p.heldRankHigh=36;
    CHECK(!OfficeShouldRemove(0x1E, p)); // valid -> keep
    p.heldRankHigh = 40; CHECK(OfficeShouldRemove(0x1E, p)); // above [34..39]
    p.heldRankHigh = 36; p.heldOfficeType = 0x1F; CHECK(OfficeShouldRemove(0x1E, p)); // type mismatch
    p.heldOfficeType = 0x1E; p.hasBuilding = false; CHECK(OfficeShouldRemove(0x1E, p));
    p.hasBuilding = true; p.exists = false; CHECK(OfficeShouldRemove(0x1E, p));
}

TEST(WorldAmt2Wages, WageCycleVisitsAllSeats) {
    static int seen = 0; static int lastSeat = -1;
    seen = 0; lastSeat = -1;
    struct C { int* seen; int* last; } c{&seen, &lastSeat};
    int n = ProcessAllOfficeWages([](int seat, void* ctx){
        auto* x = (C*)ctx; ++*x->seen; *x->last = seat;
    }, &c);
    CHECK_EQ(n, kOfficeWageSeats);
    CHECK_EQ(seen, kOfficeWageSeats);
    CHECK_EQ(lastSeat, kOfficeWageSeats - 1);
}

TEST(WorldAmt2Production, CadenceCountsAndGoods) {
    struct Tally { int states=0; int hires=0; bool goods=false; };
    static Tally t; t = Tally{};
    ProductionSetHooks(
        [](i32, u8, void* c){ ((Tally*)c)->states++; },
        [](i32, bool, void* c){ ((Tally*)c)->hires++; },
        [](void* c){ ((Tally*)c)->goods = true; },
        &t);

    ProductionPerson persons[3]{};
    persons[0].profClass=18; persons[0].personId=1;
    persons[1].profClass=5;  persons[1].personId=2;
    persons[2].profClass=18; persons[2].personId=3;
    ProductionBuilding bld[3]{};
    bld[0].profClass=5; bld[0].productionFlag=1; bld[0].atBoundary=true; bld[0].hasWorkerSlot=true; bld[0].objectId=10;
    bld[1].profClass=5; bld[1].productionFlag=1; bld[1].atBoundary=false; bld[1].objectId=11;
    bld[2].profClass=12; bld[2].productionFlag=1; bld[2].objectId=12; // profClass>=10 inactive
    ProductionResult r = RunProductionPass(persons, 3, bld, 3);
    CHECK_EQ(r.stateFlips, 2); // two profession-18 persons
    CHECK_EQ(r.ticks, 2);      // two active buildings (bld[2] inactive)
    CHECK_EQ(r.hires, 1);      // only bld[0] at boundary with a slot
    CHECK(r.goodsRan);
    CHECK_EQ(t.states, 2);
    CHECK_EQ(t.hires, 1);
    CHECK(t.goods);
    ProductionSetHooks(nullptr, nullptr, nullptr, nullptr);
}

// ===========================================================================
// Aemter serialization round-trip.
// ===========================================================================
TEST(WorldAmt2Save, RoundTrip) {
    OfficeHolderTableReset();
    for (int i = 0; i < kAemterCount; ++i) {
        g_officeHolders[i].holder = static_cast<u8>(i + 1);
        g_officeHolders[i].city   = i * 100 - 1;
        g_officeHolders[i].type   = static_cast<u8>(0x1C + (i % 7));
        g_officeHolders[i].rank   = i * 3;
        g_officeHolders[i].state  = static_cast<u8>(i % 4);
    }
    u8 buf[kAemterStreamBytes + 8] = {0};
    size_t n = SaveAemter(buf, sizeof(buf));
    CHECK_EQ(n, static_cast<size_t>(kAemterStreamBytes));

    // Clobber, then load back.
    OfficeHolderTableReset();
    CHECK_EQ(LoadAemter(buf, n), 1);
    for (int i = 0; i < kAemterCount; ++i) {
        CHECK_EQ((int)g_officeHolders[i].holder, i + 1);
        CHECK_EQ(g_officeHolders[i].city, i * 100 - 1);
        CHECK_EQ((int)g_officeHolders[i].type, 0x1C + (i % 7));
        CHECK_EQ(g_officeHolders[i].rank, i * 3);
        CHECK_EQ((int)g_officeHolders[i].state, i % 4);
    }
}

TEST(WorldAmt2Save, LoadRejectsBadCount) {
    u8 buf[kAemterStreamBytes] = {0};
    buf[0] = 5; // count 5 != 37
    CHECK_EQ(LoadAemter(buf, sizeof(buf)), 0);
    CHECK_EQ(LoadAemter(nullptr, 0), 0);
    u8 small[4] = {0};
    CHECK_EQ(LoadAemter(small, 4), 0);
}
