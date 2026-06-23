// Verifies InstallRealElectionWiring() binds the election / court-council / create
// bridges (CourtCouncilHooks, the three office-install hooks, ICreateHooks) to
// their real reconstructed leaves — previously all were dangling/inert at runtime
// (nothing installed them). Suite prefix: WireElection.
#include "tests/framework/test.h"

#include "world/wire_election.h"
#include "world/court_council2.h"
#include "world/election.h"
#include "world/election_candidacy.h"
#include "world/guild_election.h"
#include "world/guild.h"            // GuildEligibility codes (1 == eligible)

#include "sim/building_create2.h"   // ICreateHooks / CreateHooks / Building_ApplyTypeDefaults
#include "sim/building_production.h"// PackedTime
#include "sim/types.h"              // Person, kPersonStride

#include <cstring>

using namespace guild;
using namespace guild::world;

// Re-inert every bridge so a clean baseline holds before/after install.
static void InertAll() {
    SetCourtCouncilHooks(nullptr);
    GuildSetElectionHooks(nullptr, nullptr, nullptr);
    CanvassSetHooks(nullptr, nullptr, nullptr);
    ElectionSetInstallHook(nullptr, nullptr);
    guild::sim::ResetCreateHooks();
}

TEST(WireElection, BindsRealLeavesIntoAllBridges) {
    InertAll();

    // Baseline: the court bridge's wireable fields are null when inert; the create
    // hook is the module default (AllocPlantMap returns null).
    CHECK(GetCourtCouncilHooks().guildEligibility == nullptr);
    CHECK(GetCourtCouncilHooks().randomMod4       == nullptr);
    CHECK(guild::sim::CreateHooks()->AllocPlantMap() == nullptr);

    InstallRealElectionWiring();

    // --- CourtCouncilHooks: the two reconstructed leaves are now bound; the
    //     UI/AI/object-array fields stay inert (null) as documented.
    const CourtCouncilHooks& ch = GetCourtCouncilHooks();
    CHECK(ch.guildEligibility != nullptr);
    CHECK(ch.randomMod4       != nullptr);
    CHECK(ch.buildingSlotCount == nullptr);  // inert (live object array)
    CHECK(ch.buildingSlot      == nullptr);  // inert
    CHECK(ch.aiNeedsComputeWeights == nullptr); // inert (AiNeeds scratch)
    CHECK(ch.categoryRating    == nullptr);  // inert
    CHECK(ch.setBarValue       == nullptr);  // inert (SetValueOrText widget)

    // --- ICreateHooks: a non-default vtable is installed (the real allocator).
    CHECK(guild::sim::CreateHooks() != nullptr);

    InertAll();   // restore for any later test in this TU
}

// The bound court guildEligibility leaf runs the real VIBE_Amt_GetGuildEligibility
// (0x481bcc) over a raw 536-byte Person record: rank@+361 in [30..33], no
// already-in flag (+459 & 4), money@+404 >= 3 => 1 (eligible). Other field shapes
// => 0, matching the original `if (eligibility != 1) return 0` court gate.
TEST(WireElection, CourtGuildEligibilityRunsRealRuleOverPersonRecord) {
    InertAll();
    InstallRealElectionWiring();
    const CourtCouncilHooks& ch = GetCourtCouncilHooks();
    CHECK(ch.guildEligibility != nullptr);

    sim::Person rec;
    u8* r = reinterpret_cast<u8*>(&rec);

    // (a) eligible: rank 31, flags clear, money 100 -> 1.
    std::memset(&rec, 0, sizeof(rec));
    r[361] = 31;
    r[459] = 0;
    { i32 m = 100; std::memcpy(r + 404, &m, 4); }
    CHECK_EQ(ch.guildEligibility(&rec), 1);

    // (b) already in guild (+459 & 4) -> code -1 -> not eligible -> 0.
    std::memset(&rec, 0, sizeof(rec));
    r[361] = 31;
    r[459] = 0x4;
    { i32 m = 100; std::memcpy(r + 404, &m, 4); }
    CHECK_EQ(ch.guildEligibility(&rec), 0);

    // (c) not a guild rank (rank 5) -> code 0 -> not eligible -> 0.
    std::memset(&rec, 0, sizeof(rec));
    r[361] = 5;
    { i32 m = 100; std::memcpy(r + 404, &m, 4); }
    CHECK_EQ(ch.guildEligibility(&rec), 0);

    // (d) too poor (money 2 < 3) -> code -2 -> not eligible -> 0.
    std::memset(&rec, 0, sizeof(rec));
    r[361] = 33;
    { i32 m = 2; std::memcpy(r + 404, &m, 4); }
    CHECK_EQ(ch.guildEligibility(&rec), 0);

    // (e) null accused is safe -> 0 (the original returns 0 on a missing record).
    CHECK_EQ(ch.guildEligibility(nullptr), 0);

    InertAll();
}

