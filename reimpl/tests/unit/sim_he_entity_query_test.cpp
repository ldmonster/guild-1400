#include "test.h"

// Unit tests for the He entity-query API (src/sim/he_entity_query.{h,cpp}).
//
// Golden vectors computed by an independent faithful Python model of the original
// pseudocode (pair-table scan -> entity-record resolve -> owner/state filter ->
// dedup + adjacent-swap rank network). This TU supplies its OWN definitions of the
// backing globals (g_persons / g_idPairA / g_idPairB / g_straftatTable /
// g_straftatCounter) so the pool state is fully controlled and deterministic; the
// person lookup is routed through the injectable hook (no entity.cpp dependency).
#include "sim/he_entity_query.h"
#include "sim/types.h"           // Person, kPersonCapacity, kPfKind/kPfId
#include "sim/command_apply.h"   // kIdPairSlots
#include "sim/command_apply4.h"  // kStraftatStride / kStraftatSlots

#include <cstring>

using namespace guild;
using namespace guild::sim;

// --- Local definitions of the reused globals (this is a standalone unit binary;
//     the shared library keeps the single real definitions in their owning TUs). -
namespace guild::sim {
Person g_persons[kPersonCapacity];
i32    g_personIds[kPersonCapacity];
i32    g_idPairA[kIdPairSlots];
i32    g_idPairB[kIdPairSlots];
u8     g_straftatTable[kStraftatSlots * kStraftatStride];
i32    g_straftatCounter = 0;
// Minimal stub for the cross-module leaf the default hook references. Every test
// installs its own personFind hook, so this is never invoked; it exists only to
// satisfy the link of the inert default in this standalone unit binary.
Person* PersonFindRecordById(i32) { return nullptr; }
} // namespace guild::sim

namespace {

// Set entity record `i`: key(+0), owner(+22), state(+37).
void SetEnt(int i, i32 key, i32 owner, i32 state) {
    u8* r = g_straftatTable + i * kHeEntityStride;
    std::memcpy(r + kHeEntKey, &key, 4);
    std::memcpy(r + kHeEntOwner, &owner, 4);
    std::memcpy(r + kHeEntState, &state, 4);
}
void SetPair(int slot, i32 key, i32 value) { g_idPairA[slot] = key; g_idPairB[slot] = value; }
void SetPerson(int idx, i32 id, u8 kind) {
    u8* p = reinterpret_cast<u8*>(&g_persons[idx]);
    *reinterpret_cast<i32*>(p + kPfId) = id;
    p[kPfKind] = kind;
}

// Person lookup hook: returns a record pointer when `id` is in our fixture, else
// nullptr (so the absent-person early-out is exercised).
void* FindHook(i32 id) {
    // Any non-null base resolves the early-out; the functions read person fields
    // off g_persons[personIdx], not off this pointer (except notify, which only
    // forwards it). Return a stable dummy for known player ids.
    if (id == 50 || id == 77 || id == 999) return &g_persons[0];
    return nullptr;
}

// Process-lifetime hook that routes person lookups through FindHook.
const HeEntityQueryHooks& FindHookOnly() {
    static HeEntityQueryHooks h = []{
        HeEntityQueryHooks x{}; x.personFind = FindHook; return x;
    }();
    return h;
}

// Reset the whole fixture to empty (-1 keys, 0 state, -1 pairs).
void ClearFixture() {
    std::memset(g_persons, 0, sizeof(g_persons));
    std::memset(g_straftatTable, 0, sizeof(g_straftatTable));
    for (int i = 0; i < kHeEntitySlots; ++i) SetEnt(i, -1, -1, 0);
    for (int i = 0; i < kIdPairSlots; ++i) { g_idPairA[i] = -1; g_idPairB[i] = -1; }
    g_straftatCounter = 1234;
    SetHeEntityQueryHooks(&FindHookOnly());
}

// Build the standard fixture used by the golden tests:
//   persons: idx0 id=100 k6, idx1 id=200 k7, idx2 id=300 k0, idx3 id=400 k6
//   entities: 0:(1000,o50,s1) 1:(1001,o50,s1) 2:(1002,o77,s1) 3:(1003,o50,s0)
//   pairs: (100->1000),(100->1001),(100->1002),(100->1003),(200->1000)
void BuildStandard() {
    ClearFixture();
    SetPerson(0, 100, 6);
    SetPerson(1, 200, 7);
    SetPerson(2, 300, 0);
    SetPerson(3, 400, 6);
    SetEnt(0, 1000, 50, 1);
    SetEnt(1, 1001, 50, 1);
    SetEnt(2, 1002, 77, 1);
    SetEnt(3, 1003, 50, 0);
    SetPair(0, 100, 1000);
    SetPair(1, 100, 1001);
    SetPair(2, 100, 1002);
    SetPair(3, 100, 1003);
    SetPair(4, 200, 1000);
}

} // namespace

