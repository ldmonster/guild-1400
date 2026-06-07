#include "sim/command_apply10.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Byte-offset record access helpers. The originals treat every record as a flat
// byte array and read u8/i8/i16/u16/i32 at fixed offsets; we mirror that exactly.
// ===========================================================================
namespace {

inline i32  rdI32(const void* p, int off) { i32 v; std::memcpy(&v, static_cast<const u8*>(p) + off, 4); return v; }
inline u32  rdU32(const void* p, int off) { u32 v; std::memcpy(&v, static_cast<const u8*>(p) + off, 4); return v; }
inline i16  rdI16(const void* p, int off) { i16 v; std::memcpy(&v, static_cast<const u8*>(p) + off, 2); return v; }
inline u16  rdU16(const void* p, int off) { u16 v; std::memcpy(&v, static_cast<const u8*>(p) + off, 2); return v; }
inline i8   rdI8 (const void* p, int off) { return static_cast<i8>(static_cast<const u8*>(p)[off]); }
inline u8   rdU8 (const void* p, int off) { return static_cast<const u8*>(p)[off]; }
inline void wrU32(void* p, int off, u32 v) { std::memcpy(static_cast<u8*>(p) + off, &v, 4); }

// Pick the working record from a resolved triple (the shared rule).
inline void* pick(const ResolvedEntity& e) {
    if (e.immediate) return e.immediate;
    if (e.parent)    return e.parent;
    return e.object;
}

// ===========================================================================
// Cross-module hooks (inert defaults).
// ===========================================================================
int   DefResolveEntityById(i32, void*, ResolvedEntity* out) { if (out) *out = ResolvedEntity{}; return 0; }
i32   DefSpecialTarget(i32) { return 0; }
void* DefQueryFind(i32, i32, i32, i32, i32) { return nullptr; }
void* DefQueryIterNext() { return nullptr; }
void* DefPersonQueryBegin(i32, i32, i32, i32) { return nullptr; }
void* DefPersonIterNext() { return nullptr; }
void* DefPersonFindRecordById(i32) { return nullptr; }
i32   DefCountAtLocation(i32) { return 0; }
i32   DefInvFreeCapacity(void*, i32, void*, i32) { return 0; }
i32   DefInvCarryCapacity(void*, i32, i32) { return 0; }
void* DefResolveOwnerOrParentB(void*, void*) { return nullptr; }
int   DefSelectionMatch(int, int, void** out) { if (out) *out = nullptr; return 0; }
i32   DefRoomWorth(void*, i32, void*) { return 0; }
i32   DefRecordEntityId(void*) { return 0; }

const ApplyTargetHooks kDefaults = {
    &DefResolveEntityById, &DefSpecialTarget, &DefQueryFind, &DefQueryIterNext,
    &DefPersonQueryBegin, &DefPersonIterNext, &DefPersonFindRecordById,
    &DefCountAtLocation, &DefInvFreeCapacity, &DefInvCarryCapacity,
    &DefResolveOwnerOrParentB, &DefSelectionMatch, &DefRoomWorth, &DefRecordEntityId,
};

ApplyTargetHooks g_hooks = kDefaults;

} // namespace

void SetApplyTargetHooks(const ApplyTargetHooks* h) {
    if (!h) { g_hooks = kDefaults; return; }
    g_hooks = *h;
    if (!g_hooks.resolveEntityById)     g_hooks.resolveEntityById = kDefaults.resolveEntityById;
    if (!g_hooks.specialTarget)         g_hooks.specialTarget = kDefaults.specialTarget;
    if (!g_hooks.queryFind)             g_hooks.queryFind = kDefaults.queryFind;
    if (!g_hooks.queryIterNext)         g_hooks.queryIterNext = kDefaults.queryIterNext;
    if (!g_hooks.personQueryBegin)      g_hooks.personQueryBegin = kDefaults.personQueryBegin;
    if (!g_hooks.personIterNext)        g_hooks.personIterNext = kDefaults.personIterNext;
    if (!g_hooks.personFindRecordById)  g_hooks.personFindRecordById = kDefaults.personFindRecordById;
    if (!g_hooks.countAtLocation)       g_hooks.countAtLocation = kDefaults.countAtLocation;
    if (!g_hooks.invFreeCapacity)       g_hooks.invFreeCapacity = kDefaults.invFreeCapacity;
    if (!g_hooks.invCarryCapacity)      g_hooks.invCarryCapacity = kDefaults.invCarryCapacity;
    if (!g_hooks.resolveOwnerOrParentB) g_hooks.resolveOwnerOrParentB = kDefaults.resolveOwnerOrParentB;
    if (!g_hooks.selectionMatch)        g_hooks.selectionMatch = kDefaults.selectionMatch;
    if (!g_hooks.roomWorth)             g_hooks.roomWorth = kDefaults.roomWorth;
    if (!g_hooks.recordEntityId)        g_hooks.recordEntityId = kDefaults.recordEntityId;
}
const ApplyTargetHooks& GetApplyTargetHooks() { return g_hooks; }

