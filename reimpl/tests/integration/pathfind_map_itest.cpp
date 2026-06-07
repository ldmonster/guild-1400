// Integration: drive pathfind_map's VIBE_ObjectSearch_FindPeopleByPalette
// (0x47b008) against the REAL reconstructed Person sibling, wired exactly as the
// live engine forwards it. The original's people-palette search gates each
// candidate on its office-rank delta to the reference:
//     abs(refRank - VIBE_Person_ComputeOfficeRank(idx, 0)) < 5
// This reimpl routes that rank query through PathfindMapHooks.computeOfficeRank.
// We forward that hook into the genuine VIBE_Person_ComputeOfficeRank (0x58bccc,
// sim/person.cpp), populate the REAL 536-byte g_persons table (sim/entity.cpp),
// and feed the people-type table as a byte view of those same records — so the
// search and the rank query read one shared, real world. We then assert the
// cross-module collection: the candidate whose REAL computed office rank is far
// from the reference's is excluded by the rank gate.
//
// A second pathfind_map leaf is wired to a REAL sibling too: the eligibility gate
// forwards to VIBE_Person_IsValidActiveRecord (0x4f8e60, sim/person.cpp), so the
// free slots of the shared g_persons table are rejected by the real world.
// PersonComputeOfficeRank itself reaches one more leaf — VIBE_Office_GetDefinition
// — which the Person module exposes through PersonSetOfficeDefinitionHook; we
// install a small office-definition table so the ranks are deterministic. The
// FindByPaletteRange / RasterEdge / BuildDummyName tests below run through the
// module's INERT default-hook path or its pure deterministic core (noted inline).
#include "test.h"

#include "sim/pathfind_map.h"
#include "sim/person.h"
#include "sim/entity.h"
#include "sim/types.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// REAL Person_ComputeOfficeRank sibling, forwarded as the computeOfficeRank hook.
int RealComputeOfficeRank(u16 idx, int stopAtSelf) {
    return PersonComputeOfficeRank(idx, stopAtSelf);
}

// Office-definition table the REAL ComputeOfficeRank consults. We map office id
// to a rank LEVEL via def[2]; office 0 == "no office" (ok==0 -> rank 1), office 9
// == top-rank chief (level 9 -> rank 10).
int OfficeDef(u8 officeId, u8 out[24]) {
    std::memset(out, 0, 24);
    if (officeId == 0) return 0;   // no resolvable office -> rank 1
    out[2] = officeId;             // level == office id (9 => rank 10)
    return 1;
}

// Build a live person record at slot `idx` with the given id, office and kind.
void MakePerson(int idx, i32 id, u8 office, u8 kind = 1) {
    Person& p = g_persons[idx];
    std::memset(&p, 0, sizeof(Person));
    p.marker = static_cast<i16>(idx);   // != -1 -> alive
    p.kind = kind;                       // < 10 -> a real person
    p.id = id;
    p.isPlayer = 1;                      // live actor
    PersonSetByte(&p, kPfOffice, office);
}

} // namespace

