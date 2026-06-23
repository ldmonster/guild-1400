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

// dword_62EBCC @0x62EBCC: promotion-cost float matrix.
//
// CanPromoteRank reads it as a *flat 1-D array, stride 7*:
//     *a3 = dword_62EBCC[7 * reqCode(officeType) + reqCode(targetRank)];
// where reqCode(officeType) and reqCode(targetRank) each range 0..7 (the def
// table's reqCode byte legitimately reaches 7 — office types 28..34). The largest
// index reached is 7*7+7 = 56, i.e. 57 float slots. The labelled matrix region
// holds only 48 floats (0x62EBCC..0x62EC8B) and is *immediately followed in the
// binary* by the gap + the dword_62EC8E def table (0x62EC8E). So indices >= 48
// (reachable for reqOffice==7, reqTarget>=4 and for reqTarget==7) read into that
// adjacent data — this is intentional shipped behaviour, not a bug, and the exact
// values must be reproduced bit-for-bit (some are denormal/garbage reinterpreted
// from the def-table bytes). Stored as raw 32-bit patterns (recovered via
// get_bytes @0x62EBCC) and bit-cast at read time so the byte-exact result matches.
//
// Layout (flat[7*row+col], row=reqCode(officeType), col=reqCode(targetRank)):
//   flat[0..47]  : the 48-float matrix region (rows 0..6 cols 0..6 == the prior
//                  7x7 view; row6 col6 = flat[48]=0.0 already reaches past it)
//   flat[48..50] : 0.0, 0.0, 0.0   (gap tail + dword_62EC8E[0..1] zero head)
//   flat[51]     : 5.0  (0x40A00000 = dword_62EC8E record-1 word0 high half)
//   flat[52]     : 0x05010101 (def bytes -> denormal)
//   flat[53]     : 0x00000001 (1.401e-45)
//   flat[54]     : 5.0  (0x40A00000)
//   flat[55]     : 0x07020102 (def bytes)
//   flat[56]     : 0.0
static const u32 kPromotionCostBits[57] = {
    0x3F800000u, 0xC0200000u, 0xC0200000u, 0xC0200000u, 0xC0A00000u, 0xC0A00000u, 0xC1200000u, // flat[0..6]
    0x3F800000u, 0x00000000u, 0xC0200000u, 0xC0A00000u, 0xC0200000u, 0xC0E00000u, 0xC1200000u, // flat[7..13]
    0x3F800000u, 0xC0200000u, 0x00000000u, 0xC0200000u, 0xC0A00000u, 0xC0A00000u, 0xC1200000u, // flat[14..20]
    0x3F800000u, 0xC0A00000u, 0xC0200000u, 0x00000000u, 0xC0F00000u, 0xC0200000u, 0xC1200000u, // flat[21..27]
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x00000000u, 0xC0A00000u, 0xC1200000u, // flat[28..34]
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0xC0A00000u, 0x00000000u, 0xC1200000u, // flat[35..41]
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x00000000u, // flat[42..48]
    0x00000000u, 0x00000000u, 0x40A00000u, 0x05010101u, 0x00000001u, 0x40A00000u, 0x07020102u, // flat[49..55]
    0x00000000u,                                                                                // flat[56]
};

// dword_62EBCC[idx] with byte-exact float reinterpretation.
static float PromotionCost(int idx) {
    float f;
    std::memcpy(&f, &kPromotionCostBits[idx], 4);
    return f;
}

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

// Bounds-checked table byte / dword reads. The original reads dword_62EC8E with a
// type/rank index that, for VALID input (0..36), always lands inside the 446-byte
// table; the original's table is followed by other globals so an out-of-range index
// read garbage but never faulted. Our reconstruction holds ONLY the table's 446
// bytes, so an out-of-range index (a rank==37 edge, or a malformed-save type byte
// up to 0xFF reaching the mutation rules) would read out of bounds. We bound the
// reads to the table and return 0 past its end: this keeps every in-range output
// byte-identical (valid input is unaffected) and reproduces the original's
// zero-tail for the rank==37 edge, while removing the OOB on degenerate bytes that
// the reconstruction cannot faithfully reproduce anyway.
u8 TableByte(int off) {
    if (off < 0 || off >= static_cast<int>(sizeof(kOfficeDefTable)))
        return 0;
    return kOfficeDefTable[off];
}
i32 TableDword(int off) {
    if (off < 0 || off + 4 > static_cast<int>(sizeof(kOfficeDefTable)))
        return 0;
    i32 v;
    std::memcpy(&v, &kOfficeDefTable[off], 4);
    return v;
}

// Read the +2-skewed 12-byte record into 3 dwords (GetDefinition layout).
void ReadDefRecord(u8 rank, u32* w0, i32* w1, u32* w2) {
    *w0 = static_cast<u32>(TableDword(2 + 12 * rank));
    *w1 = TableDword(2 + 12 * rank + 4);
    *w2 = static_cast<u32>(TableDword(2 + 12 * rank + 8));
}

