// End-to-end test for the Character data/rule core: builds a population of live
// actors across several universes and owners, then runs the full collection +
// count + flagged-local + work-season passes and checks every result against a
// hand-computed reference.
#include "sim/character_query.h"
#include "sim/character_state.h"
#include "sim/character_mesh.h"

#include "tests/framework/test.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct Pop {
    std::vector<MeshHandle> meshes;
    std::vector<LiveActor>  actors;
    Pop() { meshes.reserve(128); actors.reserve(128); }
    LiveActor* add(int uIdx, int homeId, int group, float x) {
        meshes.push_back(MeshHandle{0, {x, 0, 0}});
        actors.push_back(LiveActor{});
        LiveActor& a = actors.back();
        a.mesh = &meshes.back();
        a.universe = &g_universes[uIdx];
        a.universeId = homeId;
        a.groupId = group;
        return &a;
    }
};

} // namespace

TEST(SimCharCoreE2E, PopulationCollectionAndFlags) {
    ResetCharacterQuery();
    ResetFadeSlots();

    // Three universes (slots 0,1,2). Owners keyed by home id:
    //   universe 0, home 100: 6 actors
    //   universe 1, home 200: 4 actors
    //   universe 2, home   -1 (wild): 3 actors
    Pop pop;
    std::vector<LiveActor*> personColumn;  // the Person+388 live-actor column

    auto enroll = [&](LiveActor* a) {
        personColumn.push_back(a);
    };

    for (int i = 0; i < 6; ++i) enroll(pop.add(0, 100, 1, (float)i));
    for (int i = 0; i < 4; ++i) enroll(pop.add(1, 200, 1, (float)i));
    for (int i = 0; i < 3; ++i) enroll(pop.add(2,  -1, 1, (float)i));

    // Place them into the live array.
    int li = 0;
    for (auto& a : pop.actors)
        g_live[li++] = &a;

    g_activeUniverse   = &g_universes[0];
    g_activeUniverseId = 0;

    // --- counts (hand reference) ------------------------------------------
    // universe 0 has 6 actors, universe 1 has 4, -1 matches all 13.
    CHECK_EQ(CountByOwner(0, 1), 6);
    CHECK_EQ(CountByOwner(1, 1), 4);
    CHECK_EQ(CountByOwner(2, 1), 3);
    CHECK_EQ(CountByOwner(-1, 1), 13);

    // --- collection by owner key ------------------------------------------
    static int hidden; hidden = 0;
    struct H { static void sv(LiveActor*, int v) { if (!v) ++hidden; } };
    CharQueryHooks qhooks = {}; qhooks.setVisible = &H::sv;
    SetCharQueryHooks(&qhooks);

    LiveActor* out[31] = {};
    int n100 = CollectByOwner(100, personColumn.data(), (int)personColumn.size(), out, 31);
    CHECK_EQ(n100, 6);
    CHECK_EQ(hidden, 0);                 // 6 <= 8, no overflow hidden

    int n200 = CollectByOwner(200, personColumn.data(), (int)personColumn.size(), out, 31);
    CHECK_EQ(n200, 4);

    int nWild = CollectByOwner(0, personColumn.data(), (int)personColumn.size(), out, 31);
    CHECK_EQ(nWild, 3);
    SetCharQueryHooks(nullptr);

    // --- flagged-local passes ---------------------------------------------
    // Flag the first 3 universe-0 actors + 1 universe-1 actor for redraw.
    personColumn[0]->flagsA |= kLaRedraw;
    personColumn[1]->flagsA |= kLaRedraw;
    personColumn[2]->flagsA |= kLaRedraw;
    personColumn[6]->flagsA |= kLaRedraw;   // universe 1 -> not in active scene

    static int pivots, vis; pivots = vis = 0;
    struct S {
        static void pv(LiveActor*) { ++pivots; }
        static void vz(LiveActor*) { ++vis; }
    };
    CharStateHooks shooks = { &S::pv, &S::vz };
    SetCharStateHooks(&shooks);

    // Only the 3 universe-0 flagged actors are in the active scene.
    CHECK_EQ(ProcessFlaggedLocal(), 3);
    CHECK_EQ(pivots, 3);

    int cleared = RefreshFlaggedLocal();
    CHECK_EQ(cleared, 3);
    CHECK_EQ(vis, 3);
    // their flags are cleared; the universe-1 actor's flag remains (not active).
    CHECK_EQ((personColumn[0]->flagsA & kLaRedraw), 0);
    CHECK_EQ((personColumn[6]->flagsA & kLaRedraw), kLaRedraw);
    SetCharStateHooks(nullptr);

    // --- work-season gate over the population -----------------------------
    // Season class 0 window is [8,20). Reference count of "in-season" at month 12
    // for season-0 actors == all of them; at month 25 == none.
    int inSeason = 0;
    for (auto& a : pop.actors) {
        (void)a;
        if (IsInWorkSeason(0, 12.0f)) ++inSeason;
    }
    CHECK_EQ(inSeason, 13);
    int outSeason = 0;
    for (auto& a : pop.actors) {
        (void)a;
        if (IsInWorkSeason(0, 25.0f)) ++outSeason;
    }
    CHECK_EQ(outSeason, 0);
}

TEST(SimCharCoreE2E, TurnOwnershipPartitionsPopulation) {
    // 3-peer networked game, my slot 1, stride 3. Records are partitioned by
    // id % 3; verify each peer owns exactly its third.
    SetTurnState(TurnState{3, 0, 1, 0});

    int mine = 0;
    for (int id = 0; id < 30; ++id) {
        TurnObject o{}; o.type = 2; o.id = id;
        if (IsObjectForTurn(&o)) ++mine;
    }
    // ids 1,4,7,...,28 -> 10 records.
    CHECK_EQ(mine, 10);

    // type-6 records are always local regardless of stride.
    int localSix = 0;
    for (int id = 0; id < 30; ++id) {
        TurnObject o{}; o.type = 6; o.id = id;
        if (IsObjectForTurn(&o)) ++localSix;
    }
    CHECK_EQ(localSix, 30);

    SetTurnState(TurnState{0, -1, 0, 0});  // restore standalone
}