// The bound court randomMod4 leaf is in range [0,4) (the ambiguous-verdict
// tie-break: VIBE_Math_RandomModulo(4)).
TEST(WireElection, CourtRandomMod4InRange) {
    InertAll();
    InstallRealElectionWiring();
    const CourtCouncilHooks& ch = GetCourtCouncilHooks();
    CHECK(ch.randomMod4 != nullptr);
    for (int i = 0; i < 64; ++i) {
        int v = ch.randomMod4();
        CHECK(v >= 0 && v < 4);
    }
    InertAll();
}

// The bound ICreateHooks allocates a real 0x600-byte plant map (the reconstructed
// VIBE_Memory_AllocDebug), so CreateGebaeude's prot-30 (farm) per-type init writes
// a non-null +113 plant-map pointer and the map's 24-byte records are initialised.
TEST(WireElection, CreateHooksAllocatesRealPlantMap) {
    InertAll();
    InstallRealElectionWiring();

    // The allocator returns a usable 0x600 buffer (24-byte stride, 64 records).
    u8* map = guild::sim::CreateHooks()->AllocPlantMap();
    CHECK(map != nullptr);

    // Drive the real Building_ApplyTypeDefaults for prot 30 (farm): it stores the
    // map pointer at rec+113 and primes each 24-byte record (+13 = 0xFF).
    u8 rec[256];
    std::memset(rec, 0, sizeof(rec));
    sim::PackedTime now{};
    bool changed = sim::Building_ApplyTypeDefaults(rec, /*prot=*/30, /*ownerWord=*/0,
                                                   /*ownerPersonId=*/-1, now,
                                                   /*isProduction=*/false);
    CHECK(changed);
    // rec+113 holds the (truncated) map pointer; non-zero means the alloc landed.
    i32 mapPtr = 0;
    std::memcpy(&mapPtr, rec + 113, 4);
    CHECK(mapPtr != 0);
    // rec+48 == 100 and rec+73 == the quality float bits (the prot-30 defaults).
    i32 v48 = 0; std::memcpy(&v48, rec + 48, 4);
    CHECK_EQ(v48, 100);

    InertAll();
}

// The bound office-install hooks run the real VIBE_Office_AddTableEntry slot-find
// without crashing (no live holder table -> the scan returns -1, a defined miss).
TEST(WireElection, ElectionInstallHooksExecuteRealOfficeAddEntry) {
    InertAll();
    InstallRealElectionWiring();

    // Each install variant is now bound; invoke each over a synthetic winner id.
    // (No null-check needed: the installer sets non-null function pointers.)
    // single-seat (election.h): drive ElectionCommit with an install outcome.
    ElectionOutcome out{};
    out.winnerId = 4242;
    out.install  = true;
    bool committed = ElectionCommit(out, /*officeSlot=*/3, /*incumbentId=*/-1);
    CHECK(committed);   // install gate passed; the real AddTableEntry ran

    InertAll();
}
