// Integration tests for the P6 CRT/util batch: compose the reconstructed leaves
// into small pipelines and cross-check end results, exercising the leaves together
// (not in isolation) the way the engine's IO/locale layers chain them.
#include "test.h"
#include "util/util_misc.h"
#include "crt/mem_block.h"
#include "crt/asctime.h"
#include "crt/rand.h"
#include <cstring>

using namespace guild;

// Pipeline A: a CRC-16/CCITT over a byte stream computed using the table built by
// util::BuildCrc16Table, where the stream is assembled via the crt block ops
// (MemSet to clear, MemMoveOverlapping to splice). Cross-checked against a direct
// bitwise CRC so the table feeds a real checksum loop.
TEST(crtutil_p6_itest, crc16_over_spliced_buffer) {
    u16 table[256];
    util::BuildCrc16Table(table);

    // Build a 32-byte frame: header "GILD" then a payload, then duplicate the
    // header at the end via an overlapping move.
    unsigned char frame[40];
    crt::MemSet(frame, 0, sizeof frame);
    std::memcpy(frame, "GILD", 4);
    std::memcpy(frame + 4, "payload-bytes-1234", 18);
    // splice: copy the first 4 bytes (header) to offset 22 via the move primitive
    crt::MemMoveOverlapping(frame + 22, frame, 4);

    // table-driven CRC-16 (MSB-first, init 0xFFFF)
    auto tableCrc = [&](const unsigned char* p, int n) {
        u16 crc = 0xFFFF;
        for (int i = 0; i < n; ++i)
            crc = static_cast<u16>((crc << 8) ^ table[(crc >> 8) ^ p[i]]);
        return crc;
    };
    // bitwise reference CRC-16/CCITT-FALSE
    auto bitCrc = [&](const unsigned char* p, int n) {
        u16 crc = 0xFFFF;
        for (int i = 0; i < n; ++i) {
            crc ^= static_cast<u16>(p[i] << 8);
            for (int b = 0; b < 8; ++b)
                crc = (crc & 0x8000) ? static_cast<u16>((crc << 1) ^ 0x1021)
                                     : static_cast<u16>(crc << 1);
        }
        return crc;
    };
    CHECK_EQ(static_cast<int>(tableCrc(frame, 26)), static_cast<int>(bitCrc(frame, 26)));
    // the spliced header bytes are present
    CHECK(std::memcmp(frame + 22, "GILD", 4) == 0);
}

// Pipeline B: a deterministic shuffled deal — InitAndShuffleByteArray to make a
// permutation, then use crt block ops to copy a payload table in the shuffled
// order, then MemCompare to confirm the inverse permutation restores order.
TEST(crtutil_p6_itest, shuffle_deal_roundtrip) {
    crt::Srand(2024);
    u8 perm[32];
    util::InitAndShuffleByteArray(32, perm);

    // a "deck" of 32 records, each a distinct 4-byte tag
    unsigned char deck[32][4];
    for (int i = 0; i < 32; ++i) {
        deck[i][0] = static_cast<unsigned char>(i);
        deck[i][1] = deck[i][2] = deck[i][3] = static_cast<unsigned char>(0xC0 | i);
    }
    // deal into shuffled order using MemSet (clear) + manual copy via MemMove
    unsigned char dealt[32][4];
    crt::MemSet(dealt, 0, sizeof dealt);
    for (int i = 0; i < 32; ++i)
        crt::MemMoveOverlapping(dealt[i], deck[perm[i]], 4);

    // restore: inverse permutation
    u8 inv[32];
    for (int i = 0; i < 32; ++i) inv[perm[i]] = static_cast<u8>(i);
    unsigned char restored[32][4];
    for (int i = 0; i < 32; ++i)
        crt::MemMoveOverlapping(restored[i], dealt[inv[i]], 4);

    // restored must equal the original deck byte-for-byte (via MemCompare)
    CHECK_EQ(crt::MemCompare(restored, deck, sizeof deck), 0);
    // and the same seed reproduces the same permutation
    crt::Srand(2024);
    u8 perm2[32];
    util::InitAndShuffleByteArray(32, perm2);
    CHECK(std::memcmp(perm, perm2, 32) == 0);
}

// Pipeline C: format a sequence of dates with FormatAsctime into a packed buffer,
// then sort the records by CompareDateFields (a simple selection sort using the
// reconstructed comparator) and confirm chronological order + that the asctime
// strings line up with the sorted dates.
TEST(crtutil_p6_itest, date_format_and_sort) {
    struct Rec { int dmy[3]; crt::TmRec tm; };
    Rec recs[4] = {
        {{15, 3, 2001}, {0,0,12, 15, 2, 101, 4, 73, 0}},   // 2001-03-15 Thu
        {{ 7, 6, 2000}, {0,0,12,  7, 5, 100, 3, 158, 0}},  // 2000-06-07 Wed
        {{ 1, 1, 2002}, {0,0,12,  1, 0, 102, 2, 0, 0}},    // 2002-01-01 Tue
        {{ 7, 6, 2000}, {0,0,12,  7, 5, 100, 3, 158, 0}},  // dup of #2
    };
    // selection sort ascending using CompareDateFields (1 == strictly before)
    for (int i = 0; i < 4; ++i) {
        int best = i;
        for (int j = i + 1; j < 4; ++j)
            if (crt::CompareDateFields(recs[j].dmy, recs[best].dmy) == 1)
                best = j;
        Rec t = recs[i]; recs[i] = recs[best]; recs[best] = t;
    }
    // chronological: 2000, 2000(dup), 2001, 2002
    CHECK_EQ(recs[0].dmy[2], 2000);
    CHECK_EQ(recs[1].dmy[2], 2000);
    CHECK_EQ(recs[2].dmy[2], 2001);
    CHECK_EQ(recs[3].dmy[2], 2002);
    // no pair is out of order (each is not-after the next)
    for (int i = 0; i + 1 < 4; ++i)
        CHECK_EQ(crt::CompareDateFields(recs[i + 1].dmy, recs[i].dmy), 0);

    // format the earliest and confirm the asctime layout
    char buf[32];
    crt::FormatAsctime(&recs[0].tm, buf);
    CHECK(std::strcmp(buf, "Wed Jun  7 12:00:00 2000\n") == 0);
}

// Pipeline D: a RandomMod-driven sampler that fills a histogram; with a fixed seed
// the histogram is exactly reproducible and every sample is in range.
TEST(crtutil_p6_itest, random_mod_sampler) {
    util::SetMsvcRandSeed(1);
    int hist[6] = {0};
    int seq[200];
    for (int i = 0; i < 200; ++i) {
        u32 r = util::RandomMod(6u);
        CHECK(r < 6u);
        hist[r]++;
        seq[i] = static_cast<int>(r);
    }
    int total = 0;
    for (int i = 0; i < 6; ++i) total += hist[i];
    CHECK_EQ(total, 200);
    // reproducible: same seed -> same sequence
    util::SetMsvcRandSeed(1);
    for (int i = 0; i < 200; ++i)
        CHECK_EQ(static_cast<int>(util::RandomMod(6u)), seq[i]);
}
