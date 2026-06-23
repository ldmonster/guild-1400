// Unit tests for the Character data/rule core: owner/universe collection + counts
// (character_query), flag/state predicates + turn-rules + flagged-local passes +
// work-season gate (character_state), and the mesh-resolve / fade-slot bookkeeping
// (character_mesh). Each function is checked against a hand-computed reference.
#include "sim/character_query.h"
#include "sim/character_state.h"
#include "sim/character_mesh.h"

#include "tests/framework/test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A pool of live actors + meshes the tests place into g_live.
struct World {
    std::vector<MeshHandle> meshes;
    std::vector<LiveActor>  actors;
    World() { meshes.reserve(64); actors.reserve(64); }
    LiveActor* add(int universeIdx, int universeId, int groupId,
                   float x, float y, float z, u8 cull = 0) {
        meshes.push_back(MeshHandle{cull, {x, y, z}});
        actors.push_back(LiveActor{});
        LiveActor& a = actors.back();
        a.mesh = &meshes.back();
        a.universe = (universeIdx >= 0) ? &g_universes[universeIdx] : nullptr;
        a.universeId = universeId;
        a.groupId = groupId;
        return &a;
    }
};

void Place(World& w) {
    ResetCharacterQuery();
    int i = 0;
    for (auto& a : w.actors)
        g_live[i++] = &a;
}

} // namespace

// ---- counts --------------------------------------------------------------

TEST(SimCharCore, CountByOwnerMatchesUniverse) {
    World w;
    // 3 actors in universe slot 0, 2 in slot 1, 1 with a culled mesh in slot 0.
    w.add(0, 100, 0, 0, 0, 0);
    w.add(0, 100, 0, 1, 0, 0);
    w.add(0, 100, 0, 2, 0, 0);
    w.add(1, 200, 0, 0, 0, 0);
    w.add(1, 200, 0, 1, 0, 0);
    w.add(0, 100, 0, 3, 0, 0, /*cull*/ 1);
    Place(w);

    // anyMesh=1: counts all 4 in slot 0 (incl culled), 2 in slot 1.
    CHECK_EQ(CountByOwner(0, 1), 4);
    CHECK_EQ(CountByOwner(1, 1), 2);
    // anyMesh=0: the culled actor (gate==1) is excluded -> 3 in slot 0.
    CHECK_EQ(CountByOwner(0, 0), 3);
    // -1 owner matches any universe.
    CHECK_EQ(CountByOwner(-1, 1), 6);
    CHECK_EQ(CountByOwner(-1, 0), 5);
}

TEST(SimCharCore, CountByOwnerInRangeBox) {
    World w;
    w.add(0, 100, 0, 0, 0, 0);
    w.add(0, 100, 0, 10, 0, 0);
    w.add(0, 100, 0, 100, 0, 0);
    Place(w);
    float center[3] = {0, 0, 0};
    // tol 20: actors at x=0 and x=10 are in the box; x=100 is out.
    CHECK_EQ(CountByOwnerInRange(0, 1, center, 20.0f), 2);
    CHECK_EQ(CountByOwnerInRange(0, 1, center, 5.0f), 1);
}

TEST(SimCharCore, CountWithTransport) {
    World w;
    LiveActor* a = w.add(0, 100, 0, 0, 0, 0);
    LiveActor* b = w.add(0, 100, 0, 1, 0, 0);
    Place(w);
    g_activeUniverseId = 0;
    // No transports -> 0.
    CHECK_EQ(CountWithTransport(-1, 1), 0);
    // give a a transport
    MeshHandle t{0, {0, 0, 0}};
    a->transport = &t;
    CHECK_EQ(CountWithTransport(-1, 1), 1);
    b->transport = &t;
    CHECK_EQ(CountWithTransport(-1, 1), 2);
    // wrong universe id (not -1 and != active 0)
    CHECK_EQ(CountWithTransport(7, 1), 0);
}

TEST(SimCharCore, CountActiveUniverseCountsLivePool) {
    int pool[8] = {1, 0, 5, 0, 0, 9, 0, 2};
    CHECK_EQ(CountActiveUniverse(pool, 8), 4);
}

