#include "util/fpu.h"

#include <cstring>

namespace guild::util {

namespace {

// Modeled live x87 control word. Default = MSVC CRT default FCW: all exceptions
// masked (0x3F), 53-bit precision (PC=2 -> 0x0200), round-to-nearest (RC=0).
u16 g_fcw = 0x023F;

// gilde.exe 0x64A958 — bit 0 = use x87-software fallback. 0 on normal CPUs.
u8 g_byte_64A958 = 0;

} // namespace

// gilde.exe 0x1426c03 — VIBE_Fp_DecodeStatusWord
u8 DecodeStatusWord(u16 a1) {
    u8 result = 0;
    if (a1 & 0x01) result = 16;
    if (a1 & 0x04) result |= 0x08;
    if (a1 & 0x08) result |= 0x04;
    if (a1 & 0x10) result |= 0x02;
    if (a1 & 0x20) return static_cast<u8>(result | 0x01);
    return result;
}

// gilde.exe 0x1426c95 — VIBE_Fp_EncodeControlWord
u8 EncodeControlWord(u32 a1) {
    u8 result = (a1 & 0x10) != 0;          // -> bit 0
    if (a1 & 0x08) result |= 0x04;
    if (a1 & 0x04) result |= 0x08;
    if (a1 & 0x02) result |= 0x10;
    if (a1 & 0x01) result |= 0x20;
    if (a1 & 0x80000) return static_cast<u8>(result | 0x02);
    return result;
}

// gilde.exe 0x1426bb8 — VIBE_Fp_SetControlWord
// Original reads the current FCW (via the byte the CRT keeps), decodes it to the
// abstract word, splices in (newWord & mask), encodes back and stores. We model
// the read from the live FCW and the store back into it.
u32 SetControlWord(u32 newWord, u32 mask) {
    u32 cur = DecodeStatusWord(g_fcw);
    u32 v3 = (newWord & mask) | (~mask & cur);
    u8 enc = EncodeControlWord(v3);
    // Write the encoded mask bits back into the modeled FCW low byte. The CRT
    // stores them into the per-thread FCW image; rounding/precision bits are
    // untouched by this helper, so preserve the high byte.
    g_fcw = static_cast<u16>((g_fcw & 0xFFC0) | (enc & 0x3F));
    return v3;
}

// gilde.exe 0x1426bed — VIBE_Fp_ControlMask
u32 ControlMask(u32 newWord, u32 mask) {
    return SetControlWord(newWord, mask & 0xFFF7FFFF);
}

// gilde.exe 0x14242a2 — VIBE_Fp_ControlWord (identity accessor)
u16 ControlWord(u16 fcw) {
    return fcw;
}

u16 GetFcw() {
    return g_fcw;
}

void SetFcw(u16 fcw) {
    g_fcw = fcw;
}

u16 PushFcw(u16 fcw) {
    u16 prev = g_fcw;
    g_fcw = fcw;
    return prev;
}

// gilde.exe 0x142416a — VIBE_Fp_ClassifyDouble
FpClass ClassifyDouble(u32 a1, u32 a2) {
    if (a2 == 0x7FF00000u) {
        if (!a1) return kFpPosInf;             // +inf
    } else if (a2 == 0xFFF00000u && !a1) {
        return kFpNegInf;                      // -inf
    }
    u32 hiword = a2 >> 16;
    if ((hiword & 0x7FF8) == 0x7FF8) return kFpQNan;
    if ((hiword & 0x7FF8) == 0x7FF0 && ((a2 & 0x7FFFF) != 0 || a1)) return kFpSNan;
    return kFpNormal;
}

FpClass Classify(double x) {
    u64 bits;
    std::memcpy(&bits, &x, sizeof(bits));
    return ClassifyDouble(static_cast<u32>(bits), static_cast<u32>(bits >> 32));
}

bool UseX87SoftwarePath() {
    return (g_byte_64A958 & 1) != 0;
}

void SetX87SoftwarePath(bool on) {
    if (on) g_byte_64A958 |= 1u;
    else    g_byte_64A958 &= static_cast<u8>(~1u);
}

} // namespace guild::util
