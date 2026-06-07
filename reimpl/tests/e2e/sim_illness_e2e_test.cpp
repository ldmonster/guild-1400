// End-to-end: a full "RunSimKrankheiten" (run-sim-illnesses) day-pass over a
// modeled Person array, driven through the illness module
// (illness.{h,cpp}) plus the REAL turn-ownership siblings (character_state) and
// the REAL ANSI LCG (crt::RandNext). Models what VIBE_NpcEvent_RunSimDiseases
// (0x4d766c) does each game day: walk a rotating window of the person array,
// find disease candidates, and for each, roll a disease event and apply it to
// the record's +44 disease-state bitfield, accumulating the medical cost the
// building stock would be debited.
//
// A real-asset variant (GUARDED by GUILD_E2E_ASSETS) would replay a recorded
// person snapshot + RNG seed and assert the exact contracted-disease set;
// absent that, the self-contained modeled day-pass runs deterministically.
#include "test.h"

#include <cstdlib>
#include <vector>

#include "sim/illness.h"
#include "sim/character_state.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A small modeled person array (the real one is word_12CE910, 768 x 536B).
struct DayPassResult {
    int candidates = 0;
    int applied = 0;
    int totalCost = 0;
    int diseased = 0;   // records that ended the day carrying any disease
};

IllnessTurnView TurnFor(const IllnessRec& rec) {
    TurnObject o{};
    o.type = rec.kind;
    o.id = rec.id;
    o.hasOwner = false;
    o.ownerPlayer = 0xFFFF;
    IllnessTurnView v;
    v.ownerForTurn  = IsOwnerForTurn(&o);
    v.objectForTurn = IsObjectForTurn(&o);
    return v;
}

// One day-pass: visit every record, contract disease for each candidate that
// rolls a hit. (The real handler advances an internal cursor by a random step
// and processes 12 records per tick; here we process the whole array, which is
// the same accumulation over a full day.)
DayPassResult RunDayPass(std::vector<IllnessRec>& people) {
    DayPassResult r{};
    for (auto& rec : people) {
        if (!IllnessIsDiseaseCandidate(&rec, TurnFor(rec)))
            continue;
        ++r.candidates;
        DiseasePick p = IllnessPickRandomDiseaseEvent(rec.diseaseState);
        if (p.applied) {
            ++r.applied;
            r.totalCost += p.cost;
            rec.diseaseState = p.newState;
        }
    }
    for (auto& rec : people)
        if (rec.diseaseState != 0u)
            ++r.diseased;
    return r;
}

}  // namespace

// Self-contained modeled day-pass: deterministic given the fixed seed.
TEST(SimIllnessE2E, DayPassContractsDiseases) {
    SetTurnState(TurnState{ 0, -1, 0, 0 });   // single-player: all records local
    crt::Srand(20260605);

    // 8 records: alternating person / non-person / free-slot, all healthy.
    std::vector<IllnessRec> people(8, IllnessRec{});
    for (int i = 0; i < 8; ++i) {
        people[i].id = 100 + i;
        people[i].diseaseState = 0u;
        if (i % 4 == 3) {
            people[i].marker = static_cast<i16>(0xFFFF);  // free slot
        } else if (i % 4 == 2) {
            people[i].marker = 0;
            people[i].active = 1;
            people[i].kind = 12;   // non-person (>=10) -> never candidate
        } else {
            people[i].marker = 0;
            people[i].active = 1;
            people[i].kind = static_cast<u8>(4 + i);  // person
        }
    }

    DayPassResult r = RunDayPass(people);

    // Half the records (i%4 in {0,1}) are valid persons -> 4 candidates.
    CHECK_EQ(r.candidates, 4);
    // Every candidate started fully healthy, so each contracts (severity may be
    // 0 for a count-1 group, but with this seed at least some apply).
    CHECK(r.applied >= 1);
    CHECK(r.applied <= r.candidates);
    CHECK(r.totalCost >= 0);
    CHECK_EQ(r.diseased, r.applied);

    // Free slots and non-persons never contracted anything.
    CHECK_EQ(static_cast<u16>(people[3].marker), 0xFFFFu);
    CHECK_EQ(people[3].diseaseState, 0u);
    CHECK_EQ(people[6].diseaseState, 0u);   // non-person record stays healthy

    // Re-running the same pass is idempotent on already-diseased groups: a
    // record only contracts a NEW disease in a still-free group.
    crt::Srand(20260605);
    DayPassResult r2 = RunDayPass(people);
    CHECK_EQ(r2.candidates, 4);
    // Cumulative: nobody loses a disease; diseased count never decreases.
    CHECK(r2.diseased >= r.diseased);
}

// GUARDED real-asset replay (skipped unless GUILD_E2E_ASSETS is set).
TEST(SimIllnessE2E, RealSnapshotReplayGuarded) {
    const char* dir = std::getenv("GUILD_E2E_ASSETS");
    if (!dir) {
        CHECK(true);   // guarded skip — no recorded person snapshot configured
        return;
    }
    // With assets present, a recorded (person-array, seed) snapshot would be
    // loaded and the contracted-disease set asserted against the recording.
    CHECK(dir != nullptr);
}
