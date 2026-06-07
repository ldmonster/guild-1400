#include "test.h"

// Integration: the He entity-query API exercised against its REAL sibling modules
// — the real Person array + id->record scan (entity.cpp, PersonFindRecordById via
// the inert default hook), the real id-pair table (command_apply.cpp, g_idPairA/B
// + ResetIdPairTable), and the real 45-byte entity/Straftat table (command_apply4
// .cpp, g_straftatTable + Apply4_StraftatSlot). No local mocks of the backing
// globals: every read/write goes through the genuine cross-module state, so this
// verifies the query loops agree with the real Person lookup and the real table
// geometry (including the enlarged 512-slot/23040-byte g_straftatTable bound).
#include "sim/he_entity_query.h"
#include "sim/entity.h"          // g_persons, g_personIds, PersonFindRecordById
#include "sim/command_apply.h"   // g_idPairA/B, ResetIdPairTable
#include "sim/command_apply4.h"  // g_straftatTable, g_straftatCounter, Apply4_StraftatSlot

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Populate Person slot via the REAL dual-column layout that PersonFindRecordById
// reads: marker (+0) must be != -1 and the parallel g_personIds[idx] == id; the
// query then reads the +4 id field (g_persons[idx].id) as the pair key.
void RealSetPerson(int idx, i32 id, u8 kind) {
    g_persons[idx].marker = 0;          // != -1 -> "alive" for the scan
    g_personIds[idx] = id;              // parallel id column (lookup key)
    g_persons[idx].id = id;             // +4 field (query reads this)
    reinterpret_cast<u8*>(&g_persons[idx])[kPfKind] = kind; // +2 rival kind byte
}

void RealSetEnt(int i, i32 key, i32 owner, i32 state) {
    u8* r = Apply4_StraftatSlot(i);      // real slot accessor (bounds-checked)
    std::memcpy(r + kHeEntKey, &key, 4);
    std::memcpy(r + kHeEntOwner, &owner, 4);
    std::memcpy(r + kHeEntState, &state, 4);
}

// Bring the whole real substrate to a known empty baseline. The rival gather
// scans the raw Person kind/id columns (no marker check, faithful to the
// original), so zero the whole Person array to avoid stale-record phantoms
// leaking across tests; ResetEntityArrays then sets the alive markers.
void RealReset() {
    std::memset(g_persons, 0, sizeof(g_persons));
    ResetEntityArrays();                 // entity.cpp: all persons marker=-1
    He_ResetEntityTables();              // our function: entity table -1, pairs -1
    SetHeEntityQueryHooks(nullptr);      // inert default -> real PersonFindRecordById
}

} // namespace

// ---------------------------------------------------------------------------
TEST(SimHeQueryItest, RealPersonLookupAndScan) {
    RealReset();
    // Person idx 5: id 100, kind 6.
    RealSetPerson(5, 100, 6);
    // Entities owned by player 50: rec0(key1000,s1), rec1(key1001,s1),
    // rec2(key1002 owner 77), rec3(key1003 owner 50 s0).
    RealSetEnt(0, 1000, 50, 1);
    RealSetEnt(1, 1001, 50, 1);
    RealSetEnt(2, 1002, 77, 1);
    RealSetEnt(3, 1003, 50, 0);
    // Pairs keyed by the person's id (100): 1000,1001,1002,1003.
    g_idPairA[0] = 100; g_idPairB[0] = 1000;
    g_idPairA[1] = 100; g_idPairB[1] = 1001;
    g_idPairA[2] = 100; g_idPairB[2] = 1002;
    g_idPairA[3] = 100; g_idPairB[3] = 1003;

    // The real PersonFindRecordById resolves player 50? No — 50 isn't a person.
    // For the query, personId need only be FOUND as a person to pass the early-out;
    // register both player ids (50 and 77) as persons so the real lookup succeeds.
    RealSetPerson(6, 50, 0);
    RealSetPerson(7, 77, 0);

    i32 buf[32];
    int n = He_FindMatchingEntityIndices(50, buf, 5);
    CHECK_EQ(n, 3);                       // rec0, rec1, rec3 (owner 50)
    CHECK_EQ(buf[0], 0);
    CHECK_EQ(buf[1], 1);
    CHECK_EQ(buf[2], 3);

    int ids_n = He_FindMatchingEntityIds(50, buf, 5);
    CHECK_EQ(ids_n, 3);
    CHECK_EQ(buf[0], 1000);
    CHECK_EQ(buf[1], 1001);
    CHECK_EQ(buf[2], 1003);

    CHECK_EQ(He_CountMatchingEntities(50, 5), 3);
    CHECK_EQ(He_CountMatchingEntities(77, 5), 1);
}

