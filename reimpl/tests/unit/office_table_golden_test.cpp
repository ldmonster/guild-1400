// Wave-14 1:1 value-pinning (W14-OFFICE cluster): GOLDEN assertions for the
// recovered office data/rules core in src/world/office.cpp. Wave-12 hardened the
// OOB edges; wave-13 pinned the playable flow; this file pins the *recovered 1:1
// VALUES* that no test asserted: the 446-byte office-definition table
// (dword_62EC8E), the per-rank reqCode / bookCat decode (the office category/rank
// mapping), the OfficeGetDefinition record decode, the OfficeGetRankRequirements
// constant bands, and the IsNextRankInCategory progression rule.
//
// Every pinned value is sourced ONLY from src/world/office.cpp's own recovered
// table bytes / switch constants (NEVER invented). The kOfficeDefTable digest is
// recomputed from those exact bytes — a digest mismatch means the table drifted.
//
// WAVE-16 (RESOLVED): the wave-12 NEEDS-LIVE-MCP flag is fixed. reqCode reaches 7
// for office types 28..34; CanPromoteRank @0x47e0f8 reads dword_62EBCC as a FLAT
// stride-7 array (index 7*reqOffice+reqTarget, up to 56), so the high indices read
// the matrix tail + adjacent dword_62EC8E bytes — reproduced bit-for-bit by
// kPromotionCostBits[57] in office.cpp. Both the reqCode 1..6 region AND the
// reqCode==7 cells are now pinned (see PromotionCostReqCode7Cells below).
#include "test.h"

#include <cstring>

#include "world/office.h"

using namespace guild::world;