// gilde.exe — the -2/-3/-4 -> dword_631288/8C/90 substitution (the prologue of
// every Check* target gate).
i32 SubstituteSpecialTarget(i32 id) {
    switch (id) {
        case -2: return g_hooks.specialTarget(-2); // dword_631288
        case -3: return g_hooks.specialTarget(-3); // dword_63128C
        case -4: return g_hooks.specialTarget(-4); // dword_631290
        default: return id;
    }
}

// ===========================================================================
// Check* predicates.
// ===========================================================================

// gilde.exe 0x495cf8.
int CheckSourceTargetReachable(u8* cmd, void* a2) {
    // a1[k] == *(u32*)(cmd + 4*k). "to" slot is a1[5] (offset 20),
    // "from" slot is a1[4] (offset 16).
    i32 toSlot = rdI32(cmd, 20);                 // a1[5]
    if (toSlot != -1) {
        i32 id = SubstituteSpecialTarget(toSlot); // case -2/-3/-4
        wrU32(cmd, 20, static_cast<u32>(id));     // a1[5] = id
        ResolvedEntity e;
        if (!g_hooks.resolveEntityById(id, a2, &e))
            return 1;                             // entity gone -> unreachable
        // v5 = immediate ? immediate+93 : (parent ? parent+10w : object+188w)
        i32 key;
        if (e.immediate)      key = rdI32(e.immediate, 93);
        else if (e.parent)    key = rdI32(e.parent, 20);   // (parent+10*2 words = +20 bytes; matches *(v13+10) on a __int16*)
        else                  key = rdI32(e.object, 376);  // object+188 words = +376 bytes
        // v6 = QueryFind(key, 1, 0, *(i32*)(cmd+22) >> 16)
        void* found = g_hooks.queryFind(key, 1, 0, rdI32(cmd, 22) >> 16, 0);
        // reject if !found OR *(found+7w == +14 bytes? no: found is __int16*, +7 => +28) < *(cmd+31)
        if (!found || rdI32(found, 28) < rdI32(cmd, 31))
            return 1;
    }

    i32 fromSlot = rdI32(cmd, 16);               // a1[4]
    if (fromSlot != -1) {
        i32 id = SubstituteSpecialTarget(fromSlot);
        wrU32(cmd, 16, static_cast<u32>(id));     // a1[4] = id
        ResolvedEntity e;
        if (!g_hooks.resolveEntityById(id, a2, &e))
            return 1;

        void* v9 = nullptr;
        if (e.immediate) {
            // v8 = *(u16*)(immediate+39); 0xFFFF -> null; else word_12CE910[268*v8]
            u16 sel = rdU16(e.immediate, 39);
            void* rec = nullptr;
            if (sel != 0xFFFF) g_hooks.selectionMatch(sel, -1, &rec); // -1 cls: just fetch record by index
            v9 = rec;
        } else if (e.parent) {
            i16 type = rdI16(e.parent, 0);
            if (type == 42 || type == 278 || type == 477) {
                if (g_hooks.invFreeCapacity(e.parent, rdI32(cmd, 22) >> 16, e.parent, rdI32(cmd, 31))
                        < rdI32(cmd, 31))
                    return 1;
            }
            v9 = g_hooks.resolveOwnerOrParentB(e.parent, e.parent);
        } else {
            if (g_hooks.invCarryCapacity(e.object, rdI32(cmd, 22) >> 16, rdI32(cmd, 31))
                    < rdI32(cmd, 31))
                return 1;
            v9 = e.object;
        }

        // if *(cmd+35) && v9 && CountAtLocation(*(v9+94w == +376 bytes)) < *(cmd+31) * *(cmd+35) -> reject
        if (rdI32(cmd, 35) && v9
                && g_hooks.countAtLocation(rdI32(v9, 376)) < rdI32(cmd, 31) * rdI32(cmd, 35))
            return 1;
    }
    return 0;
}

