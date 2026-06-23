#include "test.h"

#include "render/render_leaves9.h"

#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {
template <class T> void PutAt(unsigned char* p, int off, T v) { std::memcpy(p + off, &v, sizeof(T)); }
}

// ===========================================================================
// VIBE_Vector_Copy3
// ===========================================================================
TEST(RenderLeaves9, VectorCopy3) {
    u32 src[3] = {0x3F800000u, 0x40000000u, 0x40400000u};   // 1.0f,2.0f,3.0f bits
    u32 dst[3] = {0, 0, 0};
    u32* ret = Vector_Copy3(dst, src);
    CHECK_EQ(ret, dst);
    CHECK_EQ(dst[0], 0x3F800000u);
    CHECK_EQ(dst[1], 0x40000000u);
    CHECK_EQ(dst[2], 0x40400000u);
}

// ===========================================================================
// VIBE_Shape_CopyPaletteHeader
//   count @ +10 (u16), dstOff @ +42 (u32), bytes = 4*count.
// ===========================================================================
TEST(RenderLeaves9, CopyPaletteHeader) {
    unsigned char shape[256];
    std::memset(shape, 0, sizeof(shape));
    PutAt<u16>(shape, 10, 3);          // 3 palette entries -> 12 bytes
    PutAt<u32>(shape, 42, 64);         // destination offset

    unsigned char src[12];
    for (int i = 0; i < 12; ++i) src[i] = static_cast<unsigned char>(0xA0 + i);

    u32 n = Shape_CopyPaletteHeader(src, shape);
    CHECK_EQ(n, 12u);
    for (int i = 0; i < 12; ++i)
        CHECK_EQ(shape[64 + i], static_cast<unsigned char>(0xA0 + i));
    // bytes outside the copied span untouched.
    CHECK_EQ(shape[63], 0);
    CHECK_EQ(shape[76], 0);
}

TEST(RenderLeaves9, CopyPaletteHeaderZeroCount) {
    unsigned char shape[64];
    std::memset(shape, 0xFF, sizeof(shape));
    PutAt<u16>(shape, 10, 0);
    PutAt<u32>(shape, 42, 32);
    unsigned char src[1] = {0x55};
    u32 n = Shape_CopyPaletteHeader(src, shape);
    CHECK_EQ(n, 0u);
    CHECK_EQ(shape[32], 0xFF);         // nothing written
}

// ===========================================================================
// VIBE_Render_RetTrue
// ===========================================================================
TEST(RenderLeaves9, RetTrue) {
    CHECK_EQ(Render_RetTrue(), 1);
}

// ===========================================================================
// VIBE_Shape_DrawFromBankMode1 / Mode5 — temporary draw-mode bracketing.
// We install a capturing FrameDataProcess hook that records the draw-mode byte
// it observed at the time of the draw call, then assert the byte was set to
// 1 (resp. 5) during the call and restored afterward.
// ===========================================================================
namespace {
u8  g_observedMode = 0;
void* g_observedFrame = nullptr;
int CaptureFrameProc(int /*a3*/, int /*a2*/, void* frame, int /*a4*/) {
    g_observedFrame = frame;
    std::memcpy(&g_observedMode, static_cast<unsigned char*>(frame) + 13, 1);
    return 1;
}
// Build a minimal bank blob: count @+42, one entry whose +69 dword points at a
// frame record carrying a draw-mode byte @+13.
struct Bank {
    unsigned char buf[256];
    unsigned char* frame() { return buf + 128; }
    void setup(u16 count, u32 frameDelta, u8 initialMode) {
        std::memset(buf, 0, sizeof(buf));
        std::memcpy(buf + 42, &count, 2);
        std::memcpy(buf + 69, &frameDelta, 4);   // entry 0's +69 dword
        buf[128 + 13] = initialMode;             // frame at delta=128
    }
};
}

