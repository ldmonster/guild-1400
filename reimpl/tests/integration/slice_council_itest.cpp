// tests/integration/slice_council_itest.cpp — the COURT/COUNCIL/OFFICE (politics)
// slice on a small real-FORMAT world (synthetic entity records + a seated council
// seat in the live g_officeHolders table; no shipped assets). Drives the real loop:
//   seed -> opcode-68 candidacy command -> one game-day,
// and asserts:
//   * a FOLDED politics field changed (g_officeHolders[slot].rank bumped by the REAL
//     OfficeAssignToCandidate),
//   * HashFullWorld() differs pre/post the command AND across the whole slice,
//   * the run is BYTE-IDENTICAL on a rerun (the play-layer determinism contract:
//     ZeroWorldGlobals blanks the politics tables, Srand anchors the RNG, and the day
//     dirties law/office/crime/relation so they MUST be re-zeroed for the compare).
#include "test.h"

#include "play/slice_council.h"
#include "play/game_day.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "world/city.h"
#include "world/office.h"
#include "world/law.h"
#include "world/crime.h"
#include "world/relation.h"
#include "world/event.h"
#include "crt/rand.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace guild;
using namespace guild::play;
using namespace guild::world;
using namespace guild::sim;

namespace {

CouncilInteraction Interaction() {
    CouncilInteraction it;
    it.action      = CouncilAction::kApplyCandidacy;
    it.applicantId = 4242;
    it.holderKey   = 11;
    it.officeType  = 5;     // a valid office type
    return it;
}

// A single-record applicant resolver (lives across the test).
OfficePersonRec g_appl;
OfficePersonRec* FindAppl(i32 id, void*) { return id == 4242 ? &g_appl : nullptr; }

// Blank the politics + economy tables, seat a vacant office, install the applicant.
// Mirrors RunCouncilSlice's preamble so the itest exercises the same determinism rig
// without shipped assets. RunGameDay dirties g_law/office/crime/relation, so a
// faithful re-run MUST re-zero them here before re-seeding.
void SeedPoliticsWorld(std::uint32_t seed, const CouncilInteraction& it) {
    std::memset(g_objects, 0, sizeof(g_objects));
    std::memset(g_persons, 0, sizeof(g_persons));
    ResetEntityArrays();

    // Zero EVERY folded politics table (the day pass dirtied them on a prior run).
    std::memset(g_lawTable, 0, sizeof(g_lawTable));
    std::memset(g_officeHolders, 0, sizeof(g_officeHolders));
    std::memset(g_crimeTable, 0, sizeof(g_crimeTable));
    std::memset(g_relationMatrix, 0, sizeof(g_relationMatrix));
    std::memset(g_eventTable, 0, sizeof(g_eventTable));
    g_eventTableCount = 0;
    g_missionLcgState = 0;

    // Deterministic economy baseline.
    crt::Srand(seed);
    CityInitParameterTable(100.0f);
    g_capDivisor = 0.0f;
    g_cityTotalMoney = 0.0f;
    g_cityTotalGoods = 0.0f;

    // A few alive object records (a real-format substrate for the day to run over).
    for (int i = 0; i < 6; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = 700 + i * 3;
    }

    // Seat the vacant council office in slot 0.
    g_officeHolders[0].holder    = it.holderKey;
    g_officeHolders[0].type      = it.officeType;
    g_officeHolders[0].city      = -1;   // vacant
    g_officeHolders[0].state     = 3;    // electable
    g_officeHolders[0].rank      = 0;    // < 4
    g_officeHolders[0].secondary = -1;

    // Install the applicant + resolver.
    g_appl = OfficePersonRec{};
    g_appl.ownerId   = it.applicantId;
    g_appl.office360 = 0;
    g_appl.valid     = true;
    static CouncilApplyHooks hooks;
    hooks.find = &FindAppl;
    hooks.ctx  = nullptr;
    SetCouncilApplyHooks(&hooks);
}

} // namespace

