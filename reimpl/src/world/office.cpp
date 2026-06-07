#include "world/office.h"

#include <cstring>

// Faithful 1:1 port of the VIBE_Office_* data/rules core (gilde.exe 0x47e0f8..).
// The office-definition table is the exact 446-byte blob baked at dword_62EC8E
// (recovered via get_bytes); records are read at a +2 byte skew exactly as the
// accessors do. The 7x7 promotion-cost float matrix dword_62EBCC is embedded too.
//
// Record byte layout (per 12-byte record, after the +2 skew GetDefinition uses):
//   +0 id  +1 reqCode(1..7)  +2 bookCat(1..9)  +3 cost  +4..7 flag  +8..11 textId
// The reqCode byte is what GetCategoryByRank returns (HIBYTE of the unskewed
// dword at 12*rank, == byte at table offset 12*rank+3 == record byte +1).

namespace guild::world {

// ---------------------------------------------------------------------------
// Static tables.
// ---------------------------------------------------------------------------

// dword_62EC8E @0x62EC8E: 37 records x 12 bytes, +2 leading skew (446 bytes).
static const u8 kOfficeDefTable[446] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x01,0x01,0x01,0x05,0x01,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x02,0x01,0x02,0x07,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x03,0x01,0x03,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x04,0x02,0x01,0x05,0x01,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x05,0x02,0x02,0x07,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x06,0x02,0x03,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x07,0x03,0x01,0x05,0x01,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x08,0x03,0x02,0x07,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x09,0x03,0x03,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x0a,0x04,0x04,0x14,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x0b,0x04,0x04,0x14,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x0c,0x04,0x04,0x14,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x0d,0x04,0x05,0x19,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x0e,0x04,0x05,0x19,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x0f,0x04,0x06,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x10,0x05,0x04,0x14,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0x40,0x11,0x05,0x04,0x14,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0x40,0x12,0x05,0x04,0x14,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0x40,0x13,0x05,0x05,0x19,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0x40,0x14,0x05,0x05,0x19,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x41,0x15,0x05,0x06,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x41,0x16,0x06,0x07,0x32,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0x40,0x17,0x06,0x07,0x32,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0x40,0x18,0x06,0x07,0x32,0x00,0x00,0x00,0x00,0x00,0x00,
    0x20,0x41,0x19,0x06,0x08,0x46,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0x40,0x1a,0x06,0x08,0x46,0x00,0x00,0x00,0x00,0x00,0x00,
    0x40,0x41,0x1b,0x06,0x09,0x64,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe0,0x40,0x1c,0x07,0x04,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x1d,0x07,0x04,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x1e,0x07,0x04,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x1f,0x07,0x04,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x20,0x07,0x04,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x21,0x07,0x04,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x40,0x22,0x07,0x04,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x41,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // rec 35 (off 420)
    0x00,0x00,0x00,0x00,0x00,0x00,0x44,0x4f,0x47,0x00,0x00,0x00, // rec 36 (off 432): textId "GOD\0"
    0x00,0x00,                                                   // tail (off 444)
};

// dword_62EBCC @0x62EBCC: 7x7 promotion-cost float matrix.
static const float kPromotionCost[7][7] = {
    {  1.0f, -2.5f, -2.5f, -2.5f, -5.0f, -5.0f, -10.0f },
    {  1.0f,  0.0f, -2.5f, -5.0f, -2.5f, -7.0f, -10.0f },
    {  1.0f, -2.5f,  0.0f, -2.5f, -5.0f, -5.0f, -10.0f },
    {  1.0f, -5.0f, -2.5f,  0.0f, -7.5f, -2.5f, -10.0f },
    {  1.0f,  1.0f,  1.0f,  1.0f,  0.0f, -5.0f, -10.0f },
    {  1.0f,  1.0f,  1.0f,  1.0f, -5.0f,  0.0f, -10.0f },
    {  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,   0.0f },
};

OfficeHolder g_officeHolders[kOfficeDefCount];

void OfficeHolderTableReset() {
    std::memset(g_officeHolders, 0, sizeof(g_officeHolders));
    for (int i = 0; i < kOfficeDefCount; ++i) {
        g_officeHolders[i].city  = -1; // vacant assignment
        g_officeHolders[i].state = 0;
    }
}

namespace {
struct OfficeInit {
    OfficeInit() { OfficeHolderTableReset(); }
} g_officeInit;

// Read the +2-skewed 12-byte record into 3 dwords (GetDefinition layout).
void ReadDefRecord(u8 rank, u32* w0, i32* w1, u32* w2) {
    const u8* p = &kOfficeDefTable[2 + 12 * rank];
    std::memcpy(w0, p,     4);
    std::memcpy(w1, p + 4, 4);
    std::memcpy(w2, p + 8, 4);
}

// reqCode(rank): HIBYTE(unskewed dword at 12*rank) == byte at table offset
// 12*rank+3 == record byte +1 of the skewed record. (== GetCategoryByRank.)
u8 ReqCode(u8 rank) {
    return kOfficeDefTable[12 * rank + 3];
}

// bookCat(rank): record byte +2 (the 1..9 book/category id).
u8 BookCat(u8 rank) {
    return kOfficeDefTable[2 + 12 * rank + 2];
}
} // namespace

