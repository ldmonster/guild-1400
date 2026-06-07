#pragma once
// he_entity_query — the "He" entity filter/query API of the Guild simulation
// (gilde.exe, VIBE_He_* family). These are the leaf query functions that many
// modules (AiAction neighbor search, the evidence/"Beweis" dialogs, the office /
// election / abduction panels, the rival/apology dialogs) call to enumerate the
// per-player "He entity" records — the 45-byte-stride table at dword_11BC760 (the
// same table command_apply4 calls the Straftat/crime table, g_straftatTable) —
// keyed through the id-pair association table (dword_11C2160/dword_11C2164, the
// g_idPairA/g_idPairB columns owned by command_apply) and the Person array
// (word_12CE910, g_persons). This file recovers them 1:1.
//
// Two backing tables, both pre-existing globals (REUSED, not redefined here):
//   * 45-byte entity record table: g_straftatTable (dword_11BC760, 256*45 bytes).
//       record+0  (dword_11BC760) : type/key   — matched against a pair value.
//       record+22 (dword_11BC776) : owner id   — compared to the player id.
//       record+37 (dword_11BC785) : state      — "alive"/active (==1 selects).
//   * id-pair table: g_idPairA/g_idPairB (dword_11C2160/64, 2048 pairs).
//       pair.A (key, +0) is matched against the Person's id (record+4);
//       pair.B (value, +4) is the entity type/key looked up in the entity table.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   0x4c3ad4 VIBE_He_RequestRivalEntityPairs    — queue op35 for the player's rivals
//   0x4c3c48 VIBE_He_MatchRivalEntityPairs       — match rival pairs from a buffer
//   0x4c3de4 VIBE_He_CollectPlayerEntitiesByType — collect + rank player entity ids
//   0x4c3ffc VIBE_He_CollectPlayerEntityHandlers — collect + rank into 3 columns
//   0x4c42c0 VIBE_He_FindMatchingEntityIndices   — list matching record indices
//   0x4c4388 VIBE_He_FindMatchingEntityIds       — list matching record keys
//   0x4c4458 VIBE_He_CountMatchingEntities       — count matching records
//   0x4c44ec VIBE_He_ResetEntityTables           — clear both backing tables
//   0x4c4548 VIBE_He_SortEntitiesByRank          — insertion sort by +28 rank byte
//
// Cross-cluster leaves (Person_FindRecordById / Command_QueueRequestPair35 /
// History_NotifyRivalEvent) are routed through HeEntityQueryHooks so the query
// control flow is exercisable in isolation; the shipping build wires the real
// cluster bridge.
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Backing-table geometry (recovered constants). The tables themselves are the
// pre-existing globals g_straftatTable / g_idPairA / g_idPairB (see .cpp).
// ===========================================================================
constexpr int kHeEntityStride = 45;     // dword_11BC760 record stride
constexpr int kHeEntitySlots  = 512;    // 23040 / 45 (scan bound is 23040 bytes)
constexpr int kHeEntityBytes  = 23040;  // kHeEntityStride * kHeEntitySlots
constexpr int kHePairSlots    = 2048;   // dword_11C2160/64 pair capacity
constexpr int kHePairScanBound = 4096;  // v8 dword-index bound (2 per slot)

// Column byte offsets within a 45-byte entity record (base = record+0).
constexpr int kHeEntKey   = 0;   // dword_11BC760 — type/key
constexpr int kHeEntOwner = 22;  // dword_11BC776 — owner id
constexpr int kHeEntState = 37;  // dword_11BC785 — state (==1 active)
// SortEntitiesByRank ranks on the byte at +28.
constexpr int kHeEntRank  = 28;

// ===========================================================================
// Leaf hooks — side effects / lookups that cross into clusters this module does
// not own. Passing nullptr installs an inert default (find returns nullptr, the
// two notifiers do nothing). Person_FindRecordById has a real default that scans
// the g_persons array; override only to observe the lookups.
// ===========================================================================
struct HeEntityQueryHooks {
    // VIBE_Person_FindRecordById(id) — resolve a person id to its record base
    // (g_persons word_12CE910). The default chains to the real entity.cpp scan.
    void* (*personFind)(i32 id);
    // VIBE_Command_QueueRequestPair35(value, 1) — queue an op35 entity-pair
    // request for the matched entity key. Inert default does nothing.
    void (*queueRequestPair35)(i32 value);
    // VIBE_History_NotifyRivalEvent(slotByteOffset, personRecord) — log a rival
    // event for the person whose buffer slot matched. Inert default does nothing.
    void (*notifyRivalEvent)(int slotByteOffset, void* personRecord);
};