// gilde.exe 0x496080.
int CheckTargetOwnership(u8* cmd, void* a2) {
    i32 id = SubstituteSpecialTarget(rdI32(cmd, 16)); // *(cmd+16), -2/-3/-4 swap
    wrU32(cmd, 16, static_cast<u32>(id));
    int result = 1;
    ResolvedEntity e;
    if (g_hooks.resolveEntityById(id, a2, &e)) {
        void* base = pick(e);
        u8* v5 = static_cast<u8*>(base) + rdI32(cmd, 20); // base + *(cmd+20)
        // reject (return 0) only if: object && v5 == object+228w(+456 bytes) &&
        //   *(i8)(cmd+30) >= 0 && *(i8)(object+458) >= 0
        u8* objPlus = e.object ? static_cast<u8*>(e.object) + 456 : nullptr; // 228 words
        if (e.object && v5 == objPlus && rdI8(cmd, 30) >= 0 && rdI8(e.object, 458) >= 0)
            return 0;
        // else fall through and return result (1)
    }
    return result;
}

// gilde.exe 0x495c64.
int CheckTargetCooldown(u8* cmd, void* a2) {
    i32 slot = rdI32(cmd, 20);
    if (slot == -1) return 0;
    i32 id = SubstituteSpecialTarget(slot);
    wrU32(cmd, 20, static_cast<u32>(id));
    ResolvedEntity e;
    if (g_hooks.resolveEntityById(id, a2, &e)) {
        i32 key;
        if (e.immediate)   key = rdI32(e.immediate, 93);
        else if (e.parent) key = rdI32(e.parent, 20);   // parent+20 bytes
        else               key = rdI32(e.object, 376);  // object+188 words
        if (g_hooks.countAtLocation(key) - rdI32(cmd, 29) >= 0)
            return 0;
    }
    return 1;
}

// gilde.exe 0x4963ec.
int CheckTargetNotInUse(u8* cmd) {
    if (rdI32(cmd, 16) == -1) return 1;
    i32 ownerKey = rdI32(cmd, 16);            // *v1, the command's own object id
    for (int i = 0; i < 16; ++i) {
        i32 personId = rdI32(cmd, 16 + 4 + 4 * i); // *(v2+4), v2 = cmd+16, advancing +4
        void* rec = g_hooks.personFindRecordById(personId);
        if (rec) {
            u8 state = rdU8(rec, 2);
            if (state == 6 || state == 7) {
                i32 bound = rdI32(rec, 520);   // *((i32*)rec + 130) = +520 bytes
                if (bound != -1 && bound != ownerKey)
                    return 1;
            }
        }
    }
    return 0;
}

// gilde.exe 0x496174.
int CheckPersonHasOfficeTag(u8* cmd, void* a2) {
    i32 tag = rdI32(cmd, 16);                 // a1[4]
    i32 token = static_cast<i32>(reinterpret_cast<intptr_t>(a2)); // opaque caller token
    if (tag == 1668048242) {                  // 'rdpm'
        // original: VIBE_Person_QueryBegin(a2, 1, 1, a1[6]); a1[6] == *(cmd+24).
        void* begin = g_hooks.personQueryBegin(token, 1, 1, rdI32(cmd, 24));
        if (begin) {
            void* off = g_hooks.queryFind(rdI32(begin, 93), 2, 6, 0, 300);
            if (off) {
                // (qword_13CE852 - *((i32*)off + 10)) > 1 -> return 0
                // qword_13CE852 modelled via countAtLocation(-1) sentinel is wrong; the
                // original compares a global tick to off+40. We expose it through the
                // office record itself: off+40 holds the stored tick; the live tick is
                // threaded as recordEntityId(nullptr) (a generic "current tick" hook).
                i32 liveTick = g_hooks.recordEntityId(nullptr);
                if (liveTick - rdI32(off, 40) > 1)
                    return 0;
            }
        }
        return 1;
    }
    if (tag != 1651865888) return 1;          // ' adm' (the only other branch)
    void* begin = g_hooks.personQueryBegin(rdI32(cmd, 24), 1, 1, rdI32(cmd, 24));
    if (!begin) return 1;
    void* off = g_hooks.queryFind(rdI32(begin, 93), 2, 6, 0, 300);
    if (!off) return 1;
    // walk three +7w(+28 bytes) entries with stride +2w (+8 bytes); want a1[7]==+28
    i32 want = rdI32(cmd, 28);                 // a1[7]
    if (rdI32(off, 28) != want) {
        for (int k = 1; k < 3; ++k) {
            if (rdI32(off, 28 + 8 * k) == want)
                return 0;
        }
        return 1;
    }
    return 0;
}

