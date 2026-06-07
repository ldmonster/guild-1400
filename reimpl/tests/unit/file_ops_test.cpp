// Unit tests for guild::io file_ops (VIBE_File_* / VIBE_Vfs_* OS layer).
#include "io/file_ops.h"
#include "test.h"

#include <cstring>

using namespace guild::io;

// ---- MapCreateDisposition (0x6062c0) --------------------------------------
TEST(FileOps, MapCreateDisposition) {
    guild::u32 acc = 0, cre = 0;
    CHECK_EQ(MapCreateDisposition(2, &acc, &cre), 2);
    CHECK_EQ(acc, 0xC0000000u);
    CHECK_EQ(cre, 128u);
    CHECK_EQ(MapCreateDisposition(1, &acc, &cre), 1);
    CHECK_EQ(acc, 0x40000000u);
    CHECK_EQ(cre, 128u);
    CHECK_EQ(MapCreateDisposition(0, &acc, &cre), 0);
    CHECK_EQ(acc, 0x80000000u);
    CHECK_EQ(cre, 1u);
}

// ---- MapAccessFlags (0x6062f4) golden vectors -----------------------------
TEST(FileOps, MapAccessFlags) {
    guild::u32 out = 0xDEAD;
    // group 0x00, sub 0 -> 1|2 = 3, returns 0
    out = 0xDEAD;
    CHECK_EQ(MapAccessFlags(0x00, &out), 0x00u); CHECK_EQ(out, 3u);
    // group 0x00, sub != 0 -> 1
    out = 0xDEAD;
    CHECK_EQ(MapAccessFlags(0x01, &out), 0x00u); CHECK_EQ(out, 1u);
    // group 0x10 -> 0
    out = 0xDEAD;
    CHECK_EQ(MapAccessFlags(0x10, &out), 0x10u); CHECK_EQ(out, 0u);
    // group 0x20 -> 1
    out = 0xDEAD;
    CHECK_EQ(MapAccessFlags(0x20, &out), 0x20u); CHECK_EQ(out, 1u);
    // group 0x30 -> 2
    out = 0xDEAD;
    CHECK_EQ(MapAccessFlags(0x30, &out), 0x30u); CHECK_EQ(out, 2u);
    // group 0x40 -> 3
    out = 0xDEAD;
    CHECK_EQ(MapAccessFlags(0x40, &out), 0x40u); CHECK_EQ(out, 3u);
    // group 0x50 (unhandled) -> out untouched, returns 0x50
    out = 0xDEAD;
    CHECK_EQ(MapAccessFlags(0x50, &out), 0x50u); CHECK_EQ(out, 0xDEADu);
}

// ---- DOS time pack/unpack (0x5fe760 / 0x5fe788) ---------------------------
TEST(FileOps, ConvertFileTimeToDos) {
    guild::u16 date = 0, time = 0;
    DosTime t{2026, 6, 5, 14, 30, 44};
    CHECK(ConvertFileTimeToDos(t, &date, &time));
    CHECK_EQ(date, 0x5CC5u);   // golden from python
    CHECK_EQ(time, 0x73D6u);
    // DOS epoch
    DosTime e{1980, 1, 1, 0, 0, 0};
    CHECK(ConvertFileTimeToDos(e, &date, &time));
    CHECK_EQ(date, 0x21u);
    CHECK_EQ(time, 0x0u);
    // out of range
    DosTime bad{1979, 1, 1, 0, 0, 0};
    CHECK(!ConvertFileTimeToDos(bad, &date, &time));
}

TEST(FileOps, DosTimeRoundTrip) {
    guild::u16 date = 0, time = 0;
    DosTime t{2026, 6, 5, 14, 30, 44};
    ConvertFileTimeToDos(t, &date, &time);
    DosTime r{};
    CHECK(ConvertDosToFileTime(date, time, &r));
    CHECK_EQ(r.year, 2026);
    CHECK_EQ(r.month, 6);
    CHECK_EQ(r.day, 5);
    CHECK_EQ(r.hour, 14);
    CHECK_EQ(r.minute, 30);
    CHECK_EQ(r.second, 44);   // even second survives the >>1/<<1 round-trip
}

// ---- buffer offset math (0x5d45a0 / 0x5d45e0) -----------------------------
TEST(FileOps, AdjustBufferOffset) {
    char buffer[16];
    BufBase base{nullptr, nullptr, buffer};
    StreamBuf s;
    s.base  = reinterpret_cast<char*>(&base);
    s.ptr   = buffer + 4;   // 4 bytes already consumed
    s.cnt   = 8;            // 8 bytes ahead
    s.flags = 0x10;         // lookahead present
    // in-range forward seek: delta in [start-ptr, cnt] = [-4, 8]
    CHECK_EQ(AdjustBufferOffset(3, &s), 0);
    CHECK_EQ(s.ptr, buffer + 7);
    CHECK_EQ(s.cnt, 5);
    CHECK_EQ(s.flags & 0x10, 0);     // flag cleared
    // backward into the buffer
    CHECK_EQ(AdjustBufferOffset(-2, &s), 0);
    CHECK_EQ(s.ptr, buffer + 5);
    CHECK_EQ(s.cnt, 7);
    // out of range (beyond cnt) -> 1, state unchanged
    char* before = s.ptr;
    CHECK_EQ(AdjustBufferOffset(100, &s), 1);
    CHECK_EQ(s.ptr, before);
}

TEST(FileOps, ResetBuffer) {
    char buffer[16];
    BufBase base{nullptr, nullptr, buffer + 2};
    StreamBuf s;
    s.base  = reinterpret_cast<char*>(&base);
    s.ptr   = buffer + 9;
    s.cnt   = 5;
    s.flags = 0x10 | 0x01;
    CHECK_EQ(ResetBuffer(&s), &s);
    CHECK_EQ(s.cnt, 0);
    CHECK_EQ(s.ptr, buffer + 2);      // rewound to base->start
    CHECK_EQ(s.flags & 0x10, 0);
    CHECK_EQ(s.flags & 0x01, 1);      // other bits untouched
}

// ---- find attribute filter (0x5fe7f4) -------------------------------------
TEST(FileOps, FindEntryMatches) {
    guild::u32 a = 0;
    // zero attrs default to 0x80; mask 55 (0x37) & 0x80 == 0 -> no match
    CHECK(!FindEntryMatches(0x37, &a));
    CHECK_EQ(a, 128u);
    // directory bit, mask requests dirs
    guild::u32 d = 0x10;
    CHECK(FindEntryMatches(0x10, &d));
    // mask excludes
    guild::u32 r = 0x01;   // readonly
    CHECK(!FindEntryMatches(0x10, &r));
}

// ---- NibbleToHexChar (0x5d911c) -------------------------------------------
TEST(FileOps, NibbleToHexChar) {
    const char* expect = "0123456789abcdef";
    for (int i = 0; i < 16; ++i)
        CHECK_EQ(NibbleToHexChar(i), expect[i]);
}

// ---- BuildTempFileName (0x5d9128) -----------------------------------------
TEST(FileOps, BuildTempFileName) {
    char out[64];
    // pid 0x12345678 folds to 0x567C, idx 0xAB -> "/tmp/t567c_ab.tmp"
    BuildTempFileName(out, "/tmp/", 0x12345678u, 0xAB);
    CHECK(std::strcmp(out, "/tmp/t567c_ab.tmp") == 0);
    // backslash dir, small values
    BuildTempFileName(out, "C:\\T\\", 0x0001u, 0x03);
    CHECK(std::strcmp(out, "C:\\T\\t0001_03.tmp") == 0);
}