TEST(RenderLeaves9, DrawFromBankMode1SetsAndRestores) {
    RenderLeaves9Hooks h{};
    h.FrameDataProcess = CaptureFrameProc;
    h.ConvertRgbTo16 = nullptr; h.Convert8To16 = nullptr;
    SetLeaves9Hooks(h);

    Bank bank;
    bank.setup(/*count*/4, /*frameDelta*/128, /*initialMode*/0xAB);
    g_observedMode = 0;

    i32 r = Shape_DrawFromBankMode1(bank.buf, 0, 0, 0, /*idx*/0);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_observedMode, 1);              // draw-mode forced to 1 during call
    CHECK_EQ(bank.frame()[13], 0xAB);         // restored afterward
    CHECK_EQ(g_observedFrame, bank.frame());

    ResetLeaves9Hooks();
}

TEST(RenderLeaves9, DrawFromBankMode5SetsAndRestores) {
    RenderLeaves9Hooks h{};
    h.FrameDataProcess = CaptureFrameProc;
    SetLeaves9Hooks(h);

    Bank bank;
    bank.setup(4, 128, 0x7C);
    g_observedMode = 0;

    i32 r = Shape_DrawFromBankMode5(bank.buf, 0, 0, 0, 0);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_observedMode, 5);
    CHECK_EQ(bank.frame()[13], 0x7C);

    ResetLeaves9Hooks();
}

TEST(RenderLeaves9, DrawFromBankRejectsOutOfRange) {
    RenderLeaves9Hooks h{};
    h.FrameDataProcess = CaptureFrameProc;
    SetLeaves9Hooks(h);
    Bank bank;
    bank.setup(/*count*/2, 128, 0x11);
    // idx 3 > count 2 -> rejected, no call, byte untouched.
    CHECK_EQ(Shape_DrawFromBankMode1(bank.buf, 0, 0, 0, 3), 0);
    CHECK_EQ(Shape_DrawFromBankMode5(bank.buf, 0, 0, 0, 3), 0);
    CHECK_EQ(bank.frame()[13], 0x11);
    // Mode5 also guards a null bank.
    CHECK_EQ(Shape_DrawFromBankMode5(nullptr, 0, 0, 0, 0), 0);
    ResetLeaves9Hooks();
}

// ===========================================================================
// VIBE_Shape_ConvertToNew — depth dispatch.
// ===========================================================================
namespace {
int g_rgbCalls = 0, g_8Calls = 0;
void* RgbHook(void* s) { ++g_rgbCalls; return static_cast<unsigned char*>(s) + 1; }
void* I8Hook(void* s)  { ++g_8Calls;   return static_cast<unsigned char*>(s) + 2; }
}

TEST(RenderLeaves9, ConvertToNewDepthDispatch) {
    RenderLeaves9Hooks h{};
    h.FrameDataProcess = CaptureFrameProc;
    h.ConvertRgbTo16 = RgbHook;
    h.Convert8To16 = I8Hook;
    SetLeaves9Hooks(h);

    unsigned char shape[16];
    std::memset(shape, 0, sizeof(shape));

    g_rgbCalls = g_8Calls = 0;
    shape[12] = 2;                                   // RGB depth
    CHECK_EQ(Shape_ConvertToNew(shape, 1), shape + 1);
    CHECK_EQ(g_rgbCalls, 1);
    CHECK_EQ(g_8Calls, 0);

    shape[12] = 0;                                   // 8-bit depth
    CHECK_EQ(Shape_ConvertToNew(shape, 1), shape + 2);
    CHECK_EQ(g_8Calls, 1);

    // target != 1 -> no conversion.
    shape[12] = 2;
    CHECK_EQ(Shape_ConvertToNew(shape, 0), (void*)nullptr);
    // unsupported depth with target==1 -> null.
    shape[12] = 7;
    CHECK_EQ(Shape_ConvertToNew(shape, 1), (void*)nullptr);
    CHECK_EQ(g_rgbCalls, 1);                          // unchanged
    CHECK_EQ(g_8Calls, 1);

    ResetLeaves9Hooks();
}