// The people-palette search collects candidates whose REAL office-rank is within
// 5 of the reference's. We make the reference rank 1 (no office) and one candidate
// a top-rank chief (rank 10): abs(1 - 10) == 9 >= 5, so the REAL rank query
// excludes it, while an ordinary peer (rank 1) is collected.
TEST(PathfindMapItest, PeoplePaletteRankGateUsesRealPersonRank) {
    // Clear the real table, then populate the slots the probe will visit.
    for (int i = 0; i < kPersonCapacity; ++i) g_persons[i].marker = -1;

    MakePerson(0, /*id*/ 1000, /*office*/ 0);   // reference: rank 1
    MakePerson(1, /*id*/ 1001, /*office*/ 0);   // peer: rank 1 -> within 5 -> collected
    MakePerson(2, /*id*/ 1002, /*office*/ 9);   // chief: rank 10 -> delta 9 -> excluded
    MakePerson(3, /*id*/ 1003, /*office*/ 0);   // peer: rank 1 -> collected

    PersonSetOfficeDefinitionHook(OfficeDef);

    PathfindMapHooks h = PathfindMapGetHooks();   // start from inert defaults
    h.computeOfficeRank = RealComputeOfficeRank;  // <-- real cross-module wiring
    // A second real Person sibling: eligibility forwards to the genuine
    // VIBE_Person_IsValidActiveRecord (0x4f8e60), so the 764 free slots (marker
    // == -1) are correctly rejected by the real world, not a stub.
    h.evaluateEligibility = [](u16, u16 candId, void*) -> int {
        return PersonIsValidActiveRecord(candId) ? 1 : 0;
    };
    h.setGrayColor = [](int, int) {};
    PathfindMapSetHooks(&h);

    // peopleTypeTable is byte_12CE912 — the kind byte (+2) of each 536-byte record,
    // i.e. a byte view of g_persons offset by +2. We pass that real view.
    const u8* typeTable = reinterpret_cast<const u8*>(&g_persons[0]) + kPfKind;

    u16 refId = 0;             // reference is slot 0
    u16 outIds[8] = {0};
    // Stride index 0 -> stride 1 -> visits slots in order from probeStart=0; no
    // favourability range (minR=maxR=100 -> rangeActive false).
    int got = ObjectSearchFindPeopleByPalette(&refId, /*maxPeople*/ 8,
                                              /*minR*/ 100.0f, /*maxR*/ 100.0f,
                                              outIds, /*strideIndex*/ 0,
                                              /*probeStart*/ 0, typeTable);

    PathfindMapHooks inert{};
    PathfindMapSetHooks(nullptr); // restore inert defaults
    (void)inert;
    PersonSetOfficeDefinitionHook(nullptr);

    // Slots 0,1,3 are rank 1 (delta 0 < 5); slot 2 is rank 10 (delta 9, excluded).
    // The reference itself (slot 0) is rank 1 too and passes the gate, so it is
    // collected as well (the original does not skip self here). Expect slots
    // 0,1,3 collected, slot 2 excluded -> 3 results, none equal to 2.
    CHECK_EQ(got, 3);
    bool sawChief = false, saw1 = false, saw3 = false;
    for (int i = 0; i < got; ++i) {
        if (outIds[i] == 2) sawChief = true;
        if (outIds[i] == 1) saw1 = true;
        if (outIds[i] == 3) saw3 = true;
    }
    CHECK(!sawChief);   // the REAL rank-10 chief was gated out
    CHECK(saw1);
    CHECK(saw3);
}

// FindByPaletteRange runs through the module's INERT default-hook path: with the
// inert eligibility hook (always 0) no candidate passes, so every slot finds
// nothing and the function reports failure (0). This exercises the real default
// hooks end to end (no sibling exists for the eligibility leaf).
TEST(PathfindMapItest, FindByPaletteRangeInertDefaultFindsNothing) {
    PathfindMapSetHooks(nullptr);   // inert defaults

    u16 refId = 5;
    u16 outIds[3] = {0xFFFF, 0xFFFF, 0xFFFF};
    int r = ObjectSearchFindByPaletteRange(&refId, /*count*/ 1, /*filter*/ nullptr,
                                           /*minR*/ 100.0f, /*maxR*/ 100.0f,
                                           outIds, /*strideIndex*/ 0,
                                           /*probeStart*/ 0);
    CHECK_EQ(r, 0);   // inert eligibility -> nothing eligible -> slot miss
}

// MapRasterEdgeStepCount is a pure deterministic core (no hooks): the step count
// for an edge of length-squared lenSq is 2*(int)(sqrt(lenSq)+0.9), clamped >= 1.
TEST(PathfindMapItest, RasterEdgeStepCountPureMath) {
    // sqrt(100)=10 -> (int)(10.9)=10 -> 2*10 = 20.
    CHECK_EQ(MapRasterEdgeStepCount(100.0f), 20);
    // sqrt(0)=0 -> (int)(0.9)=0 -> 2*0=0 -> clamped to >= 1.
    CHECK_EQ(MapRasterEdgeStepCount(0.0f), 1);
    // sqrt(4)=2 -> (int)(2.9)=2 -> 2*2=4.
    CHECK_EQ(MapRasterEdgeStepCount(4.0f), 4);
}

// MapBuildDummyName is a pure deterministic core: "dummy_<UPPER(name)>".
TEST(PathfindMapItest, BuildDummyNameUppercases) {
    char buf[64];
    char* out = MapBuildDummyName("Tower", buf);
    CHECK(out != nullptr);
    if (out) CHECK(std::strcmp(out, "dummy_TOWER") == 0);
}