// ---------------------------------------------------------------------------
TEST(SimHeQuery, FindMatchingEntityIndicesGolden) {
    BuildStandard();
    i32 buf[32];
    // owner 50, person idx0 (id 100): records owned by 50 = rec0,rec1,rec3
    // (rec3 is state 0 but Find* checks OWNER only). -> indices [0,1,3].
    int n = He_FindMatchingEntityIndices(50, buf, 0);
    CHECK_EQ(n, 3);
    CHECK_EQ(buf[0], 0);
    CHECK_EQ(buf[1], 1);
    CHECK_EQ(buf[2], 3);
    CHECK_EQ(buf[3], -1);
}

TEST(SimHeQuery, FindMatchingEntityIdsGolden) {
    BuildStandard();
    i32 buf[32];
    int n = He_FindMatchingEntityIds(50, buf, 0);
    CHECK_EQ(n, 3);
    CHECK_EQ(buf[0], 1000);
    CHECK_EQ(buf[1], 1001);
    CHECK_EQ(buf[2], 1003);
    CHECK_EQ(buf[3], -1);
}

TEST(SimHeQuery, CountMatchingEntitiesGolden) {
    BuildStandard();
    CHECK_EQ(He_CountMatchingEntities(50, 0), 3);   // rec0,1,3
    CHECK_EQ(He_CountMatchingEntities(77, 0), 1);   // rec2 only
    CHECK_EQ(He_CountMatchingEntities(123, 0), 0);  // no owner match
}

TEST(SimHeQuery, FindMatchingDifferentOwner) {
    BuildStandard();
    i32 buf[32];
    int n = He_FindMatchingEntityIndices(77, buf, 0);
    CHECK_EQ(n, 1);
    CHECK_EQ(buf[0], 2);
    CHECK_EQ(buf[1], -1);
}

TEST(SimHeQuery, PersonNotFoundReturnsZero) {
    BuildStandard();
    i32 buf[32];
    // person id 5 is not in FindHook -> returns 0, buf untouched preset.
    for (int i = 0; i < 32; ++i) buf[i] = 7;
    CHECK_EQ(He_FindMatchingEntityIndices(5, buf, 0), 0);
    CHECK_EQ(He_CountMatchingEntities(5, 0), 0);
}

TEST(SimHeQuery, CollectPlayerEntitiesByTypeGolden) {
    BuildStandard();
    i32 ids[32], counts[32];
    // active (state==1) records keyed to person100: rec0(o50),rec1(o50),rec2(o77)
    // -> owners {50:2, 77:1}; total distinct = 2, sorted descending by count.
    int total = He_CollectPlayerEntitiesByType(ids, counts, 0);
    CHECK_EQ(total, 2);
    CHECK_EQ(ids[0], 50);
    CHECK_EQ(counts[0], 2);
    CHECK_EQ(ids[1], 77);
    CHECK_EQ(counts[1], 1);
    CHECK_EQ(ids[2], -1);
    CHECK_EQ(counts[2], 0);
}

TEST(SimHeQuery, ResetEntityTablesGolden) {
    BuildStandard();
    int rv = He_ResetEntityTables();
    CHECK_EQ(rv, 16384);
    CHECK_EQ(g_straftatCounter, 0);
    // every entity key/owner -> -1, state -> 0; every pair -> -1.
    CHECK_EQ(*reinterpret_cast<i32*>(g_straftatTable + 0 + kHeEntKey), -1);
    CHECK_EQ(*reinterpret_cast<i32*>(g_straftatTable + 0 + kHeEntOwner), -1);
    CHECK_EQ(*reinterpret_cast<i32*>(g_straftatTable + 0 + kHeEntState), 0);
    CHECK_EQ(*reinterpret_cast<i32*>(g_straftatTable + 511 * kHeEntityStride + kHeEntKey), -1);
    CHECK_EQ(g_idPairA[0], -1);
    CHECK_EQ(g_idPairB[2047], -1);
}