namespace {
// FNV-1a/32 over a byte span — used to pin the whole 446-byte table cheaply while
// keeping a few explicit spot bytes for human-legible drift diagnosis.
guild::u32 Fnv1a32(const guild::u8* p, int n) {
    guild::u32 h = 0x811c9dc5u;
    for (int i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}
} // namespace

// ===========================================================================
// kOfficeDefTable (dword_62EC8E) — the 446-byte office-definition blob.
// ===========================================================================
// The table is private to office.cpp; we pin it through its public accessors
// (OfficeGetCategoryByRank == reqCode byte, OfficeDefBookCat == bookCat byte,
// OfficeGetDefinition == the 3 record dwords). Reconstructing the per-rank bytes
// here and hashing them pins the table to a single 32-bit digest.
TEST(OfficeTableGolden, DefTableDigestAndSpotBytes) {
    // Per-rank reqCode (table byte 12*rank+3) recovered from the source table.
    // This IS the "office category by rank" mapping the rules switch on.
    static const guild::u8 kReqCode[37] = {
        0,1,1,1,2,2,2,3,3,3,4,4,4,4,4,4,5,5,5,5,5,5,6,6,6,6,6,6,7,7,7,7,7,7,7,0,0
    };
    // Per-rank bookCat (table byte 2+12*rank+2).
    static const guild::u8 kBookCat[37] = {
        0,1,2,3,1,2,3,1,2,3,4,4,4,5,5,6,4,4,4,5,5,6,7,7,7,8,8,9,4,4,4,4,4,4,4,0,0
    };
    for (int r = 0; r < 37; ++r) {
        CHECK_EQ(OfficeGetCategoryByRank((guild::u8)r), kReqCode[r]);
        CHECK_EQ(OfficeDefReqCode((guild::u8)r), kReqCode[r]);
        CHECK_EQ(OfficeDefBookCat((guild::u8)r), kBookCat[r]);
    }

    // Rebuild the full 446-byte table from GetDefinition's record dwords (the
    // record at byte 2+12*rank, 12 bytes/record, +2 leading skew) plus the 2-byte
    // leading skew and 2-byte tail, then hash it. The first 2 bytes (record 0
    // prefix skew) and the trailing 2 bytes are zero in the source.
    guild::u8 rebuilt[446];
    std::memset(rebuilt, 0, sizeof(rebuilt));
    for (int r = 0; r < 37; ++r) {
        OfficeDef d;
        OfficeGetDefinition((guild::u8)r, &d);
        std::memcpy(&rebuilt[2 + 12 * r + 0], &d.word0, 4);
        std::memcpy(&rebuilt[2 + 12 * r + 4], &d.flag, 4);
        std::memcpy(&rebuilt[2 + 12 * r + 8], &d.textId, 4);
    }
    // Golden digest of the recovered dword_62EC8E bytes.
    CHECK_EQ(Fnv1a32(rebuilt, 446), (guild::u32)0x6492369bu);

    // Explicit spot bytes (book/category boundaries) for legible drift triage.
    CHECK_EQ(OfficeGetCategoryByRank(1), (guild::u8)1);   // first office, reqCode 1
    CHECK_EQ(OfficeGetCategoryByRank(27), (guild::u8)6);  // top guild office reqCode 6
    CHECK_EQ(OfficeGetCategoryByRank(28), (guild::u8)7);  // reqCode 7 band begins
    CHECK_EQ(OfficeGetCategoryByRank(34), (guild::u8)7);  // last reqCode-7 office
    CHECK_EQ(OfficeDefBookCat(27), (guild::u8)9);         // highest bookCat (9)
}

// ===========================================================================
// OfficeGetDefinition (0x47f008) — record decode (3 dwords from base+2+12*rank).
// ===========================================================================
TEST(OfficeTableGolden, GetDefinitionRecordDecode) {
    OfficeDef d;
    // rank 1: id1 reqCode1 bookCat1 cost5 | flag1 | textId 0x40a00000 (5.0f id).
    CHECK_EQ(OfficeGetDefinition(1, &d), 1);
    CHECK_EQ(d.word0,  (guild::u32)0x05010101u);
    CHECK_EQ(d.flag,   (guild::i32)0x00000001);
    CHECK_EQ(d.textId, (guild::u32)0x40a00000u);
    // rank 16: word0 0x14040510 (id16 bookCat4 reqCode5 cost0x14), flag 0, textId 0x40c00000.
    CHECK_EQ(OfficeGetDefinition(16, &d), 1);
    CHECK_EQ(d.word0,  (guild::u32)0x14040510u);
    CHECK_EQ(d.flag,   (guild::i32)0);
    CHECK_EQ(d.textId, (guild::u32)0x40c00000u);
    // rank 36 (last): word0 0, flag carries "GOD\0" bytes -> 0x00474f44, textId 0.
    CHECK_EQ(OfficeGetDefinition(36, &d), 1);
    CHECK_EQ(d.word0,  (guild::u32)0);
    CHECK_EQ(d.flag,   (guild::i32)0x00474f44);
    CHECK_EQ(d.textId, (guild::u32)0);
    // rank 37 (out of range): returns 0.
    CHECK_EQ(OfficeGetDefinition(37, &d), 0);
}

// ===========================================================================
// OfficeGetRankRequirements (0x47fb30) — reqCode -> requirement band constants.
// ===========================================================================
// The original selects on reqCode(rank): bands {1,2,3}, {4,5}, {6}; reqCode 0/7
// (and out-of-range) return 0. Every constant below is from office.cpp's switch.
TEST(OfficeTableGolden, RankRequirementsBands) {
    RankRequirements rr;

    // reqCode 1..3 band (ranks 1..9 all carry reqCode 1/2/3).
    for (int rank : {1, 4, 7}) {
        std::memset(&rr, 0xAB, sizeof(rr));
        CHECK_EQ(OfficeGetRankRequirements((guild::u8)rank, &rr), 1);
        CHECK_EQ(rr.ageMin, (guild::i16)16);
        CHECK_EQ(rr.ageMax, (guild::i16)28);
        CHECK_EQ(rr.moneyMin, (guild::i32)32000);
        CHECK_EQ(rr.moneyMax, (guild::i32)192000);
        CHECK_EQ(rr.countA, (guild::u8)2);
        CHECK_EQ(rr.countB, (guild::u8)3);
        CHECK_EQ(rr.reqOfficesA, (guild::i32)1);
        CHECK_EQ(rr.reqOfficesB, (guild::i32)2);
    }

    // reqCode 4..5 band (rank 10 -> reqCode 4, rank 13 -> reqCode 5).
    for (int rank : {10, 13}) {
        CHECK_EQ(OfficeGetRankRequirements((guild::u8)rank, &rr), 1);
        CHECK_EQ(rr.ageMin, (guild::i16)21);
        CHECK_EQ(rr.ageMax, (guild::i16)30);
        CHECK_EQ(rr.moneyMin, (guild::i32)160000);
        CHECK_EQ(rr.moneyMax, (guild::i32)640000);
        CHECK_EQ(rr.countA, (guild::u8)2);
        CHECK_EQ(rr.countB, (guild::u8)4);
        CHECK_EQ(rr.reqOfficesA, (guild::i32)2);
        CHECK_EQ(rr.reqOfficesB, (guild::i32)4);
    }

    // reqCode 6 band (rank 22 -> reqCode 6).
    CHECK_EQ(OfficeGetRankRequirements(22, &rr), 1);
    CHECK_EQ(rr.ageMin, (guild::i16)24);
    CHECK_EQ(rr.ageMax, (guild::i16)30);
    CHECK_EQ(rr.moneyMin, (guild::i32)640000);
    CHECK_EQ(rr.moneyMax, (guild::i32)1600000);
    CHECK_EQ(rr.countA, (guild::u8)3);
    CHECK_EQ(rr.countB, (guild::u8)5);
    CHECK_EQ(rr.reqOfficesA, (guild::i32)3);
    CHECK_EQ(rr.reqOfficesB, (guild::i32)5);

    // reqCode 0 (rank 0) and reqCode 7 (rank 28) fall through the switch -> 0.
    CHECK_EQ(OfficeGetRankRequirements(0, &rr), 0);
    CHECK_EQ(OfficeGetRankRequirements(28, &rr), 0);
}

// ===========================================================================
// kPromotionCost (dword_62EBCC) — FLAT stride-7 cost table, reqCode 0..7 pinned.
// ===========================================================================
// WAVE-16 fix: CanPromoteRank @0x47e0f8 reads the cost as a FLAT 1-D array:
//   *a3 = dword_62EBCC[7 * reqCode(officeType) + reqCode(targetRank)];
// reqCode(officeType)/reqCode(targetRank) each reach 7 (def-table reqCode hits 7),
// so the largest index is 7*7+7 = 56. The labelled matrix region only holds 48
// floats and is immediately followed in the binary by the gap + dword_62EC8E def
// table, so indices >= 48 (reqOffice==7, reqTarget>=4 and reqTarget==7) read into
// that ADJACENT data — intentional shipped behaviour reproduced bit-for-bit by the
// 57-entry kPromotionCostBits[] in office.cpp (recovered via get_bytes @0x62EBCC).
//
// To hit flat[7*R + C] we pick an officeType whose reqCode==R and a targetRank
// whose reqCode==C such that the promotion guards (bookCat progression +
// officeCat<targetCat) pass.
TEST(OfficeTableGolden, PromotionCostInRangeCells) {
    // The 48-float matrix region as flat[0..47] (reqOffice 0..6, reqTarget 0..6 ==
    // the original 7x7 view; flat[48] = first byte past it = 0.0).
    static const float kExpect[7][7] = {
        {  1.0f, -2.5f, -2.5f, -2.5f, -5.0f, -5.0f, -10.0f },
        {  1.0f,  0.0f, -2.5f, -5.0f, -2.5f, -7.0f, -10.0f },
        {  1.0f, -2.5f,  0.0f, -2.5f, -5.0f, -5.0f, -10.0f },
        {  1.0f, -5.0f, -2.5f,  0.0f, -7.5f, -2.5f, -10.0f },
        {  1.0f,  1.0f,  1.0f,  1.0f,  0.0f, -5.0f, -10.0f },
        {  1.0f,  1.0f,  1.0f,  1.0f, -5.0f,  0.0f, -10.0f },
        {  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,   0.0f },
    };
    // Pin a guard-passing promotion that lands on cell [reqCode(office)][reqCode(target)].
    // officeType 1 has reqCode 1, bookCat 1; targetRank 5 has reqCode 2, bookCat 2.
    // bookCat(1)+1 >= bookCat(5)=2 (2>=2 ok); bookCat(office=1)=1 < bookCat(5)=2 ok.
    OfficePerson p;
    p.valid = true;
    p.candidacy = 0;
    p.ownerId = 1;
    p.officeType = 1;   // reqCode 1
    p.rank = 1;         // bookCat 1
    float cost = 999.0f;
    int ok = OfficeCanPromoteRank(p, /*targetRank=*/5, &cost); // target reqCode 2
    CHECK_EQ(ok, 1);
    CHECK_EQ(cost, kExpect[1][2]); // flat[9] == -2.5f

    // A second guard-passing pair on a different cell:
    // officeType 4 (reqCode 2, bookCat 1) -> targetRank 8 (reqCode 3, bookCat 2).
    p.officeType = 4;   // reqCode 2
    p.rank = 4;         // bookCat 1
    cost = 999.0f;
    ok = OfficeCanPromoteRank(p, 8, &cost); // target reqCode 3
    CHECK_EQ(ok, 1);
    CHECK_EQ(cost, kExpect[2][3]); // flat[17] == -2.5f
}

// WAVE-16: the previously-OOB reqCode==7 region. These are REACHABLE guard-passing
// promotions whose flat index runs into the matrix tail / adjacent def-table bytes.
// Values recovered via get_bytes @0x62EBCC and confirmed by decompiling 0x47e0f8.
TEST(OfficeTableGolden, PromotionCostReqCode7Cells) {
    OfficePerson p;
    p.valid = true; p.candidacy = 0; p.ownerId = 1;

    // reqOffice==7, reqTarget==5 -> flat[7*7+5] = flat[54] = 5.0.
    // officeType 28 (reqCode 7, bookCat 4); rank 10 (bookCat 4); targetRank 19
    // (reqCode 5, bookCat 5): bookCat(10)+1=5 >= 5 ok; bookCat(28)=4 < 5 ok.
    p.officeType = 28; p.rank = 10;
    float cost = 999.0f;
    CHECK_EQ(OfficeCanPromoteRank(p, 19, &cost), 1);
    CHECK_EQ(cost, 5.0f); // flat[54]

    // reqOffice==7, reqTarget==4 -> flat[53] = 1.401298464324817e-45 (denormal,
    // = bit pattern 0x00000001 read from dword_62EC8E's record-1 word0+1 byte).
    // officeType 28; rank 10; targetRank 13 (reqCode 4, bookCat 5).
    cost = 999.0f;
    CHECK_EQ(OfficeCanPromoteRank(p, 13, &cost), 1);
    float expect53;
    {
        guild::u32 bits = 0x00000001u;
        std::memcpy(&expect53, &bits, 4);
    }
    CHECK_EQ(cost, expect53); // flat[53]

    // reqTarget==7 -> column 7 lands on the next row's col-0 (value 1.0):
    // reqOffice==1, reqTarget==7 -> flat[7*1+7] = flat[14] = 1.0.
    // officeType 1 (reqCode 1, bookCat 1); rank 3 (bookCat 3); targetRank 28
    // (reqCode 7, bookCat 4): bookCat(3)+1=4 >= 4 ok; bookCat(1)=1 < 4 ok.
    p.officeType = 1; p.rank = 3;
    cost = 999.0f;
    CHECK_EQ(OfficeCanPromoteRank(p, 28, &cost), 1);
    CHECK_EQ(cost, 1.0f); // flat[14]
}

// ===========================================================================
// OfficeIsNextRankInCategory (0x47f6a4) — reqCode-equality + bookCat%3 rule.
// ===========================================================================
TEST(OfficeTableGolden, IsNextRankInCategoryRule) {
    OfficePerson p;
    p.valid = true; p.candidacy = 0; p.ownerId = 1; p.rank = 0;
    // Same reqCode book required: rank 1 (reqCode1,bookCat1) vs office 2 (reqCode1,bookCat2).
    // target t=bookCat(1)%3=1, office o=bookCat(2)%3=2 -> (t==1 && o==2) -> true.
    p.officeType = 2;
    CHECK(OfficeIsNextRankInCategory(p, 1));
    // office 3 (reqCode1,bookCat3): t=bookCat(1)%3=1, o=3%3=0 -> none match -> false.
    p.officeType = 3;
    CHECK(!OfficeIsNextRankInCategory(p, 1));
    // Different reqCode books never match: rank 4 (reqCode2) vs office 1 (reqCode1).
    p.officeType = 1;
    CHECK(!OfficeIsNextRankInCategory(p, 4));
    // Out-of-range guards.
    CHECK(!OfficeIsNextRankInCategory(p, 37));
    p.officeType = 37;
    CHECK(!OfficeIsNextRankInCategory(p, 1));
}