TEST(SimHeQueryItest, AbsentPlayerEarlyOut) {
    RealReset();
    RealSetPerson(5, 100, 6);
    // player id 4242 is not a registered person -> real lookup misses -> 0.
    i32 buf[32];
    for (int i = 0; i < 32; ++i) buf[i] = 9;
    CHECK_EQ(He_FindMatchingEntityIndices(4242, buf, 5), 0);
    CHECK_EQ(He_CountMatchingEntities(4242, 5), 0);
}

TEST(SimHeQueryItest, CollectByTypeAgainstRealTables) {
    RealReset();
    RealSetPerson(5, 100, 6);
    RealSetPerson(6, 50, 0);
    RealSetEnt(0, 1000, 50, 1);
    RealSetEnt(1, 1001, 50, 1);
    RealSetEnt(2, 1002, 77, 1);
    g_idPairA[0] = 100; g_idPairB[0] = 1000;
    g_idPairA[1] = 100; g_idPairB[1] = 1001;
    g_idPairA[2] = 100; g_idPairB[2] = 1002;

    i32 ids[32], counts[32];
    int total = He_CollectPlayerEntitiesByType(ids, counts, 5);
    CHECK_EQ(total, 2);                    // owners {50:2, 77:1}
    CHECK_EQ(ids[0], 50);
    CHECK_EQ(counts[0], 2);
    CHECK_EQ(ids[1], 77);
    CHECK_EQ(counts[1], 1);
}

TEST(SimHeQueryItest, ResetMatchesRealSiblingReset) {
    RealReset();
    // Dirty the real tables, then verify He_ResetEntityTables clears them the same
    // way the real ResetIdPairTable would, and zeroes the real st_id counter.
    RealSetEnt(7, 555, 12, 1);
    g_idPairA[10] = 7; g_idPairB[10] = 9;
    g_straftatCounter = 99;
    int rv = He_ResetEntityTables();
    CHECK_EQ(rv, 16384);
    CHECK_EQ(g_straftatCounter, 0);
    CHECK_EQ(g_idPairA[10], -1);
    CHECK_EQ(g_idPairB[10], -1);
    CHECK_EQ(*reinterpret_cast<i32*>(Apply4_StraftatSlot(7) + kHeEntKey), -1);
    CHECK_EQ(*reinterpret_cast<i32*>(Apply4_StraftatSlot(7) + kHeEntState), 0);
    // Cross-check: the real ResetIdPairTable leaves the pair table identical.
    g_idPairA[10] = 7; g_idPairB[10] = 9;
    ResetIdPairTable();
    CHECK_EQ(g_idPairA[10], -1);
    CHECK_EQ(g_idPairB[10], -1);
}

TEST(SimHeQueryItest, RivalScanWalksRealPersonArray) {
    RealReset();
    static std::vector<i32> queued; queued.clear();
    static std::vector<int> notified; notified.clear();
    HeEntityQueryHooks h{};
    h.personFind = nullptr;               // real PersonFindRecordById
    h.queueRequestPair35 = [](i32 v) { queued.push_back(v); };
    h.notifyRivalEvent = [](int slot, void*) { notified.push_back(slot); };
    SetHeEntityQueryHooks(&h);

    // player 50 (a person), and a rival person id 100 kind 6 at idx 5.
    RealSetPerson(6, 50, 0);
    RealSetPerson(5, 100, 6);
    RealSetEnt(0, 1000, 50, 1);           // owned-active record
    g_idPairA[0] = 100; g_idPairB[0] = 1000;  // pair links rival 100 to key 1000

    int req = He_RequestRivalEntityPairs(50, 10);
    CHECK_EQ(req, 1);                      // one owned-active record requested
    CHECK_EQ((int)queued.size(), 1);
    CHECK_EQ(queued[0], 1000);
    CHECK_EQ((int)notified.size(), 1);     // rival 100 flagged + notified

    SetHeEntityQueryHooks(nullptr);
}
