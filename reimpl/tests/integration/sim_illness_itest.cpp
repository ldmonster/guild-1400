// Integration tests for the illness / disease module (guild::sim) — gilde.exe.
//
// Cross-module: drives the disease-candidate predicate using the REAL turn-
// ownership siblings (VIBE_Character_IsObjectForTurn / _IsOwnerForTurn in
// character_state.cpp) over the shared g_turn network-stride state, and the
// disease-event pick using the REAL ANSI LCG (crt::RandNext). No mocks for the
// decision path — only the cross-cluster command/stock effect tail is omitted.
#include "test.h"

#include "sim/illness.h"
#include "sim/character_state.h"   // IsObjectForTurn / IsOwnerForTurn, g_turn, TurnObject
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {
// Build the IllnessTurnView the candidate test consumes from the REAL turn
// predicates applied to a TurnObject view of the same record.
IllnessTurnView RealTurn(const TurnObject& o) {
    IllnessTurnView v;
    v.ownerForTurn  = IsOwnerForTurn(&o);
    v.objectForTurn = IsObjectForTurn(&o);
    return v;
}
}  // namespace

// In a single-player / standalone game (stride 0, standalone -1), every record
// is local: IsObjectForTurn returns true, so any live person is a candidate.
TEST(SimIllnessI, StandaloneCandidate) {
    SetTurnState(TurnState{ /*stride*/ 0, /*standalone*/ -1, /*my*/ 0, /*local*/ 0 });

    IllnessRec rec{};
    rec.marker = 0;
    rec.active = 1;
    rec.kind = 4;        // person
    rec.id = 12345;

    TurnObject o{};
    o.type = rec.kind;
    o.id = rec.id;
    o.hasOwner = false;
    o.ownerPlayer = 0xFFFF;

    CHECK(IsObjectForTurn(&o));    // standalone -> always local
    CHECK(IllnessIsDiseaseCandidate(&rec, RealTurn(o)));

    // A free slot is never a candidate even when locally owned.
    rec.marker = static_cast<i16>(0xFFFF);
    CHECK(!IllnessIsDiseaseCandidate(&rec, RealTurn(o)));
}

// In a networked game (stride 3, standalone != -1, my slot 1), only records
// whose id % 3 == 1 are locally owned. IsObjectForTurn is then id-gated.
TEST(SimIllnessI, NetworkedTurnGate) {
    SetTurnState(TurnState{ /*stride*/ 3, /*standalone*/ 7, /*my*/ 1, /*local*/ 0 });

    IllnessRec rec{};
    rec.marker = 0;
    rec.active = 1;
    rec.kind = 4;        // person (>1, so IsOwnerForTurn rejects on type)

    // id % 3 == 1 -> object-for-turn true -> candidate.
    rec.id = 10; // 10 % 3 == 1
    TurnObject hit{}; hit.type = rec.kind; hit.id = rec.id;
    CHECK(IsObjectForTurn(&hit));
    CHECK(IllnessIsDiseaseCandidate(&rec, RealTurn(hit)));

    // id % 3 != 1 -> not ours this turn -> NOT a candidate (kind 4 also fails
    // IsOwnerForTurn's type<=1 gate, so both predicates are false).
    rec.id = 11; // 11 % 3 == 2
    TurnObject miss{}; miss.type = rec.kind; miss.id = rec.id;
    CHECK(!IsObjectForTurn(&miss));
    CHECK(!IllnessIsDiseaseCandidate(&rec, RealTurn(miss)));
}

// Drive the full candidate->pick flow with the real LCG: for a candidate record
// with a free disease state, a pick produces a state delta and a non-negative
// cost, and the packed group is no longer "free".
TEST(SimIllnessI, CandidateThenPickWithRealRng) {
    SetTurnState(TurnState{ 0, -1, 0, 0 });
    crt::Srand(2024);

    IllnessRec rec{};
    rec.marker = 0;
    rec.active = 1;
    rec.kind = 3;
    rec.id = 999;
    rec.diseaseState = 0u;   // fully healthy

    TurnObject o{}; o.type = rec.kind; o.id = rec.id;
    CHECK(IllnessIsDiseaseCandidate(&rec, RealTurn(o)));

    DiseasePick p = IllnessPickRandomDiseaseEvent(rec.diseaseState);
    CHECK(p.chosenBit >= 0 && p.chosenBit < 8);
    CHECK(p.group == p.chosenBit + 2);
    CHECK(p.cost >= 0);
    if (p.applied) {
        // The chosen group is no longer free after applying.
        CHECK(IllnessGroupIsFree(rec.diseaseState, p.chosenBit));   // was free before
        CHECK(!IllnessGroupIsFree(p.newState, p.chosenBit));        // occupied after
        rec.diseaseState = p.newState;
    }
}
