// End-to-end: one full Amt governance turn over a synthetic city/population.
// Order (from VIBE_GameTick_BeginPlayerRound / the heartbeat in amt.h):
//   1. enforcement sweep  (law-violation -> crimes/penalties via command)
//   2. office prosperity update (per active business)
//   3. guild-master election (install the wealthiest candidate)
// We wire the (mock) command hooks to a shared ledger and verify the resulting
// office/crime/treasury/command state against a hand-computed reference.
#include <cmath>
#include <vector>

#include "crt/rand.h"
#include "tests/framework/test.h"
#include "world/amt_enforcement.h"
#include "world/crime.h"
#include "world/guild_election.h"
#include "world/law.h"
#include "world/office.h"
#include "world/office_prosperity.h"
#include "world/treasury.h"

using namespace guild;
using namespace guild::world;

namespace {
bool feq(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

// Shared command ledger the mock hooks write into.
struct Ledger {
    int repCommands = 0;        // enforcement rep/penalty commands
    float lastRepValue = 0.0f;
    int prosperityCommits = 0;  // prosperity field/delta commits
    float prosperityScore = 0.0f;
    int crimesCreated = 0;      // crimes added to the table on a caught violation
    int installSlot = -1;       // guild-master install slot
    i32 installWinner = -1;     // guild-master winner
    int notifies = 0;
};
Ledger g_led;
} // namespace

TEST(WorldAmt2E2E, FullGovernanceTurn) {
    // ---- reset all tables ----
    LawTableResetDefaults();
    CrimeAndEvidenceReset();
    OfficeHolderTableReset();
    g_led = Ledger{};

    // ---- enforcement wiring: a caught violation creates a crime record ----
    // Force "caught" deterministically (no guards + RNG roll 1.0).
    GesetzSetGuardStationSumFn([](i32){ return 0; });
    GesetzSetRandomFloatFn([](){ return 1.0; });
    EnforceSetRepCommandHook([](i32, int, float v, void*){
        g_led.repCommands++; g_led.lastRepValue = v;
    }, nullptr);

    // The original queues a crime-creation command on a caught violation. Here the
    // enforcement sweep itself doesn't add to g_crimeTable (that is the command
    // layer's job), so we model the command by adding a crime record per caught
    // violation, mirroring VIBE_Command_QueueRequestObject34's effect.
    // We drive that from the tallies after the sweep.

    // Two qualifying persons in the violation branch (default law-2 threshold 0
    // -> guardSel 0, so guardHighByte>>24 must != 0 to be swept).
    EnforcePerson people[2]{};
    people[0].present=true; people[0].profClass=5; people[0].officeType=1;
    people[0].reputationWord=0x20; people[0].reputation=0.5f; people[0].personId=11;
    people[0].guardHighByte=(1<<24);
    people[1].present=true; people[1].profClass=5; people[1].officeType=2;
    people[1].reputationWord=0x20; people[1].reputation=0.0f; people[1].personId=22;
    people[1].guardHighByte=(1<<24);

    EnforceResult er = EnforceRun(/*gameTurn*/8, people, 2);
    CHECK(er.ran);
    CHECK(!er.decayBranch);
    CHECK_EQ(er.swept, 2);
    CHECK_EQ(er.caught, 2);
    CHECK_EQ(er.repClamps, 1);          // only person 11 (rep 0.5 > 0.1)
    CHECK_EQ(g_led.repCommands, 1);     // hook fired once
    CHECK(feq(g_led.lastRepValue, -0.1f));

    // Model the command-layer effect: one crime record per caught violation.
    for (int i = 0; i < er.caught; ++i) {
        int slot = StraftatFindFreeSlot();
        CHECK(slot >= 0);
        g_crimeTable[slot].id = 1000 + i;
        g_crimeTable[slot].perpetrator = (i == 0) ? 11 : 22;
        g_crimeTable[slot].provenState = 1; // active/proven
        g_crimeTable[slot].wanted = 1;
        g_led.crimesCreated++;
    }
    CHECK_EQ(g_led.crimesCreated, 2);
    // The crime table now reports both perps as actively wanted.
    CHECK_EQ(StraftatCountActiveByTarget(11), 1);
    CHECK_EQ(StraftatCountActiveByTarget(22), 1);

    // ---- prosperity wiring ----
    ProsperitySetCommitHook([](ProsperityCommit w, i32, float v, void*){
        g_led.prosperityCommits++;
        if (w == ProsperityCommit::ProsperityDelta) g_led.prosperityScore = v;
    }, nullptr);

    ProsperityInput pin;
    pin.ownerWealth = 1000; pin.room[0]=300; pin.room[1]=0; pin.room[2]=200; pin.cityMax=5000;
    ProsperityResult pr = ProsperityUpdateBuilding(pin, /*obj*/55, /*cur480*/0.3f, /*ai180*/2.0f);
    CHECK_EQ(pr.averageWealth, 500);
    CHECK(feq(pr.score, 0.6499999761581421f));
    CHECK_EQ(g_led.prosperityCommits, 3); // wealth + prosperity-delta + ai-decay
    CHECK(feq(g_led.prosperityScore, pr.prosperityDelta));

    // ---- treasury: charge the penalty fine to the city treasury ----
    // A caught violation transfers the law's penalty into the city treasury. Model
    // it: law 2's penalty is the fine each perp owes the city.
    LawRecord law2{}; CHECK(GesetzGetRecord(2, &law2) == 1);
    Treasury city; city.accountId = -1; city.balance = 0;
    i32 totalFine = static_cast<i32>(law2.penalty) * er.caught;
    city.balance += totalFine; // the city collects penalty * caught
    CHECK_EQ(city.balance, static_cast<i32>(law2.penalty) * 2);

    // ---- guild-master election ----
    g_officeHolders[5].holder = 70;
    g_officeHolders[5].type   = kGuildMasterOfficeType;
    g_officeHolders[5].city   = -1;
    GuildSetElectionHooks(
        [](int slot, i32 w, void*){ g_led.installSlot = slot; g_led.installWinner = w; },
        [](bool, i32, int, void*){ g_led.notifies++; },
        nullptr);

    GuildPerson pool[3]{};
    for (auto& p : pool) { p.employer = 1; p.empOfficeType = 14; }
    pool[0].personId=100; pool[0].totalWealth=400;
    pool[1].personId=200; pool[1].totalWealth=950; // wealthiest -> winner
    pool[2].personId=300; pool[2].totalWealth=600;
    GuildElectionResult gr = ElectGuildMasterFull(pool, 3);
    CHECK_EQ(gr.members, 3);
    CHECK_EQ(gr.candidates, 3);
    CHECK_EQ(gr.winnerId, 200);
    CHECK(gr.install);
    CHECK_EQ(g_led.installWinner, 200);
    CHECK_EQ(g_led.installSlot, 70);
    CHECK_EQ(g_led.notifies, 1); // vacant incumbent -> one notify

    // ---- final consolidated reference checks ----
    // Crime table: exactly 2 active crimes (the two caught violations).
    int active = 0;
    for (int i = 0; i < kCrimeCount; ++i)
        if (g_crimeTable[i].provenState == 1) ++active;
    CHECK_EQ(active, 2);

    // Cleanup hooks.
    GesetzSetGuardStationSumFn(nullptr);
    GesetzSetRandomFloatFn(nullptr);
    EnforceSetRepCommandHook(nullptr, nullptr);
    ProsperitySetCommitHook(nullptr, nullptr);
    GuildSetElectionHooks(nullptr, nullptr, nullptr);
}

// A second flow: the reputation-DECAY branch (law-2 threshold forced to 2) and
// the wage cycle, verifying the alternate enforcement path + the 768-seat loop.
TEST(WorldAmt2E2E, DecayBranchAndWageCycle) {
    LawTableResetDefaults();
    // Force law 2's threshold to 2 -> the decay branch.
    LawRecord law2{}; CHECK(GesetzGetRecord(2, &law2) == 1);
    g_lawTable[2].threshold = 2;

    crt::Srand(12345); // deterministic decay rolls

    int penalties = 0, flags = 0;
    struct C { int* pen; int* flg; } c{&penalties, &flags};
    EnforceSetRepCommandHook([](i32, int, float, void* ctx){
        ++*((C*)ctx)->pen;
    }, &c);
    EnforceSetFlagCommandHook([](i32, u8, void* ctx){ ++*((C*)ctx)->flg; }, &c);

    // 5 qualifying persons with low reputation so the roll*0.2 > rep often trips.
    EnforcePerson people[5]{};
    for (int i = 0; i < 5; ++i) {
        people[i].present=true; people[i].profClass=4; people[i].officeType=1;
        people[i].reputationWord=0x20; people[i].reputation=0.0f; // roll*0.2 > 0 always
        people[i].personId=i+1; people[i].flag12=0;
    }
    EnforceResult er = EnforceRun(4, people, 5);
    CHECK(er.ran);
    CHECK(er.decayBranch);
    // rep 0.0 -> RandomFloatScaled()*0.2 > 0.0 is true unless the roll is exactly 0;
    // with a seeded RNG nearly all 5 trip. Verify swept == penalties == flagsSet.
    CHECK_EQ(er.swept, er.penalties);
    CHECK_EQ(er.penalties, er.flagsSet);
    CHECK(er.swept >= 4); // at least 4 of 5 trip
    CHECK_EQ(penalties, er.penalties);
    CHECK_EQ(flags, er.flagsSet);

    // Wage cycle visits all 768 seats.
    static int seats = 0; seats = 0;
    int n = ProcessAllOfficeWages([](int, void* ctx){ ++*(int*)ctx; }, &seats);
    CHECK_EQ(n, 768);
    CHECK_EQ(seats, 768);

    LawTableResetDefaults();
    EnforceSetRepCommandHook(nullptr, nullptr);
    EnforceSetFlagCommandHook(nullptr, nullptr);
}