// reqCode(rank): HIBYTE(unskewed dword at 12*rank) == byte at table offset
// 12*rank+3 == record byte +1 of the skewed record. (== GetCategoryByRank.)
u8 ReqCode(u8 rank) {
    return TableByte(12 * rank + 3);
}

// bookCat(rank): record byte +2 (the 1..9 book/category id).
u8 BookCat(u8 rank) {
    return TableByte(2 + 12 * rank + 2);
}
} // namespace

// gilde.exe 0x47f008 — VIBE_Office_GetDefinition.   VERIFIED-1:1 (disasm 0x47f008)
//   a1<0x25 path: 3 dwords from (char*)&dword_62EC8E[3*a1]+2 (offsets 12*a1+2/+6/+10).
//   fallback: dword_62EC8E+2, dword_62EC94[0] (=table+6), unk_62EC98 (=table+10).
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
    return TableDword(2 + 12 * type + 4); // record dword[1]
}
u8 OfficeDefId(u8 type) { return TableByte(2 + 12 * type); } // record byte +0

// gilde.exe 0x47ef94 — VIBE_Office_GetCategoryByRank.  VERIFIED-1:1 (disasm 0x47ef94)
//   `cmp al,25h; jbe` then `HIBYTE(dword_62EC8E[3*a1])` (byte 12*rank+3). The
//   rank==37 edge reads table+447 (1 byte past our 446-byte blob) in the original;
//   bounded to 0 here (documented hardening; in-range 0..36 are byte-exact).
u8 OfficeGetCategoryByRank(u8 rank) {
    if (rank <= kOfficeDefCount) // original: a1 <= 0x25
        return TableByte(12 * rank + 3); // HIBYTE(dword_62EC8E[3*rank])
    return 0;
}

// gilde.exe 0x47efb4 — VIBE_Office_GetEntryByCity.  VERIFIED-1:1 (disasm 0x47efb4)
//   Scans the +0 (holder) byte of byte_B59848 at stride 24 while v5<888 (0x378),
//   `v4>=37` (0x25) fails. The original compares with 8-bit `cmp bl,bh` (byte-exact,
//   sign-agnostic), reproduced by the unsigned-byte compares here. qmemcpy 24 bytes.
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
//   VERIFIED-1:1 (disasm 0x47ef28): scans byte_B59850[i] (+8 type) at stride 24
//   while i<888 (0x378); the original zero-extends both bytes (`xor; mov bl/cl`) and
//   does a 32-bit `cmp ecx,ebx`, reproduced by the unsigned-byte compare. The v3>=37
//   (0x25) check appears both on-hit and after the loop. qmemcpy 24 bytes.
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
// gilde.exe 0x47e3e0..0x47e404 — the holder-scan inside CanRunForOffice. NOTE the
// original scans the +0 (holder-id) byte of byte_B59848 (cmp bl,byte_B59848[eax]),
// NOT the +8 (type) byte, and the search key is the input struct's +4 byte (v3[4]).
// The OfficePerson view this module exposes carries no +4 search-key field, so this
// helper substitutes p.officeType and scans the +8 type field instead. This is the
// documented BOUNDARY analogue (see OfficeCanRunForOffice). Loop: stride 24, v5<720
// (0x2D0), "idx>=30" failure — those are byte-exact.
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

// gilde.exe 0x47e3b8 — VIBE_Office_CanRunForOffice.   BOUNDARY (struct mismatch).
//
// DIFF vs disasm 0x47e3b8 (the original arg is `int *a1`, called as `a1+16` from
// VIBE_Command_CheckCanRunForOffice @0x49614c, i.e. a 4-field descriptor):
//   *a1   (dword @+0)  person id  -> VIBE_Person_FindRecordById; +360 is the
//                                    already-a-candidate flag (RecordById+0x168).
//   a1[4] (byte  @+4)  holder-id search key  -> matched vs byte_B59848[+0] (stride 24).
//   a1[5] (byte  @+5)  partner id            -> ==0xFF short-circuits to success;
//                                               else a SECOND holder is located by
//                                               +0==a1[5] and validated: idx<30,
//                                               its +8 type == first holder's +8 type,
//                                               city(+4)==-1, state(+16)==3, rank(+12)<4.
//   a1[6] (byte  @+6)  office type           -> matched vs the found holder's +8 type.
//
// The portable OfficePerson view exposed by this module carries only {ownerId,
// officeType, rank, candidacy, valid} — it has NO +4 search-key and NO +5 partner
// field, so a byte-exact reconstruction is not expressible through this signature.
// (The header's OfficePerson struct and the out-of-module world_law_test.cpp pin
// this signature; changing it is outside this unit.) This is therefore a documented
// analogue, NOT a 1:1 path: it conflates the +4/+6 keys into p.officeType, scans the
// +8 type field instead of the +0 holder field, and treats every entry as "no
// partner" (a1[5]==0xFF) so the second-holder validation is skipped. The vacant-slot
// gate (city==-1, state==3, rank<4) and the idx>=30 failure ARE byte-exact.
// Faithful reconstruction requires the +4/+5 descriptor fields, supplied by the
// live command layer (data not in this unit's input).
bool OfficeCanRunForOffice(const OfficePerson& p) {
    if (!p.valid || p.candidacy) // !RecordById || *(person+360)
        return false;
    int idx = FindHolderSlotByType(p.officeType); // BOUNDARY: should scan +0 vs a1[4]
    if (idx >= kOfficeHolderCount)
        return false;
    if (!SlotIsOpen(idx))
        return false;
    if (g_officeHolders[idx].type != p.officeType) // byte_B59850[24*idx] == a1[6]
        return false;
    // BOUNDARY: original validates a second holder unless a1[5]==0xFF (see header).
    return true;
}

