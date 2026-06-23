#include "sim/he_entity_query.h"

#include "sim/entity.h"          // g_persons, PersonFindRecordById (word_12CE910)
#include "sim/command_apply.h"   // g_idPairA / g_idPairB (dword_11C2160/64)
#include "sim/command_apply4.h"  // g_straftatTable / g_straftatCounter (dword_11BC760)

#include <cstring>  // std::memcpy
#include <cstdint>  // std::uintptr_t

// he_entity_query.cpp — implementation of the "He" entity filter/query API. The
// three backing globals are REUSED from their owning modules (ODR):
//   * g_straftatTable / g_straftatCounter  (command_apply4.cpp) — the 45-byte
//     entity record table dword_11BC760 / the st_id counter dword_632240.
//   * g_idPairA / g_idPairB                (command_apply.cpp)  — the id-pair
//     association columns dword_11C2160 / dword_11C2164.
//   * g_persons / PersonFindRecordById     (entity.cpp)         — the Person
//     array word_12CE910 / the id->record scan @0x58bc6c.
// Every function below addresses these by the exact original byte offsets so the
// arithmetic stays bit-faithful to the Hex-Rays pseudocode.

namespace guild::sim {

// The reused g_straftatTable global must cover the full 23040-byte scan bound the
// query loops use (512 records); command_apply4.h is sized to match.
static_assert(static_cast<int>(sizeof(g_straftatTable)) >= kHeEntityBytes,
              "g_straftatTable must be >= 23040 bytes for the He entity-query scan");
static_assert(kHePairSlots * 2 >= kHePairScanBound,
              "g_idPairA/B must cover the pair-table dword-index scan");

// ===========================================================================
// Leaf hooks.
// ===========================================================================
namespace {
// Default Person_FindRecordById: chain to the real entity.cpp scan (word_12CE910).
void* DefaultPersonFind(i32 id) { return PersonFindRecordById(id); }

const HeEntityQueryHooks kInert = {
    DefaultPersonFind,  // personFind
    nullptr,            // queueRequestPair35
    nullptr,            // notifyRivalEvent
};
const HeEntityQueryHooks* g_hooks = &kInert;

inline void* PersonFind(i32 id) {
    return g_hooks->personFind ? g_hooks->personFind(id) : DefaultPersonFind(id);
}
inline void QueuePair35(i32 value) {
    if (g_hooks->queueRequestPair35) g_hooks->queueRequestPair35(value);
}
inline void NotifyRival(int slotByteOffset, void* personRecord) {
    if (g_hooks->notifyRivalEvent) g_hooks->notifyRivalEvent(slotByteOffset, personRecord);
}

// --- raw column accessors on the 45-byte entity table (g_straftatTable) ----
// `off` is the byte offset of a record (0, 45, 90, ...). Reads the named column.
inline i32 EntKey(int off)   { return *reinterpret_cast<const i32*>(g_straftatTable + off + kHeEntKey); }
inline i32 EntOwner(int off) { return *reinterpret_cast<const i32*>(g_straftatTable + off + kHeEntOwner); }
inline i32 EntState(int off) { return *reinterpret_cast<const i32*>(g_straftatTable + off + kHeEntState); }

// id-pair columns at dword-index `i` (0,2,4,...): g_idPairA/B[i/2].
inline i32 PairKey(int i)   { return g_idPairA[i / 2]; }
inline i32 PairValue(int i) { return g_idPairB[i / 2]; }

// Person record id at record+4 (word_12CE910[268*idx] -> *((_DWORD*)rec + 1)).
inline i32 PersonId(const void* rec) { return *(reinterpret_cast<const i32*>(rec) + 1); }
} // namespace

void SetHeEntityQueryHooks(const HeEntityQueryHooks* hooks) {
    g_hooks = hooks ? hooks : &kInert;
}
const HeEntityQueryHooks& GetHeEntityQueryHooks() { return *g_hooks; }

// ===========================================================================
// gilde.exe 0x4c44ec — VIBE_He_ResetEntityTables().
//   Clears the 45-byte entity table (key/+18/owner -> -1, state -> 0) and the
//   id-pair table (both columns -> -1), zeroes the st_id counter; returns 16384.
// ===========================================================================
int He_ResetEntityTables() {
    g_straftatCounter = 0;  // dword_632240 = 0
    for (int v0 = kHeEntityStride; v0 != kHeEntityBytes + kHeEntityStride; v0 += kHeEntityStride) {
        // original addresses record (v0 - 45) via shifted bases dword_11BC749/745/733/758.
        int rec = v0 - kHeEntityStride;
        *reinterpret_cast<i32*>(g_straftatTable + rec + kHeEntOwner) = -1;  // +22
        *reinterpret_cast<i32*>(g_straftatTable + rec + 18)          = -1;  // +18
        *reinterpret_cast<i32*>(g_straftatTable + rec + kHeEntKey)   = -1;  // +0
        *reinterpret_cast<i32*>(g_straftatTable + rec + kHeEntState) = 0;   // +37
    }
    // id-pair table: both columns -> -1 over all 2048 slots (loop runs result to
    // 4096, returns result*4 == 16384).
    int result = 0;
    for (; result != kHePairScanBound; result += 2) {
        g_idPairA[result / 2] = -1;  // dword_11C2158/60 key
        g_idPairB[result / 2] = -1;  // dword_11C215C/64 value
    }
    return result * 4;  // 16384
}

// ===========================================================================
// Shared scan core for the three Find/Count queries. Walks the pair table for
// the Person `personIdx`'s id (the *((_DWORD*)rec+1) == pair key), resolves the
// matched entity record, and (when owner == personId) invokes `emit(recIndex,
// recByteOffset, count)`. Returns the running count or the early-out value.
// ===========================================================================

// gilde.exe 0x4c42c0 — VIBE_He_FindMatchingEntityIndices(personId, outBuf, personIdx).
int He_FindMatchingEntityIndices(i32 personId, i32* outBuf, u16 personIdx) {
    const void* rec = PersonFind(personId);
    int count = 0;
    if (!rec)
        return 0;
    const void* person = &g_persons[personIdx];  // word_12CE910[268*personIdx]
    for (int k = 0; k < 32; ++k) outBuf[k] = -1;  // pre-fill 32 dwords with -1
    i32 wanted = PersonId(person);                 // *((_DWORD*)person + 1)
    for (int v8 = 0; v8 < kHePairScanBound; v8 += 2) {
        if (PairKey(v8) == wanted) {
            i32 value = PairValue(v8);             // dword_11C2164[v8]
            int recIdx = 0;                        // v11 — table index
            int off = 0;                           // v12 — byte offset
            bool hit = (value == EntKey(0));
            if (!hit) {
                while (true) {
                    off += kHeEntityStride;
                    ++recIdx;
                    if (off >= kHeEntityBytes) break;
                    if (value == EntKey(off)) { hit = true; break; }
                }
            }
            if (hit && recIdx != -1 && personId == EntOwner(off)) {
                outBuf[count] = recIdx;            // *(v9-1) = v11
                ++count;
                if (count > 32) return count;
            }
        }
    }
    return count;
}

// gilde.exe 0x4c4388 — VIBE_He_FindMatchingEntityIds(personId, outBuf, personIdx).
int He_FindMatchingEntityIds(i32 personId, i32* outBuf, u16 personIdx) {
    const void* rec = PersonFind(personId);
    int count = 0;
    if (!rec)
        return 0;
    const void* person = &g_persons[personIdx];
    for (int k = 0; k < 32; ++k) outBuf[k] = -1;
    i32 wanted = PersonId(person);
    for (int v8 = 0; v8 < kHePairScanBound; v8 += 2) {
        if (PairKey(v8) == wanted) {
            i32 value = PairValue(v8);
            int recIdx = 0;
            int off = 0;
            bool hit = (value == EntKey(0));
            if (!hit) {
                while (true) {
                    off += kHeEntityStride;
                    ++recIdx;
                    if (off >= kHeEntityBytes) break;
                    if (value == EntKey(off)) { hit = true; break; }
                }
            }
            if (hit && recIdx != -1 && personId == EntOwner(off)) {
                outBuf[count] = EntKey(off);       // *(v9-1) = dword_11BC760[off]
                ++count;
                if (count > 32) return count;
            }
        }
    }
    return count;
}

// gilde.exe 0x4c4458 — VIBE_He_CountMatchingEntities(personId, personIdx).
int He_CountMatchingEntities(i32 personId, u16 personIdx) {
    const void* rec = PersonFind(personId);
    int count = 0;
    if (!rec)
        return 0;
    const void* person = &g_persons[personIdx];
    i32 wanted = PersonId(person);
    for (int v8 = 0; v8 < kHePairScanBound; v8 += 2) {
        if (PairKey(v8) == wanted) {
            i32 value = PairValue(v8);
            int recIdx = 0;
            int off = 0;
            bool hit = (value == EntKey(0));
            if (!hit) {
                while (true) {
                    off += kHeEntityStride;
                    ++recIdx;
                    if (off >= kHeEntityBytes) break;
                    if (value == EntKey(off)) { hit = true; break; }
                }
            }
            if (hit && recIdx != -1 && personId == EntOwner(off)) {
                if (++count > 32) return count;
            }
        }
    }
    return count;
}

// ===========================================================================
// gilde.exe 0x4c3de4 — VIBE_He_CollectPlayerEntitiesByType(outIds, outCounts,
//   personIdx). The first arg doubles as the person id (a1 == personId, and the
//   id is read from g_persons[personIdx]); the original walks the pair table by
//   the Person's id and collects state==1 entities, then dedups + rank-sorts.
//   outIds is a 32-dword array (a1 base), outCounts the parallel counts (a2).
// ===========================================================================
int He_CollectPlayerEntitiesByType(i32* outIds, i32* outCounts, u16 personIdx) {
    // v3 = a1 = outIds base; the original treats a1 both as the 32-dword output
    // array and (via *(v3+124)) the winning-key slot (dword index 31).
    i32* v3 = outIds;
    i32 scratch[513];   // v26 — 1 guard dword + 512 collected keys (1-based)
    int distinct = 0;   // v28 — number collected
    int total = 0;      // v29 — number emitted after dedup

    // Initialize the two 32-dword output arrays: outIds -> -1, outCounts -> 0,
    // and the scratch[1..512] -> -1.
    for (int k = 0; k < 32; ++k) { outIds[k] = -1; outCounts[k] = 0; }
    for (int k = 1; k <= 512; ++k) scratch[k] = -1;

    const void* person = &g_persons[personIdx];   // word_12CE910[268*personIdx]
    i32 wanted = PersonId(person);                 // *((_DWORD*)person + 1)

    // Collect: for each pair keyed to the person, find the entity record whose
    // +0 key == value and whose state (+37) == 1; append its OWNER id (+22).
    for (int v8 = 0; v8 < kHePairScanBound; v8 += 2) {
        if (PairKey(v8) == wanted) {
            i32 value = PairValue(v8);
            int recIdx = 0;
            int off = 0;
            bool hit = (value == EntKey(0));
            if (!hit) {
                while (true) {
                    off += kHeEntityStride;
                    ++recIdx;
                    if (off >= kHeEntityBytes) break;
                    if (value == EntKey(off)) { hit = true; break; }
                }
            }
            if (hit && recIdx != -1 && EntState(off) == 1) {
                ++distinct;
                scratch[distinct] = EntOwner(off);  // v26[++v6] = dword_11BC776[off]
            }
        }
    }

    // Dedup + rank: for each not-yet-consumed key, count its multiplicity over
    // the remaining scratch entries, record into the winning slot (+124) when it
    // beats the running max, then bubble the 32-entry (id,count) arrays so the
    // largest counts sink toward index 0 (one pass of an adjacent-swap network).
    for (int v31 = 0; v31 < 512; ++v31) {
        if (scratch[v31 + 1] != -1) {
            int multiplicity = 1;                   // v14
            i32 key = scratch[v31 + 1];             // v16
            scratch[v31 + 1] = -1;
            ++total;                                // v29
            for (int v18 = v31 + 1; v18 < 512; ++v18) {
                if (key == scratch[v18 + 1]) {
                    ++multiplicity;
                    scratch[v18 + 1] = -1;
                }
            }
            if (multiplicity > outCounts[31]) {     // > v30[31]
                outCounts[31] = multiplicity;
                v3[31] = key;                       // *(v3+124) = v16
            }
            // adjacent-swap network on the 32-entry arrays (index 31 down to 1).
            for (int v19 = 31; v19 > 0; --v19) {
                if (outCounts[v19] > outCounts[v19 - 1]) {
                    i32 tc = outCounts[v19];
                    outCounts[v19] = outCounts[v19 - 1];
                    outCounts[v19 - 1] = tc;
                    i32 ti = v3[v19];
                    v3[v19] = v3[v19 - 1];
                    v3[v19 - 1] = ti;
                }
            }
        }
    }
    return total <= 32 ? total : 32;
}

// ===========================================================================
// gilde.exe 0x4c3ffc — VIBE_He_CollectPlayerEntityHandlers(outA, outB, outC,
//   personIdx). Like CollectPlayerEntitiesByType but collects record POINTERS
//   (state column unrestricted), dedups by owner (+22), and emits three parallel
//   32-dword columns ranked by total count: outB[31]=active-count (state==1),
//   outC[31]=total-count, outA[31]=owner-id.
// ===========================================================================
int He_CollectPlayerEntityHandlers(i32* outA, i32* outB, i32* outC, u16 personIdx) {
    // The original memsets outA->-1 (128 bytes = 32 dwords of 0xFFFFFFFF) and
    // outB/outC->0; v29 (512 record-pointer slots) -> 0.
    for (int k = 0; k < 32; ++k) { outA[k] = -1; outB[k] = 0; outC[k] = 0; }
    const u8* recPtr[512];          // v29 — collected record bases (nullptr free)
    for (int k = 0; k < 512; ++k) recPtr[k] = nullptr;

    int totalEmitted = 0;           // v33 — return accumulator
    int collected = 0;              // v34 — number gathered
    int next = 0;                   // v7 — write cursor

    const void* person = &g_persons[personIdx];
    i32 wanted = PersonId(person);

    for (int v6 = 0; v6 < kHePairScanBound; v6 += 2) {
        if (PairKey(v6) == wanted) {
            i32 value = PairValue(v6);
            int recIdx = 0;          // v9
            int off = 0;             // v11
            bool hit = (value == EntKey(0));
            if (!hit) {
                while (true) {
                    off += kHeEntityStride;
                    ++recIdx;
                    if (off >= kHeEntityBytes) break;
                    if (value == EntKey(off)) { hit = true; break; }
                }
            }
            // collect when the record exists and its state column is nonzero
            // (the original tests *(dword_11BC785 + off) != 0).
            if (hit && recIdx != -1 && EntState(off) != 0) {
                recPtr[next++] = g_straftatTable + off;  // v29[v7++] = &rec
                ++collected;
            }
        }
    }

    int v35 = 0;
    if (collected > 0) {
        for (int idx = 0; idx < collected; ++idx, ++v35) {
            const u8* rp = recPtr[idx];          // v13 = v29[v36/4]
            if (rp) {
                int activeCount = (EntState(static_cast<int>(rp - g_straftatTable)) == 1) ? 1 : 0; // v16
                i32 owner = *reinterpret_cast<const i32*>(rp + kHeEntOwner);                        // v31
                ++totalEmitted;                  // ++v33
                int multiplicity = 1;            // v12
                i32 ownerOfThis = *reinterpret_cast<const i32*>(rp + kHeEntOwner);
                for (int j = v35 + 1; j < 512; ++j) {
                    const u8* q = recPtr[j];
                    if (q && *reinterpret_cast<const i32*>(q + kHeEntOwner) == ownerOfThis) {
                        ++multiplicity;
                        if (*reinterpret_cast<const i32*>(q + kHeEntState) == 1)
                            ++activeCount;
                        recPtr[j] = nullptr;
                    }
                }
                recPtr[idx] = nullptr;
                if (multiplicity > outB[31]) {   // > *(v39+124)
                    outB[31] = activeCount;      // *(v39+124) = v16
                    outC[31] = multiplicity;     // *(v32+124) = v12
                    outA[31] = owner;            // *(v37+124) = v31
                }
                // 3-column adjacent-swap network (index 31 down to 1), keyed on
                // outC (the total count, v32 array).
                for (int v20 = 31; v20 > 0; --v20) {
                    if (outC[v20] > outC[v20 - 1]) {
                        i32 tc = outC[v20];
                        outC[v20] = outC[v20 - 1];
                        outC[v20 - 1] = tc;
                        i32 ta = outA[v20];
                        outA[v20] = outA[v20 - 1];
                        outA[v20 - 1] = ta;
                        i32 tb = outB[v20];
                        outB[v20] = outB[v20 - 1];
                        outB[v20 - 1] = tb;
                    }
                }
            }
        }
    }
    return totalEmitted <= 32 ? totalEmitted : 32;
}

// ===========================================================================
// gilde.exe 0x4c4548 — VIBE_He_SortEntitiesByRank(count, buf).
//   Selection sort of `count` 45-byte records, descending by the +28 byte. Swaps
//   the whole 45-byte record (44 bytes via the 0x2D/0x2C qmemcpy plus the +44
//   trailing byte). Returns the address one past the last compared record.
// ===========================================================================
u8* He_SortEntitiesByRank(int count, u8* buf) {
    // The original: result=(count-1); v7=result (outer bound); buf is the record
    // base (v11). The outer record advances by 45 (i); for each, the inner record
    // sweeps from v9+1..count-1, swapping the full 45-byte record whenever the
    // outer's +28 rank byte exceeds the inner's. Returns the address one past the
    // last inner record compared (the original's eax / `result`).
    int v7 = count - 1;
    int i = 0;                       // running byte offset of the outer record
    // The original initialises result = (count - 1) (a raw integer cast to a
    // pointer). For count <= 1 the outer loop never runs and the function returns
    // that sentinel verbatim; callers treat the result as opaque. We reproduce it.
    u8* last = reinterpret_cast<u8*>(static_cast<std::uintptr_t>(static_cast<u32>(count - 1)));
    for (int v9 = 0; v9 < v7; ++v9) {
        int v4 = v9 + 1;
        if (v4 < count) {
            int v10 = i;
            u8* inner = buf + kHeEntityStride * v4;   // result = buf + 45*(v9+1)
            do {
                u8* outer = buf + v10;                // v5 = v10 + buf
                if (outer[kHeEntRank] > inner[kHeEntRank]) {
                    u8 tmp[kHeEntityStride];
                    std::memcpy(tmp, outer, kHeEntityStride);  // v6 = outer (0x2D)
                    std::memcpy(outer, inner, kHeEntityStride);// outer = inner (0x2D)
                    std::memcpy(inner, tmp, kHeEntityStride - 1);// inner = tmp[0..43] (0x2C)
                    inner[44] = tmp[44];                        // inner[44] = v6[44]
                }
                ++v4;
                inner += kHeEntityStride;
            } while (v4 < count);
            last = inner;
        }
        i += kHeEntityStride;
    }
    return last;
}

// ===========================================================================
// gilde.exe 0x4c3ad4 — VIBE_He_RequestRivalEntityPairs(personId, limit).
//   Gather up to 8 rival persons (kind byte +2 == 6 or 7, distinct id), then for
//   each owned-active entity record (owner==personId, state==1), up to `limit`
//   records, queue an op35 request for the record's key and flag any gathered
//   rival whose pair (rival.id, key) appears in the pair table. Finally notify
//   each flagged rival. Returns the number of records requested.
// ===========================================================================
namespace {
// Rival person record bases (word_12CE910[268 * slot]) and their flags.
struct RivalSet {
    const void* rec[8];   // v16[1..8] — person record bases
    int flag[8];          // v16[9..16] — set when a matching pair is found
    int count = 0;        // v22 / v27
};

// Gather rivals: scan g_persons by stride 536; kind byte +2 == 6 or 7 and a
// distinct id (!= personId) selects, up to 8.
RivalSet GatherRivals(i32 personId) {
    RivalSet rs;
    int v4 = 0;
    // scan bound 411648 == kPersonStride(536) * kPersonCapacity(768).
    for (int v5 = 0; v5 < 411648 && rs.count < 8; v5 += kPersonStride) {
        const u8* base = reinterpret_cast<const u8*>(g_persons);
        u8 kind = base[v5 + kPfKind];                       // byte_12CE912[v5]
        i32 pid = *reinterpret_cast<const i32*>(base + v5 + kPfId); // dword_12CE914[v5/4]
        if ((kind == 6 || kind == 7) && personId != pid) {
            rs.flag[v4] = 0;
            rs.rec[v4] = base + v5;                          // &word_12CE910[v5/2]
            ++v4;
            ++rs.count;
        }
    }
    return rs;
}
} // namespace

int He_RequestRivalEntityPairs(i32 personId, int limit) {
    const void* rec = PersonFind(personId);
    int requested = 0;     // v20
    if (rec) {
        RivalSet rs = GatherRivals(personId);
        // For each owned-active entity record (up to `limit`), queue op35 and flag
        // any rival whose pair (rival.id, key) is present in the pair table.
        if (requested < limit) {
            for (int v21 = 0; v21 < kHeEntityBytes && requested < limit; v21 += kHeEntityStride) {
                if (personId == EntOwner(v21) && EntState(v21) == 1) {
                    i32 key = EntKey(v21);                   // dword_11BC760[v21]
                    for (int i = 0; i < kHePairScanBound; i += 2) {
                        for (int j = 0; j < rs.count; ++j) {
                            if (PairKey(i) == PersonId(rs.rec[j]) && PairValue(i) == key)
                                rs.flag[j] = 1;
                        }
                    }
                    QueuePair35(key);                        // QueueRequestPair35(key, 1)
                    ++requested;
                }
            }
        }
        // Notify each flagged rival.
        for (int j = 0; j < rs.count; ++j) {
            if (rs.flag[j])
                NotifyRival(4 * j, const_cast<void*>(rec));  // (v14, v17 = person rec)
        }
    }
    return requested;
}

// ===========================================================================
// gilde.exe 0x4c3c48 — VIBE_He_MatchRivalEntityPairs(personId, count, keys).
//   As RequestRivalEntityPairs, but the candidate entity keys are supplied by the
//   caller (`keys[0..count-1]`, resolved through the entity table) rather than by
//   scanning all owned records. Returns the number requested.
// ===========================================================================
int He_MatchRivalEntityPairs(i32 personId, int count, const i32* keys) {
    const void* rec = PersonFind(personId);
    int requested = 0;     // v24
    if (rec) {
        RivalSet rs = GatherRivals(personId);
        if (count > 0) {
            for (int n = 0; n < count; ++n) {
                i32 wantKey = keys[n];                       // *v26
                // resolve the entity record whose +0 key == wantKey (off = v8).
                int off = 0;
                bool hit = (EntKey(0) == wantKey);
                if (!hit) {
                    while (true) {
                        off += kHeEntityStride;
                        if (off >= kHeEntityBytes) break;
                        if (EntKey(off) == wantKey) { hit = true; break; }
                    }
                }
                if (hit) {
                    i32 recKey = EntKey(off);                // *v16 (record key at off)
                    for (int i = 0; i < kHePairScanBound; i += 2) {
                        for (int j = 0; j < rs.count; ++j) {
                            if (PairKey(i) == PersonId(rs.rec[j]) && PairValue(i) == recKey)
                                rs.flag[j] = 1;
                        }
                    }
                    QueuePair35(recKey);
                    ++requested;
                }
            }
        }
        for (int j = 0; j < rs.count; ++j) {
            if (rs.flag[j])
                NotifyRival(4 * j, const_cast<void*>(rec));
        }
    }
    return requested;
}

} // namespace guild::sim
