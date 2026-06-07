// tests/unit/diff_harness_smoke_test.cpp — keeps the 1:1 differential harness
// (apps/diff_harness.cpp, the reconstruction side of tools/diff_test.py) in the
// CMake test suite so its output format can't silently drift.
//
// It exec's the built `diff_harness` binary on a handful of inputs and asserts
// the output is the documented hex encoding with known-good values. The full
// behavioural 1:1 check (vs the ORIGINAL machine code under Unicorn) lives in
// tools/diff_test.py; this is just a format/wiring guard.
//
// DIFF_HARNESS_PATH is injected by CMake ($<TARGET_FILE:diff_harness>). If it is
// not defined (harness target not built, e.g. a stripped configure) the test
// skips cleanly.
#include "test.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// Run `diff_harness <args>` and capture its first stdout line (trimmed).
std::string run_harness(const std::string& args) {
#ifndef DIFF_HARNESS_PATH
    return std::string("__NO_HARNESS__");
#else
    std::string cmd = std::string(DIFF_HARNESS_PATH) + " " + args + " 2>/dev/null";
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) return std::string("__POPEN_FAIL__");
    std::array<char, 256> buf{};
    std::string out;
    while (std::fgets(buf.data(), (int)buf.size(), p)) out += buf.data();
    ::pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                            out.back() == ' '))
        out.pop_back();
    return out;
#endif
}

}  // namespace

TEST(DiffHarnessSmoke, OutputFormatAndKnownValues) {
    std::string probe = run_harness("ColorSetRgb 1 2 3");
    if (probe == "__NO_HARNESS__") {
        std::printf("  [skip] diff_harness not built (DIFF_HARNESS_PATH unset)\n");
        return;
    }
    CHECK(probe != "__POPEN_FAIL__");

    // ColorSetRgb(a,b,c) writes dst = {a, c, b} -> "01 03 02" packed.
    CHECK(run_harness("ColorSetRgb 1 2 3") == "010302");
    // 6-hex (3 bytes), all hex digits.
    CHECK(run_harness("ColorSetRgb 255 0 16").size() == 6u);

    // ColorNotEqualRgb: equal -> 0, differing -> 1, as 8-hex.
    CHECK(run_harness("ColorNotEqualRgb 5 6 7 5 6 7") == "00000000");
    CHECK(run_harness("ColorNotEqualRgb 5 6 7 5 6 8") == "00000001");

    // CrcCompute(seed=0xffffffff, "") returns the seed (no bytes processed).
    CHECK(run_harness("CrcCompute 0xffffffff \"\"").size() == 8u);

    // Integer math leaves: Multiply64(7,0,6,0) low dword == 42 == 0x2a.
    CHECK(run_harness("Multiply64 7 0 6 0") == "0000002a");
    // ClampValueRange(lo=5,value=99999,mul=1): hi=10005, ret=1, out=10005=0x2715.
    CHECK(run_harness("ClampValueRange 5 99999 1") == "0000000100002715");
    // ret==0 path (mul<1): ret=0, out untouched (harness prints 0).
    CHECK(run_harness("ClampValueRange 5 99999 0") == "0000000000000000");
    // LongLongDivide(100,7)=14=0xe ; signed negative folds the sign.
    CHECK(run_harness("LongLongDivide 100 7") == "0000000e");
    CHECK(run_harness("LongLongDivide -100 7") == "fffffff2");  // -14
    CHECK(run_harness("UnsignedLongLongDivide 100 7") == "0000000e");

    // Float x87 ST0 return: emitted as the raw 64-bit IEEE double pattern.
    // Distance2D(0,0,0,0) == 0.0 -> all zero bits.
    CHECK(run_harness("Distance2D 0 0 0 0") == "0000000000000000");
    std::string d = run_harness("Distance2D 10 20 5 3");
    CHECK(d.size() == 16u);  // a 64-bit double pattern

    // CatmullRomInterp -> 16 hex (double).
    CHECK(run_harness("CatmullRomInterp 1 2 3 4 0.5").size() == 16u);

    // VectorNormalize(3,0,4) -> (0.6, 0, 0.8) as raw f32 patterns (3*8 hex).
    std::string vn = run_harness("VectorNormalize 3,0,4");
    CHECK(vn.size() == 24u);
    CHECK(vn.substr(0, 8) == "3f19999a");   // 0.6f
    CHECK(vn.substr(8, 8) == "00000000");   // 0.0f
    CHECK(vn.substr(16, 8) == "3f4ccccd");  // 0.8f

    // VectorWithinTolerance: identical vectors within any tol -> 1.
    CHECK(run_harness("VectorWithinTolerance 1,2,3 1,2,3 0.001") == "00000001");
    // far apart vs small tol -> 0.
    CHECK(run_harness("VectorWithinTolerance 0,0,0 9,9,9 0.5") == "00000000");

    // ---- per-day simulation-path leaves (M4) ----
    // GameTimeCompare: a==b -> 0; a<b -> -1 (0xffffffff).
    CHECK(run_harness("GameTimeCompare 5 10 30 0 5 10 30 0") == "00000000");
    CHECK(run_harness("GameTimeCompare 5 10 30 0 5 11 0 0") == "ffffffff");
    // GameTimeDiffMinutes: 1440*(6-5)+60*(1-0)+(5-0) = 1505 = 0x5e1.
    CHECK(run_harness("GameTimeDiffMinutes 5 0 0 0 6 1 5 0") == "000005e1");
    // GameTimeAdvance: +90 minutes from 10:00 -> hour 11, record bytes follow.
    CHECK(run_harness("GameTimeAdvance 5 10 0 0 0 0 90") ==
          "0000000b050000000b001e00000000000000");
    // GameTimePackToRecord(day=8): out+2 word = day_low+1400 = 0x580; +1 = 1;
    // hour=10(0x0a), minute=30(0x1e), second=123(0x7b). Only written bytes.
    CHECK(run_harness("GameTimePackToRecord 8 10 30 123") ==
          "010180050a1e7b000000");
    // RNG: seed=1 -> first RandNext = ((1*1103515245+12345)>>16)&0x7fff = 0x41c6.
    CHECK(run_harness("RandNext 1") == "000041c6");
    // RandomModulo(seed=1, n=100) = 0x41c6 % 100 = 38 = 0x26.
    CHECK(run_harness("RandomModulo 1 100") == "00000026");
    // Office DGROUP-table readers (these caught two real recon bugs):
    CHECK(run_harness("AmtGetOfficeType 1") == "00000001");
    CHECK(run_harness("OfficeGetCategoryByRank 1") == "00000001");
    // GetDefinition(1): ret=1, word0=0x01010105, flag=1, textId=0x40a00001.
    CHECK(run_harness("OfficeGetDefinition 1") == "00000001050101010000000140a00000");
    // GetDefinition(36): valid (rank<37); textId record has the "GOD" bytes.
    CHECK(run_harness("OfficeGetDefinition 36") == "000000010000000000474f4400000000");
    // GetDefinition(37): out of range -> ret=0 (fallback record 0).
    CHECK(run_harness("OfficeGetDefinition 37").substr(0, 8) == "00000000");

    // Unknown function -> non-empty error string (not a hex line).
    std::string err = run_harness("NoSuchFn");
    CHECK(err.find("ERR") != std::string::npos);
}