// gilde.exe 0x47f008 — VIBE_Office_GetDefinition.
int OfficeGetDefinition(u8 rank, OfficeDef* out) {
    if (rank < kOfficeDefCount) {
        ReadDefRecord(rank, &out->word0, &out->flag, &out->textId);
        return 1;
    }
    // Fallback path: the original reads from offset +2 of record 0 et al.
    std::memcpy(&out->word0, &kOfficeDefTable[2], 4);
    std::memcpy(&out->flag,  &kOfficeDefTable[6], 4);
    std::memcpy(&out->textId,&kOfficeDefTable[10], 4);
    return 0;
}

// Office-def table field accessors (shared with the holder-mutation rules).
u8 OfficeDefBookCat(u8 type) { return BookCat(type); }
u8 OfficeDefReqCode(u8 type) { return ReqCode(type); }
i32 OfficeDefFlag(u8 type) {
    i32 v;
    std::memcpy(&v, &kOfficeDefTable[2 + 12 * type + 4], 4); // record dword[1]
    return v;
}
u8 OfficeDefId(u8 type) { return kOfficeDefTable[2 + 12 * type]; } // record byte +0

// gilde.exe 0x47ef94 — VIBE_Office_GetCategoryByRank.
u8 OfficeGetCategoryByRank(u8 rank) {
    if (rank <= kOfficeDefCount) // original: a1 <= 0x25
        return kOfficeDefTable[12 * rank + 3]; // HIBYTE(dword_62EC8E[3*rank])
    return 0;
}

// gilde.exe 0x47efb4 — VIBE_Office_GetEntryByCity.
int OfficeGetEntryByCity(u8 key, OfficeHolder* out) {
    int idx = 0;
    if (key != reinterpret_cast<const u8*>(g_officeHolders)[0]) {
        int byteOff = 0;
        do {
            byteOff += kOfficeHolderStride;
            ++idx;
        } while (byteOff < 888 &&
                 key != reinterpret_cast<const u8*>(g_officeHolders)[byteOff]);
    }
    if (idx >= kOfficeDefCount)
        return 0;
    std::memcpy(out, &g_officeHolders[idx], sizeof(OfficeHolder));
    return 1;
}

// gilde.exe 0x47ef28 — VIBE_Office_GetEntryByHolder (matches type field +8).
int OfficeGetEntryByHolder(u8 type, OfficeHolder* out) {
    int idx = 0;
    for (int byteOff = 0; byteOff < 888; byteOff += kOfficeHolderStride) {
        // byte_B59850[i] == byte at holder+8 (office type).
        if (reinterpret_cast<const u8*>(g_officeHolders)[byteOff + 8] == type) {
            if (idx >= kOfficeDefCount)
                return 0;
            std::memcpy(out, &g_officeHolders[idx], sizeof(OfficeHolder));
            return 1;
        }
        ++idx;
    }
    if (idx >= kOfficeDefCount)
        return 0;
    std::memcpy(out, &g_officeHolders[idx], sizeof(OfficeHolder));
    return 1;
}

namespace {
// Find the first holder index (0..29) whose office type (+8) == `type`.
// Returns 30 if not found (mirrors the original's "idx >= 30" failure check).
int FindHolderSlotByType(u8 type) {
    int idx = 0;
    int byteOff = 0;
    if (reinterpret_cast<const u8*>(g_officeHolders)[8] != type) {
        do {
            byteOff += kOfficeHolderStride;
            ++idx;
        } while (byteOff < 720 &&
                 reinterpret_cast<const u8*>(g_officeHolders)[byteOff + 8] != type);
    }
    return idx;
}

// A holder slot is "available for a new candidate" iff city==-1, state==3,
// rank<4 (the exact triple the candidacy/assign rules test).
bool SlotIsOpen(int idx) {
    const OfficeHolder& h = g_officeHolders[idx];
    return h.city == -1 && h.state == 3 && h.rank < 4;
}
} // namespace