// gilde.exe 0x47e0f8 — VIBE_Office_CanPromoteRank.   VERIFIED-1:1 (disasm 0x47e0f8)
//   Guards: !a1; *(WORD*)a1==0xFFFF (==!p.valid); !a2; a2>=0x1E; person+358>=0x1E;
//   person+359>=0x1E. v4=byte_62EC92[12*rank] (==BookCat, table+4 stride12);
//   `v4+1 < (u8)byte_62EC92[12*a2]` signed-int compare; v6=3*officeType,
//   `(u8)byte_62EC92[v6*4] >= (u8)byte_62EC92[12*a2]`. Cost index is the FLAT read
//   dword_62EBCC[7*(dword_62EC8E[v6]>>24) + (dword_62EC8E[3*a2]>>24)] (reqCode hits
//   7 -> index up to 56; high indices read the matrix tail + adjacent def-table bytes,
//   reproduced bit-exact by kPromotionCostBits[57], confirmed via get_bytes @0x62EBCC).
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
        u8 row = ReqCode(p.officeType);  // dword_62EC8E[3*officeType] >> 24  (0..7)
        u8 col = ReqCode(targetRank);    // dword_62EC8E[3*targetRank] >> 24  (0..7)
        // Original: dword_62EBCC[7 * row + col] — a FLAT, stride-7 read whose index
        // reaches 56 (reqCode hits 7). The 57-entry kPromotionCostBits reproduces the
        // matrix region AND the adjacent def-table bytes the original read for the
        // high indices, so the cost is byte-identical for every reachable pair.
        *outCost = PromotionCost(7 * row + col);
    }
    return 1;
}

// gilde.exe 0x47f6a4 — VIBE_Office_IsNextRankInCategory.  VERIFIED-1:1 (disasm 0x47f6a4)
//   Guards a2<0x25 and (person+358)<0x25. reqCode gate is `sar ...,18h` of each
//   record's word0 (top byte, reqCode 0..7 so signed/unsigned identical). bookCat
//   residues use signed idiv-by-3 (values 0..9 positive -> same as %). Boolean:
//   (t==0&&o!=0)||(t==2&&o==0)||(t==1&&o==2) with t=target bookCat%3, o=office bookCat%3.
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

// gilde.exe 0x47fb30 — VIBE_Office_GetRankRequirements.  VERIFIED-1:1 (disasm 0x47fb30)
//   Switches on BYTE1(record word0) (==reqCode, table+3). All band constants and
//   field offsets (WORD@0/+2, DWORD@+4/+8, BYTE@+12/+13, DWORD@+16/+20) match the
//   original exactly. NOTE: the original does NOT memset on entry and leaves the +14
//   pad bytes (and, on the default/0 return, the whole struct) untouched; the memset
//   here only zeroes those caller-ignored pad bytes — every DEFINED output is byte-exact.
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

// gilde.exe 0x47f858 — VIBE_Office_CollectSuccessorCandidates.  (logic VERIFIED-1:1;
//   I/O reshaped — see header.) Original: a1>=0x25 -> 0; reads rank's record word0
//   (SBYTE1 == reqCode); a2>=6 -> a2=6; calls VIBE_Office_CollectByCategory(reqCode,
//   a2, scratch[6*24]); result capped to a2; per collected entry FindRecordById(
//   entry+4) then IsNextRankInCategory(record, rank); on pass stores the RECORD
//   POINTER (mov [eax],ecx) and increments the count. CollectByCategory and
//   FindRecordById live outside this unit, so the candidate pool is supplied here and
//   ownerIds are written instead of record pointers; the maxCount<=6 cap, the pool
//   cap, and the IsNextRankInCategory filter (the rule core) are byte-exact. The
//   `count < outCapacity` bound is an added OOB guard (no in-range behavioral change).
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
