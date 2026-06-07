#include "test.h"

// E2E: a full "evidence / rival entity" pass across the entire He entity-query
// cluster, driven end-to-end against the REAL sibling tables (entity.cpp Person
// array, command_apply id-pair table, command_apply4 Straftat/entity table). The
// synthetic flow seeds a small town, then chains the queries the way the office /
// evidence / rival dialogs do:
//   reset -> seed -> Count -> FindIndices -> (gather records) -> SortByRank ->
//   CollectByType -> RequestRivalEntityPairs (op35 + rival notify).
// The real-asset variant is GUARDED behind GUILD_GAME_DIR: absent the game dir
// the test runs the synthetic flow only (the dir is not needed for the in-memory
// tables, so the guarded branch just records that the sentinel was checked).
#include "sim/he_entity_query.h"
#include "sim/entity.h"
#include "sim/command_apply.h"
#include "sim/command_apply4.h"

#include <cstring>
#include <cstdlib>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

void E2ESetPerson(int idx, i32 id, u8 kind) {
    g_persons[idx].marker = 0;
    g_personIds[idx] = id;
    g_persons[idx].id = id;
    reinterpret_cast<u8*>(&g_persons[idx])[kPfKind] = kind;
}
void E2ESetEnt(int i, i32 key, i32 owner, i32 state, u8 rank) {
    u8* r = Apply4_StraftatSlot(i);
    std::memcpy(r + kHeEntKey, &key, 4);
    std::memcpy(r + kHeEntOwner, &owner, 4);
    std::memcpy(r + kHeEntState, &state, 4);
    r[kHeEntRank] = rank;
}
void E2EReset() {
    std::memset(g_persons, 0, sizeof(g_persons));
    ResetEntityArrays();
    He_ResetEntityTables();
    SetHeEntityQueryHooks(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
TEST(SimHeQueryE2E, FullEvidenceAndRivalPass) {
    E2EReset();

    // --- seed a small town ---------------------------------------------------
    // Players: 50 and 77 (registered persons). Rival NPCs: id 100 (k6), 200 (k7).
    E2ESetPerson(0, 50, 0);
    E2ESetPerson(1, 77, 0);
    E2ESetPerson(2, 100, 6);   // rival kind 6
    E2ESetPerson(3, 200, 7);   // rival kind 7
    E2ESetPerson(4, 300, 0);   // non-rival

    // Entity records (key, owner, state, rank byte +28):
    E2ESetEnt(0, 1000, 50, 1, 5);
    E2ESetEnt(1, 1001, 50, 1, 9);
    E2ESetEnt(2, 1002, 77, 1, 2);
    E2ESetEnt(3, 1003, 50, 0, 7);

    // Pairs: NPC 100 owns links to 1000,1001,1002,1003; NPC 200 -> 1000.
    g_idPairA[0] = 100; g_idPairB[0] = 1000;
    g_idPairA[1] = 100; g_idPairB[1] = 1001;
    g_idPairA[2] = 100; g_idPairB[2] = 1002;
    g_idPairA[3] = 100; g_idPairB[3] = 1003;
    g_idPairA[4] = 200; g_idPairB[4] = 1000;

    // --- step 1: count + list player 50's evidence (records owned by 50) -------
    int count = He_CountMatchingEntities(50, 2 /*NPC 100*/);
    CHECK_EQ(count, 3);                       // rec0,1,3 owned by 50

    i32 idxBuf[32];
    int n = He_FindMatchingEntityIndices(50, idxBuf, 2);
    CHECK_EQ(n, count);
    CHECK_EQ(idxBuf[0], 0);
    CHECK_EQ(idxBuf[1], 1);
    CHECK_EQ(idxBuf[2], 3);

    // --- step 2: gather those records into a contiguous buffer + rank-sort -----
    // (this is exactly what EvidenceDialog_ShowDetails @0x547e88 does before it
    // renders the list: qmemcpy each matched record, then SortEntitiesByRank.)
    u8 gathered[3 * kHeEntityStride];
    for (int i = 0; i < n; ++i)
        std::memcpy(gathered + i * kHeEntityStride,
                    Apply4_StraftatSlot(idxBuf[i]), kHeEntityStride);
    He_SortEntitiesByRank(n, gathered);
    // ranks were 5,9,7 -> ascending 5,7,9; keys reorder to 1000,1003,1001.
    CHECK_EQ(gathered[0 * kHeEntityStride + kHeEntRank], 5);
    CHECK_EQ(gathered[1 * kHeEntityStride + kHeEntRank], 7);
    CHECK_EQ(gathered[2 * kHeEntityStride + kHeEntRank], 9);
    CHECK_EQ(*reinterpret_cast<i32*>(gathered + 0 * kHeEntityStride + kHeEntKey), 1000);
    CHECK_EQ(*reinterpret_cast<i32*>(gathered + 1 * kHeEntityStride + kHeEntKey), 1003);
    CHECK_EQ(*reinterpret_cast<i32*>(gathered + 2 * kHeEntityStride + kHeEntKey), 1001);

    // --- step 3: collect the per-owner tally for the office panel --------------
    i32 ids[32], counts[32];
    int distinct = He_CollectPlayerEntitiesByType(ids, counts, 2);
    CHECK_EQ(distinct, 2);                    // owners {50:2, 77:1}
    CHECK_EQ(ids[0], 50);
    CHECK_EQ(counts[0], 2);
    CHECK_EQ(ids[1], 77);
    CHECK_EQ(counts[1], 1);

    // --- step 4: the rival apology/indulgence flow -> op35 + notify ------------
    static std::vector<i32> queued; queued.clear();
    static std::vector<int> notified; notified.clear();
    HeEntityQueryHooks h{};
    h.personFind = nullptr;                   // real PersonFindRecordById
    h.queueRequestPair35 = [](i32 v) { queued.push_back(v); };
    h.notifyRivalEvent = [](int slot, void*) { notified.push_back(slot); };
    SetHeEntityQueryHooks(&h);

    int requested = He_RequestRivalEntityPairs(50, 10);
    CHECK_EQ(requested, 2);                   // rec0(1000), rec1(1001) active+owned
    CHECK_EQ((int)queued.size(), 2);
    CHECK_EQ(queued[0], 1000);
    CHECK_EQ(queued[1], 1001);
    // rivals gathered in array order: NPC100 (j0), NPC200 (j1). Pair(100,1000/1001)
    // flags NPC100; pair(200,1000) flags NPC200 -> both notified.
    CHECK_EQ((int)notified.size(), 2);
    CHECK_EQ(notified[0], 0);                  // rival 0 byte-slot
    CHECK_EQ(notified[1], 4);                  // rival 1 byte-slot

    // --- step 5: MatchRivalEntityPairs over an explicit key list --------------
    queued.clear(); notified.clear();
    i32 keys[2] = {1000, 1001};
    int matched = He_MatchRivalEntityPairs(50, 2, keys);
    CHECK_EQ(matched, 2);
    CHECK_EQ((int)queued.size(), 2);

    SetHeEntityQueryHooks(nullptr);
}

TEST(SimHeQueryE2E, GuardedRealAssetVariant) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir) {
        std::printf("  [skip] SimHeQueryE2E.GuardedRealAssetVariant: "
                    "GUILD_GAME_DIR unset (clean skip)\n");
        return;  // clean skip — no checks recorded
    }
    // With a real game dir present we still exercise the same in-memory query
    // spine (the He tables are runtime BSS, not loaded from disk), confirming the
    // cluster operates against the real sibling globals end-to-end.
    E2EReset();
    E2ESetPerson(0, 50, 0);
    E2ESetEnt(0, 2000, 50, 1, 3);
    g_idPairA[0] = 50; g_idPairB[0] = 2000;   // self-keyed link for a smoke check
    // person 50 keyed pairs: pair key must equal person's id (50).
    CHECK_EQ(He_CountMatchingEntities(50, 0), 1);
}