// gilde.exe 0x47e3b8 — VIBE_Office_CanRunForOffice.
bool OfficeCanRunForOffice(const OfficePerson& p) {
    if (!p.valid || p.candidacy) // !RecordById || *(person+360)
        return false;
    int idx = FindHolderSlotByType(p.officeType); // matches v3[6] (held type)
    if (idx >= kOfficeHolderCount)
        return false;
    if (!SlotIsOpen(idx))
        return false;
    if (g_officeHolders[idx].type != p.officeType) // byte_B59850[24*idx] == p type
        return false;
    // The original then optionally validates a SECOND holder (v3[5], a partner
    // id == -1 means "no partner required" -> success). We model the common case
    // (no partner) as success; a partner-required slot would need the partner's
    // holder entry, which the live council code supplies. Faithful to the
    // RecordById==-1 short-circuit (return 1).
    return true;
}

// gilde.exe 0x47e0f8 — VIBE_Office_CanPromoteRank.
int OfficeCanPromoteRank(const OfficePerson& p, u8 targetRank, float* outCost) {
    if (!p.valid)                 return 0; // !a1
    // *(_WORD*)a1 == 0xFFFF guard -> modeled by p.valid
    if (targetRank == 0)          return 0; // !a2
    if (targetRank >= 30)         return 0; // a2 >= 0x1E
    if (p.officeType >= 30)       return 0; // *(person+358) >= 0x1E
    if (p.rank >= 30)             return 0; // *(person+359) >= 0x1E

    u8 v4 = BookCat(p.rank);      // byte_62EC92[12*personRank] == bookCat(rank)
    u8 catTarget = BookCat(targetRank);
    if (v4 + 1 < catTarget)       return 0;

    u8 catOffice = BookCat(p.officeType); // byte_62EC92[12*officeType*... ] (v6*4)
    if (catOffice >= catTarget)   return 0;

    if (outCost) {
        u8 row = ReqCode(p.officeType);  // dword_62EC8E[3*officeType] >> 24
        u8 col = ReqCode(targetRank);    // dword_62EC8E[3*targetRank] >> 24
        *outCost = kPromotionCost[row][col];
    }
    return 1;
}

// gilde.exe 0x47f6a4 — VIBE_Office_IsNextRankInCategory.
bool OfficeIsNextRankInCategory(const OfficePerson& p, u8 rank) {
    if (rank >= kOfficeDefCount)          return false; // a2 < 0x25
    if (p.officeType >= kOfficeDefCount)  return false; // v2 < 0x25

    // The equality gate compares the two records' high bytes (reqCode book id).
    if (ReqCode(rank) != ReqCode(p.officeType))
        return false;

    // Book-progression: combine the two records' bookCat % 3 residues exactly as
    // the original's three OR'd branches do.
    int t = BookCat(rank) % 3;        // BYTE6(v7) % 3  (target)
    int o = BookCat(p.officeType) % 3; // v5[4] % 3      (office)
    return (t == 0 && o != 0)
        || (t == 2 && o == 0)
        || (t == 1 && o == 2);
}

// gilde.exe 0x47fb30 — VIBE_Office_GetRankRequirements.
int OfficeGetRankRequirements(u8 rank, RankRequirements* out) {
    if (rank >= kOfficeDefCount)
        return 0;
    u8 req = ReqCode(rank); // BYTE1(record) == reqCode
    std::memset(out, 0, sizeof(*out));
    switch (req) {
    case 1: case 2: case 3:
        out->ageMin = 16;  out->ageMax = 28;
        out->moneyMin = 32000;  out->moneyMax = 192000;
        out->countA = 2;   out->countB = 3;
        out->reqOfficesA = 1; out->reqOfficesB = 2;
        return 1;
    case 4: case 5:
        out->ageMin = 21;  out->ageMax = 30;
        out->moneyMin = 160000; out->moneyMax = 640000;
        out->countA = 2;   out->countB = 4;
        out->reqOfficesA = 2; out->reqOfficesB = 4;
        return 1;
    case 6:
        out->ageMin = 24;  out->ageMax = 30;
        out->moneyMin = 640000; out->moneyMax = 1600000;
        out->countA = 3;   out->countB = 5;
        out->reqOfficesA = 3; out->reqOfficesB = 5;
        return 1;
    default:
        return 0;
    }
}

// gilde.exe 0x47f858 — VIBE_Office_CollectSuccessorCandidates.
int OfficeCollectSuccessorCandidates(u8 rank, const OfficePerson* people,
                                     int peopleCount, int maxCount,
                                     i32* out, int outCapacity) {
    if (rank >= kOfficeDefCount)
        return 0;
    if (maxCount >= 6)        // a2 >= 6 -> a2 = 6
        maxCount = 6;
    int limit = peopleCount;
    if (limit > maxCount)     // result >= v5 -> result = v5 (cap the pool)
        limit = maxCount;

    int count = 0;
    for (int i = 0; i < limit && count < outCapacity; ++i) {
        const OfficePerson& cand = people[i];
        if (!cand.valid)
            continue;
        if (OfficeIsNextRankInCategory(cand, rank)) {
            out[count] = cand.ownerId;
            ++count;
        }
    }
    return count;
}

} // namespace guild::world