// gilde.exe 0x49623c.
static int OfficeSlotScan(void* off, i32 want, int limit) {
    // Returns: 1 = found `want` in entries [1..limit); 0 = found a -1 free slot
    // before filling all `limit` entries; 2 = neither (all filled, none match).
    // entry k at off + 28 + 8*k (the +7 dword of a 2-word-stride record).
    for (int k = 1; k < limit; ++k) {
        if (rdI32(off, 28 + 8 * k) == want)
            return 1;
    }
    int filled = 0;
    if (rdI32(off, 28) != -1) {
        do { ++filled; } while (filled < limit && rdI32(off, 28 + 8 * filled) != -1);
    }
    return (filled < limit) ? 0 : 2;
}

int CheckOfficeSlotByTag(u8* cmd, void* a2) {
    i32 tag = rdI32(cmd, 16);
    i32 token = static_cast<i32>(reinterpret_cast<intptr_t>(a2));
    if (tag == 1785686382) {                  // 'nmab'
        void* begin = g_hooks.personQueryBegin(token, 1, 1, rdI32(cmd, 24));
        if (begin) {
            void* off = g_hooks.queryFind(rdI32(begin, 93), 2, 6, 0, 301);
            if (off) {
                i32 want = rdI32(cmd, 20);
                if (rdI32(off, 28) != want) {
                    int r = OfficeSlotScan(off, want, 4);
                    if (r == 1) return 1;
                    if (r == 0) return 0;
                }
            }
        }
        return 1;
    }
    if (tag == 1818583414) {                  // 'vmba'
        void* begin = g_hooks.personQueryBegin(token, 1, 1, rdI32(cmd, 24));
        if (begin) {
            void* off = g_hooks.queryFind(rdI32(begin, 93), 2, 6, 0, 301);
            if (off) {
                i32 want = rdI32(cmd, 20);
                int filled = 0;
                if (rdI32(off, 28) != want) {
                    do { ++filled; }
                    while (filled < 4 && rdI32(off, 28 + 8 * filled) != want);
                }
                if (filled < 4) return 0;
            }
        }
        return 1;
    }
    if (tag != 1668048242) return 1;          // 'rdpm'
    void* begin = g_hooks.personQueryBegin(token, 1, 1, rdI32(cmd, 24));
    if (!begin) return 1;
    // *(u16*)(begin+39): 0xFFFF -> use the queryFind-301 fallback. Else fetch the
    // selection record and test occupancy (+8 byte set, +2 byte != 15).
    u16 sel = rdU16(begin, 39);
    void* rec = nullptr;
    if (sel != 0xFFFF) g_hooks.selectionMatch(sel, -1, &rec);
    if (sel == 0xFFFF || rec == nullptr || rdU8(rec, 8) == 0 || rdU8(rec, 2) == 15) {
        return g_hooks.queryFind(rdI32(begin, 93), 2, 6, 0, 301) ? 0 : 1; // !QueryFind(...)
    }
    return 1;
}

// gilde.exe 0x495ef8.
int CheckParamRefsValid(u8* cmd) {
    i32 id = SubstituteSpecialTarget(rdI32(cmd, 16));
    wrU32(cmd, 16, static_cast<u32>(id));
    ResolvedEntity e;
    if (!g_hooks.resolveEntityById(id, nullptr, &e))
        return 1;
    void* base = pick(e); // immediate ? : parent ? : object

    int n = rdU8(cmd, 20);            // *(u8*)(cmd+20) field count
    int cur = 21;                     // v3 = cmd + 21
    for (int i = 0; i < n; ++i) {
        u8  width  = rdU8(cmd, cur);      // *(v5-2)
        u8  count  = rdU8(cmd, cur + 1);  // *(v5-1)
        u16 fieldOff = rdU16(cmd, cur + 2); // *v5
        void* tgt = static_cast<u8*>(base) + fieldOff; // v8 = base + *v5
        cur += 4;                          // v3 = v5 + 1 (advance past the 4-byte descriptor)

        if (e.object) {
            // v8 == object+2w(+4 bytes) -> reject
            if (tgt == static_cast<u8*>(e.object) + 4)
                return 1;
            // v8 == object+46w(+92 bytes): a person-link field; validate owner.
            if (tgt == static_cast<u8*>(e.object) + 92) {
                i32 personId = rdI32(e.object, 92) + rdI32(cmd, cur); // *((i32*)object+23) + *v3
                void* rec = g_hooks.personFindRecordById(personId);
                if (!rec) return 1;
                i32 owner = rdI32(rec, 92);       // *((i32*)rec + 23)
                if (owner != -1 && owner != rdI32(e.object, 4)) // != *((i32*)object+1)
                    return 1;
            }
        }

        // Advance the cursor past `count` inline values whose element size is set
        // by `width` (1 -> 1 byte, 2 -> 2 bytes, 4 -> 4 bytes, else none).
        if (width == 1)      cur += count;
        else if (width == 2) cur += 2 * count;
        else if (width == 4) cur += 4 * count;
        // width 0 / 3 / other: no inline payload.
    }
    return 0;
}