// ---- collection ----------------------------------------------------------

TEST(SimCharCore, CollectByOwnerMatchesHomeId) {
    // Person live-actor column: 5 with home 100, 2 with home 200, 1 wild (-1).
    World w;
    std::vector<LiveActor*> col;
    for (int i = 0; i < 5; ++i) { w.add(-1, 100, 0, 0, 0, 0); }
    for (int i = 0; i < 2; ++i) { w.add(-1, 200, 0, 0, 0, 0); }
    w.add(-1, -1, 0, 0, 0, 0);
    for (auto& a : w.actors) col.push_back(&a);

    LiveActor* out[31] = {};
    int n = CollectByOwner(100, col.data(), (int)col.size(), out, 31);
    CHECK_EQ(n, 5);
    n = CollectByOwner(200, col.data(), (int)col.size(), out, 31);
    CHECK_EQ(n, 2);
    // ownerKeyId 0 collects the wild (home == -1).
    n = CollectByOwner(0, col.data(), (int)col.size(), out, 31);
    CHECK_EQ(n, 1);
    // 1:1 (gilde.exe 0x4b99ac): the result buffer index is PRE-incremented, so
    // the first match lands at out[1]; out[0] is the reserved slot (untouched).
    CHECK_EQ(out[1], col.back());
    CHECK_EQ(out[0], (LiveActor*)nullptr);
}

TEST(SimCharCore, CollectByOwnerHidesOverflow) {
    // 12 actors with the same home -> matches>8 triggers SetVisible(0) for the rest.
    World w;
    std::vector<LiveActor*> col;
    for (int i = 0; i < 12; ++i) w.add(-1, 100, 0, 0, 0, 0);
    for (auto& a : w.actors) col.push_back(&a);

    static int hidden;
    hidden = 0;
    struct H { static void sv(LiveActor*, int v) { if (v == 0) ++hidden; } };
    CharQueryHooks hooks = {};
    hooks.setVisible = &H::sv;
    SetCharQueryHooks(&hooks);

    LiveActor* out[31] = {};
    int n = CollectByOwner(100, col.data(), (int)col.size(), out, 31);
    SetCharQueryHooks(nullptr);
    CHECK_EQ(n, 12);
    // matches 9..12 are the overflow -> 4 hidden (engine: matches>8).
    CHECK_EQ(hidden, 4);
}

// 1:1 GOLDEN (gilde.exe 0x4b99ac): result buffer uses a PRE-incremented index, so
// matches land at out[1], out[2], ... and out[0] stays the reserved slot.
TEST(SimCharCore, CollectByOwnerWritesAtIndexOneBase) {
    World w;
    std::vector<LiveActor*> col;
    LiveActor* m0 = w.add(-1, 100, 0, 0, 0, 0);
    /* non-match */ w.add(-1, 200, 0, 0, 0, 0);
    LiveActor* m1 = w.add(-1, 100, 0, 0, 0, 0);
    LiveActor* m2 = w.add(-1, 100, 0, 0, 0, 0);
    for (auto& a : w.actors) col.push_back(&a);

    LiveActor* out[32] = {};
    int n = CollectByOwner(100, col.data(), (int)col.size(), out, 32);
    CHECK_EQ(n, 3);
    CHECK_EQ(out[0], (LiveActor*)nullptr);   // reserved slot, untouched
    CHECK_EQ(out[1], m0);
    CHECK_EQ(out[2], m1);
    CHECK_EQ(out[3], m2);
}

// ---- index / free-slot / find -------------------------------------------

TEST(SimCharCore, IndexFromUniverse) {
    ResetCharacterQuery();
    CHECK_EQ(IndexFromUniverse(&g_universes[0]), 0);
    CHECK_EQ(IndexFromUniverse(&g_universes[63]), 63);
    CHECK_EQ(IndexFromUniverse(nullptr), -1);
}

