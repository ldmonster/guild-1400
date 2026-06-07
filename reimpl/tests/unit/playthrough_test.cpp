// tests/unit/playthrough_test.cpp — Wave 28 P7 seed: the multi-day SCRIPTED
// PLAYTHROUGH harness over a SYNTHETIC live world.
//
// A 3-action / 3-day script chaining three real P5 slices (council candidacy,
// church donation, crime/intrigue). Asserts:
//   * the per-day witnesses EVOLVE (the world is not a fixed point),
//   * a RE-RUN with the same (script, seed) is byte-identical (the determinism
//     proof: equal day hashes + equal run signature),
//   * a DIFFERENT seed DIVERGES.
#include "test.h"

#include "play/playthrough.h"

#include <cstdint>
#include <cstdio>

using namespace guild;
using namespace guild::play;

namespace {

// A small chained script: council apply on day 0, church donation on day 1, a
// crime on day 2. Each touches a distinct witness field (rank / treasury / crime).
PlaythroughScript MakeScript() {
    PlaythroughScript s;
    s.days = 3;

    PlaythroughAction council;
    council.onDay      = 0;
    council.kind       = PlaythroughActionKind::kCouncil;
    council.actorId    = 31337;
    council.officeType = 5;
    council.holderKey  = 3;
    s.actions.push_back(council);

    PlaythroughAction church;
    church.onDay    = 1;
    church.kind     = PlaythroughActionKind::kChurch;
    church.actorId  = 8001;   // donor
    church.targetId = 8002;   // church
    church.amount   = 250;
    s.actions.push_back(church);

    PlaythroughAction crime;
    crime.onDay        = 2;
    crime.kind         = PlaythroughActionKind::kCrime;
    crime.actorId      = 11;  // perpetrator
    crime.targetId     = 12;  // victim
    crime.crimeType    = 2;
    s.actions.push_back(crime);

    return s;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(PlaythroughUnit, ScriptedRunEvolvesAndIsDeterministic) {
    const std::uint32_t seed = 0xABCDEF;
    PlaythroughScript s = MakeScript();

    PlaythroughResult a = RunScriptedPlaythroughSynthetic(seed, /*persons=*/4, s);

    CHECK(a.loaded);
    CHECK_EQ(a.daysRun, 3);
    CHECK_EQ((int)a.witnesses.size(), 3);
    // All three scripted actions fired (council seats+applies, church donates, crime).
    CHECK_EQ(a.actionsApplied, 3);

    // The world EVOLVES: per-day hashes are not all equal to the load baseline.
    CHECK(a.worldEvolved());

    // Consecutive day hashes differ (a genuine multi-day evolution, not one mutation).
    std::uint64_t prev = a.hashAfterLoad;
    int distinct = 0;
    for (const auto& w : a.witnesses) {
        if (w.hash != prev) ++distinct;
        prev = w.hash;
    }
    CHECK(distinct >= 2);

    // The witness fields reflect the chained slice effects:
    //   council seated -> rank bumped to 1 from day 0 onward,
    CHECK_EQ(a.witnesses[0].officeRank, 1);
    //   crime committed on day 2 -> at least one active crime record by the end.
    CHECK(a.witnesses[2].crimeCount >= 1);

    std::printf("[playthrough-unit] day | hash | treasury | rank | crime | cityMoney\n");
    for (const auto& w : a.witnesses)
        std::printf("[playthrough-unit]  %d  | %016llx | %lld | %d | %d | %.2f\n",
                    w.day, (unsigned long long)w.hash, (long long)w.treasury,
                    w.officeRank, w.crimeCount, w.cityMoney);

    // RE-RUN: byte-identical (same seed + script, fresh world).
    PlaythroughResult b = RunScriptedPlaythroughSynthetic(seed, /*persons=*/4, s);
    CHECK_EQ(a.runSignature(), b.runSignature());
    CHECK_EQ(a.hashAfterLoad, b.hashAfterLoad);
    bool allEqual = (a.witnesses.size() == b.witnesses.size());
    for (size_t i = 0; allEqual && i < a.witnesses.size(); ++i)
        if (a.witnesses[i].hash != b.witnesses[i].hash) allEqual = false;
    CHECK(allEqual);

    // DIFFERENT seed: diverges.
    PlaythroughResult c = RunScriptedPlaythroughSynthetic(seed ^ 0x55AA, 4, s);
    CHECK(c.runSignature() != a.runSignature());
}

// ---------------------------------------------------------------------------
// An empty-day (no action) still advances + records a witness, and stays
// deterministic — proving the day cascade alone drives the witness table.
// ---------------------------------------------------------------------------
TEST(PlaythroughUnit, EmptyScriptStillAdvancesDeterministically) {
    PlaythroughScript s;
    s.days = 3;   // no actions

    const std::uint32_t seed = 0x1234;
    PlaythroughResult a = RunScriptedPlaythroughSynthetic(seed, 2, s);
    PlaythroughResult b = RunScriptedPlaythroughSynthetic(seed, 2, s);

    CHECK_EQ(a.daysRun, 3);
    CHECK_EQ(a.actionsApplied, 0);
    CHECK_EQ((int)a.witnesses.size(), 3);
    CHECK_EQ(a.runSignature(), b.runSignature());
}