TEST(SimHeQuery, SortEntitiesByRankAscending) {
    // 3 records, rank byte at +28: rec0 rank=5 id=10, rec1 rank=9 id=11,
    // rec2 rank=2 id=12. The original swaps when earlier > later -> ASCENDING.
    u8 buf[3 * kHeEntityStride] = {};
    buf[0 * kHeEntityStride + 0] = 10; buf[0 * kHeEntityStride + kHeEntRank] = 5;
    buf[1 * kHeEntityStride + 0] = 11; buf[1 * kHeEntityStride + kHeEntRank] = 9;
    buf[2 * kHeEntityStride + 0] = 12; buf[2 * kHeEntityStride + kHeEntRank] = 2;
    u8* end = He_SortEntitiesByRank(3, buf);
    CHECK_EQ(buf[0 * kHeEntityStride + kHeEntRank], 2);  // id 12
    CHECK_EQ(buf[0 * kHeEntityStride + 0], 12);
    CHECK_EQ(buf[1 * kHeEntityStride + kHeEntRank], 5);  // id 10
    CHECK_EQ(buf[1 * kHeEntityStride + 0], 10);
    CHECK_EQ(buf[2 * kHeEntityStride + kHeEntRank], 9);  // id 11
    CHECK_EQ(buf[2 * kHeEntityStride + 0], 11);
    CHECK(end == buf + 3 * kHeEntityStride); // one past last inner (off 135)
}

TEST(SimHeQuery, RequestRivalEntityPairsGolden) {
    BuildStandard();
    // Capture queued op35 values and notified rival slots.
    static std::vector<i32> queued;
    static std::vector<int> notified;
    queued.clear(); notified.clear();
    HeEntityQueryHooks h{};
    h.personFind = FindHook;
    h.queueRequestPair35 = [](i32 v) { queued.push_back(v); };
    h.notifyRivalEvent = [](int slot, void*) { notified.push_back(slot); };
    SetHeEntityQueryHooks(&h);

    // player 50: active owned records rec0(key1000), rec1(key1001).
    // rivals = idx0(id100,k6), idx1(id200,k7), idx3(id400,k6) (idx2 k0 excluded).
    // pair(100,1000)+pair(100,1001) flag rival0; pair(200,1000) flags rival1.
    int req = He_RequestRivalEntityPairs(50, 10);
    CHECK_EQ(req, 2);                  // rec0, rec1 requested
    CHECK_EQ((int)queued.size(), 2);
    CHECK_EQ(queued[0], 1000);
    CHECK_EQ(queued[1], 1001);
    // flagged rivals: rival0 (slot 0) and rival1 (slot 4 bytes).
    CHECK_EQ((int)notified.size(), 2);
    CHECK_EQ(notified[0], 0);
    CHECK_EQ(notified[1], 4);

    SetHeEntityQueryHooks(nullptr);
}

TEST(SimHeQuery, MatchRivalEntityPairsGolden) {
    BuildStandard();
    static std::vector<i32> queued2;
    static std::vector<int> notified2;
    queued2.clear(); notified2.clear();
    HeEntityQueryHooks h{};
    h.personFind = FindHook;
    h.queueRequestPair35 = [](i32 v) { queued2.push_back(v); };
    h.notifyRivalEvent = [](int slot, void*) { notified2.push_back(slot); };
    SetHeEntityQueryHooks(&h);

    // supply keys [1000, 1001]; both resolve in the entity table.
    i32 keys[2] = {1000, 1001};
    int req = He_MatchRivalEntityPairs(50, 2, keys);
    CHECK_EQ(req, 2);
    CHECK_EQ((int)queued2.size(), 2);
    CHECK_EQ(queued2[0], 1000);
    CHECK_EQ(queued2[1], 1001);
    // rival0 (pairs 100->1000,1001) and rival1 (pair 200->1000) flagged.
    CHECK_EQ((int)notified2.size(), 2);

    SetHeEntityQueryHooks(nullptr);
}

TEST(SimHeQuery, ReqRivalLimitCapsRequests) {
    BuildStandard();
    static int qcount = 0; qcount = 0;
    HeEntityQueryHooks h{};
    h.personFind = FindHook;
    h.queueRequestPair35 = [](i32) { ++qcount; };
    SetHeEntityQueryHooks(&h);
    // limit 1: only the first owned-active record is requested.
    int req = He_RequestRivalEntityPairs(50, 1);
    CHECK_EQ(req, 1);
    CHECK_EQ(qcount, 1);
    SetHeEntityQueryHooks(nullptr);
}
