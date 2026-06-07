#include "test.h"

#include "sim/character_path.h"
#include "sim/character_query.h"

#include <cstring>
#include <cstdint>

using namespace guild;
using namespace guild::sim;

// E2E: a spawn -> populate -> proximity-query flow across the slot-table leaves.
// 1. AllocSlot / AllocSlotAtIndex populate the real g_live table.
// 2. FindNearbyWide groups the live actors by universe/group within the box.
// 3. PickWaitAnimation picks a wait animation from a collected set deterministically.
// This exercises the alloc + scan + pick path the way the per-frame update does.

namespace {
struct Arena {
    static constexpr int kMax = 8;
    LiveActor recs[kMax];
    MeshHandle meshes[kMax];
    Universe uni;
    int next = 0;
    Arena() { std::memset(recs, 0, sizeof(recs)); std::memset(meshes, 0, sizeof(meshes)); }
    void* take() { return next < kMax ? (void*)&recs[next++] : nullptr; }
};
Arena* g_arena = nullptr;
void* E2EAlloc(unsigned, const char*) { return g_arena ? g_arena->take() : nullptr; }
void  E2EFree(void*) {}
void  E2EClear(int, int, void*) {}   // arena is already zeroed; preserve mesh ptrs set after

bool E2EVecTol(const float* a, const float* b, float tol) {
    for (int i = 0; i < 3; ++i) {
        float d = a[i] - b[i]; if (d < 0) d = -d;
        if (d > tol) return false;
    }
    return true;
}

int g_collectCount = 0;
const int* g_collectIds = nullptr;
int E2ECollect(int* out, int cap) {
    int n = g_collectCount < cap ? g_collectCount : cap;
    for (int i = 0; i < n; ++i) out[i] = g_collectIds[i];
    return n;
}
unsigned E2ERand() { return 7; }  // 7 % 4 == 3
}  // namespace

TEST(CharPathE2E, SpawnPopulateQueryPick) {
    ResetCharacterQuery();
    Arena arena; g_arena = &arena;

    CharacterPathHooks h = CharacterPathGetHooks();
    h.allocDebug = E2EAlloc;
    h.freeDebug = E2EFree;
    h.clearRecord = E2EClear;
    h.vectorWithinTolerance = E2EVecTol;
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    // Spawn the "self" actor at the free head, then peers at fixed indices.
    LiveActor* self = AllocSlot();
    CHECK(self != nullptr);
    LiveActor* peerNear = AllocSlotAtIndex(3);
    LiveActor* peerFar  = AllocSlotAtIndex(4);
    CHECK(peerNear != nullptr);
    CHECK(peerFar != nullptr);

    if (self && peerNear && peerFar) {
        // Wire meshes/universe so they share a universe + group.
        self->mesh = &arena.meshes[0];
        self->mesh->pos[0] = 0; self->mesh->pos[1] = 0; self->mesh->pos[2] = 0;
        self->universe = &arena.uni; self->universeId = 5; self->groupId = 2;

        peerNear->mesh = &arena.meshes[1];
        peerNear->mesh->pos[0] = 120; peerNear->mesh->pos[1] = 0; peerNear->mesh->pos[2] = 0;
        peerNear->universe = &arena.uni; peerNear->universeId = 5; peerNear->groupId = 2;

        peerFar->mesh = &arena.meshes[2];
        peerFar->mesh->pos[0] = 5000; peerFar->mesh->pos[1] = 0; peerFar->mesh->pos[2] = 0;
        peerFar->universe = &arena.uni; peerFar->universeId = 5; peerFar->groupId = 2;

        LiveActor* out[kNearbyWideMax];
        int n = FindNearbyWide(self, out);
        CHECK_EQ(n, 1);              // only peerNear is in the 300-box
        if (n == 1) CHECK_EQ((void*)out[0], (void*)peerNear);
    }

    // Pick a wait animation from a 4-element set; rand 7 % 4 == 3 -> last.
    static const int ids[4] = {100, 200, 300, 400};
    g_collectCount = 4; g_collectIds = ids;
    int picked = PickWaitAnimation(E2ECollect, E2ERand);
    CHECK_EQ(picked, 400);

    CharacterPathSetHooks(&prev);
    ResetCharacterQuery();
    g_arena = nullptr;
}

// E2E: the path-endpoint resolver clamps both endpoints and forwards to the
// waypoint builder, using the inert default heightmap (always-hit) wiring.
namespace {
int OkWorldToTile(int, const float* w, int* tile, float* h) {
    *tile = static_cast<int>(w[0]);   // tile coord from world X
    *h = 0.0f;
    return 1;
}
u16 OkBuildWaypoints(int* outLen, int, int startCol, int goalCol, int) {
    // Store a synthetic length proportional to the span; success.
    if (outLen) *outLen = (goalCol - startCol) * 2;
    return 0;
}
}  // namespace

TEST(CharPathE2E, ResolveEndpointsClampsAndBuilds) {
    CharacterPathHooks h = CharacterPathGetHooks();
    h.worldToTileWithHeight = OkWorldToTile;
    h.buildWaypointList = OkBuildWaypoints;
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    float start[3] = {-9, 0, 0};   // X=-9 -> tile -9 -> clamp 1
    float goal[3]  = {500, 0, 0};  // X=500 -> clamp gridDim-2 == 98
    int s = -1, g = -1, len = -1;
    u16 r = ResolvePathEndpoints(/*grid*/0, /*gridDim*/100, start, goal, /*mode*/0,
                                 &s, &g, &len);
    CHECK_EQ(r, 0);          // build succeeded (returned 0, not 0xFFFF)
    CHECK_EQ(s, 1);          // start clamped low
    CHECK_EQ(g, 98);         // goal clamped high
    CHECK_EQ(len, (98 - 1) * 2);

    CharacterPathSetHooks(&prev);
}

TEST(CharPathE2E, ResolveEndpointsFailsOnTileMiss) {
    CharacterPathHooks h = CharacterPathGetHooks();
    // worldToTile default returns 0 (miss) -> resolver returns 0xFFFF.
    CharacterPathHooks prev = CharacterPathSetHooks(&h);
    float start[3] = {1, 0, 0}, goal[3] = {2, 0, 0};
    int s = 0, g = 0, len = 0;
    u16 r = ResolvePathEndpoints(0, 100, start, goal, 0, &s, &g, &len);
    CHECK_EQ((int)r, 0xFFFF);
    CharacterPathSetHooks(&prev);
}