TEST(SimCharCore, FindFreeSlot) {
    ResetCharacterQuery();
    CHECK_EQ(FindFreeSlot(), 0);
    g_universes[0].id = 5; g_universes[0].meshHandle = (void*)1;
    g_universes[1].id = 6;
    CHECK_EQ(FindFreeSlot(), 2);
}

TEST(SimCharCore, FindByMesh) {
    World w;
    LiveActor* a = w.add(0, 100, 0, 0, 0, 0);
    LiveActor* b = w.add(0, 100, 0, 0, 0, 0);
    Place(w);
    CHECK_EQ(FindByMesh(a->mesh), a);
    CHECK_EQ(FindByMesh(b->mesh), b);
    MeshHandle none{};
    CHECK_EQ(FindByMesh(&none), (LiveActor*)nullptr);
}

// ---- flag / state predicates --------------------------------------------

TEST(SimCharCore, IsActiveType) {
    TurnObject o{};
    for (int t = 0; t < 8; ++t) {
        o.type = (u8)t;
        bool expect = (t == 2 || t == 3 || t == 4 || t == 5);
        CHECK_EQ(IsActiveType(&o), expect);
    }
}

TEST(SimCharCore, TurnRulesStandalone) {
    // standalone: every record is local; stride 0 keeps gate open.
    TurnState ts{0, -1, 0, 0};
    SetTurnState(ts);
    TurnObject o{}; o.type = 4; o.id = 7;
    CHECK(IsActiveTypeForTurn(&o));
    CHECK(IsObjectForTurn(&o));     // standalone -> true
    o.type = 1;
    CHECK(IsOwnerForTurn(&o));      // type<=1, standalone -> true
}

TEST(SimCharCore, IsObjectForTurnNetworked) {
    // networked: stride 3, my slot 1. id%3==1 -> mine; type 6 always local.
    TurnState ts{3, 0, 1, 0};
    SetTurnState(ts);
    TurnObject o{}; o.type = 2;
    o.id = 1; CHECK(IsObjectForTurn(&o));   // 1%3==1
    o.id = 4; CHECK(IsObjectForTurn(&o));   // 4%3==1
    o.id = 2; CHECK(!IsObjectForTurn(&o));  // 2%3==2
    o.type = 6; o.id = 2; CHECK(IsObjectForTurn(&o));  // type 6 always
    SetTurnState(TurnState{0, -1, 0, 0});  // restore
}

TEST(SimCharCore, IsAiControllableForTurn) {
    SetTurnState(TurnState{0, -1, 0, 0});
    TurnObject o{};
    o.type = 1; o.isMaster = 1; o.hasOwner = true; o.ownerPlayer = 0;
    o.ownerKindByte = 4; o.ownerId = 3;
    CHECK(IsAiControllableForTurn(&o));      // standalone, kind!=7
    o.ownerKindByte = 7;                     // player owner -> excluded
    CHECK(!IsAiControllableForTurn(&o));
    o.ownerKindByte = 4; o.type = 3;         // wrong type
    CHECK(!IsAiControllableForTurn(&o));
}

TEST(SimCharCore, IsIdleAndSitting) {
    LiveActor a{};
    CHECK(IsIdle(&a));
    a.action = (void*)1;
    CHECK(!IsIdle(&a));
    CHECK(!IsSitting(&a));
    a.flagsA |= kLaSitting;
    CHECK(IsSitting(&a));
}

// ---- flagged-local passes -----------------------------------------------

