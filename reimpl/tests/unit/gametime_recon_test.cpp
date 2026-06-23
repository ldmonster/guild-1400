// Golden-vector tests for the VIBE_GameTime_* record helpers reconstructed in
// src/sim/gametime_recon.{h,cpp}. Vectors are derived directly from the
// gilde.exe pseudocode (0x58320c, 0x58334c, 0x583374, 0x43c680).
#include "tests/framework/test.h"
#include "sim/gametime_recon.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// 0x58320c — InitDefault zeroes all 14 bytes and returns 14.
TEST(GameTimeReconInit, ZeroesRecordReturns14) {
    GameTime rec;
    std::memset(&rec, 0xAB, sizeof(rec));
    int n = GameTimeInitDefault(&rec);
    CHECK_EQ(n, 14);
    CHECK_EQ(rec.day, 0);
    CHECK_EQ(rec.hour, static_cast<u16>(0));
    CHECK_EQ(rec.minute, 0);
    CHECK_EQ(rec.second, 0);
    // No bytes past the 14-byte record are touched / read; sizeof is exactly 14.
    CHECK_EQ(static_cast<int>(sizeof(GameTime)), 14);
}

// 0x58334c — UnpackFromRecord: day = (head>>16) - 1400, fields zero-extended,
// second = field8; returns field8.
TEST(GameTimeReconUnpack, BasicFields) {
    GameTimePackedRecord src{};
    // head high word = 1405 (year) -> day = 1405 - 1400 = 5.
    src.packedHead = (1405 << 16) | 0x1234;  // low word ignored
    src.field4 = 200;                         // -> hour (zero-extended)
    src.field5 = 250;                         // -> minute (zero-extended)
    src.field8 = 0x0BADF00D;

    GameTime out;
    std::memset(&out, 0, sizeof(out));
    i32 r = GameTimeUnpackFromRecord(&src, &out);

    CHECK_EQ(out.day, 5);
    CHECK_EQ(out.hour, static_cast<u16>(200));   // zero-extended byte
    CHECK_EQ(out.minute, 250);                   // zero-extended byte
    CHECK_EQ(out.second, static_cast<i32>(0x0BADF00D));
    CHECK_EQ(r, static_cast<i32>(0x0BADF00D));
}

// Arithmetic shift: a negative head high word must sign-extend (SAR).
TEST(GameTimeReconUnpack, ArithmeticShiftNegativeHead) {
    GameTimePackedRecord src{};
    src.packedHead = static_cast<i32>(0x80000000u);  // head>>16 (SAR) = -32768
    src.field4 = 0;
    src.field5 = 0;
    src.field8 = 0;
    GameTime out{};
    GameTimeUnpackFromRecord(&src, &out);
    CHECK_EQ(out.day, -32768 - 1400);  // -34168
}

// WAVE-11 HARDENING — a fully-degenerate (all 0xFF) packed record. The byte
// fields zero-extend (never sign-extend) and the dword passes through verbatim;
// unpack reads only the 12-byte struct, never past it. No OOB, faithful wrap.
TEST(GameTimeReconUnpack, AllOnesRecordZeroExtendsBytes) {
    GameTimePackedRecord src{};
    std::memset(&src, 0xFF, sizeof(src));
    GameTime out{};
    std::memset(&out, 0, sizeof(out));
    i32 r = GameTimeUnpackFromRecord(&src, &out);
    // packedHead = 0xFFFFFFFF; >>16 (SAR) = -1; -1400 -> -1401.
    CHECK_EQ(out.day, -1401);
    CHECK_EQ(out.hour, static_cast<u16>(255));   // (u8)0xFF zero-extended
    CHECK_EQ(out.minute, 255);                   // (u8)0xFF zero-extended
    CHECK_EQ(out.second, static_cast<i32>(0xFFFFFFFFu));  // -1
    CHECK_EQ(r, static_cast<i32>(0xFFFFFFFFu));
}

// Year 1400 itself -> day 0.
TEST(GameTimeReconUnpack, BaseYearIsZero) {
    GameTimePackedRecord src{};
    src.packedHead = (1400 << 16);
    GameTime out{};
    GameTimeUnpackFromRecord(&src, &out);
    CHECK_EQ(out.day, 0);
}

// 0x583374 — AdvanceThunk forwards to GameTimeAdvance with the argument
// remapping (ecxArg->addMinutes, ebxArg->addDays, stackArg->addSeconds).
// Cross-check the result against a direct GameTimeAdvance call on an identical
// record.
TEST(GameTimeReconThunk, MatchesDirectAdvance) {
    GameTime a{};
    a.day = 10; a.hour = 5; a.minute = 30; a.second = 0;
    GameTime b = a;

    // thunk(rec, ecx=addMinutes=70, ebx=addDays=1, stack=addSeconds=125)
    int rt = GameTimeAdvanceThunk(&a, /*ecx*/70, /*ebx*/1, /*stack*/125);
    // direct: Advance(rec, addDays=1, addSeconds=125, addMinutes=70)
    int rd = GameTimeAdvance(&b, 1, 125, 70);

    CHECK_EQ(rt, rd);
    CHECK_EQ(a.day, b.day);
    CHECK_EQ(a.hour, b.hour);
    CHECK_EQ(a.minute, b.minute);
    CHECK_EQ(a.second, b.second);
}

// Hand-computed Advance golden vector (independent of the thunk), exercising
// the seconds->minutes->hours->days carry chain.
TEST(GameTimeReconThunk, CarryGolden) {
    // start: day=0 hour=23 minute=59 second=0
    // thunk(ecx=addMinutes=0, ebx=addDays=0, stack=addSeconds=125)
    //   second += 125 -> 125; minute += 125/60 = 2 -> 61; second = 125%60 = 5
    //   hour = 61/60 + 23 = 1 + 23 = 24 ; minute %= 60 -> 1
    //   result = 0 + 24 ; 24>=24 -> result=0, day=1
    GameTime a{};
    a.day = 0; a.hour = 23; a.minute = 59; a.second = 0;
    int r = GameTimeAdvanceThunk(&a, /*ecx*/0, /*ebx*/0, /*stack*/125);
    CHECK_EQ(r, 0);
    CHECK_EQ(a.day, 1);
    CHECK_EQ(a.hour, static_cast<u16>(0));
    CHECK_EQ(a.minute, 1);
    CHECK_EQ(a.second, 5);
}

// 0x43c680 — GetScaledDelay = uDelay * throttledTicks; inert defaults give 0.
TEST(GameTimeReconScaledDelay, Multiplies) {
    GameTickClockState clk;
    CHECK_EQ(GameTickGetScaledDelay(clk), 0u);  // inert defaults

    clk.uDelay = 50;
    clk.throttledTicks = 7;
    CHECK_EQ(GameTickGetScaledDelay(clk), 350u);

    // 32-bit unsigned wrap matches the original DWORD multiply.
    clk.uDelay = 0x10000;
    clk.throttledTicks = 0x10000;  // product = 0x1_0000_0000 -> wraps to 0
    CHECK_EQ(GameTickGetScaledDelay(clk), 0u);
}