// gilde.exe 0x493a34.
i32 CheckSyncRangeAcked(u32 start, u32 end, const u8* ackTable) {
    if (start == end) return 1;
    i32 result = 1;
    if (start >= end) return result;          // unsigned compare, as in the binary
    u32 seq = start;
    while (true) {
        u8 status = ackTable[10 * (seq & 0x7FFF)];
        if (status == 0) break;               // still pending
        if (status == 2) result = -1;         // a NAK seen
        if (++seq >= end) return result;
    }
    return 0;
}

// ===========================================================================
// Console-driven person resolvers.
// ===========================================================================

// Shared body for ResolveTargetGuard (cls=15) / ResolveTargetOfficial. The
// `clsA`/`clsB` are the profession-tag bytes the scan accepts (Guard: 15,15;
// Official: 26,21). On mode 1 we re-validate the bound id; otherwise we scan the
// 768-slot selection table for the first matching person.
static int ResolveByClass(int mode, i32* slotTable, int slotIdx,
                          int clsA, int clsB, u16* outRendered) {
    void* rec = nullptr;
    if (mode == 1) {
        rec = g_hooks.personFindRecordById(slotTable ? slotTable[2 * slotIdx + 1] : 0);
        if (!rec) return 0;
        if (rdU8(rec, 8) == 0) return 0;
        u8 c = rdU8(rec, 358);
        if (c != clsA && c != clsB) return 0;
    } else {
        for (int slot = 0; slot < 768; ++slot) {
            void* m = nullptr;
            // selectionMatch reports a record when slot is an active person whose
            // profession byte is clsA (or clsB). We try clsA then clsB.
            if (g_hooks.selectionMatch(slot, clsA, &m) ||
                (clsA != clsB && g_hooks.selectionMatch(slot, clsB, &m))) {
                rec = m;
                break;
            }
        }
        if (!rec) return 0;
        if (mode == 0 && slotTable)
            slotTable[2 * slotIdx + 1] = g_hooks.recordEntityId(rec); // *(rec+4)
    }
    if (outRendered) *outRendered = rdU16(rec, 0); // word[0] -> RenderFormattedMessage arg
    return 1;
}

int ResolveTargetGuard(int mode, i32* slotTable, int slotIdx, u16* outRendered) {
    return ResolveByClass(mode, slotTable, slotIdx, 15, 15, outRendered);
}

int ResolveTargetOfficial(int mode, i32* slotTable, int slotIdx, u16* outRendered) {
    return ResolveByClass(mode, slotTable, slotIdx, 26, 21, outRendered);
}

// gilde.exe 0x4fa178.
int ResolveTargetBestThief(int mode, i32* slotTable, int slotIdx, void** outRecord) {
    if (mode == 2) return 0;
    void* chosen = nullptr;
    if (mode == 1) {
        i32 id = slotTable ? slotTable[slotIdx] : 0; // *(a2 + 4*a4 + ...) bound id
        ResolvedEntity e;
        g_hooks.resolveEntityById(id, nullptr, &e);
        chosen = e.immediate;
        if (!chosen) return 0;
    } else {
        i32 best = -1;
        for (void* p = g_hooks.personQueryBegin(0, 1, 5, 22); p; p = g_hooks.personIterNext()) {
            // v11 = ComputeRoomWorth(p, *(i32*)(p+89) >> 24, table)
            i32 worth = g_hooks.roomWorth(p, rdI32(p, 89) >> 24, nullptr);
            if (worth > best) { chosen = p; best = worth; }
        }
        if (!chosen) return 0;
        if (mode == 0 && slotTable)
            slotTable[slotIdx] = rdI32(chosen, 4); // *(chosen+1 dword)
    }
    if (outRecord) *outRecord = chosen;
    return 1;
}

} // namespace guild::sim