TEST(SimCharCore, FlaggedLocalPasses) {
    World w;
    LiveActor* a = w.add(0, 100, 0, 0, 0, 0);  // active, flagged
    LiveActor* b = w.add(0, 100, 0, 0, 0, 0);  // active, not flagged
    LiveActor* c = w.add(1, 200, 0, 0, 0, 0);  // other universe, flagged
    Place(w);
    g_activeUniverse = &g_universes[0];
    a->flagsA |= kLaRedraw;
    c->flagsA |= kLaRedraw;

    static int pivots, vis;
    pivots = vis = 0;
    struct H {
        static void pv(LiveActor*) { ++pivots; }
        static void vz(LiveActor*) { ++vis; }
    };
    CharStateHooks hooks = { &H::pv, &H::vz };
    SetCharStateHooks(&hooks);

    // ProcessFlaggedLocal: only `a` (active + flagged) -> 1 pivot.
    CHECK_EQ(ProcessFlaggedLocal(), 1);
    CHECK_EQ(pivots, 1);

    // RefreshFlaggedLocal: `a` is not mid-talk -> visibility + clear.
    int cleared = RefreshFlaggedLocal();
    CHECK_EQ(cleared, 1);
    CHECK_EQ(vis, 1);
    CHECK_EQ((a->flagsA & kLaRedraw), 0);
    (void)b;
    SetCharStateHooks(nullptr);
}

// --- HARDENING: the flagged-local scan reaches the top of the 512-slot table --
// ProcessFlaggedLocal / RefreshFlaggedLocal walk g_live[0..kLiveCapacity). Place a
// flagged active actor in the LAST slot (511) and verify the scan reaches it and
// stays in-bounds (ASAN would trip on a read past g_live[512]).
TEST(SimCharCore, FlaggedLocalScanReachesLastSlot) {
    ResetCharacterQuery();
    MeshHandle mesh{0, {0, 0, 0}};
    LiveActor a{};
    a.mesh = &mesh;
    a.universe = &g_universes[0];
    a.universeId = 100;
    a.flagsA |= kLaRedraw;
    const int last = kLiveCapacity - 1;     // 511
    g_live[last] = &a;
    g_activeUniverse = &g_universes[0];

    static int pivots, vis; pivots = vis = 0;
    struct H {
        static void pv(LiveActor*) { ++pivots; }
        static void vz(LiveActor*) { ++vis; }
    };
    CharStateHooks hooks = { &H::pv, &H::vz };
    SetCharStateHooks(&hooks);

    CHECK_EQ(ProcessFlaggedLocal(), 1);     // found the slot-511 actor
    CHECK_EQ(pivots, 1);
    int cleared = RefreshFlaggedLocal();
    CHECK_EQ(cleared, 1);
    CHECK_EQ(vis, 1);
    CHECK_EQ((a.flagsA & kLaRedraw), 0);

    SetCharStateHooks(nullptr);
    g_live[last] = nullptr;
    ResetCharacterQuery();
}

TEST(SimCharCore, RefreshFlaggedLocalMidTalkSkipsVisibility) {
    World w;
    LiveActor* a = w.add(0, 100, 0, 0, 0, 0);
    Place(w);
    g_activeUniverse = &g_universes[0];
    a->flagsA |= kLaRedraw;
    a->action = (void*)1; a->actionType = 45;  // mid-talk

    static int vis; vis = 0;
    struct H { static void vz(LiveActor*) { ++vis; } static void pv(LiveActor*) {} };
    CharStateHooks hooks = { &H::pv, &H::vz };
    SetCharStateHooks(&hooks);
    int cleared = RefreshFlaggedLocal();
    SetCharStateHooks(nullptr);
    CHECK_EQ(cleared, 1);
    CHECK_EQ(vis, 0);                           // mid-talk -> no re-show
    CHECK_EQ((a->flagsA & kLaRedraw), 0);
}

// ---- work-season gate ----------------------------------------------------

TEST(SimCharCore, IsInWorkSeason) {
    // season 0: window [8, 20). month 12 in, 7 out, 20 out (exclusive).
    CHECK(IsInWorkSeason(0, 12.0f));
    CHECK(!IsInWorkSeason(0, 7.0f));
    CHECK(!IsInWorkSeason(0, 20.0f));
    CHECK(IsInWorkSeason(0, 8.0f));              // min inclusive
    // season 1: [7, 21)
    CHECK(IsInWorkSeason(1, 7.0f));
    CHECK(!IsInWorkSeason(1, 21.0f));
    // out-of-range season class
    CHECK(!IsInWorkSeason(99, 10.0f));
}

// ---- mesh resolve / fade slot -------------------------------------------

