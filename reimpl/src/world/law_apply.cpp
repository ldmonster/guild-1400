// Office holder-table collection / eligibility rules — see law_apply.h.
// Faithful 1:1 port of the VIBE_Office_* collection leaves from gilde.exe.
#include "world/law_apply.h"

#include <cstring>

namespace guild::world {

// ---------------------------------------------------------------------------
// Person resolver hook
// ---------------------------------------------------------------------------
namespace {
OfficePersonRecord NullResolver(i32, void*) { return OfficePersonRecord{}; }
OfficePersonResolver g_resolver = &NullResolver;
void*                g_resolverCtx = nullptr;

inline OfficePersonRecord Resolve(i32 id) { return g_resolver(id, g_resolverCtx); }

// Raw byte view over the 37-entry holder table (gilde.exe byte_B59848). The
// originals index parallel field globals (byte_B59850 == +8, dword_B5984C == +4,
// byte_B59858 == +16, dword_B5985C == +20) into this same buffer; we reproduce
// the exact byte/dword arithmetic against this view.
inline u8* HolderBytes() { return reinterpret_cast<u8*>(g_officeHolders); }
inline const u8* HolderBytesC() { return reinterpret_cast<const u8*>(g_officeHolders); }

// dword_B5984C[i] == *(i32*)(byte_B59848 + 4 + 4*i)
inline i32 DwordB5984C(int i) {
    i32 v;
    std::memcpy(&v, HolderBytesC() + 4 + 4 * i, 4);
    return v;
}
} // namespace

void OfficeSetPersonResolver(OfficePersonResolver resolver, void* ctx) {
    g_resolver = resolver ? resolver : &NullResolver;
    g_resolverCtx = ctx;
}

// NOTE: VIBE_Office_CheckPrerequisitesMet (0x47e7d8) is already translated in
// world/office_assign.cpp (decomposed signature). Not re-defined here to honor
// the one-definition rule; see that file.

// gilde.exe 0x47f03c — VIBE_Office_CollectByCategory  (al=reqCode, edx=max, ebx=out).
int OfficeCollectByCategory(u8 reqCode, int maxCount, OfficeHolder* out) {
    const u8* tbl = HolderBytesC();
    int count = 0;
    if (maxCount > 0) {
        int v5 = 696;          // byte offset into +8 field array
        int v6 = 0;            // output byte cursor
        do {
            // byte_B59850[v5] == holder byte at +8 + v5 == type of slot v5/24
            u8 type = tbl[v5 + 8];
            if (OfficeDefReqCode(type) == reqCode) {
                std::memcpy(reinterpret_cast<u8*>(out) + v6, tbl + v5, 24);
                ++count;
                v6 += 24;
            }
            v5 -= 24;
        } while (v5 >= 0 && v6 < 24 * maxCount);
    }
    return count;
}

// gilde.exe 0x47f0b4 — VIBE_Office_CollectByCategoryResolved (al=reqCode, edx=max, ebx=out).
int OfficeCollectByCategoryResolved(u8 reqCode, int maxCount, OfficeHolder* out) {
    if (reqCode == 0)
        return 0;
    const u8* tbl = HolderBytesC();
    int count = 0;
    if (maxCount > 0) {
        int v6 = 174;              // index into the +8 (x4) / +4 (x1) field arrays
        int v7 = 0;                // output byte cursor
        const int limit = 24 * maxCount;
        do {
            // byte_B59850[v6*4] == holder +8 of slot v6/6; dword_B5984C[v6] == +4 id.
            u8 type = tbl[v6 * 4 + 8];
            if (OfficeDefReqCode(type) == reqCode && Resolve(DwordB5984C(v6)).present) {
                std::memcpy(reinterpret_cast<u8*>(out) + v7, tbl + v6 * 4, 24);
                ++count;
                v7 += 24;
            }
            v6 -= 6;
        } while (v6 >= 0 && v7 < limit);
    }
    return count;
}

// gilde.exe 0x47f150 — VIBE_Office_CollectElectiveOffices (eax=allowVacant, edx=max, ebx=out).
int OfficeCollectElectiveOffices(int allowVacant, int maxCount, OfficeHolder* out) {
    const u8* tbl = HolderBytesC();
    int count = 0;
    if (maxCount > 0) {
        int v5 = 180;              // index into +8 (x4) / +4 (x1) field arrays
        int v6 = 0;                // output byte cursor
        const int limit = 24 * maxCount;
        do {
            // HIBYTE(dword_62EC8E[3*byte_B59850[v5*4]]) == reqCode of slot v5/6.
            u8 type = tbl[v5 * 4 + 8];
            if (OfficeDefReqCode(type) == 7 && (allowVacant || DwordB5984C(v5) != -1)) {
                std::memcpy(reinterpret_cast<u8*>(out) + v6, tbl + v5 * 4, 24);
                ++count;
                v6 += 24;
            }
            v5 += 6;
        } while (v5 < 222 && v6 < limit);
    }
    return count;
}

// gilde.exe 0x47fe8c — VIBE_Office_CollectHoldersByCategory (eax=bookCat, edx=max, ebx=out).
int OfficeCollectHoldersByCategory(u8 bookCat, int maxCount, u8* out) {
    if (maxCount < 1)
        return 0;
    const u8* tbl = HolderBytesC();
    int count = 0;
    if (maxCount > 0) {
        int v5 = 0;                // byte offset into +8 field array
        do {
            u8 type = tbl[v5 + 8];           // byte_B59850[v5]
            u8 cat  = (type >= 0x25) ? OfficeDefBookCat(0) : OfficeDefBookCat(type);
            if (cat == bookCat) {
                out[count++] = type;
            }
            v5 += 24;
        } while (v5 < 672 && count < maxCount);
    }
    return count;
}

// gilde.exe 0x47f66c — VIBE_Office_LookupHolderCharacter (al=type).
bool OfficeLookupHolderCharacter(u8 type, OfficePersonRecord* out) {
    const u8* tbl = HolderBytesC();
    int v2 = 0;                    // index into +8 (x4) / +4 (x1) field arrays
    while (tbl[v2 * 4 + 8] != type) {  // byte_B59850[v2*4]
        v2 += 6;
        if (v2 >= 222)
            return false;
    }
    OfficePersonRecord rec = Resolve(DwordB5984C(v2)); // dword_B5984C[v2]
    if (out)
        *out = rec;
    return rec.present;
}

// gilde.exe 0x47fac0 — VIBE_Office_FindHighestVacantRank (al=startRank).
u8 OfficeFindHighestVacantRank(u8 startRank) {
    if (startRank == 0)
        return 0;
    u8 rank = startRank;
    if (rank >= 0x1B)
        rank = 27;
    OfficeHolder entry{};
    do {
        if (OfficeGetEntryByHolder(rank, &entry) && entry.state == 3 && entry.rank <= 1)
            break;
        --rank;
    } while (rank != 0);
    return rank;
}

// gilde.exe 0x47fa8c — VIBE_Office_CopyEntriesByIndex (eax=count, edx=buf).
int OfficeCopyEntriesByIndex(int count, OfficeHolder* buf) {
    const u8* tbl = HolderBytesC();
    if (count > 0) {
        int i = 0;
        do {
            u8* dst = reinterpret_cast<u8*>(buf + i);
            u8 srcIdx = dst[0];   // first byte of the record == holder index
            std::memcpy(dst, tbl + 24 * srcIdx, 24);
            ++i;
        } while (i < count);
    }
    return count;
}

// gilde.exe 0x47f79c — VIBE_Office_HasAvailableSuccessor (al=rank).
bool OfficeHasAvailableSuccessor(u8 rank) {
    if (rank >= 0x25)
        return false;
    // reqCode of `rank` == SBYTE1 of dword_62EC8E[3*rank] == byte 12*rank+3.
    u8 reqCode = OfficeDefReqCode(rank);
    OfficeHolder pool[6];
    int n = OfficeCollectByCategory(reqCode, 6, pool);
    if (n <= 0)
        return false;
    const u8* poolBytes = reinterpret_cast<const u8*>(pool);
    int v4 = 0;
    int v5 = 24 * n;
    while (true) {
        i32 id;
        std::memcpy(&id, poolBytes + v4 + 4, 4); // *(_DWORD*)&v8[v4+4] == holder +4 id
        OfficePersonRecord rec = Resolve(id);
        if (rec.present) {
            OfficePerson p{};
            p.officeType = rec.office358; // *(person+358)
            p.rank = 0;
            p.candidacy = 0;
            p.ownerId = id;
            p.valid = true;
            if (OfficeIsNextRankInCategory(p, rank) && rec.busy433 == 0) // !*(v7+433)
                return true;
        }
        v4 += 24;
        if (v4 >= v5)
            return false;
    }
}

// gilde.exe 0x47f928 — VIBE_Office_CollectCategoryRankList (eax=person+358, edx=max, ebx=out).
int OfficeCollectCategoryRankList(u8 office358, int maxCount, u8* out) {
    if (office358 == 0 || office358 >= 0x25)
        return 0;
    // reqCode/bookCat of the held office (record at 12*office358).
    u8 reqCode  = OfficeDefReqCode(office358);  // SBYTE1(v16)
    u8 bookCat0 = OfficeDefBookCat(office358);   // BYTE2(v16)
    OfficeHolder pool[6];
    int n = OfficeCollectByCategory(reqCode, 6, pool);
    if (n <= 0)
        return n;

    // v21: target book residue. BYTE2(v16) % 3 == 1 -> 0, else BYTE2(v16) - 1.
    int v21 = (bookCat0 % 3 == 1) ? 0 : (bookCat0 - 1);

    const u8* poolBytes = reinterpret_cast<const u8*>(pool);
    int count = 0;          // v7
    int v8 = 0;             // byte cursor into pool
    int v19 = 24 * n;
    while (count < maxCount) {
        if (poolBytes[v8 + 16] == 3) {          // state(+16) == 3
            u8 type = poolBytes[v8 + 8];        // holder type (+8)
            u8 cat = (type < 0x25) ? OfficeDefBookCat(type) : OfficeDefBookCat(0);
            i32 secondLike = (type < 0x25) ? OfficeDefFlag(type) : OfficeDefFlag(0); // v17 (dword[1])
            if (cat % 3) {
                if (cat == v21) {
                    out[count++] = type;
                    if (secondLike) // v14 == v17 -> early terminal return
                        return count;
                }
            } else {
                out[count++] = type;
            }
        }
        v8 += 24;
        if (v8 >= v19)
            return count;
    }
    return count;
}

// gilde.exe 0x47e070 — VIBE_Office_GetSecondaryHolderEntry (eax=person, edx=defOut, ebx=holderOut).
// `person` is a person-record view: marker (u16 @+0), ownerId (@+4) and the
// +361 "has-secondary" flag. Scans the holder slots from index 30 (the fast path
// dword_B59B1C == g_officeHolders[30].city) up to 36 for the slot whose +4 id ==
// person.ownerId; on a hit copies that holder's 24-byte record into *holderOut
// and the 3-dword def record (record bytes +2..+13 of the slot's type) into
// defOut[0..2], returning 1. Returns 0 if person is null/invalid or no match.
int OfficeGetSecondaryHolderEntry(const OfficeSecondaryPerson& person,
                                  OfficeDef* defOut, OfficeHolder* holderOut) {
    if (!person.present || person.marker == 0xFFFF || person.has361 == 0)
        return 0;

    const u8* tbl = HolderBytesC();
    i32 ownerId = person.ownerId;            // *(a1+4)
    int v8 = 180;                            // holder index*6 (180 -> slot 30)

    auto emit = [&](int idx6) {
        int slot = idx6 / 6;
        std::memcpy(holderOut, tbl + 24 * slot, 24);
        u8 type = tbl[idx6 * 4 + 8];         // byte_B59850[v8*4]
        // 3 dwords starting at &dword_62EC8E[3*type]+2 (record bytes +2..+13).
        OfficeDef def{};
        OfficeGetDefinition(type, &def);     // GetDefinition reads the +2-skewed record
        *defOut = def;
    };

    if (ownerId == DwordB5984C(v8)) {        // dword_B59B1C == holder[30].city
        emit(v8);
        return 1;
    }
    while (true) {
        v8 += 6;
        if (v8 >= 222)
            return 0;
        if (ownerId == DwordB5984C(v8)) {    // dword_B5984C[v8]
            emit(v8);
            return 1;
        }
    }
}

} // namespace guild::world