void SetHeEntityQueryHooks(const HeEntityQueryHooks* hooks);
const HeEntityQueryHooks& GetHeEntityQueryHooks();

// ===========================================================================
// Query functions.
// ===========================================================================

// gilde.exe 0x4c44ec — VIBE_He_ResetEntityTables().
//   Clears every 45-byte entity record (key/owner/state-ish columns -> -1, and
//   the +24 dword -> 0) and resets the id-pair table to all -1, and zeroes the
//   crime/st_id counter (dword_632240 = g_straftatCounter). Returns 16384.
int He_ResetEntityTables();

// gilde.exe 0x4c42c0 — VIBE_He_FindMatchingEntityIndices(personId@eax,
//   outBuf@ecx, personIdx@bx). Resolves `personId`; if absent returns 0. Walks
//   the id-pair table; for each pair whose key == the Person(personIdx)'s id
//   (record+4), finds the entity record whose +0 key == the pair value, and if
//   that record's owner (+22) == personId, appends the record's table index to
//   outBuf (32-dword buffer, pre-filled with -1). Returns the match count (early
//   out when it exceeds 32).
int He_FindMatchingEntityIndices(i32 personId, i32* outBuf, u16 personIdx);

// gilde.exe 0x4c4388 — VIBE_He_FindMatchingEntityIds(personId@eax, outBuf@ecx,
//   personIdx@bx). As FindMatchingEntityIndices but appends the matching
//   record's +0 KEY value (not its index). Returns the match count.
int He_FindMatchingEntityIds(i32 personId, i32* outBuf, u16 personIdx);

// gilde.exe 0x4c4458 — VIBE_He_CountMatchingEntities(personId@eax,
//   personIdx@dx). Counts entity records whose pair key matches the Person's id
//   and whose owner (+22) == personId. Returns the count.
int He_CountMatchingEntities(i32 personId, u16 personIdx);

// gilde.exe 0x4c3de4 — VIBE_He_CollectPlayerEntitiesByType(outIds@eax,
//   outCounts@edx, personIdx@bx). Collects the OWNER id (+22) of every active
//   (state(+37)==1) entity record whose pair key matches the Person(personIdx)'s
//   id, then dedups equal owners counting their multiplicity, writing the top-32
//   owners into outIds[0..31] (descending by count) and the counts into
//   outCounts[0..31]; outIds[31]/outCounts[31] track the running max. Returns the
//   number of distinct owners emitted (capped at 32).
int He_CollectPlayerEntitiesByType(i32* outIds, i32* outCounts, u16 personIdx);

// gilde.exe 0x4c3ffc — VIBE_He_CollectPlayerEntityHandlers(outA@eax, outB@edx,
//   outC@ebx, personIdx@cx). Like CollectPlayerEntitiesByType but collects record
//   POINTERS (state may be 0 or 1), dedups by owner column (+22), and emits three
//   parallel 32-dword columns ranked by count: outB[31]=active-count,
//   outC[31]=total-count, outA[31]=owner-id. Returns distinct count (capped 32).
int He_CollectPlayerEntityHandlers(i32* outA, i32* outB, i32* outC, u16 personIdx);

// gilde.exe 0x4c4548 — VIBE_He_SortEntitiesByRank(count@eax, buf@edx).
//   Selection-style sort of `count` 45-byte records in `buf`, descending by the
//   rank byte at +28, swapping whole records (44 + the trailing byte). Returns
//   the address one past the last compared record (the original's eax).
u8* He_SortEntitiesByRank(int count, u8* buf);

// gilde.exe 0x4c3ad4 — VIBE_He_RequestRivalEntityPairs(personId@eax, limit@edx).
//   Gathers up to 8 of the Person's rivals (kind byte +2 == 6 or 7, distinct id),
//   then for each owned-and-active entity record (owner==personId, state==1) up
//   to `limit` records, queues an op35 request for its key and flags any rival
//   whose pair appears. Finally notifies each flagged rival. Returns the number
//   of records requested.
int He_RequestRivalEntityPairs(i32 personId, int limit);

// gilde.exe 0x4c3c48 — VIBE_He_MatchRivalEntityPairs(personId@eax, count@edx,
//   keys@ebx). As RequestRivalEntityPairs but the candidate entity keys come from
//   the caller-supplied `keys[0..count-1]` (resolved through the entity table)
//   instead of scanning all owned records. Returns the number requested.
int He_MatchRivalEntityPairs(i32 personId, int count, const i32* keys);

} // namespace guild::sim