// ---------------------------------------------------------------------------
// The folded politics field changes + the world hash moves across the slice.
// ---------------------------------------------------------------------------
TEST(SliceCouncilItest, PoliticsFieldAndHashEvolveOverSlice) {
    const std::uint32_t seed = 0xC0FFEE;
    CouncilInteraction it = Interaction();

    SeedPoliticsWorld(seed, it);
    crt::Srand(seed);
    std::uint64_t hLoad = HashFullWorld();

    i32 rankBefore = g_officeHolders[0].rank;
    u8  candBefore = g_appl.office360;

    // -- click -> opcode-68 command -> REAL apply --
    CouncilPacket pkt = BuildCouncilPacket(it);
    CHECK(pkt.built);
    CHECK_EQ((int)pkt.opcode, 68);
    int applied = ApplyCouncilPacket(pkt);
    CHECK_EQ(applied, 1);

    i32 rankAfter = g_officeHolders[0].rank;
    u8  candAfter = g_appl.office360;
    crt::Srand(seed);
    std::uint64_t hCmd = HashFullWorld();

    // -- one game-day --
    crt::Srand(seed);
    GameDayState st = SeedGameDay(seed);
    RunGameDay(seed, st);
    crt::Srand(seed);
    std::uint64_t hDay = HashFullWorld();

    std::printf("[council-itest] rank %d->%d cand %d->%d hLoad=%llu hCmd=%llu hDay=%llu\n",
                rankBefore, rankAfter, (int)candBefore, (int)candAfter,
                (unsigned long long)hLoad, (unsigned long long)hCmd,
                (unsigned long long)hDay);

    // The folded politics field genuinely changed (g_officeHolders rank bumped).
    CHECK_EQ(rankBefore, 0);
    CHECK_EQ(rankAfter, 1);
    CHECK_EQ((int)candBefore, 0);
    CHECK_EQ((int)candAfter, 5);

    // The command alone moved the whole-world hash (g_officeHolders is folded).
    CHECK(hCmd != hLoad);
    // The whole slice evolved the world further.
    CHECK(hDay != hLoad);
    CHECK(hDay != hCmd);

    SetCouncilApplyHooks(nullptr);
    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// Determinism: a full rerun reproduces byte-identical hashes (the rerun re-zeroes
// the politics tables the prior day dirtied).
// ---------------------------------------------------------------------------
TEST(SliceCouncilItest, FullSliceIsDeterministic) {
    const std::uint32_t seed = 0xBADA55;
    CouncilInteraction it = Interaction();

    auto run = [&](std::uint64_t& hLoad, std::uint64_t& hCmd, std::uint64_t& hDay) {
        SeedPoliticsWorld(seed, it);
        crt::Srand(seed);
        hLoad = HashFullWorld();
        CouncilPacket pkt = BuildCouncilPacket(it);
        ApplyCouncilPacket(pkt);
        crt::Srand(seed);
        hCmd = HashFullWorld();
        crt::Srand(seed);
        GameDayState st = SeedGameDay(seed);
        RunGameDay(seed, st);
        crt::Srand(seed);
        hDay = HashFullWorld();
    };

    std::uint64_t aLoad = 0, aCmd = 0, aDay = 0;
    std::uint64_t bLoad = 0, bCmd = 0, bDay = 0;
    run(aLoad, aCmd, aDay);
    run(bLoad, bCmd, bDay);

    std::printf("[council-itest] det A(%llu,%llu,%llu) B(%llu,%llu,%llu)\n",
                (unsigned long long)aLoad, (unsigned long long)aCmd, (unsigned long long)aDay,
                (unsigned long long)bLoad, (unsigned long long)bCmd, (unsigned long long)bDay);

    CHECK_EQ((long long)aLoad, (long long)bLoad);
    CHECK_EQ((long long)aCmd, (long long)bCmd);
    CHECK_EQ((long long)aDay, (long long)bDay);
    // The command and day each genuinely moved the hash.
    CHECK(aCmd != aLoad);
    CHECK(aDay != aCmd);

    SetCouncilApplyHooks(nullptr);
    ResetEntityArrays();
}

// ---------------------------------------------------------------------------
// RunCouncilStepsSynthetic over the same seeded world gives the same field deltas.
// ---------------------------------------------------------------------------
TEST(SliceCouncilItest, SyntheticStepsAgreeOnPoliticsDelta) {
    CouncilStepHash h[3];
    i32 rb = -1, ra = -1;
    int n = RunCouncilStepsSynthetic(0x1357, Interaction(), h, 3, &rb, &ra);
    CHECK_EQ(n, 3);
    CHECK_EQ(rb, 0);
    CHECK_EQ(ra, 1);
    CHECK(h[1].mutated);   // command
    CHECK(h[2].mutated);   // day
    SetCouncilApplyHooks(nullptr);
    ResetEntityArrays();
}