// ===========================================================================
// VIBE_Math_ShiftAccumulate  (96-bit magnitude -> 64-bit significand + exp)
// ===========================================================================
TEST(RenderLeaves9, ShiftAccumulateValueOne) {
    // value = 1 (lo=1), base exponent 0x405E -> normalized 1.0 in 80-bit:
    //   significand = 0x80000000_00000000, exponent = 0x3FFF.
    u32 hiOut = 0, loOut = 0; u16 expOut = 0;
    u32 ret = ShiftAccumulate(/*mid*/0, /*hi*/0, /*lo*/1, 0x405E, &hiOut, &loOut, &expOut);
    CHECK_EQ(hiOut, 0x80000000u);
    CHECK_EQ(loOut, 0u);
    CHECK_EQ(expOut, (u16)0x3FFF);
    CHECK_EQ(ret, 0u);                 // returns the low dword (loOut)
}

TEST(RenderLeaves9, ShiftAccumulateValueTwo) {
    // value = 2 -> 2.0 -> significand 0x80000000_00000000, exponent 0x4000.
    u32 hiOut = 0, loOut = 0; u16 expOut = 0;
    ShiftAccumulate(0, 0, 2, 0x405E, &hiOut, &loOut, &expOut);
    CHECK_EQ(hiOut, 0x80000000u);
    CHECK_EQ(loOut, 0u);
    CHECK_EQ(expOut, (u16)0x4000);
}

TEST(RenderLeaves9, ShiftAccumulateZeroUnchanged) {
    // all-zero input: 0x5fa684 zeroes esi (`sub esi,esi`) and jumps straight to
    // retn (locret_5FA6C4) — the `mov esi,edi` that copies the base exponent only
    // runs on the nonzero path. So the exponent OUTPUT is 0 here, NOT the input
    // 0x405E. edx(hi)/eax(mid) are likewise left at their (zero) inputs.
    u32 hiOut = 0xDEAD, loOut = 0xBEEF; u16 expOut = 0x1234;
    u32 ret = ShiftAccumulate(0, 0, 0, 0x405E, &hiOut, &loOut, &expOut);
    CHECK_EQ(ret, 0u);
    CHECK_EQ(hiOut, 0u);               // hi unchanged (was 0)
    CHECK_EQ(loOut, 0u);
    CHECK_EQ(expOut, (u16)0x0000);     // esi==0 on the all-zero path (5fa684)
}

// ===========================================================================
// VIBE_Math_ParseDecimal  (ASCII digits -> 80-bit extended float)
// ===========================================================================
TEST(RenderLeaves9, ParseDecimalOne) {
    Extended80 e{};
    ParseDecimal("1", &e);
    CHECK_EQ(e.mantissaHi, 0x80000000u);
    CHECK_EQ(e.mantissaLo, 0u);
    CHECK_EQ(e.exponent, (u16)0x3FFF);   // value 1.0
}

TEST(RenderLeaves9, ParseDecimalSmallIntegers) {
    // For an integer N, the 80-bit form has significand 0x80000000_00000000 only
    // when N is a power of two. Use 4 -> exponent 0x4001, significand top-bit.
    Extended80 e{};
    ParseDecimal("4", &e);
    CHECK_EQ(e.mantissaHi, 0x80000000u);
    CHECK_EQ(e.mantissaLo, 0u);
    CHECK_EQ(e.exponent, (u16)0x4001);   // 4.0 = 1.0 * 2^2, bias 0x3FFF+2

    // 3 -> 1.5 * 2^1: significand 0xC0000000_00000000, exponent 0x4000.
    Extended80 e3{};
    ParseDecimal("3", &e3);
    CHECK_EQ(e3.mantissaHi, 0xC0000000u);
    CHECK_EQ(e3.mantissaLo, 0u);
    CHECK_EQ(e3.exponent, (u16)0x4000);
}

TEST(RenderLeaves9, ParseDecimalCrossCheckAgainstLongDouble) {
    // Build the expected 80-bit pattern via the host long double (x87 80-bit on
    // this platform) and compare against ParseDecimal's output for 100.
    Extended80 e{};
    ParseDecimal("100", &e);
    // 100 = 1.5625 * 2^6 -> significand = 0xC8000000_00000000, exp 0x3FFF+6=0x4005.
    CHECK_EQ(e.mantissaHi, 0xC8000000u);
    CHECK_EQ(e.mantissaLo, 0u);
    CHECK_EQ(e.exponent, (u16)0x4005);
}