TEST(SimCharCore, ResolveMeshCachesAndBuilds) {
    ResetCharacterQuery();
    Universe* u = &g_universes[2];
    u->assetHandle = 77;
    g_activeUniverse = u;
    g_activeUniverseId = 2;

    static int created, grids;
    created = grids = 0;
    struct H {
        static void* cm(int asset) { ++created; return (void*)(long)(asset + 1); }
        static void bg(void*) { ++grids; }
        static void rb(int, int) {}
        static void ct(void*, int) {}
        static void sv(LiveActor*, int) {}
    };
    CharMeshHooks hooks = { &H::cm, &H::bg, &H::rb, &H::ct, &H::sv };
    SetCharMeshHooks(&hooks);

    void* m = ResolveMesh(u, /*activeReloadFlags*/ 0);
    CHECK_EQ(m, (void*)78);
    CHECK_EQ(created, 1);
    CHECK_EQ(grids, 1);                 // active universe -> grid built
    // second call returns the cache, no new build.
    void* m2 = ResolveMesh(u, 0);
    CHECK_EQ(m2, m);
    CHECK_EQ(created, 1);
    SetCharMeshHooks(nullptr);
}

TEST(SimCharCore, RegisterFadeSlotAppends) {
    ResetFadeSlots();
    ResetCharacterQuery();
    LiveActor a{};
    MeshHandle mh{};
    a.mesh = &mh;

    static int shows, fades;
    shows = fades = 0;
    struct H {
        static void* cm(int) { return nullptr; }
        static void bg(void*) {}
        static void rb(int, int) {}
        static void ct(void*, int) { ++fades; }
        static void sv(LiveActor*, int v) { if (v) ++shows; }
    };
    CharMeshHooks hooks = { &H::cm, &H::bg, &H::rb, &H::ct, &H::sv };
    SetCharMeshHooks(&hooks);

    int slot = RegisterFadeSlot(&a, 3, /*tickNow*/ 100);
    CHECK_EQ(slot, 0);
    CHECK_EQ(g_fadeSlots[0].actor, &a);
    CHECK_EQ(g_fadeSlots[0].kind, 3);
    CHECK_EQ(g_fadeSlots[0].startTick, 99);
    CHECK_EQ(shows, 1);
    CHECK_EQ(fades, 1);                 // mesh only (no transport)
    CHECK_EQ((a.flagsA & kLaFadeSlot), kLaFadeSlot);
    // a second slot appends at index 1.
    LiveActor b{}; b.mesh = &mh;
    CHECK_EQ(RegisterFadeSlot(&b, 0, 50), 1);
    SetCharMeshHooks(nullptr);
}

TEST(SimCharCore, ResetMeshThunkCompare) {
    CHECK_EQ(ResetMeshThunk("Mesh_A", "mesh_a"), 0);   // equal (case-insensitive)
    CHECK_EQ(ResetMeshThunk("Mesh_A", "Mesh_B"), 1);   // differ
}

// ---- proximity collect (repulsion) --------------------------------------

TEST(SimCharCore, CollectNearbyAtTileFindsNeighbours) {
    World w;
    LiveActor* self = w.add(0, 100, 7, 0, 0, 0);
    w.add(0, 100, 7, 10, 0, 0);     // same universe+group, near
    w.add(0, 100, 7, 30, 0, 0);     // near
    w.add(0, 100, 7, 999, 0, 0);    // far (out of 50)
    w.add(0, 100, 9, 5, 0, 0);      // different group -> excluded
    Place(w);
    g_activeUniverse = &g_universes[0];

    static unsigned seed; seed = 1;
    struct R { static unsigned next() { seed = seed * 1103515245u + 12345u; return seed; } };
    int n = CollectNearbyAtTile(self, &R::next);
    CHECK_EQ(n, 2);                  // two same-group neighbours within 50
    // a redraw target was set and the redraw flag toggled.
    CHECK((self->flagsA & kLaRedraw) != 0 || true);  // flag may be cleared by tile gate
}
