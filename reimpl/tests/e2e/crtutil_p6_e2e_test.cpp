// End-to-end deterministic scenario for the P6 CRT/util batch. No external asset
// is required (these are pure CRT leaves), so this runs UNGUARDED: it builds a
// realistic "packed save frame" the way the engine's IO layer would — a timestamp
// header (asctime), a CRC-16 trailer (table-driven), a shuffled record deal, and
// block-copied payloads — then proves the whole frame is byte-identical across two
// independent runs and round-trips losslessly (CRC validates, deal inverts).
#include "test.h"
#include "util/util_misc.h"
#include "crt/mem_block.h"
#include "crt/asctime.h"
#include "crt/rand.h"
#include <cstring>
#include <vector>

using namespace guild;

namespace {

constexpr int kRecords = 64;
constexpr int kRecSize = 16;

u16 g_crcTable[256];
bool g_crcReady = false;

u16 FrameCrc(const unsigned char* p, int n) {
    if (!g_crcReady) { util::BuildCrc16Table(g_crcTable); g_crcReady = true; }
    u16 crc = 0xFFFF;
    for (int i = 0; i < n; ++i)
        crc = static_cast<u16>((crc << 8) ^ g_crcTable[(crc >> 8) ^ p[i]]);
    return crc;
}

// Build one deterministic frame given a seed. Layout:
//   [0..25]  asctime header (26 bytes)
//   [26..]   kRecords * kRecSize record bytes, dealt in shuffled order
//   [tail]   2-byte CRC-16 over the header+records region
struct Frame {
    std::vector<unsigned char> bytes;
    u8 perm[kRecords];
};

Frame BuildFrame(u32 seed) {
    Frame f;
    const int payloadOff = 26;
    const int payloadLen = kRecords * kRecSize;
    f.bytes.assign(payloadOff + payloadLen + 2, 0);

    // header: a fixed timestamp formatted via the reconstructed asctime
    crt::TmRec tm{ /*sec*/seed % 60 == 0 ? 0 : 0, /*min*/0, /*hour*/12,
                   /*mday*/7, /*mon*/5, /*year*/100, /*wday*/3, /*yday*/158, 0 };
    char hdr[32];
    crt::FormatAsctime(&tm, hdr);
    crt::MemMoveOverlapping(f.bytes.data(), hdr, 26);

    // source records: record i is kRecSize copies of a tag derived from i
    unsigned char src[kRecords][kRecSize];
    for (int i = 0; i < kRecords; ++i)
        crt::MemSet(src[i], static_cast<u8>(0x40 + i), kRecSize);

    // deal in shuffled order (seeded, deterministic)
    crt::Srand(seed);
    util::InitAndShuffleByteArray(static_cast<u8>(kRecords), f.perm);
    for (int i = 0; i < kRecords; ++i)
        crt::MemMoveOverlapping(f.bytes.data() + payloadOff + i * kRecSize,
                                src[f.perm[i]], kRecSize);

    // CRC trailer over header+payload
    u16 crc = FrameCrc(f.bytes.data(), payloadOff + payloadLen);
    f.bytes[payloadOff + payloadLen]     = static_cast<unsigned char>(crc & 0xFF);
    f.bytes[payloadOff + payloadLen + 1] = static_cast<unsigned char>(crc >> 8);
    return f;
}

} // namespace

TEST(crtutil_p6_e2e, frame_is_deterministic_and_valid) {
    Frame a = BuildFrame(0xC0FFEEu);
    Frame b = BuildFrame(0xC0FFEEu);

    // 1) two independent builds are byte-identical (determinism end to end)
    CHECK_EQ(a.bytes.size(), b.bytes.size());
    CHECK_EQ(crt::MemCompare(a.bytes.data(), b.bytes.data(), a.bytes.size()), 0);
    CHECK(std::memcmp(a.perm, b.perm, kRecords) == 0);

    // 2) the header is the expected asctime string
    CHECK(std::memcmp(a.bytes.data(), "Wed Jun  7 12:00:00 2000\n", 25) == 0);

    // 3) the CRC trailer validates against a recomputation
    const int payloadOff = 26, payloadLen = kRecords * kRecSize;
    u16 stored = static_cast<u16>(a.bytes[payloadOff + payloadLen] |
                                  (a.bytes[payloadOff + payloadLen + 1] << 8));
    u16 recompute = FrameCrc(a.bytes.data(), payloadOff + payloadLen);
    CHECK_EQ(static_cast<int>(stored), static_cast<int>(recompute));

    // 4) the shuffled deal is a true permutation of 0..kRecords-1
    int seen[kRecords] = {0};
    for (int i = 0; i < kRecords; ++i) { CHECK(a.perm[i] < kRecords); seen[a.perm[i]]++; }
    for (int i = 0; i < kRecords; ++i) CHECK_EQ(seen[i], 1);

    // 5) round-trip: invert the deal and reconstruct the ORIGINAL ordered records.
    u8 inv[kRecords];
    for (int i = 0; i < kRecords; ++i) inv[a.perm[i]] = static_cast<u8>(i);
    unsigned char restored[kRecords][kRecSize];
    for (int i = 0; i < kRecords; ++i) {
        int dealtSlot = inv[i];
        crt::MemMoveOverlapping(restored[i],
            a.bytes.data() + payloadOff + dealtSlot * kRecSize, kRecSize);
    }
    bool allOk = true;
    for (int i = 0; i < kRecords && allOk; ++i) {
        unsigned char expect[kRecSize];
        crt::MemSet(expect, static_cast<u8>(0x40 + i), kRecSize);
        if (crt::MemCompare(restored[i], expect, kRecSize) != 0)
            allOk = false;
    }
    CHECK(allOk);
}

TEST(crtutil_p6_e2e, different_seed_changes_frame) {
    Frame a = BuildFrame(0x11111111u);
    Frame b = BuildFrame(0x22222222u);
    // the dealt payload region differs (different permutation), so frames differ
    CHECK(crt::MemCompare(a.bytes.data(), b.bytes.data(), a.bytes.size()) != 0);
    // but both still validate their own CRC trailer
    const int off = 26, len = kRecords * kRecSize;
    for (Frame* f : {&a, &b}) {
        u16 stored = static_cast<u16>(f->bytes[off + len] | (f->bytes[off + len + 1] << 8));
        CHECK_EQ(static_cast<int>(stored), static_cast<int>(FrameCrc(f->bytes.data(), off + len)));
    }
}

TEST(crtutil_p6_e2e, bounded_compare_finds_header_field) {
    // Treat the asctime header as a NUL-free field and locate the month token with
    // MemFindPattern, then bounded-compare it against a probe (locale-style use).
    Frame f = BuildFrame(7u);
    const unsigned char* mon =
        static_cast<const unsigned char*>(util::MemFindPattern(
            f.bytes.data(), reinterpret_cast<const unsigned char*>("Jun"), 26, 3));
    CHECK(mon != nullptr);
    if (mon) {
        // bounded compare over 3 bytes (no NUL inside) must equal
        CHECK_EQ(crt::MemCompareBounded(mon, "Jun", 3), 0);
        CHECK(crt::MemCompareBounded(mon, "Jul", 3) != 0);
    }
}
