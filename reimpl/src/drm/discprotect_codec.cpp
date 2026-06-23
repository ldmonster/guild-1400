// =============================================================================
// guild::drm — DiscProtect XOR/LFSR authentication codecs (implementation).
//
// 1:1 byte-exact reconstructions from gilde.exe. See discprotect_codec.h.
//
// Reconstruction notes (provenance / fidelity):
//
//  * 0x1419db0 InitLfsrTable: Hex-Rays renders the warm-up boundary as the
//    literals 104 / 72; the disasm shows these come from `var_4 = 0x48` (72)
//    with the outer bound `var_4 + 0x20` (104) and the emit guard
//    `var_4 <= i < var_4+0x20`. We translate the disasm form directly. The
//    feedback predicate, untangled from the decompiler's De Morgan soup, is:
//    the LFSR taps fire (shift then set bit14) iff exactly one of bit0/bit1 is
//    set, i.e. (v ^ (v>>1)) & 1. The shift is a 16-bit `shr` keeping a 15-bit
//    register (bit14 = 0x4000 is the feedback position).
//
//  * The XOR codecs are verbatim rolling-XOR loops; bounds are the exact
//    original literals (len-1, len-2, 122, 127, 126).
//
//  * 0x1417780 EvaluateSignature: the disc-timing analysis pipeline that
//    produces its decoded-frame buffer (0x1416830 AnalyzeFrameTimings ->
//    0x1416790 ComputeFrameTiming, QuickSort, Fabs) reads ~12 float tunables
//    (flt_14557E0..flt_14557F8, dbl_1455800/08) and raw timing buffers
//    (dword_1464CC8/CD8) that are ALL ZERO in the static image — they are
//    populated at run time from real disc reads. Those constants are therefore
//    not byte-recoverable and the pipeline is not a pure codec; it is omitted
//    rather than faked (project rule 8). The *scoring* tail IS pure and is
//    reconstructed exactly: threshold 27 (dword_142C460), window 72 (0x48),
//    scale 1.0 (dbl_1455818), with the original float-narrowing of the result.
// =============================================================================
#include "drm/discprotect_codec.h"

namespace guild::drm {

// gilde.exe 0x1419db0 — VIBE_DiscProtect_InitLfsrTable
void InitLfsrTable(u8* out) {
    constexpr int kWarmup = 0x48;            // var_4
    constexpr int kOuter  = kWarmup + 0x20;  // var_4 + 0x20 = 104

    // Mem_Set(out, 0, 0x20)
    for (int n = 0; n < 0x20; ++n) out[n] = 0;

    u16 v = 1;  // var_14, 15-bit LFSR seeded with 1
    for (int i = 0; i < kOuter; ++i) {
        // Emit the current low byte once we're past the warm-up window.
        if (i >= kWarmup && i < kOuter)
            out[i - kWarmup] = static_cast<u8>(v & 0xFF);

        for (int j = 0; j < 8; ++j) {
            // Taps on bits 0 and 1: fire iff exactly one is set.
            if (((v ^ (v >> 1)) & 1) != 0)
                v = static_cast<u16>((v >> 1) | 0x4000);  // shr + or ch,40h
            else
                v = static_cast<u16>(v >> 1);
        }
    }
}

// gilde.exe 0x141b060 — VIBE_DiscProtect_XorEncodeForward
void XorEncodeForward(u8* buf, i32 len) {
    if (len > 0) {
        for (i32 i = 0; i < len - 1; ++i)
            buf[i] ^= buf[i + 1];
    }
}

// gilde.exe 0x141b0b0 — VIBE_DiscProtect_XorEncodeReverse
void XorEncodeReverse(u8* buf, i32 len) {
    if (len > 1) {
        for (i32 i = len - 2; i >= 0; --i)
            buf[i] ^= buf[i + 1];
    }
}

// gilde.exe 0x141b100 — VIBE_DiscProtect_XorDecode123
void XorDecode123(u8* buf) {
    for (i32 i = 122; i >= 0; --i)
        buf[i] ^= buf[i + 1];
}

// gilde.exe 0x141b140 — VIBE_DiscProtect_XorEncode127
void XorEncode127(u8* buf) {
    for (i32 i = 0; i < 127; ++i)
        buf[i] ^= buf[i + 1];
}

// gilde.exe 0x141b180 — VIBE_DiscProtect_XorDecode127
void XorDecode127(u8* buf) {
    for (i32 i = 126; i >= 0; --i)
        buf[i] ^= buf[i + 1];
}

// gilde.exe 0x1416730 — VIBE_DiscProtect_CompareBytes
i32 CompareBytes(const u8* a, const u8* b, i32 len) {
    i32 matches = 0;
    for (i32 i = 0; i < len; ++i) {
        if (static_cast<char>(a[i]) == static_cast<char>(b[i]))
            ++matches;
    }
    return matches;
}

// gilde.exe 0x1417780 — VIBE_DiscProtect_EvaluateSignature (scoring tail)
double EvaluateSignatureScore(i32 matchCount) {
    const i32 T = kEvalThreshold;  // dword_142C460 = 27
    if (matchCount >= T) {
        // (double)(float)( 1.0 * (cnt - T) / (72 - T) )
        float s = static_cast<float>(kEvalScale *
                  static_cast<double>(matchCount - T) /
                  static_cast<double>(kEvalWindow - T));
        return static_cast<double>(s);
    } else {
        // (double)(float)( 1.0 * (cnt - T) / T )
        float s = static_cast<float>(kEvalScale *
                  static_cast<double>(matchCount - T) /
                  static_cast<double>(T));
        return static_cast<double>(s);
    }
}

// Byte-exact LFSR keystream for seed 1. These 32 bytes are exactly the
// statically-baked table found at gilde.exe 0x145AD40 — confirming the LFSR
// tap/feedback reconstruction reproduces the original keystream bit-for-bit.
// (The InitLfsrTable() generator regenerates these from seed 1; the unit test
// asserts the generator output equals this table.)
const u8 kLfsrTableSeed1[32] = {
    0x28, 0x62, 0x9e, 0xa9, 0xa8, 0x7e, 0xfe, 0xa0,
    0x40, 0x78, 0x30, 0x22, 0x94, 0x19, 0xaf, 0x4a,
    0xfc, 0x37, 0x01, 0xd6, 0x80, 0x5e, 0xe0, 0x38,
    0x48, 0x12, 0xb6, 0x8d, 0xb6, 0xe5, 0xb6, 0xcb,
};

}  // namespace guild::drm
